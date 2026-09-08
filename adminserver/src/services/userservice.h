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
    UserService(Database *db, SessionService *sessions, StationService *stations,
                ReservationService *reservations, ChargeService *charges);
    QJsonObject updateProfile(const QVariantMap &user, const QJsonObject &data);
    QJsonObject recharge(const QVariantMap &user, const QJsonObject &data);
    QJsonObject closeAccount(const QVariantMap &user);
    QJsonObject listRecharge(const QVariantMap &user) const;
private:
    Database *db_;
    SessionService *sessions_;
    StationService *stations_;
    ReservationService *reservations_;
    ChargeService *charges_;
};

#endif

