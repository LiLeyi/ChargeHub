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
    /** 记住 Database*，并 loadSessions() 把未过期 token 读回内存。 */
    explicit Dispatch(Database *db);

    /**
     * 用户端总入口。TcpServer::incomingConnection 每收到一帧就调用。
     * 流程：写操作查 8 秒幂等缓存（token|type|seq）→ LOGIN/REGISTER 或 requireUser
     *       → 对应业务函数 → ok/fail。
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
     * 无效、过期返回 0。与 userIdByToken 同类，对外给接入层。
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
    mutable QMutex sessionMutex_;
    mutable QHash<QString, int> tokenUser_;      ///< token → userId
    mutable QHash<QString, qint64> tokenAt_;     ///< token 内存签发时间
    mutable QHash<QString, qint64> tokenDbAt_;   ///< 与 session 表对齐用
    mutable QMutex idemMutex_;
    QHash<QString, QPair<qint64, QJsonObject>> idemCache_; ///< 写操作 8 秒回放

    /** 进程启动：从 session 表恢复 30 分钟内未过期的 token 到内存。 */
    void loadSessions();
    /** 把一对 token/userId 写入或更新 session 表。 */
    void persistSession(const QString &token, int userId) const;
    /** 从 session 表删除该 token。 */
    void forgetSession(const QString &token) const;
    /** 签发随机 token，记内存并 persistSession，TTL 30 分钟。登录/注册成功时调用。 */
    QString issueToken(int userId);
    /** 内存+过期校验。过期则删表。无效返回 0。 */
    int userIdByToken(const QString &token) const;
    /** 冻结或注销：作废该用户全部 token（内存 + session 表）。 */
    void dropUser(int userId);

    /** 成功回包：code=0，带 message 与 data。不写库。 */
    QJsonObject ok(const QString &type, int seq, const QString &msg, const QJsonObject &data) const;
    /** 失败回包：code 为 HTTP 风格业务码（401/403/409…）。不写库。 */
    QJsonObject fail(const QString &type, int seq, int code, const QString &msg) const;

    /**
     * 校验登录态：token 有效、用户存在、未冻结、未注销。
     * 成功返回 user 整行；失败 *err 填原因，返回空 map。
     */
    QVariantMap requireUser(const QString &token, QString *err) const;

    /** 回给用户端的公开字段（id/手机/昵称/余额/头像），不含 password_hash。 */
    QJsonObject publicUser(const QVariantMap &u) const;

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

    /**
     * 开充。先拦：已有未完成单 / 余额≤0 / 桩故障或在用 / 被他人预约。
     * 事务：插 charge_order(充电中)、pile→在用、本人预约→已履约。
     * 被 handle(START_CHARGE) 调用。
     */
    QJsonObject startCharge(const QVariantMap &user, const QJsonObject &data);

    /**
     * 查该用户未完成订单并用 calcLive 算当前费用。
     * 无单则 order=null。也被用户端开充前探测、PUSH 共用逻辑。
     * 被 handle(CHARGE_STATUS) 调用。
     */
    QJsonObject chargeStatus(const QVariantMap &user) const;

    /**
     * 充电中→待结算：写下电量费用，桩回闲置。无充电中单则失败。
     * 被 handle(STOP_CHARGE) 调用。
     */
    QJsonObject stopCharge(const QVariantMap &user);

    /**
     * 待结算扣余额（先分后元），订单→已完成。内部再读最新余额防脏读。
     * 被 handle(SETTLE_ORDER) 调用。
     */
    QJsonObject settle(const QVariantMap &user);

    /** 当前用户订单列表。被 handle(LIST_ORDERS) 调用。 */
    QJsonObject listOrders(const QVariantMap &user);
    /** 当前用户充值流水。被 handle(LIST_RECHARGE) 调用。 */
    QJsonObject listRecharge(const QVariantMap &user);
    /** 当前用户预约列表（会先过期处理）。被 handle(LIST_RESERVATIONS) 调用。 */
    QJsonObject listReservations(const QVariantMap &user);

    /**
     * 预约空闲桩。桩须闲置且无他人有效预约；写入 reservation(有效)。
     * 被 handle(RESERVE_PILE) 调用。
     */
    QJsonObject reservePile(const QVariantMap &user, const QJsonObject &data);

    /** 取消本人当前有效预约。被 handle(CANCEL_RESERVE) 调用。 */
    QJsonObject cancelReserve(const QVariantMap &user);

    /**
     * 提交评价：必须有文字。写 station_review，并浅层关键词情感写入 review_doc。
     * 被 handle(REVIEW_STATION) 调用。
     */
    QJsonObject reviewStation(const QVariantMap &user, const QJsonObject &data);

    /** 某桩的评价列表与均分、情感摘要。被 handle(LIST_PILE_REVIEWS) 调用。 */
    QJsonObject listPileReviews(const QVariantMap &user, const QJsonObject &data);

    /** 把已过 expire_at 且仍「有效」的预约标 no_show，别人就能用这根桩。 */
    void expireReservations() const;
    /** 该桩当前仍有效的预约行；没有则空 map。 */
    QVariantMap activeReserve(int pileId) const;
    /** 桩状态为闲置，且当前没有任何有效预约。开充对「预约就是你」另判。 */
    bool pileIsIdle(const QVariantMap &pile) const;
    /** 该用户「充电中或待结算」的那一单；没有则空。对应部分唯一索引。 */
    QVariantMap openOrder(int userId) const;

    /**
     * 实时计费：按小时切段。电量=功率kW×时长h；费用按该小时单价累加（先分后元）。
     * 有 tariff_rule 用谷/平/峰，否则用 station.price_per_kwh。
     * 被 startCharge / stopCharge / settle / chargeStatus / chargePushFor 共用。
     */
    QJsonObject calcLive(const QVariantMap &order, const QVariantMap &pile, const QVariantMap &station) const;

    /**
     * 订单对外字段：旧客户端认识的 id/orderNo/status/电量/金额保持不变，
     * 再并上 live（可含 currentPrice、tariffLabel）。
     */
    QJsonObject publicOrder(const QVariantMap &o, const QVariantMap &p, const QVariantMap &s, const QJsonObject &live) const;
};

#endif
