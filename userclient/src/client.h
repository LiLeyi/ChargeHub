#ifndef CHARGEHUB_CLIENT_H
#define CHARGEHUB_CLIENT_H

/**
 * @file client.h
 * @brief 用户端唯一网络出口：连管理端，不打开数据库。
 *
 * 【职责】维护一条 QTcpSocket；发出业务帧；把完整 JSON 用 responded 交给界面。
 * 【原理】request() 自增 seq，pack 后 write。readyRead 里 Protocol::append，
 *         有完整包就 emit responded（含 PUSH_CHARGE）。
 * 【协作】只被 UserWindow 使用。失败走 failed 信号，界面弹窗，不改库。
 * 【详见】docs/模块与协作说明.md
 */

#include "protocol.h"

#include <QJsonObject>
#include <QTcpSocket>

class Client : public QObject {
    Q_OBJECT
public:
    explicit Client(QObject *parent = nullptr);
    void connectTo(const QString &host, quint16 port); ///< 连管理端，默认 8888
    bool isConnected() const;
    /** 发送一帧业务请求，返回本次 seq。 */
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
