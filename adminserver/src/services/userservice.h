#ifndef CHARGEHUB_USERSERVICE_H
#define CHARGEHUB_USERSERVICE_H

#include <QJsonObject>
#include <QVariantMap>

class ChargeService;
class Database;
class ReservationService;
class SessionService;
class StationService;

/** 用户资料、余额充值、充值流水和账户注销。 */
class UserService {
public:
    /** 构造用户领域服务；所有指针均为借用依赖，不接管生命周期。 */
    UserService(Database *db, SessionService *sessions, StationService *stations,
                ReservationService *reservations, ChargeService *charges);
    /** @param user 当前用户。@param data 可含昵称、头像或地址。@return 更新后的公开用户。 */
    QJsonObject updateProfile(const QVariantMap &user, const QJsonObject &data);
    /** @param user 当前用户。@param data 含充值金额。@return 事务生成的流水及新余额。 */
    QJsonObject recharge(const QVariantMap &user, const QJsonObject &data);
    /** @param user 当前用户。@return 注销结果；同时取消预约、处理订单并清 Token。 */
    QJsonObject closeAccount(const QVariantMap &user);
    /** @param user 当前用户。@return 按时间倒序排列的充值记录数组。 */
    QJsonObject listRecharge(const QVariantMap &user) const;
private:
    Database *db_;
    SessionService *sessions_;
    StationService *stations_;
    ReservationService *reservations_;
    ChargeService *charges_;
};

#endif
