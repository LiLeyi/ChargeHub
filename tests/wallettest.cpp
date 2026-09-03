#include <QtTest>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QTcpSocket>

#include <thread>

#include "database.h"
#include "client.h"
#include "dispatch.h"
#include "protocol.h"
#include "rechargetransaction.h"
#include "tcpserver.h"
#include "walletcontroller.h"

class WalletTest : public QObject {
    Q_OBJECT

private slots:
    void keepsLegacyEnvelopeCompatible();
    void decodesVersionedEnvelopeFromFragments();
    void migratesLegacyWalletAmountsWithoutLoss();
    void appliesRechargeOnceForRepeatedRequest();
    void rejectsInvalidUsersAndAmounts();
    void rollsBackBalanceWhenLedgerInsertFails();
    void returnsStorageErrorWhenDatabaseRemainsBusy();
    void queriesKnownAndUnknownRechargeResults();
    void serializesConcurrentDuplicateRequests();
    void supportsVersionedWalletMessages();
    void roundTripsRechargeThroughTcpServer();
    void returnsStableWalletErrorCodes();
    void parsesWalletAmountWithoutFloatingPoint();
    void keepsOneRequestAcrossTimeoutAndQuery();
    void requiresExplicitNewRechargeAfterUncertainResult();
    void acceptsOnlyAuthoritativeWalletResponses();
    void ignoresStaleWalletResponsesDuringRecharge();
    void clientSendsVersionedWalletRequests();
};

static QJsonObject receive(QTcpSocket &socket)
{
    Protocol decoder;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 2000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (socket.bytesAvailable() > 0 || socket.waitForReadyRead(10))
            decoder.append(socket.readAll());
        if (decoder.hasPacket())
            return decoder.nextPacket();
    }
    return {};
}

static QJsonObject exchange(QTcpSocket &socket, const QJsonObject &request)
{
    socket.write(Protocol::pack(request));
    if (!socket.waitForBytesWritten(1000))
        return {};
    return receive(socket);
}

static QString createDatabasePath(QTemporaryDir &directory)
{
    Q_ASSERT(directory.isValid());
    return directory.filePath("wallet.db");
}

