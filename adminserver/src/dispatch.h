#ifndef CHARGEHUB_DISPATCH_H
#define CHARGEHUB_DISPATCH_H

/**
 * @file dispatch.h
 * @brief 业务门面：组合请求路由、会话服务和现有领域业务。
 *
 * 【职责】所有会改余额 / 订单 / 桩状态的规则都在这里。界面和 Socket 只把参数传进来。
 * 【原理】
 *   - 用户端：TcpServer 拆包后调用 handle()，由 RequestDispatcher 路由和鉴权。
 *   - 管理端：MainWindow 同进程直接调 listPiles / forceSettleOrder，不走 8888。
 *   - 写库一律 Database::transaction，失败整笔回滚。
 *   - 金额内部用「分」，回包仍是元，旧客户端不用改。
 * 【协作】依赖 Database、SessionService、RequestDispatcher；被 TcpServer、MainWindow 调用。
 * 【详见】docs/模块与协作说明.md
 */

#include <memory>

#include <QJsonObject>
#include <QString>
#include <QVariantMap>
#include <QVector>

class Database;
class ChargeService;
class RequestDispatcher;
class ReservationService;
class SessionService;

class Dispatch {
public:
    /** 组合数据库、会话服务和请求路由。 */
    explicit Dispatch(Database *db);
    ~Dispatch();

    /**
     * 用户端总入口。TcpServer::incomingConnection 每收到一帧就调用。
     * 流程：RequestDispatcher 查幂等缓存、鉴权并调用注册的领域处理函数。
     * @param req  已拆好的 JSON：type / seq / token / data
     * @return     统一回包 {type,seq,code,message,data}，code=0 成功
     */
    QJsonObject handle(const QJsonObject &req);

    /**
     * 运营登录。只查 admin 表，密码 SHA256。
     * 调用者：main.cpp 登录框，不经 Socket。
     * @return data 含 ok；失败带 message
     */
    QJsonObject adminLogin(const QString &user, const QString &pwd);

    /**
     * 注册管理员。用户名唯一。两次密码须在界面先对齐，这里只收最终口令。
     * 调用者：main.cpp。写 admin 表。
     */
    QJsonObject adminRegister(const QString &user, const QString &pwd);

    /**
     * 今日 / 本月 / 累计营收与电量，以及折线图点。
     * 读已完成 charge_order。调用者：MainWindow::refresh、驾驶舱。
     */
    QJsonObject salesSummary() const;

    /**
     * 电桩闲置 / 在用 / 故障数量，给饼图。
     * 读 pile.status。
     */
    QJsonObject pileStatusStats() const;

    /**
     * 驾驶舱一次打包：营收、桩状态、最近 analysis_report。
     * 避免界面连打好几次查询。
     */
    QJsonObject cockpit() const;

    /** 全部电桩（含站名），运营「电桩」表。读 pile JOIN station。 */
    QVector<QVariantMap> listPiles() const;
    /** 全部电站，运营「电站」表。读 station。 */
    QVector<QVariantMap> listStations() const;
    /** 用户检索。keyword 匹配手机号或昵称；空则列出一批。读 user。 */
    QVector<QVariantMap> listUsers(const QString &keyword) const;
    /** 窗口负荷预测行。读 load_forecast。 */
    QVector<QVariantMap> listForecasts() const;
    /** 0~23 点分时负荷。读 hourly_load。 */
    QVector<QVariantMap> listHourlyLoad() const;
    /** 故障风险列表。读 fault_risk。 */
    QVector<QVariantMap> listFaultRisks() const;
    /** 分析告警。读 analysis_alert。 */
    QVector<QVariantMap> listAlerts() const;
    /** 调度建议（含是否已采纳）。读 dispatch_plan。 */
    QVector<QVariantMap> listDispatchPlan() const;
    /** 运营侧订单，keyword 匹配单号/手机/桩号。读 charge_order。 */
    QVector<QVariantMap> listAdminOrders(const QString &keyword) const;
    /** 评价情感汇总（正面/中性/负面计数与关键词）。读 station_review / review_doc。 */
    QVector<QVariantMap> listReviewNlp() const;
    /** 最近一份分析报告（MAE/RMSE）。读 analysis_report，没有则空 map。 */
    QVariantMap latestReport() const;

    /**
     * 远程重启：故障桩改回闲置，写 audit_log「远程重启」。
     * @return 空串成功，否则错误原因给界面弹。
     */
    QString rebootPile(int pileId);

    /**
     * 标记故障。充电中的桩会拒绝，须运营先强制结束订单。
     * 闲置/已故障：pile.status=故障，写 fault_code/fault_at、audit_log。
     */
    QString markPileFault(int pileId);

    /** 故障桩恢复闲置。写 pile.status、audit_log。 */
    QString restorePile(int pileId);

