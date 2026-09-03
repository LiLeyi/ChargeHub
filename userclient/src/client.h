#ifndef CHARGEHUB_CLIENT_H
#define CHARGEHUB_CLIENT_H

/**
 * @file client.h
 * @brief 用户端 TCP 客户端。只连接管理端 IP:8888，不打开 .db。
 *
 * connectTo(host, port) 建立连接。
 * request(type, data, token) 发送一帧，seq 自增；响应经 responded 信号回到界面。
 */

#include "protocol.h"

#include <QJsonObject>
#include <QTcpSocket>

class Client : public QObject {
    Q_OBJECT
public:
    explicit Client(QObject *parent = nullptr);
    void connectTo(const QString &host, quint16 port);
    bool isConnected() const;
    int request(const QString &type, const QJsonObject &data, const QString &token);
signals:
    void connected();
    void responded(QJsonObject obj);
    void failed(QString msg);
private:
    QTcpSocket sock_;
    Protocol codec_;
    int seq_ = 0;
};

#endif
