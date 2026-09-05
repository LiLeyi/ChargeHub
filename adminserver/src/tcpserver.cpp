/**
 * @file tcpserver.cpp
 * @brief 接入层实现：拆包→Dispatch::handle→回包；断线 60s 释放；5s 推送。
 *
 * incomingConnection 是核心循环。业务判断全部在 Dispatch，这里只搬运字节。
 */
#include "tcpserver.h"

#include "protocol.h"

#include <QJsonObject>

static const int kReleaseMs = 60 * 1000;

TcpServer::TcpServer(Dispatch *dispatch, QObject *parent)
    : QTcpServer(parent), dispatch_(dispatch)
{
    pushTimer_ = new QTimer(this);
    connect(pushTimer_, &QTimer::timeout, this, &TcpServer::pushChargeTicks);
    pushTimer_->start(5000);
}

/** 只给「充电中」用户推实时电量，seq=0，旧客户端可忽略。 */
void TcpServer::pushChargeTicks()
{
    for (auto it = socketUser_.constBegin(); it != socketUser_.constEnd(); ++it) {
        QTcpSocket *socket = it.key();
        const int uid = it.value();
        if (!socket || uid <= 0)
            continue;
        const QJsonObject data = dispatch_->chargePushFor(uid);
        const QJsonObject order = data.value("order").toObject();
        if (order.value("status").toString() != QString::fromUtf8("充电中"))
            continue;
        const QJsonObject push{{"type", QStringLiteral("PUSH_CHARGE")},
                               {"seq", 0},
                               {"code", 0},
                               {"message", QString::fromUtf8("充电中")},
                               {"data", data}};
        socket->write(Protocol::pack(push));
        socket->flush();
    }
}

/** 除 except 外是否还有绑定到该用户的套接字（一人可开两个用户端）。 */
bool TcpServer::userStillOnline(int userId, QTcpSocket *except) const
{
    for (auto it = socketUser_.constBegin(); it != socketUser_.constEnd(); ++it) {
        if (it.value() == userId && it.key() != except)
            return true;
    }
    return false;
}

/** 记下线→用户，并取消正在倒计时的断线释放。 */
void TcpServer::bindUser(QTcpSocket *socket, int userId)
{
    if (userId <= 0)
        return;
    socketUser_.insert(socket, userId);
    auto *old = pendingRelease_.take(userId);
    if (old) {
        old->stop();
        old->deleteLater();
    }
}

/** 该用户已无 TCP 时启动 60s 定时器，到点仍离线则停充待结算。 */
void TcpServer::scheduleRelease(int userId)
{
    if (userId <= 0 || userStillOnline(userId))
        return;
    if (pendingRelease_.contains(userId))
        return;
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    pendingRelease_.insert(userId, timer);
    connect(timer, &QTimer::timeout, this, [this, userId]() {
        pendingRelease_.remove(userId);
        if (!userStillOnline(userId))
            dispatch_->releaseStaleSession(userId);
        sender()->deleteLater();
    });
    timer->start(kReleaseMs);
}

/** 新连接：挂 Protocol，读到完整包就 handle 并写回，同时 bindUser。 */
void TcpServer::incomingConnection(qintptr handle)
{
    auto *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(handle)) {
        socket->deleteLater();
        return;
    }
    auto *codec = new Protocol;
    codecs_.insert(socket, codec);
    connect(socket, &QTcpSocket::readyRead, this, [this, socket, codec]() {
        codec->append(socket->readAll());
        while (codec->hasPacket()) {
            const QJsonObject req = codec->nextPacket();
            const QJsonObject resp = dispatch_->handle(req);
            socket->write(Protocol::pack(resp));
            socket->flush();
            int uid = 0;
            if (req.value("type").toString() == QLatin1String("LOGIN") && resp.value("code").toInt() == 0)
                uid = resp.value("data").toObject().value("user").toObject().value("id").toInt();
            else
                uid = dispatch_->userIdOfToken(req.value("token").toString());
            bindUser(socket, uid);
        }
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket, codec]() {
        const int uid = socketUser_.take(socket);
        codecs_.remove(socket);
        delete codec;
        socket->deleteLater();
        scheduleRelease(uid);
    });
}
