#ifndef CHARGEHUB_CLIENT_H
#define CHARGEHUB_CLIENT_H

/**
 * @file client.h
 * @brief 用户端唯一 TCP 出口：只连管理端 :8888，绝不打开 SQLite。
 *
 * 【职责】
 *   维护一条 QTcpSocket；把业务请求编成 Protocol 帧写出；把完整 JSON 用 responded 交给上层。
 *   本类不知道 LOGIN / START_CHARGE 的含义，也不存 token（token 由 UserController 注入 request()）。
 *
 * 【原理】
 *   1. 构造时接好 connected / readyRead / errorOccurred，此时尚未 connectToHost。
 *   2. connectTo(host,port) 先 abort 旧连接再拨号，避免两根线抢同一个 Client。
 *      自测 host=`127.0.0.1` port=8888；联调填管理端底栏局域网 IP。
 *   3. request(type,data,token) 把 seq_ 加一，组信封 {type,seq,role:"user",token,data}，
 *      pack 后 write+flush。未 Connected 只 emit failed("未连接到服务器")，不写半包。
 *   4. readyRead：codec_.append(readAll())，while hasPacket → emit responded（含 PUSH_CHARGE）。
 *      推送与普通响应走同一信号，由 UserWindow::onResp / UserController::handleResponse 看 type。
 *
 * 【不做的事】
 *   - 不重连、不心跳（HEARTBEAT 由界面/控制器按需 request）。
 *   - 不解析 code；失败文案来自套接字 errorString 或上层读响应 message。
 *   - 不走 HTTP。地图/天气是 UserWindow 的 QNetworkAccessManager + TencentApi。
 *
 * 【协作】只被 UserController 持有。UserWindow 禁止直接 new QTcpSocket 再发明一套 JSON。
 * 【详见】protocol/messages.md 、docs/模块与协作说明.md
 */

#include "protocol.h"

#include <QJsonObject>
#include <QTcpSocket>

class Client : public QObject {
    Q_OBJECT
public:
    /**
     * 创建套接字并接好 connected / readyRead / error 信号。
     * 此时还没拨号；seq_ 从 0 起，第一次 request 变为 1。
     */
    explicit Client(QObject *parent = nullptr);

    /**
     * 拨管理端 TCP。
     *
     * @param host  点分 IPv4 或主机名。不要带 ":8888"；端口走 port。
     * @param port  管理端监听端口，作业固定 8888。
     *
     * 已有连接会 abort 掉（未完成的请求不会有回包，上层应提示用户重试）。
     * 连上后发 connected()；UserController 可能紧接着补发挂起的 LOGIN/REGISTER。
     */
    void connectTo(const QString &host, quint16 port);

    /**
     * 当前套接字是否已经 Established。
     * ConnectingState / HostLookup 都算未连接；request() 在那种状态下会 failed。
     */
    bool isConnected() const;

    /**
     * 发送一帧业务请求。
     *
     * 信封固定 role="user"。seq 使用本 Client 的自增计数（跨断线不重置，便于日志对照；
     * 服务端幂等键是 token|type|seq，断线重登会换 token，旧 seq 不会误伤新会话）。
     *
     * @param type   与 protocol/messages.md 的 type 列一致，例如 "QUERY_STATIONS"。
     * @param data   业务字段；没有则传 {}。
     * @param token  未登录传空；已登录传 LOGIN 返回的 token。
     * @return       本次使用的 seq。界面一般不自己对 seq，而是在 responded 里看 type。
     */
    int request(const QString &type, const QJsonObject &data, const QString &token);

signals:
    /** TCP 三次握手成功。此时还没有业务会话，需要再发 LOGIN/REGISTER。 */
    void connected();
    /**
     * 拆出一张完整 JSON：普通响应或服务端 PUSH_CHARGE（seq=0）。
     * 可能在任意时刻到达；充电中大约每 5 秒一张推送，与 CHARGE_STATUS 轮询并存。
     */
    void responded(QJsonObject obj);
    /**
     * 拨号失败、对端断开、写失败等。message 为 Qt 的 errorString 或「未连接到服务器」。
     * 不修改任何本地「账」；账只在成功响应的 data 里。
     */
    void failed(QString msg);

private:
    QTcpSocket sock_;
    Protocol codec_; ///< 本连接专用拆包器；abort/重连后旧缓冲仍挂在同一对象上，半包会被新流搅乱，故 connectTo 依赖 abort 后对端重发完整帧
    int seq_ = 0;    ///< 每次 request 加 1，从 1 起
};

#endif
