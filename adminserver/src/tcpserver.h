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
    /**
     * 记住 Dispatch，启动 5 秒推送定时器。
     * 真正开始听端口由 main.cpp 调用 listen(QHostAddress::Any, 8888)。
     */
    TcpServer(Dispatch *dispatch, QObject *parent = nullptr);

protected:
    /**
     * 来了一根新 TCP。为本连接建 QTcpSocket + Protocol：
     * readyRead 拆包 → Dispatch::handle → pack 写回；
     * 能从 token 认出用户则 bindUser；disconnected 则可能 scheduleRelease。
     */
    void incomingConnection(qintptr handle) override;

private:
    /**
     * 登录成功或后续请求带了有效 token：记下「这根线属于 userId」。
     * 若该用户正在 60 秒释放倒计时，则取消倒计时（人回来了）。
     */
    void bindUser(QTcpSocket *socket, int userId);

    /**
     * 这根线断了，且该用户已经没有别的线：启动 60 秒定时器。
     * 到点仍离线 → Dispatch::releaseStaleSession（视同停充）。
     */
    void scheduleRelease(int userId);

    /**
     * 每 5 秒：对每个在线且有「充电中」订单的用户，推一帧 PUSH_CHARGE。
     * 内容来自 Dispatch::chargePushFor；seq 固定 0。旧客户端可忽略。
     */
    void pushChargeTicks();

    /**
     * 该用户是否还有别的已绑定套接字。
     * except 表示「不要把这根即将断开的线算作在线」。
     */
    bool userStillOnline(int userId, QTcpSocket *except = nullptr) const;

    Dispatch *dispatch_;
    QHash<QTcpSocket *, Protocol *> codecs_;   ///< 每根线自己的拆包器
    QHash<QTcpSocket *, int> socketUser_;      ///< 线 → 用户 id
    QHash<int, QTimer *> pendingRelease_;      ///< 用户 → 60 秒释放倒计时
    QTimer *pushTimer_ = nullptr;              ///< 5 秒推送
};

#endif
