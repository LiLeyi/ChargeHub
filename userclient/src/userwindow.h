#ifndef CHARGEHUB_USERWINDOW_H
#define CHARGEHUB_USERWINDOW_H

/**
 * @file userwindow.h
 * @brief 用户端全部页面：登录、找桩、预约、充电、订单、评价、个人中心。
 *
 * 【职责】画界面、做格式校验、把按钮变成 Socket 请求；不写 SQLite。
 * 【原理】root_ 两页（登录 / 主壳）；pages_ 里各业务页。所有写操作 → Client::request。
 *         回包统一 onResp：失败弹 UiSheet；PUSH_CHARGE 只刷新充电页。
 * 【协作】依赖 Client、uidialog。地图用 HTTP 拉腾讯静态图，不经过管理端业务。
 * 【联调】服务器填管理端底栏 IP:8888；本机自测 127.0.0.1:8888。
 * 【详见】docs/模块与协作说明.md
 */

#include "client.h"

#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

class QNetworkAccessManager;

class UserWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit UserWindow(QWidget *parent = nullptr);

private slots:
    void doLogin();          ///< 校验手机号/密码后发 LOGIN
    void doRegister();       ///< 注册，需两次密码一致
    void queryStations();    ///< 按地址/半径找附近电站
    void onResp(QJsonObject obj); ///< 处理所有 Socket 回包与 PUSH_CHARGE
    void pollCharge();       ///< 充电中定时 CHARGE_STATUS（推送的补充）

private:
    QJsonObject coord() const;           ///< 当前定位（匹配地址后的经纬度）
    void applyUser(const QJsonObject &u);
    void showShell();                    ///< 登录成功后进入主框架
    void switchTab(int i);
    void renderStations(const QJsonObject &data);
    void renderPiles(const QJsonObject &data);
    void renderOrders(const QJsonArray &arr);
    void renderRecharge(const QJsonArray &arr);
    void renderReservations(const QJsonArray &arr);
    void renderPileReview(const QJsonObject &data);
    void showCharge(const QJsonObject &order);
    void tryStart(int pileId);           ///< 先查未完成订单再 START_CHARGE
    void doReserve(int pileId);
    void openPileReview(const QJsonObject &pile);
    void setReviewStars(int n);
    void submitReview();
    void showStationLocation(const QJsonObject &station); ///< 地图与导航弹窗
    void openNav(const QJsonObject &station);
    void queryTencentRoute(const QJsonObject &station, const QString &mode,
                           QLabel *resultLabel, QPushButton *queryButton);
    void refreshMe();
    void pickAvatar();
    void clearAvatar();
    void showAvatar(const QJsonObject &u);
    void closeMyAccount();               ///< 二次确认后 CLOSE_ACCOUNT
    void reconnect();
    void sendPendingAuth();              ///< 连上后再发挂起的登录/注册
    QString serverHost() const;
    quint16 serverPort() const;
    QWidget *buildLogin();
    QWidget *buildShell();
    QWidget *buildHome();
    QWidget *buildPiles();
    QWidget *buildCharge();
    QWidget *buildOrders();
    QWidget *buildMe();
    QWidget *buildPileReview();
    QWidget *buildReservations();
    static void clearBox(QLayout *lay);

    Client client_;
    QNetworkAccessManager *mapNetwork_ = nullptr;
    QTimer poll_;
    QString token_;
    QJsonObject user_;
    QJsonObject currentStation_;
    QJsonObject currentOrder_;
    QJsonObject currentPile_;
    int pendingPile_ = 0;
    int reviewStars_ = 5;
    bool wantStart_ = false;
    QString pendingAuth_;
    QString pendingPhone_;
    QString pendingPwd_;

    QStackedWidget *root_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    QLineEdit *hostEdit_ = nullptr;
    QLabel *loginHint_ = nullptr;
    QLineEdit *phone_ = nullptr;
    QLineEdit *pwd_ = nullptr;
    QLineEdit *pwd2_ = nullptr;
    QLabel *headBal_ = nullptr;
    QLabel *pageTitle_ = nullptr;
    QLabel *pageSub_ = nullptr;
    QComboBox *radius_ = nullptr;
    QLineEdit *addrEdit_ = nullptr;
    QLabel *locMatch_ = nullptr;
    double locLat_ = 39.9644;
    double locLng_ = 116.3473;
    QComboBox *pileType_ = nullptr;
    QComboBox *orderFilter_ = nullptr;
    QVBoxLayout *stationBox_ = nullptr;
    QLabel *pileTitle_ = nullptr;
    QLabel *pileMeta_ = nullptr;
    QVBoxLayout *pileBox_ = nullptr;
    QVBoxLayout *rechargeBox_ = nullptr;
    QVBoxLayout *reservationBox_ = nullptr;
    QJsonObject lastPiles_;
    QJsonArray lastOrders_;
    QJsonArray lastReservations_;
    QLabel *chStatus_ = nullptr;
    QLabel *chTime_ = nullptr;
    QLabel *chInfo_ = nullptr;
    QPushButton *stopBtn_ = nullptr;
    QPushButton *settleBtn_ = nullptr;
    QVBoxLayout *orderBox_ = nullptr;
    QLabel *avatar_ = nullptr;
    QLabel *meName_ = nullptr;
    QLabel *meBal_ = nullptr;
    QLineEdit *nickEdit_ = nullptr;
    QLineEdit *payEdit_ = nullptr;
    QListWidget *nav_ = nullptr;

    QLabel *rvTitle_ = nullptr;
    QLabel *rvStars_ = nullptr;
    QLabel *rvMeta_ = nullptr;
    QLabel *rvNlp_ = nullptr;
    QPlainTextEdit *reviewBody_ = nullptr;
    QLabel *reviewCount_ = nullptr;
    QVBoxLayout *reviewListBox_ = nullptr;
    QList<QPushButton *> reviewStarBtns_;
};

#endif