static void createLegacyWallet(const QString &path)
{
    const QString connectionName = QStringLiteral("legacy-wallet-setup");
    {
        QSqlDatabase connection = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        connection.setDatabaseName(path);
        QVERIFY(connection.open());
        QSqlQuery query(connection);
        QVERIFY(query.exec("CREATE TABLE user (id INTEGER PRIMARY KEY AUTOINCREMENT, phone TEXT NOT NULL UNIQUE, "
                           "nickname TEXT NOT NULL, avatar_path TEXT NOT NULL DEFAULT '', password_hash TEXT NOT NULL DEFAULT '', "
                           "balance REAL NOT NULL DEFAULT 0.00, status TEXT NOT NULL DEFAULT '正常', created_at TEXT NOT NULL)"));
        QVERIFY(query.exec("CREATE TABLE recharge_log (id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER NOT NULL, "
                           "amount REAL NOT NULL, result TEXT NOT NULL, created_at TEXT NOT NULL)"));
        QVERIFY(query.exec("INSERT INTO user(phone,nickname,balance,status,created_at) "
                           "VALUES('13800138001','迁移用户',12.34,'正常','2026-09-03 10:00:00')"));
        QVERIFY(query.exec("INSERT INTO recharge_log(user_id,amount,result,created_at) "
                           "VALUES(1,2.34,'成功','2026-09-03 10:01:00')"));
        QVERIFY(query.exec("INSERT INTO recharge_log(user_id,amount,result,created_at) "
                           "VALUES(1,1.00,'成功','2026-09-03 10:02:00')"));
        connection.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

void WalletTest::keepsLegacyEnvelopeCompatible()
{
    const QJsonObject request {
        {"type", "LIST_RECHARGE"},
        {"seq", 1},
        {"role", "user"},
        {"token", "test-token"},
        {"data", QJsonObject {}}
    };
    Protocol decoder;

    decoder.append(Protocol::pack(request));

    QVERIFY(decoder.hasPacket());
    QCOMPARE(decoder.nextPacket(), request);
}

void WalletTest::decodesVersionedEnvelopeFromFragments()
{
    const QJsonObject data {
        {"contractProbe", true}
    };
    const QJsonObject request {
        {"protocolVersion", 2},
        {"type", "ENVELOPE_PROBE"},
        {"seq", 2},
        {"role", "user"},
        {"token", "test-token"},
        {"data", data}
    };
    const QByteArray frame = Protocol::pack(request);
    Protocol decoder;

    decoder.append(frame.left(3));
    QVERIFY(!decoder.hasPacket());
    decoder.append(frame.mid(3, 5));
    QVERIFY(!decoder.hasPacket());
    decoder.append(frame.mid(8));

    QVERIFY(decoder.hasPacket());
    QCOMPARE(decoder.nextPacket(), request);
}

void WalletTest::migratesLegacyWalletAmountsWithoutLoss()
{
    QTemporaryDir directory;
    const QString path = createDatabasePath(directory);
    createLegacyWallet(path);
    Database database(path);

    QVERIFY(database.open());
    const auto user = database.one("SELECT balance, balance_cents FROM user WHERE id=1");
    QCOMPARE(user.value("balance_cents").toLongLong(), qint64(1234));
    const auto ledger = database.one(
        "SELECT amount_cents,balance_after_cents,request_id,trade_no,status FROM recharge_log WHERE id=1");
    QCOMPARE(ledger.value("amount_cents").toLongLong(), qint64(234));
    QCOMPARE(ledger.value("balance_after_cents").toLongLong(), qint64(0));
    QCOMPARE(ledger.value("request_id").toString(), QStringLiteral("legacy-1"));
    QCOMPARE(ledger.value("trade_no").toString(), QStringLiteral("RC00000001"));
    QCOMPARE(ledger.value("status").toString(), QStringLiteral("succeeded"));
    QCOMPARE(database.one("SELECT COUNT(*) AS n FROM recharge_log WHERE balance_after_cents=0")
                 .value("n").toInt(), 2);

    QVERIFY(database.open());
    QCOMPARE(database.one("SELECT COUNT(*) AS n FROM recharge_log WHERE request_id='legacy-1'").value("n").toInt(), 1);
}

void WalletTest::appliesRechargeOnceForRepeatedRequest()
{
    QTemporaryDir directory;
    Database database(createDatabasePath(directory));
    QVERIFY(database.open());
    const auto user = database.one("SELECT id,balance_cents FROM user WHERE phone='13800138000'");
    const int userId = user.value("id").toInt();
    const qint64 before = user.value("balance_cents").toLongLong();
    RechargeTransaction transaction(database.path());
    const QString requestId = QStringLiteral("9dc97269-7758-4de6-9e6d-e2c682913a74");

    const auto first = transaction.applyRecharge(userId, 2000, requestId);
    const auto second = transaction.applyRecharge(userId, 2000, requestId);

    QCOMPARE(first.status, RechargeStatus::Succeeded);
    QCOMPARE(first.balanceCents, before + 2000);
    QVERIFY(!first.tradeNo.isEmpty());
    QCOMPARE(second.status, RechargeStatus::Succeeded);
    QCOMPARE(second.tradeNo, first.tradeNo);
    QCOMPARE(second.balanceCents, first.balanceCents);
    QVERIFY(second.replayed);
    QCOMPARE(transaction.applyRecharge(userId, 2001, requestId).status,
             RechargeStatus::InvalidRequestId);
    QCOMPARE(database.one("SELECT COUNT(*) AS n FROM recharge_log WHERE request_id=?", {requestId}).value("n").toInt(), 1);
    QCOMPARE(database.one("SELECT balance_cents FROM user WHERE id=?", {userId}).value("balance_cents").toLongLong(), before + 2000);
}

void WalletTest::rejectsInvalidUsersAndAmounts()
{
    QTemporaryDir directory;
    Database database(createDatabasePath(directory));
    QVERIFY(database.open());
    RechargeTransaction transaction(database.path());
    const QString requestId = QStringLiteral("c8fa4d0c-56fc-49ad-b70a-44d444cb70a7");

    QCOMPARE(transaction.applyRecharge(1, 0, requestId).status, RechargeStatus::InvalidAmount);
    QCOMPARE(transaction.applyRecharge(1, 1000001, requestId).status, RechargeStatus::InvalidAmount);
    QCOMPARE(transaction.applyRecharge(1, 100, QStringLiteral("bad")).status, RechargeStatus::InvalidRequestId);
    QCOMPARE(transaction.applyRecharge(99999, 100, requestId).status, RechargeStatus::UserNotFound);
    const int frozenId = database.one("SELECT id FROM user WHERE status='冻结' LIMIT 1").value("id").toInt();
    QCOMPARE(transaction.applyRecharge(frozenId, 100, requestId).status, RechargeStatus::UserFrozen);
}

void WalletTest::rollsBackBalanceWhenLedgerInsertFails()
{
    QTemporaryDir directory;
    Database database(createDatabasePath(directory));
    QVERIFY(database.open());
    const auto user = database.one("SELECT id,balance_cents FROM user WHERE phone='13800138000'");
    const int userId = user.value("id").toInt();
    const qint64 before = user.value("balance_cents").toLongLong();
    QVERIFY(database.execute("CREATE TRIGGER fail_recharge BEFORE INSERT ON recharge_log "
                             "BEGIN SELECT RAISE(ABORT, 'forced ledger failure'); END") >= 0);
    RechargeTransaction transaction(database.path());

    const auto result = transaction.applyRecharge(
        userId, 500, QStringLiteral("449d09f8-a779-4da6-9007-f20375e40433"));

    QCOMPARE(result.status, RechargeStatus::StorageError);
    QCOMPARE(database.one("SELECT balance_cents FROM user WHERE id=?", {userId}).value("balance_cents").toLongLong(), before);
    QCOMPARE(database.one("SELECT COUNT(*) AS n FROM recharge_log WHERE request_id=?",
                          {QStringLiteral("449d09f8-a779-4da6-9007-f20375e40433")}).value("n").toInt(), 0);
}

void WalletTest::returnsStorageErrorWhenDatabaseRemainsBusy()
{
    QTemporaryDir directory;
    const QString path = createDatabasePath(directory);
    Database database(path);
    QVERIFY(database.open());
    const QString connectionName = QStringLiteral("wallet-busy-test");
    {
        QSqlDatabase blocker = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        blocker.setDatabaseName(path);
        QVERIFY(blocker.open());
        QSqlQuery query(blocker);
        QVERIFY(query.exec("PRAGMA busy_timeout = 5000"));
        QVERIFY(query.exec("BEGIN IMMEDIATE"));

        QElapsedTimer timer;
        timer.start();
        const auto result = RechargeTransaction(path).applyRecharge(
            1, 500, QStringLiteral("56c3c94c-aa9b-4978-a29b-0f1ff72114f4"));
        const qint64 elapsed = timer.elapsed();

        QCOMPARE(result.status, RechargeStatus::StorageError);
        QVERIFY2(elapsed >= 4500 && elapsed < 8000,
                 qPrintable(QStringLiteral("unexpected busy timeout: %1 ms").arg(elapsed)));
        QVERIFY(query.exec("ROLLBACK"));
        blocker.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

void WalletTest::queriesKnownAndUnknownRechargeResults()
{
    QTemporaryDir directory;
    Database database(createDatabasePath(directory));
    QVERIFY(database.open());
    RechargeTransaction transaction(database.path());
    const QString requestId = QStringLiteral("61301367-a940-43b7-a540-648ce7cb0b50");
    const auto applied = transaction.applyRecharge(1, 1, requestId);

    const auto found = transaction.queryRecharge(1, requestId);
    QCOMPARE(found.status, RechargeStatus::Succeeded);
    QCOMPARE(found.tradeNo, applied.tradeNo);
    QCOMPARE(found.amountCents, qint64(1));
    QCOMPARE(transaction.queryRecharge(1, QStringLiteral("f6f51250-4614-451b-8203-52e2e898fba5")).status,
             RechargeStatus::NotFound);
}

void WalletTest::serializesConcurrentDuplicateRequests()
{
    QTemporaryDir directory;
    Database database(createDatabasePath(directory));
    QVERIFY(database.open());
    const QString path = database.path();
    const QString requestId = QStringLiteral("56c0de94-47c2-4e6a-9854-c96c103ddaf5");
    RechargeResult left;
    RechargeResult right;
    std::thread first([&] { left = RechargeTransaction(path).applyRecharge(1, 321, requestId); });
    std::thread second([&] { right = RechargeTransaction(path).applyRecharge(1, 321, requestId); });
    first.join();
    second.join();

    QCOMPARE(left.status, RechargeStatus::Succeeded);
    QCOMPARE(right.status, RechargeStatus::Succeeded);
    QCOMPARE(left.tradeNo, right.tradeNo);
    QCOMPARE(database.one("SELECT COUNT(*) AS n FROM recharge_log WHERE request_id=?", {requestId}).value("n").toInt(), 1);
}

void WalletTest::supportsVersionedWalletMessages()
{
    QTemporaryDir directory;
    Database database(createDatabasePath(directory));
    QVERIFY(database.open());
    Dispatch dispatch(&database);
    const QJsonObject login = dispatch.handle(QJsonObject{
        {"type", "LOGIN"}, {"seq", 1}, {"data", QJsonObject{{"phone", "13800138000"}, {"password", "123456"}}}
    });
    QCOMPARE(login.value("code").toInt(), 0);
    const QString token = login.value("data").toObject().value("token").toString();
    const QString requestId = QStringLiteral("27fa5b7a-7300-4310-a719-feb0508b2539");
    const QJsonObject rechargeRequest{
        {"protocolVersion", 2}, {"type", "RECHARGE"}, {"seq", 2}, {"token", token},
        {"data", QJsonObject{{"amountCents", 2000}, {"requestId", requestId}}}
    };

    const QJsonObject first = dispatch.handle(rechargeRequest);
    const QJsonObject replay = dispatch.handle(rechargeRequest);
    QCOMPARE(first.value("code").toInt(), 0);
    QCOMPARE(first.value("data").toObject().value("balanceCents").toVariant().toLongLong(), qint64(10000));
    QCOMPARE(replay.value("data").toObject().value("tradeNo"), first.value("data").toObject().value("tradeNo"));
    QVERIFY(replay.value("data").toObject().value("replayed").toBool());

    const QJsonObject queried = dispatch.handle(QJsonObject{
        {"type", "QUERY_RECHARGE"}, {"seq", 3}, {"token", token},
        {"data", QJsonObject{{"requestId", requestId}}}
    });
    QCOMPARE(queried.value("code").toInt(), 0);
    QCOMPARE(queried.value("data").toObject().value("amountCents").toVariant().toLongLong(), qint64(2000));

    const QJsonObject missing = dispatch.handle(QJsonObject{
        {"type", "QUERY_RECHARGE"}, {"seq", 4}, {"token", token},
        {"data", QJsonObject{{"requestId", "c6b81085-fcbc-43e7-a30a-bca85effa3bd"}}}
    });
    QCOMPARE(missing.value("code").toInt(), 404);
    QCOMPARE(missing.value("errorCode").toString(), QStringLiteral("RECHARGE_NOT_FOUND"));

    const QJsonObject wallet = dispatch.handle(QJsonObject{
        {"type", "QUERY_WALLET"}, {"seq", 5}, {"token", token}, {"data", QJsonObject{}}
    });
    QCOMPARE(wallet.value("code").toInt(), 0);
    QCOMPARE(wallet.value("data").toObject().value("balanceCents").toVariant().toLongLong(), qint64(10000));
    QVERIFY(!wallet.value("data").toObject().value("records").toArray().isEmpty());

    const QJsonObject invalidVersion = dispatch.handle(QJsonObject{
        {"protocolVersion", 3}, {"type", "RECHARGE"}, {"seq", 6}, {"token", token},
        {"data", QJsonObject{{"amountCents", 100},
                             {"requestId", "2638cfae-ac1a-4d15-a0d5-8b3dd700bf08"}}}
    });
    QCOMPARE(invalidVersion.value("errorCode").toString(), QStringLiteral("PROTOCOL_ERROR"));

    const QJsonObject incompleteV2 = dispatch.handle(QJsonObject{
        {"protocolVersion", 2}, {"type", "RECHARGE"}, {"seq", 7}, {"token", token},
        {"data", QJsonObject{{"amount", 1.0}}}
    });
    QCOMPARE(incompleteV2.value("errorCode").toString(), QStringLiteral("PROTOCOL_ERROR"));
}

void WalletTest::roundTripsRechargeThroughTcpServer()
{
    QTemporaryDir directory;
    Database database(createDatabasePath(directory));
    QVERIFY(database.open());
    Dispatch dispatch(&database);
    TcpServer server(&dispatch);
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(socket.waitForConnected(1000));

    const QJsonObject login = exchange(socket, QJsonObject{
        {"type", "LOGIN"}, {"seq", 1},
        {"data", QJsonObject{{"phone", "13800138000"}, {"password", "123456"}}}
    });
    QCOMPARE(login.value("code").toInt(-1), 0);
    const QString token = login.value("data").toObject().value("token").toString();
    QVERIFY(!token.isEmpty());
    const QString requestId = QStringLiteral("2348ad7d-a166-45a9-b888-192647a17c22");
    const QJsonObject request{
        {"protocolVersion", 2}, {"type", "RECHARGE"}, {"seq", 2}, {"token", token},
        {"data", QJsonObject{{"amountCents", 123}, {"requestId", requestId}}}
    };

    const QJsonObject first = exchange(socket, request);
    const QJsonObject replay = exchange(socket, request);
    QCOMPARE(first.value("code").toInt(-1), 0);
    QCOMPARE(replay.value("code").toInt(-1), 0);
    QCOMPARE(replay.value("data").toObject().value("tradeNo"),
             first.value("data").toObject().value("tradeNo"));
    QCOMPARE(database.one("SELECT COUNT(*) AS n FROM recharge_log WHERE request_id=?", {requestId})
                 .value("n").toInt(), 1);

    const QJsonObject wallet = exchange(socket, QJsonObject{
        {"protocolVersion", 2}, {"type", "QUERY_WALLET"}, {"seq", 3}, {"token", token},
        {"data", QJsonObject{}}
    });
    QCOMPARE(wallet.value("code").toInt(-1), 0);
    const QJsonObject walletData = wallet.value("data").toObject();
    QCOMPARE(walletData.value("balanceCents"), first.value("data").toObject().value("balanceCents"));
    const QJsonArray records = walletData.value("records").toArray();
    QVERIFY(!records.isEmpty());
    QCOMPARE(records.first().toObject().value("requestId").toString(), requestId);

    QTcpSocket otherSocket;
    otherSocket.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(otherSocket.waitForConnected(1000));
    const QString concurrentRequestId = QStringLiteral("bc248b21-15b1-4cb7-b553-ca7f31eb0dbf");
    const QJsonObject concurrentRequest{
        {"protocolVersion", 2}, {"type", "RECHARGE"}, {"seq", 4}, {"token", token},
        {"data", QJsonObject{{"amountCents", 321}, {"requestId", concurrentRequestId}}}
    };
    socket.write(Protocol::pack(concurrentRequest));
    otherSocket.write(Protocol::pack(concurrentRequest));
    QVERIFY(socket.waitForBytesWritten(1000));
    QVERIFY(otherSocket.waitForBytesWritten(1000));
    const QJsonObject left = receive(socket);
    const QJsonObject right = receive(otherSocket);
    QCOMPARE(left.value("code").toInt(-1), 0);
    QCOMPARE(right.value("code").toInt(-1), 0);
    QCOMPARE(left.value("data").toObject().value("tradeNo"),
             right.value("data").toObject().value("tradeNo"));
    QCOMPARE(database.one("SELECT COUNT(*) AS n FROM recharge_log WHERE request_id=?",
                          {concurrentRequestId}).value("n").toInt(), 1);
}

void WalletTest::returnsStableWalletErrorCodes()
{
    QTemporaryDir directory;
    Database database(createDatabasePath(directory));
    QVERIFY(database.open());
    Dispatch dispatch(&database);
    const QJsonObject login = dispatch.handle(QJsonObject{
        {"type", "LOGIN"}, {"seq", 1}, {"data", QJsonObject{{"phone", "13800138000"}, {"password", "123456"}}}
    });
    const QString token = login.value("data").toObject().value("token").toString();
    const QJsonObject invalid = dispatch.handle(QJsonObject{
        {"type", "RECHARGE"}, {"seq", 2}, {"token", token},
        {"data", QJsonObject{{"amountCents", 0}, {"requestId", "bad"}}}
    });
    QCOMPARE(invalid.value("code").toInt(), 400);
    QCOMPARE(invalid.value("errorCode").toString(), QStringLiteral("INVALID_AMOUNT"));

    const QJsonObject huge = dispatch.handle(QJsonObject{
        {"type", "RECHARGE"}, {"seq", 3}, {"token", token},
        {"data", QJsonObject{{"amountCents", 1e100},
                             {"requestId", "a34cf7e7-6904-4617-b13e-a2d54c01a53b"}}}
    });
    QCOMPARE(huge.value("code").toInt(), 400);
    QCOMPARE(huge.value("errorCode").toString(), QStringLiteral("INVALID_AMOUNT"));

    const QJsonObject oversized = dispatch.handle(QJsonObject{
        {"type", "RECHARGE"}, {"seq", 4}, {"token", token},
        {"data", QJsonObject{{"amountCents", 100},
                             {"requestId", "4ecaa9b3-f6dc-436c-85f6-8993a590d30d"},
                             {"padding", QString(4096, QLatin1Char('x'))}}}
    });
    QCOMPARE(oversized.value("code").toInt(), 400);
    QCOMPARE(oversized.value("errorCode").toString(), QStringLiteral("PROTOCOL_ERROR"));

    database.execute("UPDATE user SET status='冻结' WHERE id=1");
    const QJsonObject frozen = dispatch.handle(QJsonObject{
        {"type", "RECHARGE"}, {"seq", 5}, {"token", token},
        {"data", QJsonObject{{"amountCents", 100},
                             {"requestId", "b7f1338b-54f4-4c10-a336-b1e74e5315c9"}}}
    });
    QCOMPARE(frozen.value("code").toInt(), 403);
    QCOMPARE(frozen.value("errorCode").toString(), QStringLiteral("USER_FROZEN"));

    database.execute("UPDATE user SET status='注销' WHERE id=1");
    const QJsonObject closed = dispatch.handle(QJsonObject{
        {"type", "RECHARGE"}, {"seq", 6}, {"token", token},
        {"data", QJsonObject{{"amountCents", 100},
                             {"requestId", "6c15bfb7-3001-4cf4-988e-c3787a1737d4"}}}
    });
    QCOMPARE(closed.value("code").toInt(), 403);
    QCOMPARE(closed.value("errorCode").toString(), QStringLiteral("USER_NOT_FOUND"));
}

void WalletTest::parsesWalletAmountWithoutFloatingPoint()
{
    const auto minimum = WalletController::parseAmountCents(QStringLiteral("0.01"));
    QVERIFY(minimum.valid);
    QCOMPARE(minimum.cents, qint64(1));
    QCOMPARE(minimum.normalized, QStringLiteral("0.01"));
    const auto maximum = WalletController::parseAmountCents(QStringLiteral("10000.00"));
    QVERIFY(maximum.valid);
    QCOMPARE(maximum.cents, qint64(1000000));
    QCOMPARE(WalletController::formatCents(1), QString::fromUtf8("¥0.01"));

    for (const QString &invalid : {QString(), QStringLiteral("abc"), QStringLiteral("0"),
                                   QStringLiteral("-1"), QStringLiteral("10000.01"),
                                   QStringLiteral("1.001")}) {
        QVERIFY2(!WalletController::parseAmountCents(invalid).valid,
                 qPrintable(QStringLiteral("unexpected valid amount: %1").arg(invalid)));
    }
}

void WalletTest::keepsOneRequestAcrossTimeoutAndQuery()
{
    WalletController wallet;
    wallet.beginLoading();
    QCOMPARE(wallet.acceptWallet(QJsonObject{{"balanceCents", 8000}}),
             WalletResponseStatus::Applied);
    wallet.beginEditing();
    QVERIFY(wallet.beginConfirmation(2000));
    const WalletRequest recharge = wallet.submitConfirmed();
    QVERIFY(recharge.isValid());
    const QString requestId = recharge.data.value("requestId").toString();
    QVERIFY(!requestId.isEmpty());
    QCOMPARE(wallet.submitConfirmed().isValid(), false);

    wallet.markTimeout();
    QCOMPARE(wallet.state(), WalletState::Uncertain);
    const WalletRequest query = wallet.queryPending();
    QCOMPARE(query.type, QStringLiteral("QUERY_RECHARGE"));
    QCOMPARE(query.data.value("requestId").toString(), requestId);
    QCOMPARE(wallet.queryPending().isValid(), false);
}

void WalletTest::requiresExplicitNewRechargeAfterUncertainResult()
{
    WalletController wallet;
    QVERIFY(!wallet.canSubmit());
    wallet.beginLoading();
    QCOMPARE(wallet.acceptWallet(QJsonObject{{"balanceCents", 8000}}),
             WalletResponseStatus::Applied);
    QVERIFY(wallet.beginConfirmation(2000));
    const QString originalRequestId = wallet.submitConfirmed().data.value("requestId").toString();

    wallet.markTimeout();
    wallet.markFailure(QStringLiteral("RECHARGE_NOT_FOUND"));
    QCOMPARE(wallet.state(), WalletState::Uncertain);
    QVERIFY(!wallet.beginConfirmation(2000));

    wallet.startNewRecharge();
    QVERIFY(wallet.beginConfirmation(2000));
    const QString newRequestId = wallet.submitConfirmed().data.value("requestId").toString();
    QVERIFY(!newRequestId.isEmpty());
    QVERIFY(newRequestId != originalRequestId);
}

void WalletTest::acceptsOnlyAuthoritativeWalletResponses()
{
    WalletController wallet;
    wallet.beginLoading();
    QCOMPARE(wallet.acceptWallet(QJsonObject{{"balanceCents", 8000}}),
             WalletResponseStatus::Applied);
    QVERIFY(wallet.beginConfirmation(2000));
    const WalletRequest request = wallet.submitConfirmed();
    const QString requestId = request.data.value("requestId").toString();

    QCOMPARE(wallet.acceptRecharge(QJsonObject{{"requestId", requestId},
                                                {"amountCents", 2000},
                                                {"status", "succeeded"},
                                                {"tradeNo", "RC00000001"}}),
             WalletResponseStatus::ProtocolError);
    QCOMPARE(wallet.balanceCents(), qint64(8000));

    wallet.startNewRecharge();
    QVERIFY(wallet.beginConfirmation(2000));
    const WalletRequest retry = wallet.submitConfirmed();
    const QJsonObject success{{"requestId", retry.data.value("requestId").toString()},
                              {"amountCents", 2000},
                              {"balanceCents", 12345},
                              {"status", "succeeded"},
                              {"tradeNo", "RC00000002"}};
    QCOMPARE(wallet.acceptRecharge(success), WalletResponseStatus::Applied);
    QCOMPARE(wallet.balanceCents(), qint64(12345));
    QCOMPARE(wallet.balanceText(), QString::fromUtf8("¥123.45"));
    QCOMPARE(wallet.acceptRecharge(success), WalletResponseStatus::Duplicate);
    QCOMPARE(wallet.balanceCents(), qint64(12345));
}

void WalletTest::ignoresStaleWalletResponsesDuringRecharge()
{
    WalletController wallet;
    wallet.beginLoading();
    QCOMPARE(wallet.acceptWallet(QJsonObject{{"balanceCents", 8000}}),
             WalletResponseStatus::Applied);
    QVERIFY(wallet.beginConfirmation(2000));
    const WalletRequest request = wallet.submitConfirmed();

    QCOMPARE(wallet.acceptWallet(QJsonObject{{"balanceCents", 999999}}),
             WalletResponseStatus::Ignored);
    QCOMPARE(wallet.state(), WalletState::Submitting);
    QCOMPARE(wallet.balanceCents(), qint64(8000));

    const QJsonObject success{{"requestId", request.data.value("requestId").toString()},
                              {"amountCents", 2000},
                              {"balanceCents", 10000},
                              {"status", "succeeded"},
                              {"tradeNo", "RC00000003"}};
    QCOMPARE(wallet.acceptRecharge(success), WalletResponseStatus::Applied);
    QCOMPARE(wallet.balanceCents(), qint64(10000));
}

void WalletTest::clientSendsVersionedWalletRequests()
{
    QTcpServer receiver;
    QVERIFY(receiver.listen(QHostAddress::LocalHost, 0));
    Client client;
    client.connectTo(QStringLiteral("127.0.0.1"), receiver.serverPort());
    QTRY_VERIFY(client.isConnected());
    QTRY_VERIFY(receiver.hasPendingConnections());
    QScopedPointer<QTcpSocket> peer(receiver.nextPendingConnection());
    QVERIFY(peer);

    client.request(QStringLiteral("RECHARGE"),
                   QJsonObject{{"amountCents", 100},
                               {"requestId", "d2b0e1d0-8bdf-4322-849d-2321b091ef8d"}},
                   QStringLiteral("test-token"));
    QTRY_VERIFY(peer->bytesAvailable() > 0);
    Protocol decoder;
    decoder.append(peer->readAll());
    QVERIFY(decoder.hasPacket());
    const QJsonObject request = decoder.nextPacket();
    QCOMPARE(request.value("protocolVersion").toInt(), 2);
    QCOMPARE(request.value("data").toObject().value("amountCents").toInt(), 100);
}

QTEST_MAIN(WalletTest)
#include "wallettest.moc"
