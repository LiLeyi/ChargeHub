#ifndef CHARGEHUB_USERWINDOW_H
#define CHARGEHUB_USERWINDOW_H

/**
 * @file userwindow.h
 * @brief 用户端全部页面：登录、找桩、预约、充电、订单、评价、个人中心。
 *
 * 【职责】画界面、做格式校验、把页面操作交给 UserController；不写 SQLite。
 * 【原理】root_ 两页（登录 / 主壳）；pages_ 里各业务页。网络状态由 UserController 管理。
 *         回包统一 onResp：失败弹 UiSheet；PUSH_CHARGE 只刷新充电页。
 * 【协作】依赖 UserController、uidialog。地图默认 OSM/Carto 瓦片（无日配额），不经过管理端业务。
 * 【联调】服务器填管理端底栏 IP:8888；本机自测 127.0.0.1:8888。
 * 【详见】docs/模块与协作说明.md
 */

#include "usercontroller.h"

#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

class QNetworkAccessManager;
class ChargePage;
class LoginPage;
class OrdersPage;
class ReservationsPage;

class UserWindow : public QMainWindow {
    Q_OBJECT
public:
    /**
     * 搭登录页和主壳、接 Client 信号、准备充电轮询定时器。
     * 不打开数据库，也不在构造时立刻拨号（等用户点登录）。
     */
    explicit UserWindow(QWidget *parent = nullptr);

private slots:
    /** 按地址或 GPS 坐标发 QUERY_STATIONS。 */
    void queryStations();
    /** 登录后弹出位置授权；同意才采集坐标。 */
    void askLocationConsent();
    /** 先 Windows 定位，不准再地图选点，按经纬度找最近桩。 */
    void startLocate();
    /** 调 Windows 定位服务（Wi-Fi/系统定位）。 */
    void tryWindowsLocate();
    /** 在地图上点选当前位置；确定则按该点找桩。 */
    bool pickMyLocation();
    /** 把选好的点记为导航/找桩起点。 */
    void applyGpsFix(double lat, double lng, const QString &place);
    /**
     * 处理所有 Socket 回包与 PUSH_CHARGE。
     * code≠0 弹窗（登录失败才退回登录页）；成功按 type 分支刷新对应页。
     */
    void onResp(QJsonObject obj);

private:
    /** 当前定位 {lat,lng}，给找站和导航。默认北京演示点。 */
    QJsonObject coord() const;
    /** 用控制器中的用户快照刷新顶栏余额和头像。 */
    void applyUser(const QJsonObject &u);
    /** 登录成功：root_ 切到主壳，再问位置授权。 */
    void showShell();
    /** 左边导航点第 i 项：切 pages_，必要时发 LIST_ORDERS / LIST_RESERVATIONS。 */
    void switchTab(int i);
    /** 画附近电站卡片（距离、电价、进站按钮）。 */
    void renderStations(const QJsonObject &data);
    /** 画某站桩列表（状态、预约、开充、评价）。 */
    void renderPiles(const QJsonObject &data);
    /** 画订单卡片；orderFilter_ 可筛充电中/待结算/已完成。 */
    void renderOrders(const QJsonArray &arr);
    /** 画充值流水。 */
    void renderRecharge(const QJsonArray &arr);
    /** 画某桩评价页：均分、星、已有评论、情感摘要。 */
    void renderPileReview(const QJsonObject &data);
    /** 刷新充电页：时长、电量、费用、分时标签；切换停充/结算按钮。 */
    void showCharge(const QJsonObject &order);
    /**
     * 先 CHARGE_STATUS 探有没有未完成单，没有再 START_CHARGE。
     * 服务端 startCharge 还会再拦一次。
     */
    void tryStart(int pileId);
    /** 对空闲桩发 RESERVE_PILE。 */
    void doReserve(int pileId);
    /** 记下 currentPile_，切到评价页并 LIST_PILE_REVIEWS。 */
    void openPileReview(const QJsonObject &pile);
    /** 点亮 1~n 颗星，提交时带 score。 */
    void setReviewStars(int n);
    /** 文字不能空，发 REVIEW_STATION（stationId/pileId/score/comment）。 */
    void submitReview();
    /** 弹出地图+天气+路线；天气优先走高德 HTTP，不经 Dispatch。 */
    void showStationLocation(const QJsonObject &station);
    /** 找站成功后按当前定位拉天气，写到附近电站页。 */
    void fetchLocalWeather();
    /** 打开导航弹窗：从用户当前位置到电站。 */
    void openNav(const QJsonObject &station);
    /**
     * 按用户当前经纬度到电站估算路程时间；开始导航走高德/OSM，不经 Dispatch。
     */
    void queryTencentRoute(const QJsonObject &station, const QString &mode,
                           QLabel *resultLabel, QPushButton *queryButton);
    /** 「我的」页：发 UPDATE_PROFILE 空改动或再拉用户信息，刷新余额头像。 */
    void refreshMe();
    /** 选本地图片，压成小图后 UPDATE_PROFILE 带 avatar。 */
    void pickAvatar();
    /** 清掉头像字段再 UPDATE_PROFILE。 */
    void clearAvatar();
    /** 把 user 里的头像 Base64 或首字母画到头像标签。 */
    void showAvatar(const QJsonObject &u);
    /** 二次确认后发 CLOSE_ACCOUNT；成功退回登录页。 */
    void closeMyAccount();
    /** 按登录页填的 host:port 让控制器重新连接。 */
    void reconnect();
    /** 通过IP获取用户位置 */
    void fetchLocationByIP();
    /** 计算两点间距离（公里） */
    static double haversine(double lat1, double lng1, double lat2, double lng2);

