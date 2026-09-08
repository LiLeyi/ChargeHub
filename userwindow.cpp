/**
 * @file userwindow.cpp
 * @brief 用户端页面。按钮 → Client::request；回包 → onResp。不打开数据库。
 *
 * 页：登录 / 找站 / 桩列表 / 充电 / 订单 / 评价 / 预约 / 我的。
 * 充电刷新：UserController 负责轮询，本窗口负责渲染 PUSH_CHARGE / CHARGE_STATUS。
 */
#include "userwindow.h"
#include "tencentapi.h"
#include "uidialog.h"
#include "pages/chargepage.h"
#include "pages/loginpage.h"
#include "pages/orderspage.h"
#include "pages/reservationspage.h"

#include <functional>
#include <QAbstractItemView>
#include <QButtonGroup>
#include <QVector>
#include <QBuffer>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QListView>
#include <QComboBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QFileInfo>
#include <QMouseEvent>
#include <QProcess>
#include <QSizePolicy>
#include <QStatusBar>
#include <QStringList>
#include <QStyle>
#include <QTextCursor>
#include <QUrl>
#include <QUrlQuery>
#include <QTimer>
#include <QSettings>

static QString u8(const char *s) { return QString::fromUtf8(s); }

// 计算两点间距离（公里）
static double haversine(double lat1, double lng1, double lat2, double lng2)
{
    const double r = 6371.0;
    const double p1 = qDegreesToRadians(lat1);
    const double p2 = qDegreesToRadians(lat2);
    const double dphi = qDegreesToRadians(lat2 - lat1);
    const double dl = qDegreesToRadians(lng2 - lng1);
    const double a = qSin(dphi / 2) * qSin(dphi / 2)
        + qCos(p1) * qCos(p2) * qSin(dl / 2) * qSin(dl / 2);
    return r * 2 * qAtan2(qSqrt(a), qSqrt(1 - a));
}

