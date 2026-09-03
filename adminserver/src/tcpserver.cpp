/**
 * @file tcpserver.cpp
 * @brief 接受用户端连接，解码后交给 Dispatch
 */
#include "tcpserver.h"

#include "protocol.h"

TcpServer::TcpServer(Dispatch *dispatch, QObject *parent)
    : QTcpServer(parent), dispatch_(dispatch)
{
}

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
            const QJsonObject resp = dispatch_->handle(codec->nextPacket());
            socket->write(Protocol::pack(resp));
            socket->flush();
        }
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket, codec]() {
        codecs_.remove(socket);
        delete codec;
        socket->deleteLater();
    });
}
