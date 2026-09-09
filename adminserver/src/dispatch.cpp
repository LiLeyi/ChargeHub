/**
 * @file dispatch.cpp
 * @brief Dispatch 实现。头文件写清职责与调用关系；这里是具体校验和 SQL。
 *
 * 读代码顺序建议：registerRoutes → startCharge / stopCharge / settle → calcLive。
 * 金额 fenOf/moneyFen 保证接口仍是元。详见 docs/模块与协作说明.md
 */
#include "dispatch.h"

#include "database.h"
#include "services/adminservice.h"
#include "services/analyticsservice.h"
#include "services/chargeservice.h"
#include "services/reservationservice.h"
#include "services/reviewservice.h"
#include "services/sessionservice.h"
#include "services/stationservice.h"
#include "services/userservice.h"
#include "transport/requestdispatcher.h"

namespace {
ServiceResult legacyResult(QJsonObject body, const QString &message = QStringLiteral("ok"),
                           int defaultErrorCode = 400)
{
    if (body.contains("error"))
        return ServiceResult::fail(body.value("errorCode").toInt(defaultErrorCode),
                                   body.value("error").toString());
    body.remove("errorCode");
    return ServiceResult::ok(body, message);
}
}

Dispatch::Dispatch(Database *db)
    : sessions_(std::make_unique<SessionService>(db)),
      reservations_(std::make_unique<ReservationService>(db)),
      stations_(std::make_unique<StationService>(db, reservations_.get())),
      charges_(std::make_unique<ChargeService>(db, sessions_.get(), reservations_.get())),
      admin_(std::make_unique<AdminService>(db, sessions_.get(), charges_.get())),
      analytics_(std::make_unique<AnalyticsService>(db)),
      users_(std::make_unique<UserService>(db, sessions_.get(), stations_.get(),
                                           reservations_.get(), charges_.get())),
      reviews_(std::make_unique<ReviewService>(db)),
      requestDispatcher_(std::make_unique<RequestDispatcher>(sessions_.get()))
{
    registerRoutes();
}

Dispatch::~Dispatch() = default;

