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
    /**
     * 创建套接字并接好 connected / readyRead / error 信号。
     * 此时还没拨号，等 connectTo。
     * @param parent Qt 父对象，负责 Client 生命周期。
     */
    explicit Client(QObject *parent = nullptr);

    /**
     * 拨管理端。自测填 127.0.0.1:8888；联调填管理端底栏 IP。
     * 已有连接会先断开再连。连上后发 connected 信号。
     * @param host 主机名、IPv4 或 IPv6 地址。
     * @param port 服务端监听端口。
     */
    void connectTo(const QString &host, quint16 port);

    /** @return 当前套接字是否处于 ConnectedState。 */
    bool isConnected() const;

    /**
     * 发送一帧业务请求：{type, seq, role:"user", token, data}。
     * 没连上只 emit failed，不写半包。
     * @param type 协议请求类型。
     * @param data 业务参数对象。
     * @param token 登录令牌；LOGIN/REGISTER 传空串。
     * @return 本次自增后的 seq，方便界面对照回包（一般用不到，onResp 看 type）。
     */
    int request(const QString &type, const QJsonObject &data, const QString &token);

signals:
    /** 拨号成功。UserWindow 可能紧接着 sendPendingAuth 补发登录/注册。 */
    void connected();
    /** @param obj 拆出的完整 JSON：普通响应或服务端推送 PUSH_CHARGE。 */
    void responded(QJsonObject obj);
    /** @param msg 套接字错误文本；界面负责展示，不改任何业务数据。 */
    void failed(QString msg);

private:
    QTcpSocket sock_;
    Protocol codec_;
    int seq_ = 0; ///< 每次 request 加 1，从 1 起
};

#endif
