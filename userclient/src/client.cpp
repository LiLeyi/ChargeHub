/**
 * @file client.cpp
 * @brief 用户端套接字。构造时接好 connected/readyRead/error，界面只调 connectTo/request。
 */
#include "client.h"

#include <QAbstractSocket>
#include <QtGlobal>

Client::Client(QObject *parent) : QObject(parent)
{
    connect(&sock_, &QTcpSocket::connected, this, &Client::connected);
    connect(&sock_, &QTcpSocket::readyRead, this, [this]() {
        codec_.append(sock_.readAll());
        while (codec_.hasPacket())
            emit responded(codec_.nextPacket());
    });
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(&sock_, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        emit failed(sock_.errorString());
    });
#else
    connect(&sock_, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
            this, [this](QAbstractSocket::SocketError) { emit failed(sock_.errorString()); });
#endif
}

/** 先 abort 旧连接再拨号，避免两根线抢同一 Client。 */
void Client::connectTo(const QString &host, quint16 port)
{
    sock_.abort();
    sock_.connectToHost(host, port);
}

/** 仅当套接字已 Connected 才算通，连接中不算。 */
bool Client::isConnected() const
{
    return sock_.state() == QAbstractSocket::ConnectedState;
}

/** 未连接只报错，不把半包写进套接字。 */
int Client::request(const QString &type, const QJsonObject &data, const QString &token)
{
    ++seq_;
    QJsonObject payload{{"type", type}, {"seq", seq_}, {"role", "user"}, {"token", token}, {"data", data}};
    if (sock_.state() != QAbstractSocket::ConnectedState) {
        emit failed(QString::fromUtf8("未连接到服务器"));
        return seq_;
    }
    sock_.write(Protocol::pack(payload));
    sock_.flush();
    return seq_;
}
