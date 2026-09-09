/**
 * @file usercontroller.cpp
 * @brief UserWindow 与 Client 之间的网络适配层。
 */
#include "usercontroller.h"

#include <QDateTime>
#include <QSettings>

namespace {
const QString AUTO_LOGIN_KEY = QStringLiteral("auto_login");
const QString AUTO_LOGIN_PHONE = QStringLiteral("auto_login_phone");
const QString AUTO_LOGIN_PASSWORD = QStringLiteral("auto_login_password");
const QString AUTO_LOGIN_EXPIRY = QStringLiteral("auto_login_expiry");
const int AUTO_LOGIN_DAYS = 7;
}

UserController::UserController(QObject *parent)
    : QObject(parent), client_(this)
{
    connect(&client_, &Client::connected, this, [this] {
        emit connected();
        sendPendingAuth();
    });
    connect(&client_, &Client::responded,
            this, &UserController::handleResponse);
    connect(&client_, &Client::failed,
            this, &UserController::failed);
    connect(&chargePoll_, &QTimer::timeout, this, [this] {
        if (isAuthenticated()
            && currentOrder_.value("status").toString() == QString::fromUtf8("充电中")) {
            request(QStringLiteral("CHARGE_STATUS"));
        }
    });
}

void UserController::connectTo(const QString &host, quint16 port)
{
    client_.connectTo(host, port);
}

bool UserController::isConnected() const
{
    return client_.isConnected();
}

void UserController::authenticate(const QString &type, const QString &phone,
                                  const QString &password)
{
    pendingAuth_ = type;
    pendingPhone_ = phone;
    pendingPassword_ = password;
    sendPendingAuth();
}

int UserController::request(const QString &type, const QJsonObject &data)
{
    return client_.request(type, data, token_);
}

void UserController::beginCharge(int pileId)
{
    if (pileId <= 0)
        return;
    pendingPileId_ = pileId;
    request(QStringLiteral("CHARGE_STATUS"));
}

void UserController::signOut()
{
    chargePoll_.stop();
    token_.clear();
    user_ = {};
    currentOrder_ = {};
    pendingAuth_.clear();
    pendingPhone_.clear();
    pendingPassword_.clear();
    pendingPileId_ = 0;
}

void UserController::saveCredentials(const QString &phone, const QString &password)
{
    QSettings settings(QStringLiteral("ChargeHub"), QStringLiteral("UserClient"));
    settings.setValue(AUTO_LOGIN_KEY, true);
    settings.setValue(AUTO_LOGIN_PHONE, phone);
    settings.setValue(AUTO_LOGIN_PASSWORD, password);
    settings.setValue(AUTO_LOGIN_EXPIRY,
                     QDateTime::currentDateTime().addDays(AUTO_LOGIN_DAYS).toString(Qt::ISODate));
}

bool UserController::loadCredentials(QString &phone, QString &password)
{
    QSettings settings(QStringLiteral("ChargeHub"), QStringLiteral("UserClient"));
    if (!settings.value(AUTO_LOGIN_KEY, false).toBool())
        return false;
    phone = settings.value(AUTO_LOGIN_PHONE).toString();
    password = settings.value(AUTO_LOGIN_PASSWORD).toString();
    return !phone.isEmpty() && !password.isEmpty();
}

bool UserController::isAutoLoginValid() const
{
    QSettings settings(QStringLiteral("ChargeHub"), QStringLiteral("UserClient"));
    if (!settings.value(AUTO_LOGIN_KEY, false).toBool())
        return false;
    QString expiry = settings.value(AUTO_LOGIN_EXPIRY).toString();
    if (expiry.isEmpty())
        return false;
    QDateTime expiryDt = QDateTime::fromString(expiry, Qt::ISODate);
    return expiryDt.isValid() && QDateTime::currentDateTime() < expiryDt;
}

