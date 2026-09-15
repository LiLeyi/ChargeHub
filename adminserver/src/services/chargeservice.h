#ifndef CHARGEHUB_CHARGESERVICE_H
#define CHARGEHUB_CHARGESERVICE_H

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

class Database;
class ReservationService;
class SessionService;

/** 充电订单、实时计费、结算及其运营收尾操作。 */
class ChargeService {
public:
    ChargeService(Database *db, SessionService *sessions, ReservationService *reservations);

    QJsonObject start(const QVariantMap &user, const QJsonObject &data);
    QJsonObject status(const QVariantMap &user) const;
    QJsonObject stop(const QVariantMap &user);
    QJsonObject settle(const QVariantMap &user);
    QJsonObject listOrders(const QVariantMap &user) const;
    QJsonObject pushFor(int userId) const;

    QVariantMap openOrder(int userId) const;
    QString forceStop(int orderId);
    QString forceSettle(int orderId);
    void releaseStaleSession(int userId);

private:
    /**
     * 统一实时计费入口：总费用 = 固定 ¥1.00 起步价 + 按小时切段的电量费。
     * START/STATUS/PUSH/STOP/SETTLE 必须共用此结果，保证展示金额与落库扣款一致。
     */
    QJsonObject calculateLive(const QVariantMap &order, const QVariantMap &pile,
                              const QVariantMap &station) const;
    static QJsonObject publicOrder(const QVariantMap &order, const QVariantMap &pile,
                                   const QVariantMap &station, const QJsonObject &live);

    Database *db_;
    SessionService *sessions_;
    ReservationService *reservations_;
};

#endif
