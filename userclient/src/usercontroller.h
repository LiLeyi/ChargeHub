#ifndef CHARGEHUB_USERCONTROLLER_H
#define CHARGEHUB_USERCONTROLLER_H

/**
 * @file usercontroller.h
 * @brief 用户端网络控制器：隔离 UserWindow 与 TCP Client。
 *
 * 第一阶段只承接连接和请求出口，并转发 Client 的异步信号。
 * 页面状态和响应渲染仍由 UserWindow 负责，后续可继续按页面拆分。
 */

#include "client.h"

#include <QJsonObject>
#include <QObject>
#include <QTimer>

class UserController : public QObject
{
    Q_OBJECT
public:
    /** @param parent Qt 父对象；连接 Client 信号并配置充电轮询定时器。 */
    explicit UserController(QObject *parent = nullptr);

    /** @param host 服务端主机名或 IP。@param port TCP 端口。无返回值。 */
    void connectTo(const QString &host, quint16 port);
    /** @return TCP 套接字是否已进入 ConnectedState。 */
    bool isConnected() const;
    /** @return 内存中是否持有非空登录 Token。 */
    bool isAuthenticated() const { return !token_.isEmpty(); }
    /** @return 最近一次认证或资料更新得到的公开用户快照。 */
    const QJsonObject &user() const { return user_; }

    /**
     * 暂存认证参数；若 TCP 已连接则立即发送，否则等待 connected 后发送。
     * @param type `LOGIN` 或 `REGISTER`。
     * @param phone 手机号。
     * @param password 明文密码，仅保留到请求发出。
     */
    void authenticate(const QString &type, const QString &phone, const QString &password);
    /** @param type 协议请求类型。@param data 业务参数。@return Client 分配的请求序号。 */
    int request(const QString &type, const QJsonObject &data = {});
    /** @param pileId 待开充电桩；先查未完成订单，再决定是否发送 START_CHARGE。 */
    void beginCharge(int pileId);
    /** 停止轮询并清空 Token、用户、订单及待发认证状态。 */
    void signOut();

    /** 勾选「记住我」后写入本机设置，7 天内连上服务器可自动 LOGIN。 */
    void saveCredentials(const QString &phone, const QString &password);
    bool loadCredentials(QString &phone, QString &password) const;
    bool isAutoLoginValid() const;
    void clearCredentials();

signals:
    void connected();
    void responded(QJsonObject obj);
    void failed(QString message);
    void chargeStartBlocked(QJsonObject order);

private:
    Client client_;
    QTimer chargePoll_;
    QString token_;
    QJsonObject user_;
    QJsonObject currentOrder_;
    QString pendingAuth_;
    QString pendingPhone_;
    QString pendingPassword_;
    int pendingPileId_ = 0;

    /** TCP 已连接时发送一次待处理认证，并立即清掉内存中的明文密码。 */
    void sendPendingAuth();
    /** @param obj 服务端响应；更新会话/订单状态后向窗口转发。 */
    void handleResponse(QJsonObject obj);
    /** @param type 响应类型。@param data 成功响应数据；按类型更新 Token、用户或订单。 */
    void updateSessionState(const QString &type, const QJsonObject &data);
};

#endif