    /** 登录页：服务器地址、手机、密码、注册确认。 */
    QWidget *buildLogin();
    /** 主壳：顶栏 + pages_ + 底部 Tab。 */
    QWidget *buildShell();
    /** 找站页：地址、半径、电站列表。 */
    QWidget *buildHome();
    /** 桩列表页。 */
    QWidget *buildPiles();
    /** 充电进行页：状态、停充、结算。 */
    QWidget *buildCharge();
    /** 订单 + 充值流水页。 */
    QWidget *buildOrders();
    /** 个人中心：昵称、充值、头像、注销。 */
    QWidget *buildMe();
    /** 评价页：星、正文、历史评论。 */
    QWidget *buildPileReview();
    /** 预约列表页。 */
    QWidget *buildReservations();
    /** 清空布局里的控件，重新 render 前用。 */
    static void clearBox(QLayout *lay);

    /** 底部 Tab 高亮，行号与 pages_ 映射不变。 */
    void highlightTab(int pageIndex);

    UserController controller_;
    QNetworkAccessManager *mapNetwork_ = nullptr; ///< 地图、天气与路线 HTTP
    QJsonObject currentStation_;                  ///< 点进去的那座站
    QJsonObject currentOrder_;                    ///< 充电页正在看的订单
    QJsonObject currentPile_;                     ///< 评价页正在看的桩
    int reviewStars_ = 5;

    QStackedWidget *root_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    LoginPage *loginPage_ = nullptr;
    QLabel *headBal_ = nullptr;
    QLabel *pageTitle_ = nullptr;
    QLabel *pageSub_ = nullptr;
    QComboBox *radius_ = nullptr;
    QLineEdit *addrEdit_ = nullptr;
    QLabel *locMatch_ = nullptr;
    QLabel *weatherHint_ = nullptr;
    double locLat_ = 39.9644;
    double locLng_ = 116.3473;
    bool useGps_ = false;
    bool pendingConsent_ = false;
    bool hasLocationFromIP_ = false;
    QString gpsPlace_;
    QComboBox *pileType_ = nullptr;
    QVBoxLayout *stationBox_ = nullptr;
    QLabel *pileTitle_ = nullptr;
    QLabel *pileMeta_ = nullptr;
    QVBoxLayout *pileBox_ = nullptr;
    QVBoxLayout *rechargeBox_ = nullptr;
    QJsonObject lastPiles_;
    ChargePage *chargePage_ = nullptr;
    OrdersPage *ordersPage_ = nullptr;
    ReservationsPage *reservationsPage_ = nullptr;
    QLabel *avatar_ = nullptr;
    QLabel *meName_ = nullptr;
    QLabel *meBal_ = nullptr;
    QLineEdit *nickEdit_ = nullptr;
    QLineEdit *payEdit_ = nullptr;
    QList<QPushButton *> tabBtns_;

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
