#include "reservationservice.h"

#include "database.h"

#include <QDateTime>
#include <QJsonArray>

namespace {
QString nowStr() { return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"); }
}

ReservationService::ReservationService(Database *db) : db_(db) {}

void ReservationService::expire() const
{
    db_->execute("UPDATE reservation SET status=?, no_show=1 WHERE status=? AND expire_at < ?",
                 {QString::fromUtf8("已取消"), QString::fromUtf8("有效"), nowStr()});
}

QVariantMap ReservationService::activeForPile(int pileId) const
{
    return db_->one("SELECT * FROM reservation WHERE pile_id=? AND status='有效' ORDER BY id DESC LIMIT 1",
                    {pileId});
}

bool ReservationService::pileIsIdle(const QVariantMap &pile) const
{
    return pile.value("status").toString() == QString::fromUtf8("闲置")
        && activeForPile(pile.value("id").toInt()).isEmpty();
}

QJsonObject ReservationService::list(const QVariantMap &user) const
{
    expire();
    const auto rows = db_->query(
        "SELECT r.id, r.pile_id, r.status, r.expire_at, r.created_at, "
        "p.pile_no, p.type, p.power_kw, p.status AS pile_status, "
        "s.id AS station_id, s.name AS station_name, s.address, s.lng, s.lat, s.price_per_kwh "
        "FROM reservation r JOIN pile p ON p.id=r.pile_id JOIN station s ON s.id=p.station_id "
        "WHERE r.user_id=? AND r.status='有效' ORDER BY r.expire_at, r.id DESC",
        {user.value("id")});
    QJsonArray reservations;
    const QDateTime now = QDateTime::currentDateTime();
    for (const auto &row : rows) {
        const QDateTime expiry = QDateTime::fromString(row.value("expire_at").toString(),
                                                       "yyyy-MM-dd HH:mm:ss");
        reservations.append(QJsonObject{
            {"id", row.value("id").toInt()}, {"pileId", row.value("pile_id").toInt()},
            {"status", row.value("status").toString()}, {"expireAt", row.value("expire_at").toString()},
            {"createdAt", row.value("created_at").toString()},
            {"remainingSeconds", expiry.isValid() ? qMax(0, int(now.secsTo(expiry))) : 0},
            {"pileNo", row.value("pile_no").toString()}, {"type", row.value("type").toString()},
            {"powerKw", row.value("power_kw").toDouble()}, {"pileStatus", row.value("pile_status").toString()},
            {"stationId", row.value("station_id").toInt()}, {"stationName", row.value("station_name").toString()},
            {"address", row.value("address").toString()}, {"lng", row.value("lng").toDouble()},
            {"lat", row.value("lat").toDouble()}, {"pricePerKwh", row.value("price_per_kwh").toDouble()}});
    }
    return {{"reservations", reservations}};
}

QJsonObject ReservationService::reserve(const QVariantMap &user, const QJsonObject &data)
{
    expire();
    const int pileId = data.value("pileId").toInt();
    const auto pile = db_->one("SELECT * FROM pile WHERE id=?", {pileId});
    if (pile.isEmpty()) return {{"errorCode", 404}, {"error", QString::fromUtf8("电桩不存在")}};
    if (pile.value("status").toString() != QString::fromUtf8("闲置"))
        return {{"errorCode", 409}, {"error", QString::fromUtf8("仅闲置电桩可预约")}};
    if (!activeForPile(pileId).isEmpty())
        return {{"errorCode", 409}, {"error", QString::fromUtf8("该桩已被预约")}};
    if (!db_->one("SELECT * FROM reservation WHERE user_id=? AND status='有效'", {user.value("id")}).isEmpty())
        return {{"errorCode", 409}, {"error", QString::fromUtf8("您已有有效预约，请先取消或履约")}};
    const QString expiry = QDateTime::currentDateTime().addSecs(15 * 60).toString("yyyy-MM-dd HH:mm:ss");
    const int id = db_->execute(
        "INSERT INTO reservation(user_id,pile_id,status,expire_at,created_at) VALUES(?,?,?,?,?)",
        {user.value("id"), pileId, QString::fromUtf8("有效"), expiry, nowStr()});
    return {{"reservation", QJsonObject{{"id", id}, {"pileId", pileId}, {"expireAt", expiry}}}};
}

QJsonObject ReservationService::cancel(const QVariantMap &user)
{
    const auto row = db_->one("SELECT * FROM reservation WHERE user_id=? AND status='有效' ORDER BY id DESC LIMIT 1",
                              {user.value("id")});
    if (row.isEmpty()) return {{"errorCode", 404}, {"error", QString::fromUtf8("没有有效预约")}};
    db_->execute("UPDATE reservation SET status=? WHERE id=?",
                 {QString::fromUtf8("已取消"), row.value("id")});
    return {};
}

bool ReservationService::fulfill(int reservationId)
{
    return db_->execute("UPDATE reservation SET status=? WHERE id=?",
                        {QString::fromUtf8("已履约"), reservationId}) >= 0;
}

void ReservationService::cancelActiveForUser(int userId)
{
    db_->execute("UPDATE reservation SET status=? WHERE user_id=? AND status=?",
                 {QString::fromUtf8("已取消"), userId, QString::fromUtf8("有效")});
}