namespace {

/** 高德网页导航：from/to 为 经度,纬度,名称。坐标系用 WGS-84，与网络定位一致。 */
QUrl amapNavigationUrl(const QJsonObject &station, const QJsonObject &origin, const QString &mode)
{
    QString amapMode = QStringLiteral("car");
    if (mode == QStringLiteral("walk"))
        amapMode = QStringLiteral("walk");
    else if (mode == QStringLiteral("bike"))
        amapMode = QStringLiteral("ride");
    else if (mode == QStringLiteral("bus"))
        amapMode = QStringLiteral("bus");
    QUrl url(QStringLiteral("https://uri.amap.com/navigation"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("from"),
                       QStringLiteral("%1,%2,%3")
                           .arg(origin.value("lng").toDouble(), 0, 'f', 6)
                           .arg(origin.value("lat").toDouble(), 0, 'f', 6)
                           .arg(u8("我的位置")));
    query.addQueryItem(QStringLiteral("to"),
                       QStringLiteral("%1,%2,%3")
                           .arg(station.value("lng").toDouble(), 0, 'f', 6)
                           .arg(station.value("lat").toDouble(), 0, 'f', 6)
                           .arg(station.value("name").toString()));
    query.addQueryItem(QStringLiteral("mode"), amapMode);
    query.addQueryItem(QStringLiteral("coordinate"), QStringLiteral("wgs84"));
    query.addQueryItem(QStringLiteral("callnative"), QStringLiteral("0"));
    query.addQueryItem(QStringLiteral("src"), QStringLiteral("ChargeHub"));
    url.setQuery(query);
    return url;
}

/** OSM 路线页：参数是 lat,lng;lat,lng。 */
QUrl osmDirectionsUrl(const QJsonObject &station, const QJsonObject &origin, const QString &mode)
{
    QString engine = QStringLiteral("fossgis_osrm_car");
    if (mode == QStringLiteral("walk"))
        engine = QStringLiteral("fossgis_osrm_foot");
    else if (mode == QStringLiteral("bike"))
        engine = QStringLiteral("fossgis_osrm_bike");
    const QString route = QStringLiteral("%1,%2;%3,%4")
                              .arg(origin.value("lat").toDouble(), 0, 'f', 6)
                              .arg(origin.value("lng").toDouble(), 0, 'f', 6)
                              .arg(station.value("lat").toDouble(), 0, 'f', 6)
                              .arg(station.value("lng").toDouble(), 0, 'f', 6);
    QUrl url(QStringLiteral("https://www.openstreetmap.org/directions"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("engine"), engine);
    query.addQueryItem(QStringLiteral("route"), route);
    url.setQuery(query);
    return url;
}

void openExternalUrl(const QUrl &url)
{
    if (QDesktopServices::openUrl(url))
        return;
    if (QProcess::startDetached(QStringLiteral("xdg-open"), {url.toString()}))
        return;
    QProcess::startDetached(QStringLiteral("wslview"), {url.toString()});
}

/** 路线方式的中文名，给界面标签。 */
QString routeModeName(const QString &mode)
{
    if (mode == QStringLiteral("walk"))
        return u8("步行");
    if (mode == QStringLiteral("bike"))
        return u8("骑行");
    if (mode == QStringLiteral("bus"))
        return u8("公交");
    return u8("驾车");
}

/** 秒数转「约 x 分钟 / 约 x 小时」。 */
QString durationText(int seconds)
{
    if (seconds <= 0)
        return u8("未知");
    const int minutes = qMax(1, qRound(seconds / 60.0));
    if (minutes < 60)
        return u8("约 %1 分钟").arg(minutes);
    return u8("约 %1 小时 %2 分钟").arg(minutes / 60).arg(minutes % 60);
}

/** 经纬度是否在地球范围内。 */
bool validCoordinate(double lat, double lng)
{
    return qIsFinite(lat) && qIsFinite(lng) && lat >= -90.0 && lat <= 90.0 && lng >= -180.0 && lng <= 180.0
           && (qAbs(lat) > 1e-9 || qAbs(lng) > 1e-9);
}

/** 地图选点：点一下把像素换成经纬度。 */
class MapPickLabel : public QLabel {
public:
    explicit MapPickLabel(QWidget *parent = nullptr) : QLabel(parent) {}
    std::function<void(QPoint)> onClick;
protected:
    void mousePressEvent(QMouseEvent *ev) override
    {
        if (onClick)
            onClick(ev->pos());
        QLabel::mousePressEvent(ev);
    }
};

QString windowsLocateScript()
{
    return QStringLiteral(
        "Add-Type -AssemblyName System.Device; "
        "$a=[System.Device.Location.GeoPositionAccuracy]::High; "
        "$w=New-Object System.Device.Location.GeoCoordinateWatcher $a; "
        "$null=$w.TryStart($false,(New-TimeSpan -Seconds 8)); "
        "$d=(Get-Date).AddSeconds(8); "
        "while((Get-Date) -lt $d -and $w.Position.Location.IsUnknown){Start-Sleep -Milliseconds 250}; "
        "$c=$w.Position.Location; $w.Stop(); "
        "if($null -eq $c -or $c.IsUnknown){Write-Output 'FAIL'; exit 0}; "
        "$ci=[Globalization.CultureInfo]::InvariantCulture; "
        "Write-Output ('OK {0} {1} {2}' -f $c.Latitude.ToString($ci),$c.Longitude.ToString($ci),"
        "$c.HorizontalAccuracy.ToString($ci))");
}

} // namespace

static QString goldStars(int n)
{
    QString s;
    const int filled = qBound(0, n, 5);
    for (int i = 1; i <= 5; ++i)
        s += i <= filled ? QString::fromUtf8("★") : QString::fromUtf8("☆");
    return s;
}

static QFrame *card()
{
    auto *f = new QFrame;
    f->setObjectName("card");
    f->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    return f;
}

static void prepCombo(QComboBox *c)
{
    c->setMaxVisibleItems(8);
    c->setMinimumHeight(30);
    c->setFocusPolicy(Qt::StrongFocus);
    c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    c->setMinimumContentsLength(4);
    auto *view = new QListView(c);
    view->setUniformItemSizes(true);
    view->setMinimumHeight(30);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setTextElideMode(Qt::ElideRight);
    view->setAlternatingRowColors(false);
    c->setView(view);
}

static QScrollArea *makeScroll(QWidget *inner)
{
    inner->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto *s = new QScrollArea;
    s->setWidgetResizable(true);
    s->setFrameShape(QFrame::NoFrame);
    s->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    s->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    s->setWidget(inner);
    return s;
}

UserWindow::UserWindow(QWidget *parent) : QMainWindow(parent)
{
    setObjectName("desktop");
    setWindowTitle(u8("ChargeHub 用户端"));
    setMinimumSize(360, 680);
    setMaximumWidth(480);
    resize(420, 800);
    root_ = new QStackedWidget;
    setCentralWidget(root_);
    mapNetwork_ = new QNetworkAccessManager(this);
    root_->addWidget(buildLogin());
    root_->addWidget(buildShell());

    connect(&controller_, &UserController::connected, this, [this] {
        loginPage_->setStatus(u8("已连接运营平台"));
        statusBar()->showMessage(u8("已连接服务器"));
    });
    connect(&controller_, &UserController::failed, this, [this](const QString &m) {
        loginPage_->setStatus(m);
        statusBar()->showMessage(m);
    });
    connect(&controller_, &UserController::responded, this, &UserWindow::onResp);
    connect(&controller_, &UserController::chargeStartBlocked, this, [this](const QJsonObject &order) {
        uiWarn(this, u8("无法开始充电"), u8("您有未完成的充电订单，请先结算"));
        showCharge(order);
    });

    // 状态栏显示连接状态
    statusBar()->showMessage(u8("正在连接服务器..."));

    // 自动连接（使用固定地址）
    QSettings settings(QStringLiteral("ChargeHub"), QStringLiteral("UserClient"));
    QString server = settings.value(QStringLiteral("server"), QStringLiteral("127.0.0.1:8888")).toString();
    QString host = server.section(':', 0, -2);
    quint16 port = server.section(':', -1).toUShort();
    if (port == 0) port = 8888;

    // 延迟一点自动连接，让界面先显示
    QTimer::singleShot(500, this, [this, host, port]() {
        controller_.connectTo(host, port);
    });
}

/** 清空动态卡片列表，避免刷新时叠一层。 */
/** 重新画列表前拆掉旧控件。 */
void UserWindow::clearBox(QLayout *lay)
{
    while (lay->count()) {
        QLayoutItem *it = lay->takeAt(0);
        if (it->widget())
            it->widget()->deleteLater();
        delete it;
    }
}

QWidget *UserWindow::buildLogin()
{
    loginPage_ = new LoginPage;
    connect(loginPage_, &LoginPage::connectRequested, this, &UserWindow::reconnect);
    connect(loginPage_, &LoginPage::authenticationRequested, this,
            [this](const QString &type, const QString &phone, const QString &password) {
        controller_.authenticate(type, phone, password);
        if (!controller_.isConnected()) {
            loginPage_->setStatus(u8("尚未连接，正在连接服务器…"));
            reconnect();
        }
    });
    return loginPage_;
}

/** 主壳：顶栏 + 内容 + 底部 Tab。页下标映射不变。 */
QWidget *UserWindow::buildShell()
{
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto *top = new QFrame;
    top->setObjectName("topbar");
    auto *tl = new QHBoxLayout(top);
    tl->setContentsMargins(16, 10, 16, 6);
    auto *titles = new QVBoxLayout;
    titles->setContentsMargins(0, 0, 0, 0);
    titles->setSpacing(2);
    pageTitle_ = new QLabel(u8("找桩"));
    pageTitle_->setObjectName("pageTitle");
    pageSub_ = new QLabel(u8("附近可用充电站"));
    pageSub_->setObjectName("muted");
    titles->addWidget(pageTitle_);
    titles->addWidget(pageSub_);
    headBal_ = new QLabel(u8("余额 --"));
    headBal_->setObjectName("pillOk");
    headBal_->setAlignment(Qt::AlignCenter);
    tl->addLayout(titles, 1);
    tl->addWidget(headBal_, 0, Qt::AlignRight | Qt::AlignVCenter);

    pages_ = new QStackedWidget;
    pages_->addWidget(buildHome());
    pages_->addWidget(buildPiles());
    pages_->addWidget(buildCharge());
    pages_->addWidget(buildOrders());
    pages_->addWidget(buildMe());
    pages_->addWidget(buildPileReview());
    pages_->addWidget(buildReservations());

    auto *tabs = new QFrame;
    tabs->setObjectName("tabBar");
    auto *tabLay = new QHBoxLayout(tabs);
    tabLay->setContentsMargins(4, 2, 4, 4);
    tabLay->setSpacing(2);
    const QStringList names = {u8("找桩"), u8("预约"), u8("充电"), u8("订单"), u8("我的")};
    tabBtns_.clear();
    for (int row = 0; row < names.size(); ++row) {
        auto *b = new QPushButton(names.at(row));
        b->setObjectName("tabBtn");
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, [this, row] {
            if (row == 0)
                switchTab(0);
            else if (row == 1)
                switchTab(6);
            else if (row == 2)
                switchTab(2);
            else if (row == 3)
                switchTab(3);
            else
                switchTab(4);
        });
        tabBtns_.append(b);
        tabLay->addWidget(b, 1);
    }
    if (!tabBtns_.isEmpty())
        tabBtns_.first()->setChecked(true);

    lay->addWidget(top);
    lay->addWidget(pages_, 1);
    lay->addWidget(tabs);
    return w;
}

void UserWindow::highlightTab(int pageIndex)
{
    int row = 4;
    if (pageIndex == 0 || pageIndex == 1 || pageIndex == 5)
        row = 0;
    else if (pageIndex == 6)
        row = 1;
    else if (pageIndex == 2)
        row = 2;
    else if (pageIndex == 3)
        row = 3;
    else if (pageIndex == 4)
        row = 4;
    for (int i = 0; i < tabBtns_.size(); ++i) {
        tabBtns_[i]->blockSignals(true);
        tabBtns_[i]->setChecked(i == row);
        tabBtns_[i]->blockSignals(false);
    }
}

/** 切业务页；订单/预约会再拉一次列表。 */
void UserWindow::switchTab(int i)
{
    pages_->setCurrentIndex(i);
    highlightTab(i);
    if (pageTitle_ && pageSub_) {
        if (i == 0) {
            pageTitle_->setText(u8("找桩"));
            pageSub_->setText(u8("附近可用充电站"));
        } else if (i == 1) {
            pageTitle_->setText(u8("选桩"));
            pageSub_->setText(u8("预约 15 分钟或直接开充"));
        } else if (i == 2) {
            pageTitle_->setText(u8("充电"));
            pageSub_->setText(u8("实时度数与费用"));
        } else if (i == 3) {
            pageTitle_->setText(u8("订单"));
            pageSub_->setText(u8("充电记录"));
        } else if (i == 6) {
            pageTitle_->setText(u8("预约"));
            pageSub_->setText(u8("尚未到期的占桩"));
        } else if (i == 5) {
            pageTitle_->setText(u8("评价"));
            pageSub_->setText(u8("这根桩的充电体验"));
        } else {
            pageTitle_->setText(u8("我的"));
            pageSub_->setText(u8("资料与钱包"));
        }
    }
    if (i == 0) {
        if (!pendingConsent_)
            queryStations();
    } else if (i == 2) {
        if (controller_.isAuthenticated())
            controller_.request("CHARGE_STATUS");
    } else if (i == 3) {
        if (controller_.isAuthenticated())
            controller_.request("LIST_ORDERS");
    } else if (i == 4) {
        refreshMe();
    } else if (i == 6) {
        if (controller_.isAuthenticated())
            controller_.request("LIST_RESERVATIONS");
    }
}

/** 找站页。 */
QWidget *UserWindow::buildHome()
{
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(16, 4, 16, 8);
    lay->setSpacing(10);

    auto *bar = new QFrame;
    bar->setObjectName("searchCard");
    auto *locCol = new QVBoxLayout(bar);
    locCol->setContentsMargins(10, 10, 10, 10);
    locCol->setSpacing(6);
    addrEdit_ = new QLineEdit;
    addrEdit_->setPlaceholderText(u8("搜索住址或地标"));
    connect(addrEdit_, &QLineEdit::returnPressed, this, [this] {
        useGps_ = false;
        queryStations();
    });
    auto *actRow = new QHBoxLayout;
    actRow->setSpacing(8);
    auto *gpsBtn = new QPushButton(u8("我的位置"));
    gpsBtn->setObjectName("ghost");
    connect(gpsBtn, &QPushButton::clicked, this, &UserWindow::askLocationConsent);
    auto *pickBtn = new QPushButton(u8("地图选点"));
    pickBtn->setObjectName("ghost");
    connect(pickBtn, &QPushButton::clicked, this, [this] {
        if (!controller_.isAuthenticated())
            return;
        pickMyLocation();
    });
    actRow->addWidget(gpsBtn, 1);
    actRow->addWidget(pickBtn, 1);
    auto *findRow = new QHBoxLayout;
    findRow->setSpacing(8);
    radius_ = new QComboBox;
    radius_->addItems({u8("3 km"), u8("5 km"), u8("10 km"), u8("20 km")});
    radius_->setCurrentIndex(3);
    prepCombo(radius_);
    connect(radius_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { queryStations(); });
    auto *find = new QPushButton(u8("查找附近"));
    connect(find, &QPushButton::clicked, this, [this] {
        useGps_ = false;
        queryStations();
    });
    findRow->addWidget(radius_);
    findRow->addWidget(find, 1);
    locCol->addWidget(addrEdit_);
    locCol->addLayout(actRow);
    locCol->addLayout(findRow);
    lay->addWidget(bar);

    auto *chips = new QHBoxLayout;
    chips->setSpacing(6);
    for (const auto &name : {u8("北京理工"), u8("软件园"), u8("五道口")}) {
        auto *b = new QPushButton(name);
        b->setObjectName("chip");
        connect(b, &QPushButton::clicked, this, [this, name] {
            useGps_ = false;
            if (addrEdit_)
                addrEdit_->setText(name == u8("北京理工") ? u8("北京理工大学")
                                  : (name == u8("软件园") ? u8("中关村软件园") : u8("五道口")));
            queryStations();
        });
        chips->addWidget(b);
    }
    lay->addLayout(chips);

    locMatch_ = new QLabel(u8("授权定位、地图选点或填写住址后查找"));
    locMatch_->setObjectName("muted");
    locMatch_->setWordWrap(true);
    lay->addWidget(locMatch_);
    weatherHint_ = new QLabel(u8("查找后显示当地天气"));
    weatherHint_->setObjectName("muted");
    weatherHint_->setWordWrap(true);
    lay->addWidget(weatherHint_);
    auto *inner = new QWidget;
    stationBox_ = new QVBoxLayout(inner);
    stationBox_->setContentsMargins(0, 0, 0, 8);
    stationBox_->setSpacing(10);
    lay->addWidget(makeScroll(inner), 1);
    return w;
}

/** 桩列表页。 */
QWidget *UserWindow::buildPiles()
{
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(16, 8, 16, 8);
    lay->setSpacing(10);
    auto *back = new QPushButton(u8("← 返回"));
    back->setObjectName("link");
    back->setMaximumWidth(72);
    connect(back, &QPushButton::clicked, this, [this] { switchTab(0); });
    pileTitle_ = new QLabel;
    pileTitle_->setObjectName("title");
    pileMeta_ = new QLabel;
    pileMeta_->setObjectName("muted");
    pileMeta_->setWordWrap(true);
    lay->addWidget(back);
    pileType_ = new QComboBox;
    pileType_->addItems({u8("全部类型"), u8("快充"), u8("慢充")});
    prepCombo(pileType_);
    connect(pileType_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!lastPiles_.isEmpty())
            renderPiles(lastPiles_);
    });
    lay->addWidget(pileTitle_);
    lay->addWidget(pileMeta_);
    lay->addWidget(pileType_);
    auto *inner = new QWidget;
    pileBox_ = new QVBoxLayout(inner);
    pileBox_->setContentsMargins(0, 0, 8, 0);
    pileBox_->setSpacing(10);
    lay->addWidget(makeScroll(inner), 1);
    return w;
}