void Dispatch::registerRoutes()
{
    using SR = ServiceResult;
    requestDispatcher_->addPublicRoute("LOGIN", [this](const QVariantMap &, const QJsonObject &data) {
        return sessions_->login(data);
    });
    requestDispatcher_->addPublicRoute("REGISTER", [this](const QVariantMap &, const QJsonObject &data) {
        return sessions_->registerUser(data);
    });

    const auto add = [this](const QString &type, bool mutating,
                            const RequestDispatcher::Handler &handler) {
        requestDispatcher_->addAuthenticatedRoute(type, mutating, handler);
    };
    add("UPDATE_PROFILE", false, [this](const QVariantMap &u, const QJsonObject &d) {
        return legacyResult(users_->updateProfile(u, d), QString::fromUtf8("保存成功"));
    });
    add("RECHARGE", true, [this](const QVariantMap &u, const QJsonObject &d) {
        return legacyResult(users_->recharge(u, d), QString::fromUtf8("充值成功"));
    });
    add("QUERY_STATIONS", false, [this](const QVariantMap &u, const QJsonObject &d) {
        return SR::ok(stations_->queryStations(u, d));
    });
    add("CLOSE_ACCOUNT", false, [this](const QVariantMap &u, const QJsonObject &) {
        return legacyResult(users_->closeAccount(u), QString::fromUtf8("账号已注销，历史订单与评价已留档"));
    });
    add("QUERY_PILES", false, [this](const QVariantMap &u, const QJsonObject &d) {
        return legacyResult(stations_->queryPiles(u, d), QStringLiteral("ok"), 404);
    });
    add("START_CHARGE", true, [this](const QVariantMap &u, const QJsonObject &d) {
        return legacyResult(charges_->start(u, d), QString::fromUtf8("充电已开始"));
    });
    add("CHARGE_STATUS", false, [this](const QVariantMap &u, const QJsonObject &) {
        const QJsonObject body = charges_->status(u);
        return SR::ok(body, body.value("order").isNull() ? QString::fromUtf8("无进行中订单")
                                                          : QStringLiteral("ok"));
    });
    add("STOP_CHARGE", true, [this](const QVariantMap &u, const QJsonObject &) {
        return legacyResult(charges_->stop(u), QString::fromUtf8("请结算订单"));
    });
    add("SETTLE_ORDER", true, [this](const QVariantMap &u, const QJsonObject &) {
        return legacyResult(charges_->settle(u), QString::fromUtf8("结算成功"));
    });
    add("LIST_ORDERS", false, [this](const QVariantMap &u, const QJsonObject &) {
        return SR::ok(charges_->listOrders(u));
    });
    add("LIST_RECHARGE", false, [this](const QVariantMap &u, const QJsonObject &) {
        return SR::ok(users_->listRecharge(u));
    });
    add("LIST_RESERVATIONS", false, [this](const QVariantMap &u, const QJsonObject &) {
        return SR::ok(reservations_->list(u));
    });
    add("RESERVE_PILE", true, [this](const QVariantMap &u, const QJsonObject &d) {
        return legacyResult(reservations_->reserve(u, d), QString::fromUtf8("预约成功，15分钟内有效"));
    });
    add("CANCEL_RESERVE", true, [this](const QVariantMap &u, const QJsonObject &) {
        return legacyResult(reservations_->cancel(u), QString::fromUtf8("已取消预约"));
    });
    add("REVIEW_STATION", false, [this](const QVariantMap &u, const QJsonObject &d) {
        QJsonObject body = reviews_->submit(u, d);
        const QString message = body.value("updated").toBool()
            ? QString::fromUtf8("已更新你对这根桩的评价") : QString::fromUtf8("评价已提交");
        return legacyResult(body, message);
    });
    add("LIST_PILE_REVIEWS", false, [this](const QVariantMap &u, const QJsonObject &d) {
        return legacyResult(reviews_->listForPile(u, d), QStringLiteral("ok"), 404);
    });
    add("HEARTBEAT", false, [](const QVariantMap &, const QJsonObject &) {
        return SR::ok();
    });
}

QJsonObject Dispatch::handle(const QJsonObject &request)
{
    return requestDispatcher_->handle(request);
}

int Dispatch::userIdOfToken(const QString &token) const
{
    return sessions_->userIdOfToken(token);
}

/** 只查 admin 表，SHA256 比对；失败文案统一「账号或密码错误」。 */
QJsonObject Dispatch::adminLogin(const QString &user, const QString &pwd)
{
    return admin_->adminLogin(user, pwd);
}

/** 写入 admin；用户名 3~16 位字母开头。 */
QJsonObject Dispatch::adminRegister(const QString &user, const QString &pwd)
{
    return admin_->adminRegister(user, pwd);
}

/** 今日/本月/累计营收与电量，给 KPI 和折线。 */
QJsonObject Dispatch::salesSummary() const
{
    return analytics_->salesSummary();
}

/** 闲置/在用/故障计数，给饼图。 */
QJsonObject Dispatch::pileStatusStats() const
{
    return analytics_->pileStatusStats();
}

/** 全部电桩含站名。 */
QVector<QVariantMap> Dispatch::listPiles() const
{
    return admin_->listPiles();
}

/** 全部电站。 */
QVector<QVariantMap> Dispatch::listStations() const
{
    return admin_->listStations();
}

/** 按手机号或昵称检索用户。 */
QVector<QVariantMap> Dispatch::listUsers(const QString &keyword) const
{
    return admin_->listUsers(keyword);
}

/** 故障桩改回闲置，写审计「远程重启」。 */
QString Dispatch::rebootPile(int pileId)
{
    return admin_->rebootPile(pileId);
}

