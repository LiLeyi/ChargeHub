/**
 * @file client.cpp
 * @brief 用户端套接字实现。界面 / UserController 只调 connectTo 与 request，禁止直接操作 sock_。
 *
 * Qt5.15 前后错误信号不同：新版 errorOccurred，旧版 overload 的 error。两种都转到 failed。
 * readyRead 里必须把 readAll 一次喂给 Protocol，再 while 取包，否则粘包会积在内核缓冲。
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

/**
 * @brief 先 abort 旧连接再 connectToHost，保证本 Client 始终最多一根 TCP。
 * @note 不在这里发 LOGIN；等 connected 信号后由 UserController::sendPendingAuth。
 */
void Client::connectTo(const QString &host, quint16 port)
{
    sock_.abort();
    sock_.connectToHost(host, port);
}

/**
 * @brief 仅 ConnectedState 为通。HostLookup / Connecting 时 request 必须失败而不是写半包。
 */
bool Client::isConnected() const
{
    return sock_.state() == QAbstractSocket::ConnectedState;
}

/**
 * @brief 组 {type,seq,role:user,token,data} 后 pack 写出。
 *
 * seq 在判断连接之前就自增：即使没连上，返回值仍是「本应使用的序号」，
 * 调用方若用 seq 做日志不会和下一次成功发送撞号。未连接不 write。
 */
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
