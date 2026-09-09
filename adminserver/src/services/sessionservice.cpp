#include "sessionservice.h"

#include "database.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QUuid>

namespace {
constexpr qint64 kSessionTtlSeconds = 30 * 60;
constexpr qint64 kPersistIntervalSeconds = 5 * 60;

QString nowStr()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

bool validPhone(const QString &phone)
{
    static const QRegularExpression expression(QStringLiteral("^1[3-9][0-9]{9}$"));
    return expression.match(phone).hasMatch();
}

bool strongRegisterPassword(const QString &password)
{
    if (password.size() < 6 || password.size() > 20)
        return false;
    bool hasUpper = false, hasLower = false, hasDigit = false;
    for (const QChar &ch : password) {
        if (ch.isUpper())
            hasUpper = true;
        else if (ch.isLower())
            hasLower = true;
        else if (ch.isDigit())
            hasDigit = true;
    }
    return hasUpper && hasLower && hasDigit;
}

double money(double value)
{
    return qRound64(value * 100.0) / 100.0;
}
}

SessionService::SessionService(Database *db) : db_(db)
{
    loadSessions();
}

void SessionService::loadSessions()
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const auto &row : db_->query("SELECT token, user_id, updated_at FROM session")) {
        const QDateTime at = QDateTime::fromString(row.value("updated_at").toString(),
                                                   QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (!at.isValid() || at.secsTo(QDateTime::currentDateTime()) > kSessionTtlSeconds) {
            db_->execute("DELETE FROM session WHERE token=?", {row.value("token")});
            continue;
        }
        const QString token = row.value("token").toString();
        tokenUser_.insert(token, row.value("user_id").toInt());
        tokenAt_.insert(token, at.toSecsSinceEpoch());
        tokenDbAt_.insert(token, now);
    }
}

void SessionService::persistSession(const QString &token, int userId) const
{
    db_->execute("INSERT OR REPLACE INTO session(token,user_id,updated_at) VALUES(?,?,?)",
                 {token, userId, nowStr()});
}

void SessionService::forgetSession(const QString &token) const
{
    db_->execute("DELETE FROM session WHERE token=?", {token});
}

QString SessionService::issueToken(int userId)
{
    const QString token = QUuid::createUuid().toString().remove('{').remove('}').remove('-');
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    {
        QMutexLocker locker(&mutex_);
        tokenUser_.insert(token, userId);
        tokenAt_.insert(token, now);
        tokenDbAt_.insert(token, now);
    }
    persistSession(token, userId);
    return token;
}

