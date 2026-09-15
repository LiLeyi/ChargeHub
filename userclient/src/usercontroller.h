#ifndef CHARGEHUB_USERCONTROLLER_H
#define CHARGEHUB_USERCONTROLLER_H

/**
 * @file usercontroller.h
 * @brief 用户端网络控制器：隔离 UserWindow 与 TCP Client，是界面发 Socket 的唯一入口。
 *
 * 【职责】
 *   - 持有 Client，转发 connected / responded / failed。
 *   - 记住登录后的 token_ 与 user_ 快照；后续 request(type,data) 自动带 token。
 *   - 充电中每 1s 发 CHARGE_STATUS（与服务器 5s PUSH_CHARGE 并存；推送只刷新 UI，轮询防漏）。
 *   - 「记住我」只写本机 QSettings（ChargeHub/UserClient），不是 Socket 字段，服务端无感知。
 *
 * 【原理】
 *   authenticate(LOGIN|REGISTER, phone, password)：若尚未 connected，先记下 pending*，
 *   等 Client::connected 再 sendPendingAuth（token 空）。成功响应里取出 token/user。
 *   beginCharge(pileId)：先 CHARGE_STATUS；若已有未完成单则 chargeStartBlocked，否则 START_CHARGE。
 *   signOut：清内存会话并停轮询，不断开 TCP（可再登录）；注销成功还会 clearCredentials。
 *
 * 【不做的事】
 *   不打开 .db；不直接 QNetworkAccessManager（地图/天气在 UserWindow::mapNetwork_）。
 *   不发明新 type。所有 type 必须能在 protocol/messages.md 找到。
 *
 * 【协作】UserWindow 只调本类的 connectTo / authenticate / request / beginCharge。
 */

#include "client.h"

#include <QJsonObject>
#include <QObject>
#include <QTimer>

class UserController : public QObject
{
    Q_OBJECT
public:
    explicit UserController(QObject *parent = nullptr);

    /**
     * 拨管理端。host 不含端口。
     * @see Client::connectTo
     */
    void connectTo(const QString &host, quint16 port);

    /** TCP 是否已 Established。与是否已 LOGIN 无关。 */
    bool isConnected() const;

    /** 是否已拿到非空 token（LOGIN/REGISTER 成功之后）。 */
    bool isAuthenticated() const { return !token_.isEmpty(); }

    /** 最近一次成功登录/资料更新后的用户快照（含 balance，单位元）。 */
    const QJsonObject &user() const { return user_; }

    /**
     * 发送 LOGIN 或 REGISTER。未连接时只排队，连上后自动发出。
     * @param type  只能是 "LOGIN" 或 "REGISTER"。
     */
    void authenticate(const QString &type, const QString &phone, const QString &password);

    /**
     * 已登录业务请求。自动附带 token_。
     * @return Client 的 seq。
     */
    int request(const QString &type, const QJsonObject &data = {});

    /**
     * 开充入口：先探 CHARGE_STATUS，避免第二单。真正 START_CHARGE 在 handleResponse 里发出。
     */
    void beginCharge(int pileId);

    /**
     * 清 token/user/订单轮询/挂起的登录。不断开套接字。
     * 主动退出登录时界面再调 clearCredentials；本函数本身不清 QSettings。
     */
    void signOut();

    /**
     * 勾选「记住我」后写入本机设置，7 天内连上服务器可自动 LOGIN。
     * 明文存在 QSettings，仅作业演示用，不是协议字段。
     */
    void saveCredentials(const QString &phone, const QString &password);

    /**
     * 读出记住的手机号密码。未勾选或字段空返回 false。
     * 不检查是否过期；过期用 isAutoLoginValid()。
     */
    bool loadCredentials(QString &phone, QString &password) const;

    /** auto_login 为真且 expiry 仍晚于现在。 */
    bool isAutoLoginValid() const;

    /** 去掉记住我的四个键。注销、自动登录失败、用户取消时应调用。 */
    void clearCredentials();

signals:
    void connected();
    /** 多数响应原样转给 UserWindow::onResp；开充探测命中未完成单时不转这条，改发 chargeStartBlocked。 */
    void responded(QJsonObject obj);
    void failed(QString message);
    /**
     * beginCharge 时发现已有进行中/待结算订单，把该 order 交给界面提示，不再发 START_CHARGE。
     */
    void chargeStartBlocked(QJsonObject order);

private:
    Client client_;
    QTimer chargePoll_;          ///< 充电中 1s 一次 CHARGE_STATUS
    QString token_;              ///< 服务端 session token，内存持有
    QJsonObject user_;
    QJsonObject currentOrder_;   ///< 用于决定是否启动轮询
    QString pendingAuth_;        ///< "LOGIN"/"REGISTER"，空表示没有挂起
    QString pendingPhone_;
    QString pendingPassword_;
    int pendingPileId_ = 0;      ///< beginCharge 等待 CHARGE_STATUS 回包期间的目标桩

    /** 已连接且 pendingAuth_ 非空时发出认证请求，随后清掉 pendingPassword_。 */
    void sendPendingAuth();
    /** 先更新会话，再处理开充探测，最后 emit responded。 */
    void handleResponse(QJsonObject obj);
    /** 按 type 写入 token_/user_/currentOrder_，并开停充电轮询。 */
    void updateSessionState(const QString &type, const QJsonObject &data);
};

#endif