void UserController::clearCredentials()
{
    QSettings settings(QStringLiteral("ChargeHub"), QStringLiteral("UserClient"));
    settings.remove(AUTO_LOGIN_KEY);
    settings.remove(AUTO_LOGIN_PHONE);
    settings.remove(AUTO_LOGIN_PASSWORD);
    settings.remove(AUTO_LOGIN_EXPIRY);
}

void UserController::sendPendingAuth()
{
    if (pendingAuth_.isEmpty() || !client_.isConnected())
        return;
    const QString type = pendingAuth_;
    pendingAuth_.clear();
    client_.request(type,
                    QJsonObject{{"phone", pendingPhone_}, {"password", pendingPassword_}},
                    QString());
    // 不立即清空密码，以便失败时重试
}

void UserController::handleResponse(QJsonObject obj)
{
    const QString type = obj.value("type").toString();
    const int code = obj.value("code").toInt();
    const QJsonObject data = obj.value("data").toObject();

    // 处理认证失败
    if ((type == QLatin1String("LOGIN") || type == QLatin1String("REGISTER")) && code != 0) {
        // 如果是自动登录失败，静默清除凭证
        if (pendingAuth_.isEmpty()) {
            clearCredentials();
        }
        emit responded(obj);
        return;
    }

    // 处理Token过期 (401)
    if (code == 401 && !type.isEmpty() && type != QLatin1String("LOGIN")
        && type != QLatin1String("REGISTER")) {
        signOut();
        clearCredentials();
        emit sessionExpired();
        emit responded(obj);
        return;
    }

    // 处理账号封禁/冻结 (403)
    if (code == 403 && (type == QLatin1String("LOGIN") || type == QLatin1String("REGISTER"))) {
        QString message = obj.value("message").toString();
        clearCredentials();
        emit accountBlocked(message);
        emit responded(obj);
        return;
    }

    if (code != 0) {
        // 其他错误直接转发
        emit responded(obj);
        return;
    }

    if (code == 0)
        updateSessionState(type, data);

    // 处理登录/注册成功
    if (type == QLatin1String("LOGIN") || type == QLatin1String("REGISTER")) {
        // 如果登录成功且用户勾选了"记住我"，凭证已在调用时保存
        emit responded(obj);
        return;
    }

    // 处理充电开始请求
    if (type == QLatin1String("CHARGE_STATUS") && pendingPileId_ > 0) {
        const int pileId = pendingPileId_;
        pendingPileId_ = 0;
        const QJsonObject order = data.value("order").toObject();
        if (!order.isEmpty() && order.value("id").toInt() > 0) {
            emit chargeStartBlocked(order);
            emit responded(obj);
            return;
        }
        request(QStringLiteral("START_CHARGE"), QJsonObject{{"pileId", pileId}});
        return;
    }
    emit responded(obj);
}

void UserController::updateSessionState(const QString &type, const QJsonObject &data)
{
    if (type == QLatin1String("LOGIN") || type == QLatin1String("REGISTER")) {
        token_ = data.value("token").toString();
        user_ = data.value("user").toObject();
        return;
    }
    if (type == QLatin1String("CLOSE_ACCOUNT")) {
        signOut();
        clearCredentials();
        return;
    }
    if (type == QLatin1String("RECHARGE") || type == QLatin1String("UPDATE_PROFILE")
        || type == QLatin1String("SETTLE_ORDER")) {
        user_ = data.value("user").toObject();
    }
    if (type == QLatin1String("CHARGE_STATUS") || type == QLatin1String("PUSH_CHARGE")
        || type == QLatin1String("START_CHARGE") || type == QLatin1String("STOP_CHARGE")) {
        currentOrder_ = data.value("order").toObject();
        if (currentOrder_.value("status").toString() == QString::fromUtf8("充电中"))
            chargePoll_.start(1000);
        else
            chargePoll_.stop();
    } else if (type == QLatin1String("SETTLE_ORDER")) {
        currentOrder_ = {};
        chargePoll_.stop();
    }
}
