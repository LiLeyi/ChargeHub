#ifndef CHARGEHUB_TCPSERVER_H
#define CHARGEHUB_TCPSERVER_H

/**
 * @file tcpserver.h
 * @brief 用户接入层：0.0.0.0:8888，只做拆包/回包/推送/断线计时。
 *
 * 【职责】把 TCP 字节变成 JSON 交给 Dispatch，再把 JSON 写回；不管怎么计费。
 * 【原理】一连接一个 Protocol；readyRead 循环 nextPacket→handle→pack。
 *         登录成功 bindUser；该用户最后一个连接断开后 60s 调 releaseStaleSession。
 *         另有 5s 定时器 pushChargeTicks，只推「充电中」的 PUSH_CHARGE（seq=0）。
 * 【协作】持有 Dispatch*。不写 SQL。管理端 MainWindow 不经过本类。
 * 【详见】docs/模块与协作说明.md
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
    void bindUser(QTcpSocket *socket, int userId);     ///< 登录成功后绑定连接与用户
    void scheduleRelease(int userId);                  ///< 全部断开后 60s 再释放充电
    void pushChargeTicks();                            ///< 每 5 秒推 PUSH_CHARGE
    bool userStillOnline(int userId, QTcpSocket *except = nullptr) const;
    Dispatch *dispatch_;
    QHash<QTcpSocket *, Protocol *> codecs_;
    QHash<QTcpSocket *, int> socketUser_;
    QHash<int, QTimer *> pendingRelease_;
    QTimer *pushTimer_ = nullptr;
};

#endif
