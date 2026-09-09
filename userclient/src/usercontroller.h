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
    explicit UserController(QObject *parent = nullptr);

    void connectTo(const QString &host, quint16 port);
    bool isConnected() const;
    bool isAuthenticated() const { return !token_.isEmpty(); }
    const QJsonObject &user() const { return user_; }

    void authenticate(const QString &type, const QString &phone, const QString &password);
    int request(const QString &type, const QJsonObject &data = {});
    void beginCharge(int pileId);
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

    void sendPendingAuth();
    void handleResponse(QJsonObject obj);
    void updateSessionState(const QString &type, const QJsonObject &data);
};

#endif
