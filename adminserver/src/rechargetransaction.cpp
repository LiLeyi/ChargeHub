#include "rechargetransaction.h"

#include <QAtomicInteger>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>

#include <limits>

namespace {

QString nextConnectionName()
{
    static QAtomicInteger<quint64> sequence(0);
    return QStringLiteral("recharge_%1_%2")
        .arg(quintptr(QThread::currentThreadId()))
        .arg(sequence.fetchAndAddRelaxed(1));
}

QString newTradeNo()
{
    QString value = QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-').left(16).toUpper();
    return QStringLiteral("RC") + value;
}

RechargeResult rowResult(
    const QSqlQuery &query, int amountIndex, int balanceIndex, int requestIndex, int tradeIndex,
    bool replayed)
{
    RechargeResult result;
    result.status = RechargeStatus::Succeeded;
    result.requestId = query.value(requestIndex).toString();
    result.tradeNo = query.value(tradeIndex).toString();
    result.amountCents = query.value(amountIndex).toLongLong();
    result.balanceCents = query.value(balanceIndex).toLongLong();
    result.replayed = replayed;
    return result;
}

void rollback(QSqlDatabase &database)
{
    QSqlQuery query(database);
    query.exec(QStringLiteral("ROLLBACK"));
}

}

RechargeTransaction::RechargeTransaction(const QString &databasePath)
    : databasePath(databasePath)
{
}

bool RechargeTransaction::isValidRequestId(const QString &requestId)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(?:[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-5][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}|legacy-[A-Za-z0-9-]{1,64})$"));
    return pattern.match(requestId).hasMatch();
}

RechargeResult RechargeTransaction::applyRecharge(
    int userId, qint64 amountCents, const QString &requestId) const
{
    RechargeResult result;
    result.requestId = requestId;
    result.amountCents = amountCents;
    if (amountCents < 1 || amountCents > 1000000) {
        result.status = RechargeStatus::InvalidAmount;
        return result;
    }
    if (!isValidRequestId(requestId)) {
        result.status = RechargeStatus::InvalidRequestId;
        return result;
    }

    const QString connectionName = nextConnectionName();
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        if (!database.open()) {
            result.status = RechargeStatus::StorageError;
        } else {
            QSqlQuery query(database);
            query.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
            if (!query.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
                result.status = RechargeStatus::StorageError;
            } else {
                query.prepare(QStringLiteral(
                    "SELECT user_id,amount_cents,balance_after_cents,request_id,trade_no,status "
                    "FROM recharge_log WHERE request_id=?"));
                query.addBindValue(requestId);
                if (!query.exec()) {
                    rollback(database);
                    result.status = RechargeStatus::StorageError;
                } else if (query.next()) {
                    if (query.value(0).toInt() != userId
                        || query.value(1).toLongLong() != amountCents
                        || query.value(5).toString() != QStringLiteral("succeeded")) {
                        rollback(database);
                        result.status = RechargeStatus::InvalidRequestId;
                    } else {
                        result = rowResult(query, 1, 2, 3, 4, true);
                        if (!database.commit()) {
                            rollback(database);
                            result.status = RechargeStatus::StorageError;
                        }
                    }
                } else {
                    query.prepare(QStringLiteral("SELECT balance_cents,status FROM user WHERE id=?"));
                    query.addBindValue(userId);
                    if (!query.exec() || !query.next()) {
                        rollback(database);
                        result.status = query.lastError().isValid() ? RechargeStatus::StorageError
                                                                    : RechargeStatus::UserNotFound;
                    } else if (query.value(1).toString() == QString::fromUtf8("冻结")) {
                        rollback(database);
                        result.status = RechargeStatus::UserFrozen;
                    } else if (query.value(1).toString() != QString::fromUtf8("正常")) {
                        rollback(database);
                        result.status = RechargeStatus::UserNotFound;
                    } else {
                        const qint64 balanceCents = query.value(0).toLongLong();
                        if (balanceCents > std::numeric_limits<qint64>::max() - amountCents) {
                            rollback(database);
                            result.status = RechargeStatus::StorageError;
                        } else {
                            result.balanceCents = balanceCents + amountCents;
                            result.tradeNo = newTradeNo();
                            query.prepare(QStringLiteral(
                                "UPDATE user SET balance_cents=?,balance=? WHERE id=? AND status='正常'"));
                            query.addBindValue(result.balanceCents);
                            query.addBindValue(result.balanceCents / 100.0);
                            query.addBindValue(userId);
                            const bool userUpdated = query.exec() && query.numRowsAffected() == 1;
                            query.prepare(QStringLiteral(
                                "INSERT INTO recharge_log(user_id,amount,result,created_at,amount_cents,"
                                "balance_after_cents,request_id,trade_no,status) "
                                "VALUES(?,?,?,datetime('now','localtime'),?,?,?,?,?)"));
                            query.addBindValue(userId);
                            query.addBindValue(amountCents / 100.0);
                            query.addBindValue(QString::fromUtf8("成功"));
                            query.addBindValue(amountCents);
                            query.addBindValue(result.balanceCents);
                            query.addBindValue(requestId);
                            query.addBindValue(result.tradeNo);
                            query.addBindValue(QStringLiteral("succeeded"));
                            const bool ledgerInserted = userUpdated && query.exec();
                            if (!ledgerInserted || !database.commit()) {
                                rollback(database);
                                result.status = RechargeStatus::StorageError;
                                result.balanceCents = 0;
                                result.tradeNo.clear();
                            } else {
                                result.status = RechargeStatus::Succeeded;
                            }
                        }
                    }
                }
            }
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return result;
}

RechargeResult RechargeTransaction::queryRecharge(int userId, const QString &requestId) const
{
    RechargeResult result;
    result.requestId = requestId;
    if (!isValidRequestId(requestId)) {
        result.status = RechargeStatus::InvalidRequestId;
        return result;
    }
    const QString connectionName = nextConnectionName();
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        if (!database.open()) {
            result.status = RechargeStatus::StorageError;
        } else {
            QSqlQuery query(database);
            query.exec(QStringLiteral("PRAGMA query_only = ON"));
            query.prepare(QStringLiteral(
                "SELECT amount_cents,balance_after_cents,request_id,trade_no,status "
                "FROM recharge_log WHERE user_id=? AND request_id=?"));
            query.addBindValue(userId);
            query.addBindValue(requestId);
            if (!query.exec())
                result.status = RechargeStatus::StorageError;
            else if (!query.next())
                result.status = RechargeStatus::NotFound;
            else
                result = rowResult(query, 0, 1, 2, 3, true);
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return result;
}
