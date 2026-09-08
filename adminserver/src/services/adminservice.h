#ifndef CHARGEHUB_ADMINSERVICE_H
#define CHARGEHUB_ADMINSERVICE_H
/**
 * @file adminservice.h
 * @brief 管理员认证与运营写操作；供 Dispatch 门面调用，不处理 TCP/JSON 信封。
 */
#include <QJsonObject>
#include <QString>
#include <QVariantMap>
#include <QVector>
class ChargeService; class Database; class SessionService;
class AdminService {
public:
    /** 依赖仅借用，生命周期由 Dispatch 保证。 */
    AdminService(Database *db, SessionService *sessions, ChargeService *charges);
    /** 管理员认证与注册；密码仍使用既有 SHA-256 存储协议。 */
    QJsonObject adminLogin(const QString &user, const QString &password);
    QJsonObject adminRegister(const QString &user, const QString &password);
    /** 运营列表查询，不改变业务状态。 */
    QVector<QVariantMap> listPiles() const;
    QVector<QVariantMap> listStations() const;
    QVector<QVariantMap> listUsers(const QString &keyword) const;
    /** 设备、用户和站点写操作；每次操作维持原审计日志语义。 */
    QString rebootPile(int pileId);
    void freezeUser(int userId, bool freeze);
    int addStation(const QVariantMap &data);
    QString applyDefaultTariff(int stationId);
    QString adoptDispatchPlan(int planId);
    QString markPileFault(int pileId);
    QString restorePile(int pileId);
    QString updateStation(int stationId, const QVariantMap &data);
    /** 审计与订单查询，limit 会被限制在安全范围内。 */
    QVector<QVariantMap> listAudit(int limit = 80) const;
    QVector<QVariantMap> listAdminOrders(const QString &keyword) const;
private:
    Database *db_; SessionService *sessions_; ChargeService *charges_;
};
#endif
