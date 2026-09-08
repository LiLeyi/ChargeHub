/**
 * @file usercontroller.cpp
 * @brief UserWindow 与 Client 之间的网络适配层。
 */
#include "usercontroller.h"

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

void UserController::updateSessionState(const QString &type, const QJsonObject &data)
{
    if (type == QLatin1String("LOGIN") || type == QLatin1String("REGISTER")) {
        token_ = data.value("token").toString();
        user_ = data.value("user").toObject();
        return;
    }
    if (type == QLatin1String("CLOSE_ACCOUNT")) {
        signOut();
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