    /**
     * 改站名 / 地址 / 经纬度 / 基准电价。
     * data 键与界面表单对应。写 station。
     */
    QString updateStation(int stationId, const QVariantMap &data);

    /**
     * 运营强制结束充电：充电中 → 待结算，桩回闲置，calcLive 落电量费用。
     * 与用户 stopCharge 同类，但由订单 id 指定、写审计。
     */
    QString forceStopOrder(int orderId);

    /**
     * 运营代结算：待结算 → 已完成，按分扣该用户余额。
     * 余额不够也会尽量按规则处理并写审计，供现场收尾。
     */
    QString forceSettleOrder(int orderId);

    /** 最近 limit 条运营审计。读 audit_log，默认 80。 */
    QVector<QVariantMap> listAudit(int limit = 80) const;

    /**
     * token → 用户 id。TcpServer 绑定连接、推送时用。
     * 无效、过期返回 0。实现委托给 SessionService。
     */
    int userIdOfToken(const QString &token) const;

    /**
     * 断线释放：该用户充电中订单改为待结算、桩回闲置、写审计「断线释放」。
     * 调用者：TcpServer 在该用户全部 TCP 断开且 60s 未重连之后。
     */
    void releaseStaleSession(int userId);

    /**
     * 冻结 / 解冻。注销账号不可再冻。冻结时 dropUser 作废全部 token。
     * 写 user.status、audit_log。
     */
    void freezeUser(int userId, bool freeze);

    /**
     * 新建电站，并按 data["pileCount"] 生成闲置桩。
     * @return 新站 id；失败 ≤0。
     */
    int addStation(const QVariantMap &data);

    /**
     * 重算预测、分时负荷、风险、告警、调度建议、analysis_report。
     * 先清空分析表再写入。不改 charge_order / user.balance / pile.status。
     * @return 写入的预测行数一类计数，给界面提示。
     */
    int refreshForecast();

    /**
     * 为该站写入默认谷/平/峰规则（0–7/22–24 谷，7–17 平，17–22 峰）。
     * 调用者：MainWindow::enableTariff。写 tariff_rule。
     */
    QString applyDefaultTariff(int stationId);

    /**
     * 采纳一条调度建议：该站峰价上浮，plan.adopted=1。
     * 调用者：MainWindow::adoptPlan。写 tariff_rule、dispatch_plan。
     */
    QString adoptDispatchPlan(int planId);

    /**
     * 给推送用的订单快照（含 calcLive）。
     * 调用者：TcpServer::pushChargeTicks。无充电中订单则 data.order 为空。
     */
    QJsonObject chargePushFor(int userId) const;

private:
    Database *db_;
    std::unique_ptr<SessionService> sessions_;
    std::unique_ptr<ReservationService> reservations_;
    std::unique_ptr<ChargeService> charges_;
    std::unique_ptr<RequestDispatcher> requestDispatcher_;

    /** 注册用户端协议路由，业务实现仍由各领域函数承接。 */
    void registerRoutes();

    /**
     * 模拟充值。单笔 0~10000 元。事务：加 user.balance + 插 recharge_log。
     * 被 handle(RECHARGE) 调用。
     */
    QJsonObject recharge(const QVariantMap &user, const QJsonObject &data);

    /**
     * 改昵称和/或头像。头像压成小 JPEG 写入 user_avatar，avatar_path='db'。
     * 被 handle(UPDATE_PROFILE) 调用。
     */
    QJsonObject updateProfile(const QVariantMap &user, const QJsonObject &data);

    /**
     * 按地址关键字或半径找附近电站，可带用户定位。
     * 被 handle(QUERY_STATIONS) 调用。只读 station。
     */
    QJsonObject queryStations(const QVariantMap &user, const QJsonObject &data);

    /**
     * 注销：status=注销，dropUser，历史订单保留。同一手机号不能再注册。
     * 被 handle(CLOSE_ACCOUNT) 调用。
     */
    QJsonObject closeAccount(const QVariantMap &user);

    /**
     * 列出某站的桩，并标出是否被预约、当前用户能不能用。
     * 先 expireReservations。被 handle(QUERY_PILES) 调用。
     */
    QJsonObject queryPiles(const QVariantMap &user, const QJsonObject &data);

    /** 当前用户充值流水。被 handle(LIST_RECHARGE) 调用。 */
    QJsonObject listRecharge(const QVariantMap &user);
    /**
     * 提交评价：必须有文字。写 station_review，并浅层关键词情感写入 review_doc。
     * 被 handle(REVIEW_STATION) 调用。
     */
    QJsonObject reviewStation(const QVariantMap &user, const QJsonObject &data);

    /** 某桩的评价列表与均分、情感摘要。被 handle(LIST_PILE_REVIEWS) 调用。 */
    QJsonObject listPileReviews(const QVariantMap &user, const QJsonObject &data);

};

#endif
