/**
 * @file tcpserver.cpp
 * @brief 接入层实现：拆包 → Dispatch::handle → 回包；断线 60s 释放；5s 推送 PUSH_CHARGE。
 *
 * 读代码顺序：构造（开推送钟）→ incomingConnection（主循环）→ bindUser / scheduleRelease。
 * 业务判断全部在 Dispatch / RequestDispatcher / services，这里只搬运字节和记「谁在线上」。
 *
 * 推送不是路由表里的 type：客户端发 PUSH_CHARGE 会被当成未知请求（需登录后 400）。
 * 客户端应只接收，不要主动发这个 type。
 */
#include "tcpserver.h"

#include "protocol.h"

#include <QJsonObject>

/** 同一用户全部 TCP 断开后，等多久才视同停充。与 HEARTBEAT 文档中的 60 秒一致。 */
static const int kReleaseMs = 60 * 1000;

TcpServer::TcpServer(Dispatch *dispatch, QObject *parent)
    : QTcpServer(parent), dispatch_(dispatch)
{
    pushTimer_ = new QTimer(this);
    connect(pushTimer_, &QTimer::timeout, this, &TcpServer::pushChargeTicks);
    pushTimer_->start(5000);
}

/**
 * @brief 只给「充电中」用户推实时电量。seq 固定 0，旧客户端可忽略 type。
 *
 * 遍历的是 socketUser_（已 bind 的线）。同一用户两根线会各收到一份推送，这是有意的。
 * write 失败不在这里重试；下一轮 5s 再推。
 */
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

/**
 * @brief 除 except 外是否还有绑定到该用户的套接字（一人可开两个用户端）。
 */
bool TcpServer::userStillOnline(int userId, QTcpSocket *except) const
{
    for (auto it = socketUser_.constBegin(); it != socketUser_.constEnd(); ++it) {
        if (it.value() == userId && it.key() != except)
            return true;
    }
    return false;
}

/**
 * @brief 记下线→用户，并取消正在倒计时的断线释放。
 */
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

/**
 * @brief 该用户已无 TCP 时启动 60s 定时器，到点仍离线则停充待结算。
 *
 * 重复 disconnect 不会叠两个钟：contains 则直接 return。
 * timeout 里再次 userStillOnline，防止倒计时期间另一根线 bind 上了。
 */
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

/**
 * @brief 新连接：挂 Protocol，读到完整包就 handle 并写回，同时 bindUser。
 *
 * handle 是同步的：计费、SQL 都在这个槽里跑完再回包。作业并发量很小，不用工作线程。
 * LOGIN 成功必须从响应里取 user.id，因为请求还没有 token。
 */
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