/** 评价页。 */
QWidget *UserWindow::buildPileReview()
{
    auto *w = new QWidget;
    auto *outer = new QVBoxLayout(w);
    outer->setContentsMargins(16, 8, 16, 8);
    outer->setSpacing(8);
    auto *back = new QPushButton(u8("← 返回"));
    back->setObjectName("link");
    back->setMaximumWidth(72);
    connect(back, &QPushButton::clicked, this, [this] { switchTab(1); });
    outer->addWidget(back);

    auto *col = new QWidget;
    auto *lay = new QVBoxLayout(col);
    lay->setContentsMargins(8, 0, 8, 16);
    lay->setSpacing(12);

    rvTitle_ = new QLabel;
    rvTitle_->setObjectName("reviewTitle");
    rvTitle_->setWordWrap(true);
    rvStars_ = new QLabel;
    rvStars_->setObjectName("reviewAvg");
    rvMeta_ = new QLabel;
    rvMeta_->setObjectName("muted");
    rvMeta_->setWordWrap(true);
    rvNlp_ = new QLabel;
    rvNlp_->setObjectName("muted");
    rvNlp_->setWordWrap(true);
    lay->addWidget(rvTitle_);
    lay->addWidget(rvStars_);
    lay->addWidget(rvMeta_);
    lay->addWidget(rvNlp_);

    auto *div = new QLabel(u8("—  我的评价  —"));
    div->setObjectName("reviewDiv");
    div->setAlignment(Qt::AlignCenter);
    lay->addSpacing(6);
    lay->addWidget(div);

    auto *hint = new QLabel(u8("请客观描述充电体验：功率是否稳定、是否排队、车位是否好停、价格是否合适。"
                               "每位用户对每根桩可写一条评价，再次发布会覆盖自己的旧评。"));
    hint->setObjectName("hintBox");
    hint->setWordWrap(true);
    lay->addWidget(hint);

    auto *starRow = new QHBoxLayout;
    starRow->setSpacing(4);
    reviewStarBtns_.clear();
    for (int i = 1; i <= 5; ++i) {
        auto *b = new QPushButton(u8("★"));
        b->setObjectName("starOn");
        b->setCursor(Qt::PointingHandCursor);
        b->setFlat(true);
        b->setFocusPolicy(Qt::NoFocus);
        connect(b, &QPushButton::clicked, this, [this, i] { setReviewStars(i); });
        starRow->addWidget(b);
        reviewStarBtns_.append(b);
    }
    starRow->addStretch();
    lay->addLayout(starRow);
    setReviewStars(5);

    reviewBody_ = new QPlainTextEdit;
    reviewBody_->setPlaceholderText(u8("写下这根桩的充电体验，2～300 字"));
    reviewBody_->setMinimumHeight(88);
    connect(reviewBody_, &QPlainTextEdit::textChanged, this, [this] {
        QString t = reviewBody_->toPlainText();
        if (t.size() > 300) {
            reviewBody_->blockSignals(true);
            t = t.left(300);
            reviewBody_->setPlainText(t);
            auto c = reviewBody_->textCursor();
            c.movePosition(QTextCursor::End);
            reviewBody_->setTextCursor(c);
            reviewBody_->blockSignals(false);
        }
        if (reviewCount_)
            reviewCount_->setText(QString("%1 / 300").arg(t.size()));
    });
    lay->addWidget(reviewBody_);

    auto *foot = new QHBoxLayout;
    reviewCount_ = new QLabel(u8("0 / 300"));
    reviewCount_->setObjectName("muted");
    auto *send = new QPushButton(u8("发布评价"));
    send->setObjectName("postReview");
    send->setMinimumWidth(128);
    connect(send, &QPushButton::clicked, this, &UserWindow::submitReview);
    foot->addWidget(reviewCount_);
    foot->addStretch();
    foot->addWidget(send);
    lay->addLayout(foot);

    auto *listDiv = new QLabel(u8("—  全部评价  —"));
    listDiv->setObjectName("reviewDiv");
    listDiv->setAlignment(Qt::AlignCenter);
    lay->addSpacing(8);
    lay->addWidget(listDiv);

    auto *listInner = new QWidget;
    reviewListBox_ = new QVBoxLayout(listInner);
    reviewListBox_->setContentsMargins(0, 0, 0, 0);
    reviewListBox_->setSpacing(10);
    lay->addWidget(listInner);
    lay->addStretch();
    outer->addWidget(makeScroll(col), 1);
    return w;
}

/** 充电进行页。 */
QWidget *UserWindow::buildCharge()
{
    chargePage_ = new ChargePage;
    connect(chargePage_, &ChargePage::stopRequested,
            this, [this] { controller_.request("STOP_CHARGE"); });
    connect(chargePage_, &ChargePage::settleRequested,
            this, [this] { controller_.request("SETTLE_ORDER"); });
    connect(chargePage_, &ChargePage::findPileRequested,
            this, [this] { switchTab(0); });
    return chargePage_;
}

/** 订单与充值流水页。 */
QWidget *UserWindow::buildOrders()
{
    ordersPage_ = new OrdersPage;
    return ordersPage_;
}

/** 预约列表页。 */
QWidget *UserWindow::buildReservations()
{
    reservationsPage_ = new ReservationsPage;
    connect(reservationsPage_, &ReservationsPage::refreshRequested, this, [this] {
        if (controller_.isAuthenticated()) controller_.request("LIST_RESERVATIONS");
    });
    connect(reservationsPage_, &ReservationsPage::findStationsRequested,
            this, [this] { switchTab(0); });
    connect(reservationsPage_, &ReservationsPage::stationRequested,
            this, [this](const QJsonObject &station) {
        currentStation_ = station;
        controller_.request("QUERY_PILES", QJsonObject{{"stationId", station.value("id").toInt()}});
    });
    connect(reservationsPage_, &ReservationsPage::chargeRequested,
            this, &UserWindow::tryStart);
    connect(reservationsPage_, &ReservationsPage::cancelRequested,
            this, [this] { controller_.request("CANCEL_RESERVE"); });
    connect(reservationsPage_, &ReservationsPage::navigationRequested,
            this, &UserWindow::openNav);
    return reservationsPage_;
}

/** 个人中心。 */
QWidget *UserWindow::buildMe()
{
    auto *w = new QWidget;
    auto *outer = new QVBoxLayout(w);
    outer->setContentsMargins(16, 4, 16, 8);
    outer->setSpacing(0);

    auto *form = new QWidget;
    auto *lay = new QVBoxLayout(form);
    lay->setContentsMargins(0, 4, 0, 16);
    lay->setSpacing(10);

    auto *hero = new QFrame;
    hero->setObjectName("card");
    auto *heroLay = new QVBoxLayout(hero);
    heroLay->setContentsMargins(16, 16, 16, 16);
    heroLay->setSpacing(10);
    auto *top = new QHBoxLayout;
    top->setSpacing(12);
    avatar_ = new QLabel(u8("用"));
    avatar_->setFixedSize(52, 52);
    avatar_->setAlignment(Qt::AlignCenter);
    avatar_->setObjectName("userAvatar");
    avatar_->setScaledContents(true);
    auto *col = new QVBoxLayout;
    col->setSpacing(4);
    meName_ = new QLabel;
    meName_->setObjectName("title");
    meName_->setWordWrap(true);
    meBal_ = new QLabel;
    meBal_->setObjectName("muted");
    meBal_->setWordWrap(true);
    col->addWidget(meName_);
    col->addWidget(meBal_);
    top->addWidget(avatar_, 0, Qt::AlignTop);
    top->addLayout(col, 1);
    heroLay->addLayout(top);
    auto *avRow = new QHBoxLayout;
    avRow->setSpacing(8);
    auto *pick = new QPushButton(u8("更换头像"));
    pick->setObjectName("ghost");
    auto *resetAv = new QPushButton(u8("恢复默认"));
    resetAv->setObjectName("ghost");
    connect(pick, &QPushButton::clicked, this, &UserWindow::pickAvatar);
    connect(resetAv, &QPushButton::clicked, this, &UserWindow::clearAvatar);
    avRow->addWidget(pick, 1);
    avRow->addWidget(resetAv, 1);
    heroLay->addLayout(avRow);
    lay->addWidget(hero);

    auto *nickLab = new QLabel(u8("昵称（1~20 字）"));
    nickEdit_ = new QLineEdit;
    auto *save = new QPushButton(u8("保存资料"));
    connect(save, &QPushButton::clicked, this, [this] {
        const QString nick = nickEdit_->text().trimmed();
        if (nick.size() < 1 || nick.size() > 20) {
            uiWarn(this, u8("格式错误"), u8("昵称长度须为 1~20 个字符"));
            return;
        }
        controller_.request("UPDATE_PROFILE", QJsonObject{{"nickname", nick}});
    });
    lay->addWidget(nickLab);
    lay->addWidget(nickEdit_);
    lay->addWidget(save);

    auto *payLab = new QLabel(u8("钱包充值（模拟，单笔 ≤ 10000 元）"));
    payEdit_ = new QLineEdit("20");
    auto *row = new QHBoxLayout;
    row->setSpacing(6);
    for (int a : {20, 50, 100, 200}) {
        auto *b = new QPushButton(QString("¥%1").arg(a));
        b->setObjectName("chip");
        connect(b, &QPushButton::clicked, this, [this, a] { payEdit_->setText(QString::number(a)); });
        row->addWidget(b, 1);
    }
    auto *pay = new QPushButton(u8("确认充值"));
    connect(pay, &QPushButton::clicked, this, [this] {
        const double a = payEdit_->text().toDouble();
        if (a <= 0 || a > 10000) {
            uiWarn(this, u8("金额错误"), u8("单笔充值须大于 0 且不超过 10000 元"));
            return;
        }
        controller_.request("RECHARGE", QJsonObject{{"amount", a}});
    });
    lay->addWidget(payLab);
    lay->addLayout(row);
    lay->addWidget(payEdit_);
    lay->addWidget(pay);

    auto *recTitle = new QLabel(u8("充值记录"));
    recTitle->setObjectName("h2");
    lay->addWidget(recTitle);
    auto *rinner = new QWidget;
    rechargeBox_ = new QVBoxLayout(rinner);
    rechargeBox_->setContentsMargins(0, 0, 0, 0);
    rechargeBox_->setSpacing(8);
    lay->addWidget(rinner);

    auto *logout = new QPushButton(u8("退出登录"));
    logout->setObjectName("ghost");
    connect(logout, &QPushButton::clicked, this, [this] {
        controller_.signOut();
        root_->setCurrentIndex(0);
        loginPage_->setStatus(u8("已安全退出，请重新登录"));
        statusBar()->showMessage(u8("已退出登录"));
    });
    auto *closeAcc = new QPushButton(u8("注销账号"));
    closeAcc->setObjectName("danger");
    connect(closeAcc, &QPushButton::clicked, this, &UserWindow::closeMyAccount);
    auto *closeHint = new QLabel(u8("注销后不能登录。历史订单仍保留，账号标记为「注销」。"));
    closeHint->setObjectName("muted");
    closeHint->setWordWrap(true);
    lay->addWidget(closeHint);
    lay->addWidget(closeAcc);
    lay->addWidget(logout);

    outer->addWidget(makeScroll(form), 1);
    return w;
}

