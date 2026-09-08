#include "chargeservice.h"

#include "database.h"
#include "sessionservice.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonValue>
#include <QTime>
#include <QtMath>

namespace {
qint64 fenOf(double yuan) { return qRound(yuan * 100.0); }
double money(double yuan) { return fenOf(yuan) / 100.0; }
double moneyFen(qint64 fen) { return fen / 100.0; }
QString nowStr() { return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"); }
}

ChargeService::ChargeService(Database *db, SessionService *sessions)
    : db_(db), sessions_(sessions)
{
}

void ChargeService::expireReservations() const
{
    db_->execute("UPDATE reservation SET status=?, no_show=1 WHERE status=? AND expire_at < ?",
                 {QString::fromUtf8("已取消"), QString::fromUtf8("有效"), nowStr()});
}

QVariantMap ChargeService::activeReservation(int pileId) const
{
    return db_->one("SELECT * FROM reservation WHERE pile_id=? AND status='有效' ORDER BY id DESC LIMIT 1",
                    {pileId});
}

QVariantMap ChargeService::openOrder(int userId) const
{
    return db_->one(
        "SELECT * FROM charge_order WHERE user_id=? AND status IN ('充电中','待结算') ORDER BY id DESC LIMIT 1",
        {userId});
}

QJsonObject ChargeService::calculateLive(const QVariantMap &order, const QVariantMap &pile,
                                         const QVariantMap &station) const
{
    const QDateTime start = QDateTime::fromString(order.value("start_time").toString(), "yyyy-MM-dd HH:mm:ss");
    QDateTime end = QDateTime::currentDateTime();
    if (order.value("status").toString() != QString::fromUtf8("充电中")
        && !order.value("end_time").toString().isEmpty())
        end = QDateTime::fromString(order.value("end_time").toString(), "yyyy-MM-dd HH:mm:ss");
    const int seconds = qMax(0, start.isValid() && end.isValid() ? int(start.secsTo(end)) : 0);
    const double power = pile.value("power_kw").toDouble();
    const double fallback = station.value("price_per_kwh").toDouble();
    const auto rules = db_->query(
        "SELECT start_hour, end_hour, price_per_kwh, label FROM tariff_rule WHERE station_id=? ORDER BY start_hour",
        {station.value("id")});
    const auto priceAt = [&](int hour) {
        hour = qBound(0, hour, 23);
        for (const auto &rule : rules) {
            if (hour >= rule.value("start_hour").toInt() && hour < rule.value("end_hour").toInt())
                return rule.value("price_per_kwh").toDouble();
        }
        return fallback;
    };
    const auto labelAt = [&](int hour) {
        hour = qBound(0, hour, 23);
        for (const auto &rule : rules) {
            if (hour >= rule.value("start_hour").toInt() && hour < rule.value("end_hour").toInt())
                return rule.value("label").toString();
        }
        return QString();
    };
    double energy = 0;
    qint64 fen = 0;
    if (start.isValid() && end.isValid() && end > start) {
        QDateTime cursor = start;
        while (cursor < end) {
            QDateTime hourEnd(cursor.date(), QTime(cursor.time().hour(), 0, 0));
            hourEnd = hourEnd.addSecs(3600);
            if (hourEnd > end)
                hourEnd = end;
            const int segmentSeconds = qMax(0, int(cursor.secsTo(hourEnd)));
            const double segmentEnergy = power * (segmentSeconds / 3600.0);
            energy += segmentEnergy;
            fen += fenOf(segmentEnergy * priceAt(cursor.time().hour()));
            cursor = hourEnd;
        }
    }
    energy = qRound(energy * 1000) / 1000.0;
    const double amount = moneyFen(fen);
    const int currentHour = QDateTime::currentDateTime().time().hour();
    const double currentPrice = priceAt(currentHour);
    return {{"seconds", seconds},
            {"energyKwh", energy},
            {"amount", amount},
            {"powerKw", power},
            {"pricePerKwh", energy > 1e-9 ? money(amount / energy) : currentPrice},
            {"currentPrice", currentPrice},
            {"tariffLabel", labelAt(currentHour)}};
}

QJsonObject ChargeService::publicOrder(const QVariantMap &order, const QVariantMap &pile,
                                       const QVariantMap &station, const QJsonObject &live)
{
    QJsonObject result{{"id", order.value("id").toInt()},
                       {"orderNo", order.value("order_no").toString()},
                       {"status", order.value("status").toString()},
                       {"startTime", order.value("start_time").toString()},
                       {"endTime", order.value("end_time").toString()},
                       {"pileNo", pile.value("pile_no").toString()},
                       {"stationName", station.value("name").toString()},
                       {"type", pile.value("type").toString()}};
    for (auto it = live.begin(); it != live.end(); ++it)
        result.insert(it.key(), it.value());
    return result;
}

QJsonObject ChargeService::start(const QVariantMap &user, const QJsonObject &data)
{
    expireReservations();
    const int userId = user.value("id").toInt();
    if (!openOrder(userId).isEmpty())
        return {{"errorCode", 409}, {"error", QString::fromUtf8("您有未完成的充电订单，请先结算")}};
    if (user.value("balance").toDouble() <= 0)
        return {{"errorCode", 402}, {"error", QString::fromUtf8("余额不足，请先充值")}};
    const int pileId = data.value("pileId").toInt();
    auto pile = db_->one("SELECT * FROM pile WHERE id=?", {pileId});
    if (pile.isEmpty())
        return {{"errorCode", 404}, {"error", QString::fromUtf8("电桩不存在")}};
    if (pile.value("status").toString() == QString::fromUtf8("故障"))
        return {{"errorCode", 409}, {"error", QString::fromUtf8("电桩故障，请选择其他电桩")}};
    if (pile.value("status").toString() == QString::fromUtf8("在用"))
        return {{"errorCode", 409}, {"error", QString::fromUtf8("电桩正在使用中")}};
    const auto reservation = activeReservation(pileId);
    if (!reservation.isEmpty() && reservation.value("user_id").toInt() != userId)
        return {{"errorCode", 409}, {"error", QString::fromUtf8("该桩已被他人预约")}};
    const auto station = db_->one("SELECT * FROM station WHERE id=?", {pile.value("station_id")});
    const QString timestamp = nowStr();
    const QString orderNo = "CH" + QDateTime::currentDateTime().toString("yyyyMMddHHmmss")
        + QString::number(userId);
    int orderId = 0;
    if (!db_->transaction([&] {
            if (!openOrder(userId).isEmpty())
                return false;
            const auto currentPile = db_->one("SELECT * FROM pile WHERE id=?", {pileId});
            if (currentPile.isEmpty()
                || currentPile.value("status").toString() != QString::fromUtf8("闲置"))
                return false;
            orderId = db_->execute(
                "INSERT INTO charge_order(order_no,user_id,pile_id,status,start_time,energy_kwh,amount,created_at) VALUES(?,?,?,?,?,?,?,?)",
                {orderNo, userId, pileId, QString::fromUtf8("充电中"), timestamp, 0, 0, timestamp});
            if (orderId <= 0)
                return false;
            if (db_->execute("UPDATE pile SET status=?, last_seen_at=? WHERE id=?",
                             {QString::fromUtf8("在用"), timestamp, pileId}) < 0)
                return false;
            return reservation.isEmpty()
                || db_->execute("UPDATE reservation SET status=? WHERE id=?",
                                {QString::fromUtf8("已履约"), reservation.value("id")}) >= 0;
        }))
        return {{"errorCode", 409}, {"error", QString::fromUtf8("开充未成功，请确认没有未完成订单且电桩空闲")}};
    const auto order = db_->one("SELECT * FROM charge_order WHERE id=?", {orderId});
    pile = db_->one("SELECT * FROM pile WHERE id=?", {pileId});
    return {{"order", publicOrder(order, pile, station, calculateLive(order, pile, station))}};
}

QJsonObject ChargeService::status(const QVariantMap &user) const
{
    const auto order = openOrder(user.value("id").toInt());
    if (order.isEmpty())
        return {{"order", QJsonValue::Null}};
    const auto pile = db_->one("SELECT * FROM pile WHERE id=?", {order.value("pile_id")});
    const auto station = db_->one("SELECT * FROM station WHERE id=?", {pile.value("station_id")});
    return {{"order", publicOrder(order, pile, station, calculateLive(order, pile, station))}};
}

QJsonObject ChargeService::stop(const QVariantMap &user)
{
    auto order = db_->one("SELECT * FROM charge_order WHERE user_id=? AND status='充电中' ORDER BY id DESC LIMIT 1",
                          {user.value("id")});
    if (order.isEmpty())
        return {{"errorCode", 404}, {"error", QString::fromUtf8("没有正在进行的充电")}};
    auto pile = db_->one("SELECT * FROM pile WHERE id=?", {order.value("pile_id")});
    const auto station = db_->one("SELECT * FROM station WHERE id=?", {pile.value("station_id")});
    const auto live = calculateLive(order, pile, station);
    const int minutes = qMax(1, live.value("seconds").toInt() / 60);
    const QString timestamp = nowStr();
    if (!db_->transaction([&] {
            if (db_->execute("UPDATE charge_order SET status=?, end_time=?, energy_kwh=?, amount=? WHERE id=?",
                             {QString::fromUtf8("待结算"), timestamp, live.value("energyKwh").toDouble(),
                              live.value("amount").toDouble(), order.value("id")}) < 0)
                return false;
            return db_->execute(
                "UPDATE pile SET status=?, total_charge_count=total_charge_count+1, total_charge_minutes=total_charge_minutes+?, last_seen_at=? WHERE id=?",
                {QString::fromUtf8("闲置"), minutes, timestamp, pile.value("id")}) >= 0;
        }))
        return {{"errorCode", 409}, {"error", QString::fromUtf8("结束充电未写入，请重试")}};
    order = db_->one("SELECT * FROM charge_order WHERE id=?", {order.value("id")});
    pile = db_->one("SELECT * FROM pile WHERE id=?", {pile.value("id")});
    return {{"order", publicOrder(order, pile, station, calculateLive(order, pile, station))}};
}

QJsonObject ChargeService::settle(const QVariantMap &inputUser)
{
    auto order = db_->one("SELECT * FROM charge_order WHERE user_id=? AND status='待结算' ORDER BY id DESC LIMIT 1",
                          {inputUser.value("id")});
    if (order.isEmpty())
        return {{"errorCode", 404}, {"error", QString::fromUtf8("没有待结算订单")}};
    auto user = db_->one("SELECT * FROM user WHERE id=?", {inputUser.value("id")});
    if (fenOf(user.value("balance").toDouble()) < fenOf(order.value("amount").toDouble()))
        return {{"errorCode", 402}, {"error", QString::fromUtf8("余额不足，请先充值后再结算")}};
    if (!db_->transaction([&] {
            const auto fresh = db_->one("SELECT balance FROM user WHERE id=?", {user.value("id")});
            if (fenOf(fresh.value("balance").toDouble()) < fenOf(order.value("amount").toDouble()))
                return false;
            const qint64 balance = fenOf(fresh.value("balance").toDouble())
                - fenOf(order.value("amount").toDouble());
            if (db_->execute("UPDATE user SET balance=? WHERE id=?",
                             {moneyFen(balance), user.value("id")}) < 0)
                return false;
            return db_->execute("UPDATE charge_order SET status=? WHERE id=?",
                                {QString::fromUtf8("已完成"), order.value("id")}) >= 0;
        }))
        return {{"errorCode", 409}, {"error", QString::fromUtf8("结算未写入，请重试")}};
    user = db_->one("SELECT * FROM user WHERE id=?", {user.value("id")});
    const auto pile = db_->one("SELECT * FROM pile WHERE id=?", {order.value("pile_id")});
    const auto station = db_->one("SELECT * FROM station WHERE id=?", {pile.value("station_id")});
    return {{"order", publicOrder(order, pile, station, calculateLive(order, pile, station))},
            {"user", sessions_->publicUser(user)}};
}

QJsonObject ChargeService::listOrders(const QVariantMap &user) const
{
    const auto rows = db_->query(
        "SELECT o.*, p.pile_no, p.type, p.power_kw, s.name, s.price_per_kwh FROM charge_order o JOIN pile p ON p.id=o.pile_id JOIN station s ON s.id=p.station_id WHERE o.user_id=? ORDER BY o.id DESC LIMIT 40",
        {user.value("id")});
    QJsonArray orders;
    for (const auto &order : rows) {
        QJsonObject live;
        if (order.value("status").toString() == QString::fromUtf8("充电中")
            || order.value("status").toString() == QString::fromUtf8("待结算"))
            live = calculateLive(order, order, order);
        else
            live = {{"seconds", 0}, {"energyKwh", order.value("energy_kwh").toDouble()},
                    {"amount", order.value("amount").toDouble()},
                    {"powerKw", order.value("power_kw").toDouble()},
                    {"pricePerKwh", order.value("price_per_kwh").toDouble()}};
        orders.append(publicOrder(order, order, order, live));
    }
    return {{"orders", orders}};
}

QJsonObject ChargeService::pushFor(int userId) const
{
    const auto user = db_->one("SELECT * FROM user WHERE id=?", {userId});
    return user.isEmpty() ? QJsonObject() : status(user);
}

QString ChargeService::forceStop(int orderId)
{
    const auto order = db_->one("SELECT * FROM charge_order WHERE id=?", {orderId});
    if (order.isEmpty()) return QString::fromUtf8("订单不存在");
    if (order.value("status").toString() != QString::fromUtf8("充电中"))
        return QString::fromUtf8("只有充电中的订单可以强制结束");
    const auto result = stop(db_->one("SELECT * FROM user WHERE id=?", {order.value("user_id")}));
    if (result.contains("error")) return result.value("error").toString();
    db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                 {"admin", QString::fromUtf8("强制结束充电"), order.value("order_no"),
                  QString::fromUtf8("待结算"), nowStr()});
    return QString::fromUtf8("已强制结束，订单进入待结算");
}

