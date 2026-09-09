#include "database.h"
#include "dispatch.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonObject>
#include <QTemporaryDir>

namespace {

QJsonObject request(Dispatch &dispatch, const QString &type, int seq,
                    const QString &token = QString(), const QJsonObject &data = {})
{
    return dispatch.handle(QJsonObject{{"type", type},
                                       {"seq", seq},
                                       {"token", token},
                                       {"data", data}});
}

bool require(bool condition, const QString &message)
{
    if (!condition)
        qCritical().noquote() << "FAIL:" << message;
    return condition;
}

QString login(Dispatch &dispatch, const QString &phone, int seq)
{
    const QJsonObject response = request(
        dispatch, QStringLiteral("LOGIN"), seq, QString(),
        QJsonObject{{"phone", phone}, {"password", QStringLiteral("123456")}});
    if (!require(response.value("code").toInt(-1) == 0,
                 QStringLiteral("login should succeed for %1").arg(phone)))
        return {};
    const QString token = response.value("data").toObject().value("token").toString();
    require(!token.isEmpty(), QStringLiteral("login should return a token"));
    return token;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporaryDirectory;
    if (!require(temporaryDirectory.isValid(), QStringLiteral("temporary directory is unavailable")))
        return 1;

    Database database(temporaryDirectory.filePath(QStringLiteral("reservation-test.db")));
    if (!require(database.open(), QStringLiteral("temporary database should open")))
        return 1;
    Dispatch dispatch(&database);

    int seq = 1;
    const QString firstToken = login(dispatch, QStringLiteral("13800138000"), seq++);
    const QString secondToken = login(dispatch, QStringLiteral("13912345678"), seq++);
    if (firstToken.isEmpty() || secondToken.isEmpty())
        return 1;

    const auto clearReservations = [&database]() {
        return require(database.executeAffected(QStringLiteral("DELETE FROM reservation")) >= 0,
                       QStringLiteral("reservation cleanup in temporary database should succeed"));
    };
    if (!clearReservations())
        return 1;

    QJsonObject response = request(dispatch, QStringLiteral("RESERVE_PILE"), seq++, firstToken,
                                   QJsonObject{{"pileId", 1}});
    if (!require(response.value("code").toInt(-1) == 0,
                 QStringLiteral("normal reservation creation should succeed")))
        return 1;
    const QJsonObject reservation = response.value("data").toObject().value("reservation").toObject();
    if (!require(reservation.value("id").toInt() > 0,
                 QStringLiteral("successful creation should return a valid reservation id"))
        || !require(reservation.value("pileId").toInt() == 1
                        && !reservation.value("expireAt").toString().isEmpty(),
                    QStringLiteral("successful creation should preserve response fields"))
        || !require(response.value("message").toString()
                        == QString::fromUtf8("预约成功，15分钟内有效"),
                    QStringLiteral("successful creation should preserve response message")))
        return 1;

    response = request(dispatch, QStringLiteral("RESERVE_PILE"), seq++, secondToken,
                       QJsonObject{{"pileId", 1}});
    if (!require(response.value("code").toInt() == 409,
                 QStringLiteral("a pile with an active reservation should be rejected")))
        return 1;
    response = request(dispatch, QStringLiteral("RESERVE_PILE"), seq++, firstToken,
                       QJsonObject{{"pileId", 2}});
    if (!require(response.value("code").toInt() == 409,
                 QStringLiteral("a user with an active reservation should be rejected")))
        return 1;

    response = request(dispatch, QStringLiteral("CANCEL_RESERVE"), seq++, firstToken);
    if (!require(response.value("code").toInt(-1) == 0,
                 QStringLiteral("normal reservation cancellation should succeed"))
        || !require(response.value("message").toString() == QString::fromUtf8("已取消预约"),
                    QStringLiteral("successful cancellation should preserve response message")))
        return 1;

    if (!clearReservations())
        return 1;
    if (!require(database.execute(
                     QStringLiteral("CREATE TRIGGER fail_reservation_insert BEFORE INSERT ON reservation "
                                    "BEGIN SELECT RAISE(ABORT, 'forced insert failure'); END")) >= 0,
                 QStringLiteral("insert failure trigger should be installed")))
        return 1;
    response = request(dispatch, QStringLiteral("RESERVE_PILE"), seq++, firstToken,
                       QJsonObject{{"pileId", 1}});
    if (!require(response.value("code").toInt() == 500,
                 QStringLiteral("failed INSERT should return a server error"))
        || !require(!response.value("message").toString().contains(QStringLiteral("forced")),
                    QStringLiteral("failed INSERT should not expose database details"))
        || !require(response.value("data").toObject().isEmpty(),
                    QStringLiteral("failed INSERT should not return a reservation id"))
        || !require(database.one(QStringLiteral("SELECT id FROM reservation")).isEmpty(),
                    QStringLiteral("failed INSERT should not create a reservation")))
        return 1;
    if (!require(database.execute(QStringLiteral("DROP TRIGGER fail_reservation_insert")) >= 0,
                 QStringLiteral("insert failure trigger should be removed")))
        return 1;

    response = request(dispatch, QStringLiteral("RESERVE_PILE"), seq++, firstToken,
                       QJsonObject{{"pileId", 1}});
    if (!require(response.value("code").toInt(-1) == 0,
                 QStringLiteral("reservation for cancellation failure test should succeed")))
        return 1;
    if (!require(database.execute(
                     QStringLiteral("CREATE TRIGGER fail_reservation_cancel BEFORE UPDATE OF status ON reservation "
                                    "WHEN OLD.status='有效' AND NEW.status='已取消' "
                                    "BEGIN SELECT RAISE(ABORT, 'forced update failure'); END")) >= 0,
                 QStringLiteral("update failure trigger should be installed")))
        return 1;
    response = request(dispatch, QStringLiteral("CANCEL_RESERVE"), seq++, firstToken);
    if (!require(response.value("code").toInt() == 500,
                 QStringLiteral("failed UPDATE should return a server error"))
        || !require(!response.value("message").toString().contains(QStringLiteral("forced")),
                    QStringLiteral("failed UPDATE should not expose database details"))
        || !require(database.one(QStringLiteral(
                                     "SELECT id FROM reservation WHERE status='有效'"))
                            .value("id").toInt() > 0,
                    QStringLiteral("failed UPDATE should preserve the active reservation")))
        return 1;
    if (!require(database.execute(QStringLiteral("DROP TRIGGER fail_reservation_cancel")) >= 0,
                 QStringLiteral("update failure trigger should be removed")))
        return 1;

    if (!require(database.execute(
                     QStringLiteral("CREATE TRIGGER ignore_reservation_cancel BEFORE UPDATE OF status ON reservation "
                                    "WHEN OLD.status='有效' AND NEW.status='已取消' "
                                    "BEGIN SELECT RAISE(IGNORE); END")) >= 0,
                 QStringLiteral("zero-row update trigger should be installed")))
        return 1;
    response = request(dispatch, QStringLiteral("CANCEL_RESERVE"), seq++, firstToken);
    if (!require(response.value("code").toInt() == 409,
                 QStringLiteral("zero-row UPDATE should not be reported as success")))
        return 1;

    qInfo() << "reservation service integration tests passed";
    return 0;
}
