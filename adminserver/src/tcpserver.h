#ifndef CHARGEHUB_TCPSERVER_H
#define CHARGEHUB_TCPSERVER_H

/**
 * @file tcpserver.h
 * @brief 管理端里的用户接入层：监听 0.0.0.0:8888。
 *
 * 每来一个用户端 TCP 连接，就配一个 Protocol 拆包，再 Dispatch::handle，
 * 把响应写回同一条连接。不做业务判断。可同时接多个用户端。
 * 同一用户全部连接断开后 60 秒仍未重连，则把「充电中」订单收成待结算并释放桩。
 */

#include "dispatch.h"
#include "protocol.h"

#include <QHash>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

class TcpServer : public QTcpServer {
    Q_OBJECT
public:
    TcpServer(Dispatch *dispatch, QObject *parent = nullptr);

protected:
    void incomingConnection(qintptr handle) override;

private:
    void bindUser(QTcpSocket *socket, int userId);
    void scheduleRelease(int userId);
    void pushChargeTicks();
    bool userStillOnline(int userId, QTcpSocket *except = nullptr) const;
    Dispatch *dispatch_;
    QHash<QTcpSocket *, Protocol *> codecs_;
    QHash<QTcpSocket *, int> socketUser_;
    QHash<int, QTimer *> pendingRelease_;
    QTimer *pushTimer_ = nullptr;
};

#endif