QString ChargeService::forceSettle(int orderId)
{
    const auto order = db_->one("SELECT * FROM charge_order WHERE id=?", {orderId});
    if (order.isEmpty()) return QString::fromUtf8("订单不存在");
    const auto user = db_->one("SELECT * FROM user WHERE id=?", {order.value("user_id")});
    if (order.value("status").toString() == QString::fromUtf8("充电中")) {
        const QString message = forceStop(orderId);
        if (!message.contains(QString::fromUtf8("待结算"))) return message;
    }
    const auto result = settle(user);
    if (result.contains("error")) return result.value("error").toString();
    db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                 {"admin", QString::fromUtf8("代结算"), order.value("order_no"),
                  QString::fromUtf8("已完成"), nowStr()});
    return QString::fromUtf8("已代结算并扣款");
}

void ChargeService::releaseStaleSession(int userId)
{
    const auto user = db_->one("SELECT * FROM user WHERE id=?", {userId});
    if (user.isEmpty()) return;
    const auto order = db_->one("SELECT * FROM charge_order WHERE user_id=? AND status='充电中' ORDER BY id DESC LIMIT 1",
                                {userId});
    if (order.isEmpty()) return;
    if (!stop(user).contains("error"))
        db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                     {"system", QString::fromUtf8("断线释放"), order.value("order_no"),
                      QString::fromUtf8("待结算"), nowStr()});
}
