#ifndef CHARGEHUB_DISPATCH_H
#define CHARGEHUB_DISPATCH_H

/**
 * @file dispatch.h
 * @brief 全系统唯一业务层：登录、找桩、充电、结算、预约、资费、运营操作。
 *
 * 【职责】所有会改余额 / 订单 / 桩状态的规则都在这里。界面和 Socket 只把参数传进来。
 * 【原理】
 *   - 用户端：TcpServer 拆包后调用 handle()，按 JSON 的 type 分发到 startCharge 等。
 *   - 管理端：MainWindow 同进程直接调 listPiles / forceSettleOrder，不走 8888。
 *   - 写库一律 Database::transaction，失败整笔回滚。
 *   - 金额内部用「分」，回包仍是元，旧客户端不用改。
 * 【协作】依赖 Database；被 TcpServer、MainWindow 调用。不依赖 Qt 界面类。
 * 【详见】docs/模块与协作说明.md
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
    /**
     * 用户端总入口。
     * 流程：幂等缓存（写操作 8s）→ LOGIN/REGISTER 或 requireUser → 对应业务函数 → ok/fail。
     * 调用者：TcpServer::incomingConnection。
     */
    QJsonObject handle(const QJsonObject &req);
    /** 管理端登录。只认 admin 表。调用者：main.cpp 登录框、不经 Socket。 */
    QJsonObject adminLogin(const QString &user, const QString &pwd);
    /** 注册管理员。两次密码须在界面先对齐。调用者：main.cpp。 */
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
    /** token→用户 id。TcpServer 绑定连接、推送时用。无效为 0。 */
    int userIdOfToken(const QString &token) const;
    /**
     * 断线释放：充电中订单改为待结算、桩回闲置、写审计「断线释放」。
     * 调用者：TcpServer 在该用户全部 TCP 断开且 60s 未重连之后。
     */
    void releaseStaleSession(int userId);
    void freezeUser(int userId, bool freeze);        ///< 冻结 / 解冻（注销账号不可）
    int addStation(const QVariantMap &data);         ///< 新建站并按 pileCount 生成桩
    int refreshForecast();                           ///< 重算预测、高峰、调度建议
    /** 写入谷/平/峰默认规则。调用者：MainWindow::enableTariff。 */
    QString applyDefaultTariff(int stationId);
    /** 采纳调度建议，该站峰价上浮。调用者：MainWindow::adoptPlan。 */
    QString adoptDispatchPlan(int planId);
    /**
     * 给推送用的订单快照（含 calcLive）。
     * 调用者：TcpServer::pushChargeTicks；无充电中订单则 order 为空。
     */
    QJsonObject chargePushFor(int userId) const;

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
    /** 开充。先 openOrder/余额/桩闲置，再事务插单、桩改在用。被 handle(START_CHARGE) 调用。 */
    QJsonObject startCharge(const QVariantMap &user, const QJsonObject &data);
    /** 查未完成单 + calcLive。也被用户端开充前探测、PUSH 共用。 */
    QJsonObject chargeStatus(const QVariantMap &user) const;
    /** 充电中→待结算，写下电量费用，桩闲置。 */
    QJsonObject stopCharge(const QVariantMap &user);
    /** 待结算扣余额（分），订单已完成。内部会再读最新余额。 */
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
    /**
     * 实时计费：按小时切段，电量=功率kW×时长h，费用按该小时单价累加（先分后元）。
     * 被 startCharge / stopCharge / settle / chargeStatus / chargePushFor 共用。
     */
    QJsonObject calcLive(const QVariantMap &order, const QVariantMap &pile, const QVariantMap &station) const;
    /** 订单对外字段：旧字段保持，再并上 live（可含 currentPrice、tariffLabel）。 */
    QJsonObject publicOrder(const QVariantMap &o, const QVariantMap &p, const QVariantMap &s, const QJsonObject &live) const;
};

#endif