int SessionService::userIdOfToken(const QString &token) const
{
    if (token.isEmpty())
        return 0;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    int userId = 0;
    bool needLoad = false;
    bool expired = false;
    bool persist = false;
    {
        QMutexLocker locker(&mutex_);
        if (!tokenUser_.contains(token))
            needLoad = true;
        else if (now - tokenAt_.value(token) > kSessionTtlSeconds)
            expired = true;
        else {
            tokenAt_.insert(token, now);
            userId = tokenUser_.value(token);
            if (now - tokenDbAt_.value(token) >= kPersistIntervalSeconds) {
                tokenDbAt_.insert(token, now);
                persist = true;
            }
        }
    }

    if (needLoad) {
        const auto row = db_->one("SELECT user_id, updated_at FROM session WHERE token=?", {token});
        if (row.isEmpty())
            return 0;
        const QDateTime at = QDateTime::fromString(row.value("updated_at").toString(),
                                                   QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (!at.isValid() || at.secsTo(QDateTime::currentDateTime()) > kSessionTtlSeconds) {
            forgetSession(token);
            return 0;
        }
        userId = row.value("user_id").toInt();
        QMutexLocker locker(&mutex_);
        tokenUser_.insert(token, userId);
        tokenAt_.insert(token, now);
        tokenDbAt_.insert(token, now);
        return userId;
    }

    if (expired) {
        {
            QMutexLocker locker(&mutex_);
            tokenUser_.remove(token);
            tokenAt_.remove(token);
            tokenDbAt_.remove(token);
        }
        forgetSession(token);
        return 0;
    }
    if (persist)
        persistSession(token, userId);
    return userId;
}

AuthenticationResult SessionService::authenticate(const QString &token) const
{
    const int userId = userIdOfToken(token);
    if (!userId)
        return {false, 401, QString::fromUtf8("登录已失效，请重新登录"), {}};

    const auto user = db_->one("SELECT * FROM user WHERE id=?", {userId});
    if (user.isEmpty())
        return {false, 401, QString::fromUtf8("登录已失效，请重新登录"), {}};
    if (user.value("status").toString() == QString::fromUtf8("注销"))
        return {false, 403, QString::fromUtf8("账号已注销，历史数据已留档"), {}};
    if (user.value("status").toString() == QString::fromUtf8("冻结"))
        return {false, 403, QString::fromUtf8("账号已冻结，请联系管理员"), {}};
    return {true, 0, {}, user};
}

void SessionService::dropUser(int userId)
{
    QStringList tokens;
    {
        QMutexLocker locker(&mutex_);
        const auto keys = tokenUser_.keys();
        for (const QString &token : keys) {
            if (tokenUser_.value(token) == userId) {
                tokens.append(token);
                tokenUser_.remove(token);
                tokenAt_.remove(token);
                tokenDbAt_.remove(token);
            }
        }
    }
    for (const QString &token : tokens)
        forgetSession(token);
}

QJsonObject SessionService::publicUser(const QVariantMap &user) const
{
    QJsonObject object{
        {"id", user.value("id").toInt()},
        {"phone", user.value("phone").toString()},
        {"nickname", user.value("nickname").toString()},
        {"avatarPath", user.value("avatar_path").toString()},
        {"hasAvatar", false},
        {"balance", money(user.value("balance").toDouble())},
        {"status", user.value("status").toString()},
        {"createdAt", user.value("created_at").toString()},
        {"address", user.value("address").toString()},
        {"lat", user.contains("loc_lat") ? user.value("loc_lat").toDouble() : 39.9644},
        {"lng", user.contains("loc_lng") ? user.value("loc_lng").toDouble() : 116.3473},
        {"closeReason", user.value("close_reason").toString()},
        {"closedAt", user.value("closed_at").toString()},
    };
    const auto avatar = db_->one("SELECT mime, data FROM user_avatar WHERE user_id=?", {user.value("id")});
    if (!avatar.isEmpty() && !avatar.value("data").toByteArray().isEmpty()) {
        object.insert("hasAvatar", true);
        object.insert("avatarMime", avatar.value("mime").toString());
        object.insert("avatarBase64",
                      QString::fromLatin1(avatar.value("data").toByteArray().toBase64()));
    }
    return object;
}

ServiceResult SessionService::login(const QJsonObject &data)
{
    const QString phone = data.value("phone").toString().trimmed();
    const QString password = data.value("password").toString();
    if (!validPhone(phone))
        return ServiceResult::fail(400, QString::fromUtf8("请输入正确的手机号格式"));
    if (password.size() < 6 || password.size() > 20)
        return ServiceResult::fail(400, QString::fromUtf8("密码长度须为 6~20 位"));

    const auto user = db_->one("SELECT * FROM user WHERE phone=?", {phone});
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex());
    if (user.isEmpty() || user.value("password_hash").toString() != hash)
        return ServiceResult::fail(401, QString::fromUtf8("账号或密码错误"));
    if (user.value("status").toString() == QString::fromUtf8("注销"))
        return ServiceResult::fail(403, QString::fromUtf8("账号已注销，历史数据已留档，无法登录"));
    if (user.value("status").toString() == QString::fromUtf8("冻结"))
        return ServiceResult::fail(403, QString::fromUtf8("账号已冻结，请联系管理员"));

    const QString token = issueToken(user.value("id").toInt());
    return ServiceResult::ok({{"user", publicUser(user)}, {"token", token}, {"isNew", false}},
                             QString::fromUtf8("登录成功"));
}

ServiceResult SessionService::registerUser(const QJsonObject &data)
{
    const QString phone = data.value("phone").toString().trimmed();
    const QString password = data.value("password").toString();
    if (!validPhone(phone))
        return ServiceResult::fail(400, QString::fromUtf8("请输入正确的手机号格式"));
    if (!strongRegisterPassword(password))
        return ServiceResult::fail(400, QString::fromUtf8("注册密码须为 6～20 位，并同时包含大写字母、小写字母和数字"));

    const auto old = db_->one("SELECT status FROM user WHERE phone=?", {phone});
    if (!old.isEmpty()) {
        if (old.value("status").toString() == QString::fromUtf8("注销"))
            return ServiceResult::fail(409, QString::fromUtf8("该手机号已注销留档，无法再次注册，请联系管理员"));
        return ServiceResult::fail(409, QString::fromUtf8("该账号已注册"));
    }

    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex());
    const QString nickname = QString::fromUtf8("用户") + phone.right(4);
    const int userId = db_->execute(
        "INSERT INTO user(phone,nickname,avatar_path,password_hash,balance,status,created_at) VALUES(?,?,?,?,?,?,?)",
        {phone, nickname, "", hash, 0.0, QString::fromUtf8("正常"), nowStr()});
    const auto user = db_->one("SELECT * FROM user WHERE id=?", {userId});
    const QString token = issueToken(userId);
    return ServiceResult::ok({{"user", publicUser(user)}, {"token", token}, {"isNew", true}},
                             QString::fromUtf8("注册成功"));
}
