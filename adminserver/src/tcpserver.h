#ifndef CHARGEHUB_TCPSERVER_H
#define CHARGEHUB_TCPSERVER_H

/**
 * @file tcpserver.h
 * @brief 管理端里的用户接入层：监听 0.0.0.0:8888。
 *
 * 每来一个用户端 TCP 连接，就配一个 Protocol 拆包，再 Dispatch::handle，
 * 把响应写回同一条连接。不做业务判断。可同时接多个用户端。
 */

#include "dispatch.h"
#include "protocol.h"

#include <QHash>
#include <QTcpServer>
#include <QTcpSocket>

class TcpServer : public QTcpServer {
    Q_OBJECT
public:
    TcpServer(Dispatch *dispatch, QObject *parent = nullptr);

protected:
    void incomingConnection(qintptr handle) override;

private:
    Dispatch *dispatch_;
    QHash<QTcpSocket *, Protocol *> codecs_;
};

#endif
