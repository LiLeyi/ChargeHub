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
    /** 三个参数均为借用依赖：数据库、会话服务和预约服务。 */
    ChargeService(Database *db, SessionService *sessions, ReservationService *reservations);

    /** @param user 当前用户。@param data 含 pileId。@return 事务创建的充电订单或冲突错误。 */
    QJsonObject start(const QVariantMap &user, const QJsonObject &data);
    /** @param user 当前用户。@return 进行中/待结算订单的实时计费快照。 */
    QJsonObject status(const QVariantMap &user) const;
    /** @param user 当前用户。@return 停充后的待结算订单；事务中同时释放电桩。 */
    QJsonObject stop(const QVariantMap &user);
    /** @param user 当前用户。@return 扣款后的订单和用户余额；余额不足时不提交事务。 */
    QJsonObject settle(const QVariantMap &user);
    /** @param user 当前用户。@return 按时间倒序排列的历史订单。 */
    QJsonObject listOrders(const QVariantMap &user) const;
    /** @param userId 在线用户主键。@return PUSH_CHARGE 使用的实时订单数据。 */
    QJsonObject pushFor(int userId) const;

    /** @param userId 用户主键。@return 其充电中或待结算订单；不存在则为空 Map。 */
    QVariantMap openOrder(int userId) const;
    /** @param orderId 订单主键。@return 运营端强制停止结果文本。 */
    QString forceStop(int orderId);
    /** @param orderId 订单主键。@return 运营端代结算结果文本。 */
    QString forceSettle(int orderId);
    /** @param userId 断线超时用户；将充电中订单收尾并释放电桩。 */
    void releaseStaleSession(int userId);

private:
    /**
     * 统一实时计费入口：总费用 = 固定 ¥1.00 起步价 + 按小时切段的电量费。
     * START/STATUS/PUSH/STOP/SETTLE 必须共用此结果，保证展示金额与落库扣款一致。
     * @param order 订单数据库行，提供开始时间和已落库值。
     * @param pile 电桩数据库行，提供功率。
     * @param station 电站数据库行，提供基础电价和分时规则归属。
     * @return 电量、总价、当前电价、起步价及分时标签组成的 JSON。
     */
    QJsonObject calculateLive(const QVariantMap &order, const QVariantMap &pile,
                              const QVariantMap &station) const;
    /**
     * @param order 订单行。@param pile 电桩行。@param station 电站行。
     * @param live calculateLive 的结果。
     * @return 对客户端公开的标准订单对象。
     */
    static QJsonObject publicOrder(const QVariantMap &order, const QVariantMap &pile,
                                   const QVariantMap &station, const QJsonObject &live);

    Database *db_;
    SessionService *sessions_;
    ReservationService *reservations_;
};

#endif
