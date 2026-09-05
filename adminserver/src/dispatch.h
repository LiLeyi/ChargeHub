#ifndef CHARGEHUB_DISPATCH_H
#define CHARGEHUB_DISPATCH_H

/**
 * @file dispatch.h
 * @brief 全部业务规则。用户端报文和运营窗口都进这里，再写 Database。
 *
 * 对外两路调用：
 *   1) handle(req)     —— TcpServer 收到用户端 JSON 后调用（LOGIN/充电/预约等）
 *   2) adminLogin 等   —— 管理端 GUI 同进程直接调用，不走 Socket
 *
 * 会话：登录成功发 token，后续请求必须带 token。注销/冻结账号会 drop token。
 */

#include "database.h"

#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QPair>
#include <QString>
#include <QVariantMap>
#include <QVector>

class Dispatch {
public:
    explicit Dispatch(Database *db);
    /** 用户端请求入口。返回带 code/message/data 的 JSON。 */
    QJsonObject handle(const QJsonObject &req);
    /** 管理端登录，成功后本进程可直接调后续运营接口。 */
    QJsonObject adminLogin(const QString &user, const QString &pwd);
    /** 注册新管理员账号（演示用）。 */
    QJsonObject adminRegister(const QString &user, const QString &pwd);
    /** 今日 / 本月 / 累计营收与电量。 */
    QJsonObject salesSummary() const;
    /** 电桩闲置 / 在用 / 故障数量。 */
    QJsonObject pileStatusStats() const;
    /** 驾驶舱汇总：营收、桩状态、预测指标。 */
    QJsonObject cockpit() const;
    QVector<QVariantMap> listPiles() const;          ///< 全部电桩
    QVector<QVariantMap> listStations() const;       ///< 全部电站
    QVector<QVariantMap> listUsers(const QString &keyword) const; ///< 用户检索
    QVector<QVariantMap> listForecasts() const;      ///< 窗口负荷预测
    QVector<QVariantMap> listHourlyLoad() const;     ///< 分时负荷
    QVector<QVariantMap> listFaultRisks() const;     ///< 故障风险
    QVector<QVariantMap> listAlerts() const;         ///< 分析告警
    QVector<QVariantMap> listDispatchPlan() const;   ///< 调度建议
    QVector<QVariantMap> listAdminOrders(const QString &keyword) const; ///< 运营侧订单
    QVector<QVariantMap> listReviewNlp() const;      ///< 评价情感统计
    QVariantMap latestReport() const;                ///< 最近一份分析报告
    QString rebootPile(int pileId);                  ///< 远程重启（写审计）
    QString markPileFault(int pileId);               ///< 标记故障，占用中则先停充
    QString restorePile(int pileId);                 ///< 故障桩恢复闲置
    QString updateStation(int stationId, const QVariantMap &data); ///< 改站名/地址/电价
    QString forceStopOrder(int orderId);             ///< 运营强制结束充电
    QString forceSettleOrder(int orderId);           ///< 运营代结算
    QVector<QVariantMap> listAudit(int limit = 80) const; ///< 操作审计
    int userIdOfToken(const QString &token) const;   ///< token → 用户 id，无效为 0
    /** 用户全部 TCP 断线超时后：充电中订单改为待结算并释放桩。 */
    void releaseStaleSession(int userId);
    void freezeUser(int userId, bool freeze);        ///< 冻结 / 解冻（注销账号不可）
    int addStation(const QVariantMap &data);         ///< 新建站并按 pileCount 生成桩
    int refreshForecast();                           ///< 重算预测、高峰、调度建议
    QString applyDefaultTariff(int stationId);       ///< 写入默认谷/平/峰规则
    QString adoptDispatchPlan(int planId);           ///< 采纳建议，峰价上浮
    QJsonObject chargePushFor(int userId) const;     ///< 推送用的当前订单快照

private:
    Database *db_;
    mutable QMutex sessionMutex_;
    mutable QHash<QString, int> tokenUser_;
    mutable QHash<QString, qint64> tokenAt_;
    mutable QHash<QString, qint64> tokenDbAt_;
    mutable QMutex idemMutex_;
    QHash<QString, QPair<qint64, QJsonObject>> idemCache_;
    void loadSessions();                 ///< 启动时从 session 表恢复未过期 token
    void persistSession(const QString &token, int userId) const;
    void forgetSession(const QString &token) const;
    QString issueToken(int userId);      ///< 签发并落库，TTL 30 分钟
    int userIdByToken(const QString &token) const;
    void dropUser(int userId);           ///< 踢掉该用户全部 token
    QJsonObject ok(const QString &type, int seq, const QString &msg, const QJsonObject &data) const;
    QJsonObject fail(const QString &type, int seq, int code, const QString &msg) const;
    QVariantMap requireUser(const QString &token, QString *err) const; ///< 校验登录态
    QJsonObject publicUser(const QVariantMap &u) const; ///< 回给用户端的公开字段
    QJsonObject recharge(const QVariantMap &user, const QJsonObject &data);
    QJsonObject updateProfile(const QVariantMap &user, const QJsonObject &data);
    QJsonObject queryStations(const QVariantMap &user, const QJsonObject &data);
    QJsonObject closeAccount(const QVariantMap &user); ///< 注销留档，历史订单仍在
    QJsonObject queryPiles(const QVariantMap &user, const QJsonObject &data);
    QJsonObject startCharge(const QVariantMap &user, const QJsonObject &data);
    QJsonObject chargeStatus(const QVariantMap &user) const;
    QJsonObject stopCharge(const QVariantMap &user);
    QJsonObject settle(const QVariantMap &user);
    QJsonObject listOrders(const QVariantMap &user);
    QJsonObject listRecharge(const QVariantMap &user);
    QJsonObject listReservations(const QVariantMap &user);
    QJsonObject reservePile(const QVariantMap &user, const QJsonObject &data);
    QJsonObject cancelReserve(const QVariantMap &user);
    QJsonObject reviewStation(const QVariantMap &user, const QJsonObject &data);
    QJsonObject listPileReviews(const QVariantMap &user, const QJsonObject &data);
    void expireReservations() const;     ///< 超时预约标 no_show
    QVariantMap activeReserve(int pileId) const;
    bool pileIsIdle(const QVariantMap &pile) const;
    QVariantMap openOrder(int userId) const; ///< 未完成订单（充电中/待结算）
    /** 按功率与分时电价估算当前电量、费用；金额对外仍是元。 */
    QJsonObject calcLive(const QVariantMap &order, const QVariantMap &pile, const QVariantMap &station) const;
    QJsonObject publicOrder(const QVariantMap &o, const QVariantMap &p, const QVariantMap &s, const QJsonObject &live) const;
};

#endif