/** 按登录页地址重新拨号。 */
void UserWindow::reconnect()
{
    // 使用固定地址
    QString host = QStringLiteral("127.0.0.1");
    quint16 port = 8888;

    QSettings ini(QStringLiteral("ChargeHub"), QStringLiteral("UserClient"));
    ini.setValue("server", QStringLiteral("127.0.0.1:8888"));
    loginPage_->setStatus(u8("正在连接 ") + host + ":" + QString::number(port) + u8(" …"));
    controller_.connectTo(host, port);
}

/** 当前定位，给找站和导航。 */
QJsonObject UserWindow::coord() const
{
    return QJsonObject{{"lat", locLat_}, {"lng", locLng_}};
}

/** 发 QUERY_STATIONS。同意定位后带 useGps，否则仍按住址解析。 */
void UserWindow::queryStations()
{
    if (!controller_.isAuthenticated())
        return;

    // 如果还没有位置信息且未尝试过IP定位，尝试IP定位
    if (!useGps_ && !hasLocationFromIP_ &&
        locLat_ == 39.9644 && locLng_ == 116.3473) {
        fetchLocationByIP();
        return;
    }

    QJsonObject data = coord();
    const double radii[] = {3, 5, 10, 20};
    data["radiusKm"] = radii[qBound(0, radius_ ? radius_->currentIndex() : 3, 3)];
    if (useGps_) {
        data["useGps"] = true;
        data["placeName"] = gpsPlace_.isEmpty() ? u8("GPS 定位") : gpsPlace_;
    } else if (addrEdit_) {
        data["address"] = addrEdit_->text().trimmed();
    }
    controller_.request("QUERY_STATIONS", data);
}

/** 用控制器维护的用户快照刷新顶栏余额和头像。 */
void UserWindow::applyUser(const QJsonObject &u)
{
    const double bal = u.value("balance").toDouble();
    headBal_->setText(u8("余额 ¥") + QString::number(bal, 'f', 2));
    if (u.contains("lat"))
        locLat_ = u.value("lat").toDouble(locLat_);
    if (u.contains("lng"))
        locLng_ = u.value("lng").toDouble(locLng_);
    if (addrEdit_ && addrEdit_->text().trimmed().isEmpty() && !u.value("address").toString().isEmpty())
        addrEdit_->setText(u.value("address").toString());
    refreshMe();
}

/** 登录成功切到主壳，先问位置授权再找桩。 */
void UserWindow::showShell()
{
    pendingConsent_ = true;
    root_->setCurrentIndex(1);
    switchTab(0);
    QTimer::singleShot(0, this, &UserWindow::askLocationConsent);
}

/** 登录后协议：不同意则不采集位置，仍可用住址查找。 */
void UserWindow::askLocationConsent()
{
    pendingConsent_ = false;
    const bool ok = uiAsk(this, u8("位置信息授权"),
                          u8("为了按距离为您匹配最近的充电桩并导航，ChargeHub 需要您的位置。\n\n"
                             "将优先使用 Windows 定位服务（Wi-Fi / 系统定位，一般可到几十到几百米）。"
                             "若仍不准，您可以在地图上点选，或填写精确住址。\n\n"
                             "桌面端没有手机 GPS 芯片，公网 IP 只能定到城市，不会再当作精确位置。"
                             "坐标仅用于计算到电站的距离，不会用于广告。\n\n"
                             "是否同意获取位置？"),
                          u8("同意并定位"), u8("暂不使用"));
    if (!ok) {
        useGps_ = false;
        if (locMatch_)
            locMatch_->setText(u8("未授权定位。请填写住址后点击查找，或再点「使用我的位置」。"));
        queryStations();
        return;
    }
    startLocate();
}

/** 先 Windows 定位；精度不够则地图选点。不用公网 IP 冒充精确位置。 */
void UserWindow::startLocate()
{
    if (!controller_.isAuthenticated() || !mapNetwork_) {
        queryStations();
        return;
    }
    if (locMatch_)
        locMatch_->setText(u8("正在通过 Windows 定位服务获取位置…"));
    tryWindowsLocate();
}

void UserWindow::applyGpsFix(double lat, double lng, const QString &place)
{
    locLat_ = lat;
    locLng_ = lng;
    gpsPlace_ = place;
    useGps_ = true;
    queryStations();
}

void UserWindow::tryWindowsLocate()
{
    QString exe = QStringLiteral("/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe");
    if (!QFileInfo::exists(exe))
        exe = QStringLiteral("powershell.exe");
    auto *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    QTimer::singleShot(14000, proc, [proc] {
        if (proc->state() != QProcess::NotRunning)
            proc->kill();
    });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc](int, QProcess::ExitStatus) {
                const QString out = QString::fromLocal8Bit(proc->readAllStandardOutput()).trimmed();
                proc->deleteLater();
                QString line = out;
                const int okAt = out.lastIndexOf(QLatin1String("OK "));
                if (okAt >= 0)
                    line = out.mid(okAt).split('\n').first().trimmed();
                const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                double acc = 0;
                TencentApi::Geo g;
                if (parts.size() >= 3 && parts.at(0) == QLatin1String("OK")) {
                    g.lat = parts.at(1).toDouble();
                    g.lng = parts.at(2).toDouble();
                    if (parts.size() >= 4)
                        acc = parts.at(3).toDouble();
                    g.ok = validCoordinate(g.lat, g.lng);
                }
                if (g.ok && (acc <= 0 || acc <= 2000.0)) {
                    const QString place = acc > 0 ? u8("系统定位（约 %1 米）").arg(qRound(acc))
                                                 : u8("系统定位");
                    applyGpsFix(g.lat, g.lng, place);
                    return;
                }
                if (locMatch_)
                    locMatch_->setText(g.ok ? u8("系统定位精度不够，请在地图上点选您所在的位置。")
                                            : u8("未拿到精确系统定位，请在地图上点选，或填写住址。"));
                pickMyLocation();
            });
    proc->start(exe, {QStringLiteral("-NoProfile"), QStringLiteral("-STA"),
                      QStringLiteral("-NonInteractive"), QStringLiteral("-Command"),
                      windowsLocateScript()});
}

/** 通过IP获取用户位置 */
void UserWindow::fetchLocationByIP()
{
    if (!mapNetwork_) {
        mapNetwork_ = new QNetworkAccessManager(this);
    }

    hasLocationFromIP_ = true;
    if (locMatch_) {
        locMatch_->setText(u8("正在通过IP获取位置…"));
    }

    QUrl url("http://ip-api.com/json/?fields=status,lat,lon,city,regionName&lang=zh-CN");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "ChargeHub/1.0");

    auto *reply = mapNetwork_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonObject obj = doc.object();
            if (obj.value("status").toString() == "success") {
                double lat = obj.value("lat").toDouble();
                double lng = obj.value("lon").toDouble();
                QString city = obj.value("city").toString();
                QString region = obj.value("regionName").toString();

                if (lat != 0.0 || lng != 0.0) {
                    locLat_ = lat;
                    locLng_ = lng;
                    useGps_ = true;
                    gpsPlace_ = region.isEmpty() ? city : (city.isEmpty() ? region : region + " " + city);

                    if (locMatch_) {
                        locMatch_->setText(u8("已通过IP定位：") + gpsPlace_);
                    }
                    // 自动查找附近电站
                    queryStations();
                } else {
                    if (locMatch_) {
                        locMatch_->setText(u8("IP定位失败，请填写住址查找"));
                    }
                }
            } else {
                if (locMatch_) {
                    locMatch_->setText(u8("IP定位服务不可用，请填写住址查找"));
                }
            }
        } else {
            if (locMatch_) {
                locMatch_->setText(u8("IP定位请求失败，请填写住址查找"));
            }
        }
        reply->deleteLater();
    });
}

