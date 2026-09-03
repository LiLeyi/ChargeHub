#ifndef CHARGEHUB_USERWINDOW_H
#define CHARGEHUB_USERWINDOW_H

/**
 * @file userwindow.h
 * @brief 充电用户端主界面：登录、找桩、充电、订单、个人中心。
 *
 * 所有按钮最终都走 Client::request。登录页「服务器地址」即管理端 IP:端口。
 * 本机自测填 127.0.0.1:8888；连组里服务器填那台电脑底栏的局域网 IP:8888。
 */

#include "client.h"
#include "walletcontroller.h"

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

class UserWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit UserWindow(QWidget *parent = nullptr);

private slots:
    void doLogin();
    void doRegister();
    void queryStations();
    void onResp(QJsonObject obj);
    void pollCharge();

private:
    QJsonObject coord() const;
    void applyUser(const QJsonObject &u);
    void showShell();
    void switchTab(int i);
    void renderStations(const QJsonObject &data);
    void renderPiles(const QJsonObject &data);
    void renderOrders(const QJsonArray &arr);
    void renderRecharge(const QJsonArray &arr);
    void renderPileReview(const QJsonObject &data);
    void showCharge(const QJsonObject &order);
    void tryStart(int pileId);
    void doReserve(int pileId);
    void openPileReview(const QJsonObject &pile);
    void setReviewStars(int n);
    void submitReview();
    void openNav(const QJsonObject &station);
    void refreshMe();
    void loadWallet();
    void submitRecharge();
    void queryPendingRecharge();
    void startNewRecharge();
    void updateWalletUi();
    void handleWalletError(const QString &type, const QJsonObject &response);
    void pickAvatar();
    void clearAvatar();
    void showAvatar(const QJsonObject &u);
    void closeMyAccount();
    void reconnect();
    void sendPendingAuth();
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
    static void clearBox(QLayout *lay);

    Client client_;
    QTimer poll_;
    WalletController walletController;
    QTimer walletTimeout;
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
    QJsonObject lastPiles_;
    QJsonArray lastOrders_;
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
    QLabel *walletHint = nullptr;
    QPushButton *rechargeButton = nullptr;
    QPushButton *newRechargeButton = nullptr;
    QList<QPushButton *> rechargeQuickButtons;
    bool walletBlocked = false;
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
