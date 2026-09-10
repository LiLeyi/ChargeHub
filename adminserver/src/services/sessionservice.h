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
    bool success = false; ///< 是否通过 Token 和账号状态校验。
    int code = 401;       ///< 协议状态码：0 成功，401 失效，403 冻结/注销。
    QString message;      ///< 失败时可直接返回客户端的中文原因。
    QVariantMap user;     ///< 成功时的完整用户数据库行。
};

/** 用户登录、持久会话和公开用户快照的唯一所有者。 */
class SessionService {
public:
    /** @param db 非空数据库依赖，不接管所有权；构造时恢复未过期会话。 */
    explicit SessionService(Database *db);

    /** @param data 含 phone/password。@return 登录结果，成功 data 含 user 和新 Token。 */
    ServiceResult login(const QJsonObject &data);
    /** @param data 含 phone/password。@return 注册并自动登录的结果或格式/冲突错误。 */
    ServiceResult registerUser(const QJsonObject &data);
    /** @param token 客户端令牌。@return Token 对应用户及认证状态，并刷新滑动过期时间。 */
    AuthenticationResult authenticate(const QString &token) const;

    /** @param token 客户端令牌。@return 有效 Token 对应用户 ID；无效或过期返回 0。 */
    int userIdOfToken(const QString &token) const;
    /** @param userId 用户主键；删除该用户全部内存和数据库会话。 */
    void dropUser(int userId);
    /** @param user 完整用户数据库行。@return 去除密码等敏感字段的客户端 JSON。 */
    QJsonObject publicUser(const QVariantMap &user) const;

private:
    /** 从 session 表恢复 30 分钟内的会话，加载时顺便删除过期行。 */
    void loadSessions();
    /** @param token 会话令牌。@param userId 用户主键；插入或刷新 session 行。 */
    void persistSession(const QString &token, int userId) const;
    /** @param token 要从 session 表删除的令牌。 */
    void forgetSession(const QString &token) const;
    /** @param userId 用户主键。@return 32 位 UUID Token；同步写入内存和数据库。 */
    QString issueToken(int userId);

    Database *db_;
    mutable QMutex mutex_;
    mutable QHash<QString, int> tokenUser_;
    mutable QHash<QString, qint64> tokenAt_;
    mutable QHash<QString, qint64> tokenDbAt_;
};

#endif