/** 冻结则 dropUser；注销账号不能再改状态。 */
void Dispatch::freezeUser(int userId, bool freeze)
{
    admin_->freezeUser(userId, freeze);
}

/** 插 station，再按 pileCount 生成闲置桩。 */
int Dispatch::addStation(const QVariantMap &data)
{
    return admin_->addStation(data);
}

/** 谷 0–7/22–24、平 7–17、峰 17–22，系数乘站点标价。 */
/** 写入谷 0–7/22–24、平 7–17、峰 17–22。 */
QString Dispatch::applyDefaultTariff(int stationId)
{
    return admin_->applyDefaultTariff(stationId);
}

/** 采纳建议：保证有默认分时，再把该站峰价上浮并标 adopted。 */
QString Dispatch::adoptDispatchPlan(int planId)
{
    return admin_->adoptDispatchPlan(planId);
}

/** 推送用：等价于该用户的 chargeStatus。 */
QJsonObject Dispatch::chargePushFor(int userId) const
{
    return charges_->pushFor(userId);
}

/** 闲置桩标故障；充电中须先强制结束。 */
QString Dispatch::markPileFault(int pileId)
{
    return admin_->markPileFault(pileId);
}

/** 与 rebootPile 相同：故障→闲置。 */
QString Dispatch::restorePile(int pileId)
{
    return admin_->restorePile(pileId);
}

/** 改站名/地址/经纬/基准电价，写审计。 */
QString Dispatch::updateStation(int stationId, const QVariantMap &data)
{
    return admin_->updateStation(stationId, data);
}

/** 运营指定订单走 stopCharge，并写审计。 */
QString Dispatch::forceStopOrder(int orderId)
{
    return charges_->forceStop(orderId);
}

/** 充电中则先强制停，再 settle 扣款，写审计。 */
QString Dispatch::forceSettleOrder(int orderId)
{
    return charges_->forceSettle(orderId);
}

/** 最近若干条 audit_log，最多 200。 */
QVector<QVariantMap> Dispatch::listAudit(int limit) const
{
    return admin_->listAudit(limit);
}

/** 断线超时：对该用户充电中订单走 stopCharge，审计写「断线释放」。 */
void Dispatch::releaseStaleSession(int userId)
{
    charges_->releaseStaleSession(userId);
}

/** 运营侧评价情感汇总由 ReviewService 统一生成。 */
QVector<QVariantMap> Dispatch::listReviewNlp() const
{
    return reviews_->listNlp();
}


/** 营收 + 桩状态 + 最近分析报告，一次给驾驶舱。 */
QJsonObject Dispatch::cockpit() const
{
    return analytics_->cockpit();
}

/** 读 load_forecast。 */
QVector<QVariantMap> Dispatch::listForecasts() const
{
    return analytics_->listForecasts();
}

/** 读 hourly_load。 */
QVector<QVariantMap> Dispatch::listHourlyLoad() const
{
    return analytics_->listHourlyLoad();
}

/** 读 fault_risk。 */
QVector<QVariantMap> Dispatch::listFaultRisks() const
{
    return analytics_->listFaultRisks();
}

/** 读 analysis_alert。 */
QVector<QVariantMap> Dispatch::listAlerts() const
{
    return analytics_->listAlerts();
}

/** 读 dispatch_plan（含是否已采纳）。 */
QVector<QVariantMap> Dispatch::listDispatchPlan() const
{
    return analytics_->listDispatchPlan();
}

/** 运营侧按单号/手机/桩号筛订单。 */
QVector<QVariantMap> Dispatch::listAdminOrders(const QString &keyword) const
{
    return admin_->listAdminOrders(keyword);
}

/** 最近一行 analysis_report。 */
QVariantMap Dispatch::latestReport() const
{
    return analytics_->latestReport();
}

/** 清空分析表后按已完成订单重算预测、风险、告警、调度建议。不改账。 */
int Dispatch::refreshForecast()
{
    return analytics_->refreshForecast();
}