bool UserWindow::pickMyLocation()
{
    if (!controller_.isAuthenticated())
        return false;

    UiSheet dialog(this, u8("标定我的位置"),
                   u8("在地图上点击您所在的位置。可先填住址缩小范围，再点地图精确定点。"));
    dialog.setWindowTitle(u8("标定我的位置"));
    dialog.polish(380, 640);

    auto *addr = new QLineEdit;
    addr->setPlaceholderText(u8("住址或地标，例如：北京理工大学中心教学楼"));
    if (addrEdit_ && !addrEdit_->text().trimmed().isEmpty())
        addr->setText(addrEdit_->text().trimmed());
    auto *goAddr = new QPushButton(u8("定位到此处"));
    auto *addrRow = new QHBoxLayout;
    addrRow->addWidget(addr, 1);
    addrRow->addWidget(goAddr);
    dialog.body()->addLayout(addrRow);

    auto *zoomRow = new QHBoxLayout;
    auto *zoomOut = new QPushButton(u8("缩小"));
    zoomOut->setObjectName("ghost");
    auto *zoomIn = new QPushButton(u8("放大"));
    zoomIn->setObjectName("ghost");
    auto *zoomHint = new QLabel(u8("放大后点击更准"));
    zoomHint->setObjectName("uiSheetHint");
    zoomRow->addWidget(zoomOut);
    zoomRow->addWidget(zoomIn);
    zoomRow->addWidget(zoomHint, 1);
    dialog.body()->addLayout(zoomRow);

    auto *map = new MapPickLabel;
    map->setAlignment(Qt::AlignCenter);
    map->setMinimumSize(320, 280);
    map->setCursor(Qt::CrossCursor);
    map->setStyleSheet(QStringLiteral(
        "background:#F8FAFC;border:1px solid #E2E8F0;border-radius:12px;color:#64748B;font-size:14px;"));
    map->setText(u8("正在加载地图…"));
    dialog.body()->addWidget(map, 1);

    auto *status = new QLabel(u8("请点击地图上您所在的位置。"));
    status->setObjectName("uiSheetHint");
    status->setWordWrap(true);
    dialog.body()->addWidget(status);

    double pickLat = locLat_;
    double pickLng = locLng_;
    int zoom = 16;
    int tileX = 0, tileY = 0;
    bool picked = false;

    auto loadMap = [this, map, status, &pickLat, &pickLng, &zoom, &tileX, &tileY]() {
        if (!mapNetwork_ || !map)
            return;
        TencentApi::mapTile(pickLat, pickLng, zoom, &tileX, &tileY);
        map->setText(u8("正在加载地图…"));
        auto *reply = mapNetwork_->get(TencentApi::request(TencentApi::fallbackMapUrl(pickLat, pickLng, zoom)));
        const QPointer<MapPickLabel> guard(map);
        connect(reply, &QNetworkReply::finished, this, [reply, guard, status] {
            if (guard) {
                QPixmap pix;
                if (reply->error() == QNetworkReply::NoError && pix.loadFromData(reply->readAll())) {
                    guard->setPixmap(pix);
                    guard->setText(QString());
                } else if (status) {
                    status->setText(u8("地图加载失败，仍可填写住址后点「定位到此处」。"));
                }
            }
            reply->deleteLater();
        });
    };

    map->onClick = [map, status, &pickLat, &pickLng, &zoom, &tileX, &tileY, &picked](QPoint pos) {
        const QPixmap pm = map->pixmap(Qt::ReturnByValue);
        if (pm.isNull() || pm.width() <= 0)
            return;
        const int x0 = (map->width() - pm.width()) / 2;
        const int y0 = (map->height() - pm.height()) / 2;
        const double px = pos.x() - x0;
        const double py = pos.y() - y0;
        if (px < 0 || py >= pm.height() || py < 0 || px >= pm.width())
            return;
        TencentApi::tilePixelToLatLng(tileX, tileY, zoom, px, py, pm.width(), &pickLat, &pickLng);
        picked = validCoordinate(pickLat, pickLng);
        if (picked && status)
            status->setText(u8("已选点：%1, %2  可再点一次微调，或点确定。")
                                .arg(pickLat, 0, 'f', 5)
                                .arg(pickLng, 0, 'f', 5));
    };

    connect(zoomIn, &QPushButton::clicked, this, [&zoom, loadMap] {
        if (zoom < 18) {
            ++zoom;
            loadMap();
        }
    });
    connect(zoomOut, &QPushButton::clicked, this, [&zoom, loadMap] {
        if (zoom > 12) {
            --zoom;
            loadMap();
        }
    });
    connect(goAddr, &QPushButton::clicked, this, [this, addr, status, &pickLat, &pickLng, &picked, loadMap] {
        const QString text = addr->text().trimmed();
        if (text.isEmpty()) {
            status->setText(u8("请先填写住址或地标。"));
            return;
        }
        status->setText(u8("正在解析地址…"));
        QUrl nom(QStringLiteral("https://nominatim.openstreetmap.org/search"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("q"), text);
        q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
        q.addQueryItem(QStringLiteral("limit"), QStringLiteral("1"));
        nom.setQuery(q);
        auto *reply = mapNetwork_->get(TencentApi::request(nom));
        connect(reply, &QNetworkReply::finished, this,
                [reply, status, &pickLat, &pickLng, &picked, loadMap, text] {
                    const QJsonArray hits = QJsonDocument::fromJson(reply->readAll()).array();
                    reply->deleteLater();
                    if (hits.isEmpty()) {
                        status->setText(u8("没找到该地址，请换个写法或直接点地图。"));
                        return;
                    }
                    const QJsonObject first = hits.first().toObject();
                    pickLat = first.value(QStringLiteral("lat")).toString().toDouble();
                    pickLng = first.value(QStringLiteral("lon")).toString().toDouble();
                    picked = validCoordinate(pickLat, pickLng);
                    status->setText(picked ? u8("已定位到：%1，可再点地图微调。").arg(text)
                                           : u8("地址坐标无效，请点地图。"));
                    if (picked)
                        loadMap();
                });
    });

    dialog.addCancel(u8("取消"));
    auto *ok = dialog.addOk(u8("确定用该点"));
    disconnect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(ok, &QPushButton::clicked, &dialog, [this, &picked, &pickLat, &pickLng, addr, status, &dialog] {
        if (!picked) {
            status->setText(u8("请先在地图上点一下，或先定位到住址。"));
            return;
        }
        const QString place = addr->text().trimmed().isEmpty() ? u8("地图选点") : addr->text().trimmed();
        if (addrEdit_ && !addr->text().trimmed().isEmpty())
            addrEdit_->setText(addr->text().trimmed());
        applyGpsFix(pickLat, pickLng, place);
        dialog.accept();
    });

    loadMap();
    return dialog.exec() == QDialog::Accepted;
}

/** 画头像：Base64 或昵称首字。 */
void UserWindow::showAvatar(const QJsonObject &u)
{
    if (!avatar_)
        return;
    const QString nick = u.value("nickname").toString();
    const QByteArray raw = QByteArray::fromBase64(u.value("avatarBase64").toString().toLatin1());
    QPixmap pm;
    if (!raw.isEmpty() && pm.loadFromData(raw)) {
        avatar_->setPixmap(pm.scaled(72, 72, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        avatar_->setText(QString());
        return;
    }
    avatar_->setPixmap(QPixmap());
    avatar_->setText(nick.isEmpty() ? QStringLiteral("用") : nick.left(1));
}

/** 选本地图，压小后 UPDATE_PROFILE。 */
void UserWindow::pickAvatar()
{
    if (!controller_.isAuthenticated())
        return;
    const QString path = QFileDialog::getOpenFileName(this, u8("选择头像"), QString(),
                                                      u8("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp)"));
    if (path.isEmpty())
        return;
    QImage img(path);
    if (img.isNull()) {
        uiWarn(this, u8("无法打开"), u8("请选择有效的 jpg / png 图片"));
        return;
    }
    if (img.width() > 512 || img.height() > 512)
        img = img.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    if (!img.save(&buf, "JPEG", 88)) {
        uiWarn(this, u8("无法处理"), u8("图片编码失败，请换一张"));
        return;
    }
    if (bytes.size() > 400 * 1024) {
        uiWarn(this, u8("图片过大"), u8("请选择更小的图片（压缩后需小于 400KB）"));
        return;
    }
    controller_.request("UPDATE_PROFILE",
                        QJsonObject{{"avatarBase64", QString::fromLatin1(bytes.toBase64())}});
}

/** 清头像再 UPDATE_PROFILE。 */
void UserWindow::clearAvatar()
{
    if (!controller_.isAuthenticated())
        return;
    controller_.request("UPDATE_PROFILE", QJsonObject{{"clearAvatar", true}});
}

/** 刷新「我的」页资料。 */
void UserWindow::refreshMe()
{
    const QJsonObject user = controller_.user();
    if (user.isEmpty())
        return;
    const QString nick = user.value("nickname").toString();
    meName_->setText(nick);
    meBal_->setText(user.value("phone").toString() + u8("  ·  钱包 ¥")
                    + QString::number(user.value("balance").toDouble(), 'f', 2));
    nickEdit_->setText(nick);
    showAvatar(user);
    if (controller_.isAuthenticated())
        controller_.request("LIST_RECHARGE");
}

/** 二次确认后 CLOSE_ACCOUNT。 */
void UserWindow::closeMyAccount()
{
    if (!uiAsk(this, u8("注销账号"),
               u8("注销后账号将被禁用并留档，订单、评价、充值记录均保留以便追溯。\n"
                  "同一手机号不能再注册。确定注销吗？"),
               u8("确认注销"), u8("再想想"), true))
        return;
    controller_.request("CLOSE_ACCOUNT");
}

/** 按当前定位拉天气，写到附近电站页顶部。腾讯配额用尽时改 Open-Meteo。 */
void UserWindow::fetchLocalWeather()
{
    if (!weatherHint_ || !mapNetwork_)
        return;
    weatherHint_->setText(u8("正在查询当地天气…"));
    const double lat = locLat_;
    const double lng = locLng_;
    const QPointer<QLabel> guard(weatherHint_);
    auto apply = [guard](const TencentApi::Weather &wx) {
        if (!guard)
            return;
        if (wx.ok)
            guard->setText(u8("当地天气：") + wx.detail + u8("（") + wx.source + u8("，已用于附近电站参考）"));
        else
            guard->setText(u8("天气暂不可用，仍可查找电站、查看地图和路线。"));
    };
    auto meteo = [this, lat, lng, apply]() {
        auto *fb = mapNetwork_->get(TencentApi::request(TencentApi::openMeteoUrl(lat, lng)));
        connect(fb, &QNetworkReply::finished, this, [fb, apply] {
            apply(TencentApi::parseOpenMeteo(QJsonDocument::fromJson(fb->readAll()).object()));
            fb->deleteLater();
        });
    };
    if (TencentApi::key().isEmpty()) {
        meteo();
        return;
    }
    auto *reply = mapNetwork_->get(TencentApi::request(TencentApi::weatherNowUrl(lat, lng)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, apply, meteo] {
        const TencentApi::Weather wx =
            TencentApi::parseWeather(QJsonDocument::fromJson(reply->readAll()).object());
        reply->deleteLater();
        if (wx.ok)
            apply(wx);
        else
            meteo();
    });
}

/** 画附近电站卡片。 */
void UserWindow::renderStations(const QJsonObject &data)
{
    const auto loc = data.value("location").toObject();
    locLat_ = loc.value("lat").toDouble(locLat_);
    locLng_ = loc.value("lng").toDouble(locLng_);
    const QString matched = loc.value("matchedPlace").toString();
    const QString addr = loc.value("address").toString();
    if (loc.value("matched").toBool() && !matched.isEmpty()) {
        if (useGps_)
            locMatch_->setText(u8("已按您的位置定位：%1（%2, %3）")
                                   .arg(matched)
                                   .arg(locLat_, 0, 'f', 5)
                                   .arg(locLng_, 0, 'f', 5));
        else if (addr.isEmpty())
            locMatch_->setText(u8("已按%1匹配附近电站和电桩").arg(matched));
        else
            locMatch_->setText(u8("已定位：%1（%2）").arg(matched, addr));
    } else if (!addr.isEmpty()) {
        locMatch_->setText(u8("已按地址估算位置：%1").arg(addr));
    } else {
        locMatch_->setText(u8("未填写地址时默认按中关村匹配附近站点"));
    }
    if (useGps_ && data.value("stations").toArray().isEmpty())
        locMatch_->setText(locMatch_->text() + u8("  演示电站多在北京海淀，当前位置较远时可改填住址查找。"));
    fetchLocalWeather();

    clearBox(stationBox_);

    // 获取电站数组并按距离排序
    QJsonArray stations = data.value("stations").toArray();
    QVector<QJsonObject> stationList;
    for (const auto &v : stations) {
        stationList.append(v.toObject());
    }

    // 如果已定位，按距离排序
    if (useGps_ || (locLat_ != 39.9644 || locLng_ != 116.3473)) {
        std::sort(stationList.begin(), stationList.end(),
            [this](const QJsonObject &a, const QJsonObject &b) {
                double distA = haversine(locLat_, locLng_,
                                         a.value("lat").toDouble(),
                                         a.value("lng").toDouble());
                double distB = haversine(locLat_, locLng_,
                                         b.value("lat").toDouble(),
                                         b.value("lng").toDouble());
                return distA < distB;
            });
    }

    // 渲染排序后的电站列表
    auto *sh = new QLabel(u8("附近充电站"));
    sh->setObjectName("h2");
    stationBox_->addWidget(sh);

    if (stationList.isEmpty()) {
        auto *lab = new QLabel(u8("附近暂无充电站，请换一个更具体的地址试试"));
        lab->setObjectName("muted");
        stationBox_->addWidget(lab);
        stationBox_->addStretch();
        return;
    }

    for (const auto &s : stationList) {
        auto *c = card();
        auto *cl = new QVBoxLayout(c);
        cl->setContentsMargins(16, 14, 16, 14);
        cl->setSpacing(8);
        auto *row = new QHBoxLayout;
        auto *name = new QLabel(s.value("name").toString());
        name->setObjectName("cardTitle");
        const int idle = s.value("idlePiles").toInt();
        auto *pill = new QLabel(u8("空闲 %1/%2").arg(idle).arg(s.value("totalPiles").toInt()));
        pill->setObjectName(idle > 0 ? "pillOk" : "pillOff");
        row->addWidget(name, 1);
        row->addWidget(pill, 0, Qt::AlignRight | Qt::AlignVCenter);
        cl->addLayout(row);
        auto *addr = new QLabel(s.value("address").toString());
        addr->setObjectName("muted");
        addr->setWordWrap(true);
        cl->addWidget(addr);
        auto *meta = new QLabel(QString::fromUtf8("¥%1/度  ·  %2 km  ·  快%3 慢%4  ·  ★%5")
                                    .arg(s.value("pricePerKwh").toDouble(), 0, 'f', 2)
                                    .arg(s.value("distanceKm").toDouble(), 0, 'f', 1)
                                    .arg(s.value("fastPiles").toInt())
                                    .arg(s.value("slowPiles").toInt())
                                    .arg(s.value("score").toDouble(), 0, 'f', 1));
        meta->setObjectName("muted");
        meta->setWordWrap(true);
        cl->addWidget(meta);
        auto *btns = new QHBoxLayout;
        btns->setSpacing(8);
        auto *nav = new QPushButton(u8("导航"));
        nav->setObjectName("ghost");
        auto *go = new QPushButton(u8("选桩充电"));
        connect(nav, &QPushButton::clicked, this, [this, s] { openNav(s); });
        connect(go, &QPushButton::clicked, this, [this, s] {
            currentStation_ = s;
            controller_.request("QUERY_PILES", QJsonObject{{"stationId", s.value("id").toInt()}});
        });
        btns->addWidget(nav, 1);
        btns->addWidget(go, 2);
        cl->addLayout(btns);
        stationBox_->addWidget(c);
    }
    stationBox_->addStretch();
}

/** 画某站桩列表。 */
void UserWindow::renderPiles(const QJsonObject &data)
{
    lastPiles_ = data;
    currentStation_ = data.value("station").toObject();
    pileTitle_->setText(currentStation_.value("name").toString());
    pileMeta_->setText(QString::fromUtf8("%1  ·  %2 元/度\n点击电桩右侧「评价」，进入该桩独立评价页")
                           .arg(currentStation_.value("address").toString())
                           .arg(currentStation_.value("pricePerKwh").toDouble(), 0, 'f', 2));
    clearBox(pileBox_);
    const auto piles = data.value("piles").toArray();
    const QString want = pileType_ && pileType_->currentIndex() > 0 ? pileType_->currentText() : QString();
    int shown = 0;
    if (piles.isEmpty())
        pileBox_->addWidget(new QLabel(u8("该站暂无电桩数据")));
    for (const auto &v : piles) {
        const auto p = v.toObject();
        if (!want.isEmpty() && p.value("type").toString() != want)
            continue;
        ++shown;
        auto *c = card();
        auto *cl = new QVBoxLayout(c);
        cl->setContentsMargins(14, 12, 14, 12);
        cl->setSpacing(8);
        const QString st = p.value("status").toString();
        auto *head = new QHBoxLayout;
        auto *info = new QLabel(QString("%1  ·  %2  %3 kW").arg(p.value("pileNo").toString(), p.value("type").toString())
                                    .arg(p.value("powerKw").toDouble(), 0, 'f', 0));
        info->setObjectName("cardTitle");
        auto *stPill = new QLabel(st);
        if (st == u8("闲置"))
            stPill->setObjectName("pillOk");
        else if (st == u8("故障"))
            stPill->setObjectName("pillOff");
        else
            stPill->setObjectName("pillWarn");
        head->addWidget(info, 1);
        head->addWidget(stPill, 0, Qt::AlignRight);
        cl->addLayout(head);
        const auto pileRevs = p.value("reviews").toArray();
        if (!pileRevs.isEmpty()) {
            const auto latest = pileRevs.at(0).toObject();
            auto *revLab = new QLabel(QString::fromUtf8("%1  ★%2  %3")
                                          .arg(latest.value("nickname").toString())
                                          .arg(latest.value("score").toInt())
                                          .arg(latest.value("comment").toString()));
            revLab->setObjectName("muted");
            revLab->setWordWrap(true);
            cl->addWidget(revLab);
        }
        auto *rsv = new QPushButton(p.value("reservedByMe").toBool() ? u8("取消预约") : u8("预约"));
        rsv->setObjectName("ghost");
        auto *go = new QPushButton(u8("开始充电"));
        const bool idle = st == u8("闲置") || p.value("reservedByMe").toBool();
        rsv->setEnabled(idle && (st == u8("闲置") || p.value("reservedByMe").toBool()));
        go->setEnabled(st == u8("闲置") || p.value("reservedByMe").toBool());
        if (st == u8("故障") || st == u8("在用") || st == u8("已预约")) {
            rsv->setEnabled(p.value("reservedByMe").toBool());
            go->setEnabled(p.value("reservedByMe").toBool());
        }
        const int pid = p.value("id").toInt();
        connect(rsv, &QPushButton::clicked, this, [this, p, pid] {
            if (p.value("reservedByMe").toBool())
                controller_.request("CANCEL_RESERVE");
            else
                doReserve(pid);
        });
        connect(go, &QPushButton::clicked, this, [this, pid] { tryStart(pid); });
        auto *rev = new QPushButton(u8("评价"));
        rev->setObjectName("ghost");
        connect(rev, &QPushButton::clicked, this, [this, p] { openPileReview(p); });
        auto *btns = new QHBoxLayout;
        btns->setSpacing(8);
        btns->addWidget(rsv, 1);
        btns->addWidget(go, 2);
        btns->addWidget(rev, 1);
        cl->addLayout(btns);
        pileBox_->addWidget(c);
    }
    if (shown == 0 && !piles.isEmpty())
        pileBox_->addWidget(new QLabel(u8("当前筛选下暂无电桩，可切换快充/慢充")));
    pileBox_->addStretch();
    pages_->setCurrentIndex(1);
}

/** 画订单卡片。 */
void UserWindow::renderOrders(const QJsonArray &arr)
{
    if (ordersPage_)
        ordersPage_->setOrders(arr);
}

/** 画充值流水。 */
void UserWindow::renderRecharge(const QJsonArray &arr)
{
    if (!rechargeBox_)
        return;
    clearBox(rechargeBox_);
    if (arr.isEmpty()) {
        auto *lab = new QLabel(u8("暂无充值记录"));
        lab->setObjectName("muted");
        rechargeBox_->addWidget(lab);
        rechargeBox_->addStretch();
        return;
    }
    for (const auto &v : arr) {
        const auto r = v.toObject();
        auto *c = card();
        auto *cl = new QVBoxLayout(c);
        cl->setContentsMargins(12, 8, 12, 8);
        cl->addWidget(new QLabel(QString::fromUtf8("¥ %1    %2")
                                     .arg(r.value("amount").toDouble(), 0, 'f', 2)
                                     .arg(r.value("result").toString())));
        auto *m = new QLabel(r.value("tradeNo").toString() + "  " + r.value("createdAt").toString());
        m->setObjectName("muted");
        cl->addWidget(m);
        rechargeBox_->addWidget(c);
    }
    rechargeBox_->addStretch();
}

/** 刷新充电页：时长、电量、费用、停充/结算按钮。 */
void UserWindow::showCharge(const QJsonObject &order)
{
    currentOrder_ = order;
    const QString st = order.value("status").toString();
    if (chargePage_)
        chargePage_->setOrder(order);
    if (st == u8("充电中") || st == u8("待结算")) {
        pages_->setCurrentIndex(2);
        highlightTab(2);
    }
}

/** 先 CHARGE_STATUS，有未完成单则提示结算，否则再开充。 */
/** 先 CHARGE_STATUS，没有未完成单再 START_CHARGE。 */
void UserWindow::tryStart(int pileId)
{
    controller_.beginCharge(pileId);
}

/** 发 RESERVE_PILE。 */
void UserWindow::doReserve(int pileId)
{
    controller_.request("RESERVE_PILE", QJsonObject{{"pileId", pileId}});
}

/** 切到评价页并拉该桩评论。 */
void UserWindow::openPileReview(const QJsonObject &pile)
{
    currentPile_ = pile;
    const int pileId = currentPile_.value("id").toInt();
    if (pileId <= 0)
        return;
    if (reviewBody_)
        reviewBody_->clear();
    setReviewStars(5);
    if (reviewCount_)
        reviewCount_->setText(u8("0 / 300"));
    switchTab(5);
    controller_.request("LIST_PILE_REVIEWS", QJsonObject{{"pileId", pileId}});
}

/** 点亮 1~n 颗星。 */
void UserWindow::setReviewStars(int n)
{
    reviewStars_ = qBound(1, n, 5);
    for (int i = 0; i < reviewStarBtns_.size(); ++i) {
        auto *b = reviewStarBtns_[i];
        b->setObjectName(i < reviewStars_ ? "starOn" : "star");
        b->style()->unpolish(b);
        b->style()->polish(b);
        b->update();
    }
}

/** 画均分、历史评论、情感摘要。 */
void UserWindow::renderPileReview(const QJsonObject &data)
{
    const auto pile = data.value("pile").toObject();
    const auto station = data.value("station").toObject();
    const auto nlp = data.value("nlp").toObject();
    currentPile_ = pile;
    if (!station.isEmpty())
        currentStation_ = station;

    const QString pileNo = pile.value("pileNo").toString();
    rvTitle_->setText(pileNo + u8("  ·  ") + station.value("name").toString());
    const int count = nlp.value("count").toInt();
    const double avg = nlp.value("avgScore").toDouble();
    rvStars_->setText(goldStars(qRound(avg)) + u8("  ") + QString::number(avg, 'f', 1)
                      + u8("    ") + QString::number(count) + u8(" 人评价"));
    rvMeta_->setText(QString::fromUtf8("桩号 %1    类型 %2    功率 %3 kW\n地址 %4    电价 ¥%5 / 度")
                         .arg(pileNo, pile.value("type").toString())
                         .arg(pile.value("powerKw").toDouble(), 0, 'f', 0)
                         .arg(station.value("address").toString())
                         .arg(station.value("pricePerKwh").toDouble(), 0, 'f', 2));
    QStringList kws;
    for (const auto &v : nlp.value("keywords").toArray())
        kws.append(v.toObject().value("word").toString());
    if (count <= 0)
        rvNlp_->setText(u8("暂无评价文档，欢迎写下第一条体验"));
    else
        rvNlp_->setText(QString::fromUtf8("情感  正面 %1  ·  中性 %2  ·  负面 %3%4")
                            .arg(nlp.value("positive").toInt())
                            .arg(nlp.value("neutral").toInt())
                            .arg(nlp.value("negative").toInt())
                            .arg(kws.isEmpty() ? QString() : (u8("    关键词  ") + kws.join(u8("、")))));

    const auto mine = data.value("mine").toObject();
    if (!mine.isEmpty() && mine.value("score").toInt() > 0) {
        setReviewStars(mine.value("score").toInt());
        if (reviewBody_)
            reviewBody_->setPlainText(mine.value("comment").toString());
    } else if (reviewBody_ && reviewBody_->toPlainText().trimmed().isEmpty()) {
        setReviewStars(5);
    }

    clearBox(reviewListBox_);
    const auto reviews = data.value("reviews").toArray();
    if (reviews.isEmpty()) {
        auto *empty = new QLabel(u8("还没有人评价这根桩"));
        empty->setObjectName("muted");
        reviewListBox_->addWidget(empty);
    }
    for (const auto &v : reviews) {
        const auto r = v.toObject();
        auto *c = card();
        auto *cl = new QVBoxLayout(c);
        cl->setContentsMargins(14, 12, 14, 12);
        cl->setSpacing(6);
        auto *head = new QHBoxLayout;
        const QString nick = r.value("nickname").toString();
        auto *av = new QLabel(nick.isEmpty() ? u8("评") : nick.left(1));
        av->setObjectName("avatarDot");
        av->setFixedSize(36, 36);
        av->setAlignment(Qt::AlignCenter);
        auto *who = new QLabel(nick + (r.value("mine").toBool() ? u8("（我）") : QString()));
        who->setObjectName("cardTitle");
        auto *when = new QLabel(r.value("createdAt").toString());
        when->setObjectName("muted");
        auto *sent = new QLabel(r.value("sentiment").toString());
        const QString st = r.value("sentiment").toString();
        if (st == u8("正面"))
            sent->setObjectName("pillOk");
        else if (st == u8("负面"))
            sent->setObjectName("pillOff");
        else
            sent->setObjectName("pillWarn");
        head->addWidget(av);
        auto *whoCol = new QVBoxLayout;
        whoCol->setSpacing(2);
        whoCol->addWidget(who);
        whoCol->addWidget(when);
        head->addLayout(whoCol, 1);
        head->addWidget(sent, 0, Qt::AlignTop);
        cl->addLayout(head);
        auto *stars = new QLabel(goldStars(r.value("score").toInt()));
        stars->setObjectName("reviewAvg");
        cl->addWidget(stars);
        auto *body = new QLabel(r.value("comment").toString());
        body->setWordWrap(true);
        cl->addWidget(body);
        reviewListBox_->addWidget(c);
    }
    reviewListBox_->addStretch();
}

/** 文字不能空，发 REVIEW_STATION。 */
void UserWindow::submitReview()
{
    const int pileId = currentPile_.value("id").toInt();
    if (pileId <= 0) {
        uiWarn(this, u8("无法评价"), u8("请从电桩列表进入对应桩的评价页"));
        return;
    }
    const QString comment = reviewBody_ ? reviewBody_->toPlainText().trimmed() : QString();
    if (comment.size() < 2 || comment.size() > 300) {
        uiWarn(this, u8("评语不完整"), u8("请写下 2~300 字的充电体验，不能只打分"));
        return;
    }
    controller_.request("REVIEW_STATION",
                        QJsonObject{{"stationId", currentStation_.value("id").toInt()},
                                    {"pileId", pileId},
                                    {"score", reviewStars_},
                                    {"comment", comment}});
}

/** 用系统浏览器打开导航。 */
void UserWindow::openNav(const QJsonObject &station)
{
    showStationLocation(station);
}

/** 地图弹窗：静态图 + 天气 + 路线，不经 Dispatch。 */
void UserWindow::showStationLocation(const QJsonObject &station)
{
    const double lat = station.value("lat").toDouble();
    const double lng = station.value("lng").toDouble();
    if (!validCoordinate(lat, lng)) {
        uiWarn(this, u8("位置不可用"), u8("该充电站暂时没有有效的经纬度信息。"));
        return;
    }

    UiSheet dialog(this, station.value("name").toString(),
                   QString::fromUtf8("%1\n坐标 %2, %3")
                       .arg(station.value("address").toString())
                       .arg(lat, 0, 'f', 6)
                       .arg(lng, 0, 'f', 6));
    dialog.setWindowTitle(u8("充电站位置与导航"));
    dialog.polish(400, 680);

    auto *map = new QLabel(u8("正在加载地图…"));
    map->setAlignment(Qt::AlignCenter);
    map->setMinimumSize(340, 220);
    map->setStyleSheet(QStringLiteral(
        "background:#F8FAFC;border:1px solid #E2E8F0;border-radius:12px;color:#64748B;font-size:14px;"));
    map->setScaledContents(false);
    dialog.body()->addWidget(map, 1);

    auto *routeBar = new QHBoxLayout;
    routeBar->setSpacing(10);
    routeBar->addWidget(uiFieldLabel(u8("出行方式")));
    auto *mode = new QComboBox;
    mode->addItem(u8("驾车"), QStringLiteral("drive"));
    mode->addItem(u8("步行"), QStringLiteral("walk"));
    mode->addItem(u8("骑行"), QStringLiteral("bike"));
    mode->addItem(u8("公交"), QStringLiteral("bus"));
    mode->setMinimumWidth(140);
    prepCombo(mode);
    routeBar->addWidget(mode);
    auto *route = new QPushButton(u8("查询路线"));
    routeBar->addWidget(route);
    auto *open = new QPushButton(u8("开始导航"));
    routeBar->addWidget(open);
    auto *osm = new QPushButton(u8("网页路线"));
    osm->setObjectName("ghost");
    routeBar->addWidget(osm);
    routeBar->addStretch();
    dialog.body()->addLayout(routeBar);

    auto *originHint = new QLabel;
    originHint->setObjectName("uiSheetHint");
    originHint->setWordWrap(true);
    const QJsonObject originNow = coord();
    if (useGps_) {
        originHint->setText(u8("导航起点：您的位置 %1（%2, %3） → 本站")
                                .arg(gpsPlace_.isEmpty() ? u8("网络定位") : gpsPlace_)
                                .arg(originNow.value("lat").toDouble(), 0, 'f', 5)
                                .arg(originNow.value("lng").toDouble(), 0, 'f', 5));
    } else {
        originHint->setText(u8("尚未授权定位。将用当前参考点出发；建议先点「使用我的位置」，再按实际位置导航。"));
    }
    dialog.body()->addWidget(originHint);

    auto *weather = new QLabel(u8("正在查询当地天气…"));
    weather->setObjectName("uiSheetHint");
    weather->setWordWrap(true);
    dialog.body()->addWidget(weather);

    auto *routeResult = new QLabel(u8("选择出行方式后可查询距离和预计时间。"));
    routeResult->setObjectName("uiSheetHint");
    routeResult->setWordWrap(true);
    dialog.body()->addWidget(routeResult);
    dialog.addClose();

    const QJsonObject stationCopy = station;
    const QPointer<QLabel> mapGuard(map);
    const QPointer<QLabel> weatherGuard(weather);
    if (mapNetwork_) {
        auto *fb = mapNetwork_->get(TencentApi::request(TencentApi::fallbackMapUrl(lat, lng)));
        connect(fb, &QNetworkReply::finished, this, [fb, mapGuard] {
            if (mapGuard) {
                QPixmap pix;
                if (fb->error() == QNetworkReply::NoError && pix.loadFromData(fb->readAll())) {
                    mapGuard->setPixmap(pix.scaled(mapGuard->size(), Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation));
                    mapGuard->setText(QString());
                } else {
                    mapGuard->setText(u8("地图图片暂时无法加载，可点击“开始导航”打开路线。"));
                }
            }
            fb->deleteLater();
        });
        auto applyWx = [weatherGuard](const TencentApi::Weather &wx) {
            if (!weatherGuard)
                return;
            if (wx.ok)
                weatherGuard->setText(u8("当地天气：") + wx.detail + u8("（") + wx.source + u8("）"));
            else
                weatherGuard->setText(u8("天气暂不可用，仍可查看位置和路线。"));
        };
        auto *wreply = mapNetwork_->get(TencentApi::request(TencentApi::weatherNowUrl(lat, lng)));
        connect(wreply, &QNetworkReply::finished, this, [this, wreply, lat, lng, applyWx] {
            const TencentApi::Weather wx =
                TencentApi::parseWeather(QJsonDocument::fromJson(wreply->readAll()).object());
            wreply->deleteLater();
            if (wx.ok) {
                applyWx(wx);
                return;
            }
            auto *fb = mapNetwork_->get(TencentApi::request(TencentApi::openMeteoUrl(lat, lng)));
            connect(fb, &QNetworkReply::finished, this, [fb, applyWx] {
                applyWx(TencentApi::parseOpenMeteo(QJsonDocument::fromJson(fb->readAll()).object()));
                fb->deleteLater();
            });
        });
    }

    connect(route, &QPushButton::clicked, this, [this, stationCopy, mode, routeResult, route] {
        queryTencentRoute(stationCopy, mode->currentData().toString(), routeResult, route);
    });
    auto startNav = [this, stationCopy, mode](bool useOsm) {
        const QJsonObject origin = coord();
        const double fromLat = origin.value("lat").toDouble();
        const double fromLng = origin.value("lng").toDouble();
        const double toLat = stationCopy.value("lat").toDouble();
        const double toLng = stationCopy.value("lng").toDouble();
        if (!validCoordinate(fromLat, fromLng) || !validCoordinate(toLat, toLng)) {
            uiWarn(this, u8("无法导航"), u8("起点或充电站坐标无效。请先同意定位或填写住址。"));
            return;
        }
        const QString m = mode->currentData().toString();
        if (useOsm)
            openExternalUrl(osmDirectionsUrl(stationCopy, origin, m));
        else
            openExternalUrl(amapNavigationUrl(stationCopy, origin, m));
    };
    connect(open, &QPushButton::clicked, this, [startNav] { startNav(false); });
    connect(osm, &QPushButton::clicked, this, [startNav] { startNav(true); });
    queryTencentRoute(stationCopy, mode->currentData().toString(), routeResult, route);

    dialog.exec();
}

/** 按用户当前位置到电站查询路况时间，优先 OSRM。 */
void UserWindow::queryTencentRoute(const QJsonObject &station, const QString &mode,
                                   QLabel *resultLabel, QPushButton *queryButton)
{
    if (!resultLabel || !mapNetwork_)
        return;
    const QJsonObject origin = coord();
    const double fromLat = origin.value("lat").toDouble();
    const double fromLng = origin.value("lng").toDouble();
    const double toLat = station.value("lat").toDouble();
    const double toLng = station.value("lng").toDouble();
    if (!validCoordinate(fromLat, fromLng) || !validCoordinate(toLat, toLng)) {
        resultLabel->setText(u8("当前位置或充电站坐标无效，无法规划路线。"));
        return;
    }
    if (queryButton)
        queryButton->setEnabled(false);
    resultLabel->setText(useGps_ ? u8("正在按您的位置规划到本站的路线…")
                                 : u8("正在按当前参考点规划路线…"));

    const QPointer<QLabel> resultGuard(resultLabel);
    const QPointer<QPushButton> buttonGuard(queryButton);
    const bool fromGps = useGps_;
    auto *fb = mapNetwork_->get(
        TencentApi::request(TencentApi::osrmUrl(mode, fromLat, fromLng, toLat, toLng)));
    connect(fb, &QNetworkReply::finished, this, [fb, mode, resultGuard, buttonGuard, fromGps] {
        double meters = 0;
        int seconds = 0;
        const bool ok = TencentApi::parseOsrm(QJsonDocument::fromJson(fb->readAll()).object(),
                                              &meters, &seconds);
        if (resultGuard) {
            if (ok)
                resultGuard->setText((fromGps ? u8("从您的位置%1：%2，路程约 %3 km")
                                              : u8("从当前参考点%1：%2，路程约 %3 km"))
                                         .arg(routeModeName(mode), durationText(seconds))
                                         .arg(meters / 1000.0, 0, 'f', 1));
            else
                resultGuard->setText(u8("路线估算失败，仍可点「开始导航」打开高德或网页路线。"));
        }
        if (buttonGuard)
            buttonGuard->setEnabled(true);
        fb->deleteLater();
    });
}

/** 失败弹提示；PUSH_CHARGE / CHARGE_STATUS 刷新充电页，不改登录态。 */
/** 所有回包和 PUSH_CHARGE 的总入口：失败弹窗，成功按 type 刷新对应页。 */
void UserWindow::onResp(QJsonObject obj)
{
    const QString type = obj.value("type").toString();
    const int code = obj.value("code").toInt();
    if (code != 0) {
        const QString msg = obj.value("message").toString();
        loginPage_->setStatus(msg.isEmpty() ? u8("请求失败") : msg);
        uiWarn(this, u8("提示"), msg.isEmpty() ? (type + u8(" 失败")) : msg);
        // 只有登录/注册失败才停在登录页；进首页后的接口失败不得把人踢回去
        if (type == "LOGIN" || type == "REGISTER") {
            controller_.signOut();
            root_->setCurrentIndex(0);
        }
        return;
    }
    const QJsonObject data = obj.value("data").toObject();
    if (type == "LOGIN" || type == "REGISTER") {
        showShell();
        applyUser(controller_.user());
    } else if (type == "CLOSE_ACCOUNT") {
        locLat_ = 39.9644;
        locLng_ = 116.3473;
        useGps_ = false;
        gpsPlace_.clear();
        if (addrEdit_)
            addrEdit_->clear();
        uiInfo(this, u8("账号已注销"),
               obj.value("message").toString(u8("账号已禁用留档，历史记录可追溯。")));
        loginPage_->setStatus(u8("账号已注销留档，同一手机号不能再注册"));
        root_->setCurrentIndex(0);
    } else if (type == "QUERY_STATIONS") {
        renderStations(data);
    } else if (type == "QUERY_PILES") {
        renderPiles(data);
    } else if (type == "RESERVE_PILE" || type == "CANCEL_RESERVE") {
        uiInfo(this, u8("ChargeHub"), obj.value("message").toString());
        if (pages_->currentIndex() == 6)
            controller_.request("LIST_RESERVATIONS");
        else if (currentStation_.value("id").toInt() > 0)
            controller_.request("QUERY_PILES", QJsonObject{{"stationId", currentStation_.value("id").toInt()}});
        else
            queryStations();
    } else if (type == "REVIEW_STATION") {
        uiInfo(this, u8("ChargeHub"), obj.value("message").toString());
        const int pileId = currentPile_.value("id").toInt();
        if (pages_->currentIndex() == 5 && pileId > 0)
            controller_.request("LIST_PILE_REVIEWS", QJsonObject{{"pileId", pileId}});
        else if (currentStation_.value("id").toInt() > 0)
            controller_.request("QUERY_PILES", QJsonObject{{"stationId", currentStation_.value("id").toInt()}});
        else
            queryStations();
    } else if (type == "LIST_PILE_REVIEWS") {
        renderPileReview(data);
    } else if (type == "PUSH_CHARGE") {
        const QJsonObject order = data.value("order").toObject();
        if (!order.isEmpty() && order.value("id").toInt() > 0)
            showCharge(order);
    } else if (type == "CHARGE_STATUS") {
        const QJsonObject order = data.value("order").toObject();
        if (!order.isEmpty() && order.value("id").toInt() > 0) {
            showCharge(order);
        } else if (pages_->currentIndex() == 2) {
            currentOrder_ = {};
            if (chargePage_)
                chargePage_->clearOrder();
        }
    } else if (type == "START_CHARGE" || type == "STOP_CHARGE") {
        showCharge(data.value("order").toObject());
    } else if (type == "SETTLE_ORDER") {
        applyUser(controller_.user());
        const auto o = data.value("order").toObject();
        uiInfo(this, u8("结算成功"),
               u8("订单 %1\n电量 %2 kWh\n费用 ¥%3\n余额 ¥%4")
                   .arg(o.value("orderNo").toString())
                   .arg(o.value("energyKwh").toDouble(), 0, 'f', 3)
                   .arg(o.value("amount").toDouble(), 0, 'f', 2)
                   .arg(controller_.user().value("balance").toDouble(), 0, 'f', 2));
        currentOrder_ = {};
        switchTab(3);
    } else if (type == "LIST_ORDERS") {
        renderOrders(data.value("orders").toArray());
    } else if (type == "LIST_RESERVATIONS") {
        if (reservationsPage_)
            reservationsPage_->setReservations(data.value("reservations").toArray());
    } else if (type == "RECHARGE") {
        uiInfo(this, u8("充值成功"),
               u8("流水号 %1\n金额 ¥%2")
                   .arg(data.value("tradeNo").toString())
                   .arg(data.value("amount").toDouble(), 0, 'f', 2));
        applyUser(controller_.user());
    } else if (type == "UPDATE_PROFILE") {
        applyUser(controller_.user());
        uiInfo(this, u8("ChargeHub"), obj.value("message").toString());
    } else if (type == "LIST_RECHARGE") {
        renderRecharge(data.value("records").toArray());
    }
}

double UserWindow::haversine(double lat1, double lng1, double lat2, double lng2)
{
    const double r = 6371.0;
    const double p1 = qDegreesToRadians(lat1);
    const double p2 = qDegreesToRadians(lat2);
    const double dphi = qDegreesToRadians(lat2 - lat1);
    const double dl = qDegreesToRadians(lng2 - lng1);
    const double a = qSin(dphi / 2) * qSin(dphi / 2)
        + qCos(p1) * qCos(p2) * qSin(dl / 2) * qSin(dl / 2);
    return r * 2 * qAtan2(qSqrt(a), qSqrt(1 - a));
}
