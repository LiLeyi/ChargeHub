#ifndef CHARGEHUB_SESSIONSERVICE_H
#define CHARGEHUB_SESSIONSERVICE_H

#include "transport/serviceresult.h"

#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QVariantMap>

class Database;

struct AuthenticationResult {
    bool success = false;
    int code = 401;
    QString message;
    QVariantMap user;
};

/** 用户登录、持久会话和公开用户快照的唯一所有者。 */
class SessionService {
public:
    explicit SessionService(Database *db);

    ServiceResult login(const QJsonObject &data);
    ServiceResult registerUser(const QJsonObject &data);
    AuthenticationResult authenticate(const QString &token) const;

    int userIdOfToken(const QString &token) const;
    void dropUser(int userId);
    QJsonObject publicUser(const QVariantMap &user) const;

private:
    void loadSessions();
    void persistSession(const QString &token, int userId) const;
    void forgetSession(const QString &token) const;
    QString issueToken(int userId);

    Database *db_;
    mutable QMutex mutex_;
    mutable QHash<QString, int> tokenUser_;
    mutable QHash<QString, qint64> tokenAt_;
    mutable QHash<QString, qint64> tokenDbAt_;
};

#endif
