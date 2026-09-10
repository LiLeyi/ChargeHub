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
    /** @param db 数据库。@param sessions 会话服务。@param charges 充电服务；均仅借用。 */
    AdminService(Database *db, SessionService *sessions, ChargeService *charges);
    /** @param user 管理员名。@param password 明文密码。@return `{ok,message}` 登录结果。 */
    QJsonObject adminLogin(const QString &user, const QString &password);
    /** @param user 新管理员名。@param password 明文密码。@return 注册结果。 */
    QJsonObject adminRegister(const QString &user, const QString &password);
    /** 运营列表查询，不改变业务状态。 */
    QVector<QVariantMap> listPiles() const;
    QVector<QVariantMap> listStations() const;
    /** @param keyword 手机或昵称关键字；空值列出全部。@return 匹配用户行。 */
    QVector<QVariantMap> listUsers(const QString &keyword) const;
    /** 设备、用户和站点写操作；每次操作维持原审计日志语义。 */
    /** @param pileId 电桩主键。@return 空串成功，否则为拒绝原因。 */
    QString rebootPile(int pileId);
    /** @param userId 用户主键。@param freeze true 冻结、false 解冻。 */
    void freezeUser(int userId, bool freeze);
    /** @param data 电站字段及 pileCount。@return 新电站 ID，失败返回非正数。 */
    int addStation(const QVariantMap &data);
    /** @param stationId 电站主键。@return 空串成功，否则错误原因。 */
    QString applyDefaultTariff(int stationId);
    /** @param planId 调度建议主键。@return 空串成功，否则错误原因。 */
    QString adoptDispatchPlan(int planId);
    /** @param pileId 电桩主键。@return 空串成功，否则错误原因。 */
    QString markPileFault(int pileId);
    /** @param pileId 故障桩主键。@return 空串成功，否则错误原因。 */
    QString restorePile(int pileId);
    /** @param stationId 电站主键。@param data 新字段。@return 空串成功，否则错误原因。 */
    QString updateStation(int stationId, const QVariantMap &data);
    /** 审计与订单查询，limit 会被限制在安全范围内。 */
    /** @param limit 最大条数，内部限制安全范围。@return 最新审计行。 */
    QVector<QVariantMap> listAudit(int limit = 80) const;
    /** @param keyword 单号/手机/桩号关键字。@return 运营订单行。 */
    QVector<QVariantMap> listAdminOrders(const QString &keyword) const;
private:
    Database *db_; SessionService *sessions_; ChargeService *charges_;
};
#endif
