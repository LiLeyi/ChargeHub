#ifndef CHARGEHUB_TCPSERVER_H
#define CHARGEHUB_TCPSERVER_H

/**
 * @file tcpserver.h
 * @brief 用户接入层：监听 0.0.0.0:8888，只做拆包、回包、PUSH_CHARGE、断线 60 秒释放。
 *
 * 【职责】
 *   把 TCP 字节变成 QJsonObject 交给 Dispatch::handle，再把返回的 JSON pack 写回。
 *   不解释计费、不写 SQL、不画界面。管理端 MainWindow 与本类同进程，但操作走 Dispatch 方法，不经 8888。
 *
 * 【原理】
 *   incomingConnection：为每个 qintptr 建 QTcpSocket + 独立 Protocol。
 *   readyRead：append → 对每个完整请求 handle → pack+flush；能认出用户则 bindUser。
 *   认用户两条路径：
 *     - 本帧是 LOGIN 且 code==0：从 resp.data.user.id 取；
 *     - 否则用请求里的 token 问 Dispatch::userIdOfToken（无效为 0，bindUser 直接 return）。
 *   disconnected：从 socketUser_/codecs_ 摘掉；若该 userId 已无其它套接字，scheduleRelease 60s。
 *   60s 内同一用户再 LOGIN / 带 token 的请求会 bindUser 并取消倒计时（人回来了）。
 *   到点仍离线：Dispatch::releaseStaleSession → 充电中订单改待结算、桩回闲置。
 *
 *   另有 5s 定时器 pushChargeTicks：遍历在线套接字，chargePushFor(uid) 若 order.status=="充电中"
 *   则推 {type:PUSH_CHARGE, seq:0, code:0, data}。不是请求，旧客户端可忽略；CHARGE_STATUS 轮询仍可用。
 *
 * 【一人多端】同一 userId 允许两根 TCP（两台电脑登录同一号）。userStillOnline 任一还在就不释放。
 *
 * 【端口】main.cpp 在弹出管理员登录框之前就 listen(QHostAddress::Any, 8888)，方便烟测。
 *   第二份管理端会抢端口失败。全组联调只允许一台电脑开管理端。
 *
 * 【协作】持有 Dispatch*。PUSH 的 amount 仍是元。详见 protocol/messages.md。
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
     * 记住 Dispatch，并启动 5 秒一次的充电推送定时器。
     * 真正开始听端口由 main.cpp 调用 listen(QHostAddress::Any, 8888)。
     *
     * @param dispatch  必须已构造完且 registerRoutes 已执行；本类不负责它的寿命（与 main 同生）。
     */
    TcpServer(Dispatch *dispatch, QObject *parent = nullptr);

protected:
    /**
     * 来了一根新 TCP。为本连接建 QTcpSocket + Protocol。
     *
     * setSocketDescriptor 失败则丢掉这个 QTcpSocket（描述符可能已被收回）。
     * 读循环在 lambda 里捕获 socket/codec 指针，disconnected 时 delete codec 并 deleteLater socket。
     */
    void incomingConnection(qintptr handle) override;

private:
    /**
     * 登录成功或后续请求带了有效 token：记下「这根线属于 userId」。
     * 若该用户正在 60 秒释放倒计时，则停表删除（人回来了，不要误停充）。
     *
     * @param userId  ≤0 视为未识别，直接返回（例如 LOGIN 失败、token 空）。
     */
    void bindUser(QTcpSocket *socket, int userId);

    /**
     * 这根线已经从 socketUser_ 摘掉之后调用。
     * 若该用户已经没有别的线，且尚未有倒计时，则单次 60s 定时器；
     * 到点仍 userStillOnline==false 才 releaseStaleSession。
     */
    void scheduleRelease(int userId);

    /**
     * 每 5 秒：对每个已 bind 的套接字，若对应订单仍是「充电中」，写一帧 PUSH_CHARGE。
     * data 来自 Dispatch::chargePushFor（含 calcLive 后的 amount/energyKwh 等，单位元）。
     * 无充电中订单则跳过，不发空推送。
     */
    void pushChargeTicks();

    /**
     * 该用户是否还有别的已绑定套接字。
     * @param except  「不要把这根即将断开的线算作在线」；绑定时传 nullptr。
     */
    bool userStillOnline(int userId, QTcpSocket *except = nullptr) const;

    Dispatch *dispatch_;
    QHash<QTcpSocket *, Protocol *> codecs_;   ///< 每根线自己的拆包器，断线必须 delete
    QHash<QTcpSocket *, int> socketUser_;      ///< 线 → 用户 id；未登录的线不在表里
    QHash<int, QTimer *> pendingRelease_;      ///< 用户 → 60 秒释放倒计时（单次）
    QTimer *pushTimer_ = nullptr;              ///< 5 秒推送，与 QTcpServer 同寿命
};

#endif
