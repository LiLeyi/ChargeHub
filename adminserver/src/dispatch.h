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
    QJsonObject adminLogin(const QString &user, const QString &pwd);
    QJsonObject adminRegister(const QString &user, const QString &pwd);
    QJsonObject salesSummary() const;
    QJsonObject pileStatusStats() const;
    QJsonObject cockpit() const;
    QVector<QVariantMap> listPiles() const;
    QVector<QVariantMap> listStations() const;
    QVector<QVariantMap> listUsers(const QString &keyword) const;
    QVector<QVariantMap> listForecasts() const;
    QVector<QVariantMap> listHourlyLoad() const;
    QVector<QVariantMap> listFaultRisks() const;
    QVector<QVariantMap> listAlerts() const;
    QVector<QVariantMap> listDispatchPlan() const;
    QVector<QVariantMap> listAdminOrders(const QString &keyword) const;
    QVector<QVariantMap> listReviewNlp() const;
    QVariantMap latestReport() const;
    QString rebootPile(int pileId);
    QString markPileFault(int pileId);
    QString restorePile(int pileId);
    QString updateStation(int stationId, const QVariantMap &data);
    QString forceStopOrder(int orderId);
    QString forceSettleOrder(int orderId);
    QVector<QVariantMap> listAudit(int limit = 80) const;
    int userIdOfToken(const QString &token) const;
    void releaseStaleSession(int userId);
    void freezeUser(int userId, bool freeze);
    int addStation(const QVariantMap &data);
    int refreshForecast();
    QString applyDefaultTariff(int stationId);
    QString adoptDispatchPlan(int planId);
    QJsonObject chargePushFor(int userId) const;

private:
    Database *db_;
    mutable QMutex sessionMutex_;
    mutable QHash<QString, int> tokenUser_;
    mutable QHash<QString, qint64> tokenAt_;
    mutable QHash<QString, qint64> tokenDbAt_;
    mutable QMutex idemMutex_;
    QHash<QString, QPair<qint64, QJsonObject>> idemCache_;
    void loadSessions();
    void persistSession(const QString &token, int userId) const;
    void forgetSession(const QString &token) const;
    QString issueToken(int userId);
    int userIdByToken(const QString &token) const;
    void dropUser(int userId);
    QJsonObject ok(const QString &type, int seq, const QString &msg, const QJsonObject &data) const;
    QJsonObject fail(const QString &type, int seq, int code, const QString &msg) const;
    QVariantMap requireUser(const QString &token, QString *err) const;
    QJsonObject publicUser(const QVariantMap &u) const;
    QJsonObject recharge(const QVariantMap &user, const QJsonObject &data);
    QJsonObject updateProfile(const QVariantMap &user, const QJsonObject &data);
    QJsonObject queryStations(const QVariantMap &user, const QJsonObject &data);
    QJsonObject closeAccount(const QVariantMap &user);
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
    void expireReservations() const;
    QVariantMap activeReserve(int pileId) const;
    bool pileIsIdle(const QVariantMap &pile) const;
    QVariantMap openOrder(int userId) const;
    QJsonObject calcLive(const QVariantMap &order, const QVariantMap &pile, const QVariantMap &station) const;
    QJsonObject publicOrder(const QVariantMap &o, const QVariantMap &p, const QVariantMap &s, const QJsonObject &live) const;
};

#endif
