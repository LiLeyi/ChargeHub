/**
 * @file usercontroller.cpp
 * @brief UserWindow 与 Client 之间的网络适配层：挂起登录、注入 token、充电轮询、记住我（本地设置）。
 *
 * 信号流：Client::responded → handleResponse → updateSessionState →（开充探测）→ responded。
 * QSettings 组织名 ChargeHub、应用名 UserClient，与 Socket 信封无关。
 */

#include "usercontroller.h"

#include <QDateTime>
#include <QSettings>

namespace {
const QString kAutoLogin = QStringLiteral("auto_login");
const QString kAutoPhone = QStringLiteral("auto_login_phone");
const QString kAutoPassword = QStringLiteral("auto_login_password");
const QString kAutoExpiry = QStringLiteral("auto_login_expiry");
}

UserController::UserController(QObject *parent)
    : QObject(parent), client_(this)
{
    /** 连上立刻把挂起的 LOGIN/REGISTER 发出去，避免用户还要点第二次。 */
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

/** @brief 记下认证类型与口令；已连接则马上发出，否则等 connected。 */
void UserController::authenticate(const QString &type, const QString &phone,
                                  const QString &password)
{
    pendingAuth_ = type;
    pendingPhone_ = phone;
    pendingPassword_ = password;
    sendPendingAuth();
}

/** @brief 业务请求出口。未登录时 token_ 为空，服务端会 401（LOGIN 应走 authenticate）。 */
int UserController::request(const QString &type, const QJsonObject &data)
{
    return client_.request(type, data, token_);
}

/**
 * @brief 开充两步：pendingPileId_ + CHARGE_STATUS；回包里若已有订单则挡住，否则 START_CHARGE。
 */
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

/**
 * @brief 本机 7 天自动登录。不是协议字段，管理端重启与否都不读这些键。
 */
void UserController::saveCredentials(const QString &phone, const QString &password)
{
    QSettings settings(QStringLiteral("ChargeHub"), QStringLiteral("UserClient"));
    settings.setValue(kAutoLogin, true);
    settings.setValue(kAutoPhone, phone);
    settings.setValue(kAutoPassword, password);
    settings.setValue(kAutoExpiry,
                      QDateTime::currentDateTime().addDays(7).toString(Qt::ISODate));
}

bool UserController::loadCredentials(QString &phone, QString &password) const
{
    QSettings settings(QStringLiteral("ChargeHub"), QStringLiteral("UserClient"));
    if (!settings.value(kAutoLogin, false).toBool())
        return false;
    phone = settings.value(kAutoPhone).toString();
    password = settings.value(kAutoPassword).toString();
    return !phone.isEmpty() && !password.isEmpty();
}

bool UserController::isAutoLoginValid() const
{
    QSettings settings(QStringLiteral("ChargeHub"), QStringLiteral("UserClient"));
    if (!settings.value(kAutoLogin, false).toBool())
        return false;
    const QDateTime expiry =
        QDateTime::fromString(settings.value(kAutoExpiry).toString(), Qt::ISODate);
    return expiry.isValid() && QDateTime::currentDateTime() < expiry;
}

void UserController::clearCredentials()
{
    QSettings settings(QStringLiteral("ChargeHub"), QStringLiteral("UserClient"));
    settings.remove(kAutoLogin);
    settings.remove(kAutoPhone);
    settings.remove(kAutoPassword);
    settings.remove(kAutoExpiry);
}

/**
 * @brief 认证请求 token 必须空串。发出后立刻清 pendingPassword_，避免堆在内存里。
 */
void UserController::sendPendingAuth()
{
    if (pendingAuth_.isEmpty() || !client_.isConnected())
        return;
    const QString type = pendingAuth_;
    pendingAuth_.clear();
    client_.request(type,
                    QJsonObject{{"phone", pendingPhone_}, {"password", pendingPassword_}},
                    QString());
    pendingPassword_.clear();
}

/**
 * @brief 先落会话，再处理 beginCharge 的探测。探测命中未完成单时不把 CHARGE_STATUS 传给窗口当「刷新充电页」。
 */
void UserController::handleResponse(QJsonObject obj)
{
    const QString type = obj.value("type").toString();
    const bool ok = obj.value("code").toInt() == 0;
    const QJsonObject data = obj.value("data").toObject();
    if (ok)
        updateSessionState(type, data);

    if (type == QLatin1String("CHARGE_STATUS") && pendingPileId_ > 0) {
        const int pileId = pendingPileId_;
        pendingPileId_ = 0;
        if (!ok) {
            emit responded(obj);
            return;
        }
        const QJsonObject order = data.value("order").toObject();
        if (!order.isEmpty() && order.value("id").toInt() > 0) {
            emit chargeStartBlocked(order);
            return;
        }
        request(QStringLiteral("START_CHARGE"), QJsonObject{{"pileId", pileId}});
        return;
    }
    emit responded(obj);
}

/**
 * @brief 成功响应才更新。充电中启动 1s 轮询；结算或非充电状态停表。
 * PUSH_CHARGE 与 CHARGE_STATUS 共用 currentOrder_。
 */
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
