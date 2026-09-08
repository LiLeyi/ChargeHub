/**
 * @file userwindow.cpp
 * @brief 用户端页面。按钮 → Client::request；回包 → onResp。不打开数据库。
 *
 * 页：登录 / 找站 / 桩列表 / 充电 / 订单 / 评价 / 预约 / 我的。
 * 充电刷新：UserController 负责轮询，本窗口负责渲染 PUSH_CHARGE / CHARGE_STATUS。
 */
#include "userwindow.h"
#include "uidialog.h"
#include "pages/chargepage.h"
#include "pages/loginpage.h"
#include "pages/orderspage.h"
#include "pages/reservationspage.h"

#if __has_include("tencentmap_credentials.h")
#include "tencentmap_credentials.h"
#endif

#include <algorithm>
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
#include <QListWidget>
#include <QComboBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QSizePolicy>
#include <QStatusBar>
#include <QStringList>
#include <QStyle>
#include <QTextCursor>
#include <QUrl>
#include <QUrlQuery>
#include <QSettings>
#include <QCryptographicHash>

static QString u8(const char *s) { return QString::fromUtf8(s); }

#ifndef CHARGEHUB_TENCENT_MAP_KEY
// 腾讯位置服务的 key 可以放在 tencentmap_credentials.h 或环境变量中覆盖。
#define CHARGEHUB_TENCENT_MAP_KEY "2L2BZ-7WE6C-ZFP2W-A23GC-5WZK2-KHBGF"
#endif

#ifndef CHARGEHUB_TENCENT_MAP_SK
#define CHARGEHUB_TENCENT_MAP_SK ""
#endif

namespace {

/** 腾讯地图 key：环境变量优先，否则用编译期宏。 */
QString tencentMapKey()
{
    const QString env = qEnvironmentVariable("CHARGEHUB_TENCENT_MAP_KEY").trimmed();
    return env.isEmpty() ? QString::fromLatin1(CHARGEHUB_TENCENT_MAP_KEY) : env;
}

/** 腾讯地图 SK，签路线请求用；可空。 */
QString tencentMapSk()
{
    const QString env = qEnvironmentVariable("CHARGEHUB_TENCENT_MAP_SK").trimmed();
    return env.isEmpty() ? QString::fromLatin1(CHARGEHUB_TENCENT_MAP_SK) : env;
}

/** 拼成 "lat,lng" 给腾讯 API。 */
QString coordinateText(double lat, double lng)
{
    return QString::number(lat, 'f', 6) + "," + QString::number(lng, 'f', 6);
}

/** 把键值对编成 query string。 */
QString encodedQuery(const QList<QPair<QString, QString>> &params)
{
    QList<QPair<QString, QString>> sorted = params;
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        if (a.first == b.first)
            return a.second < b.second;
        return a.first < b.first;
    });

    QStringList parts;
    parts.reserve(sorted.size());
    for (const auto &p : sorted) {
        parts.append(QString::fromLatin1(QUrl::toPercentEncoding(p.first)) + "="
                     + QString::fromLatin1(QUrl::toPercentEncoding(p.second)));
    }
    return parts.join('&');
}

QUrl tencentApiUrl(const QString &path, QList<QPair<QString, QString>> params)
{
    params.append({QStringLiteral("key"), tencentMapKey()});
    const QString query = encodedQuery(params);
    QString full = QStringLiteral("https://apis.map.qq.com") + path + "?" + query;
    const QString sk = tencentMapSk();
    if (!sk.isEmpty()) {
        const QByteArray source = (path + "?" + query + sk).toUtf8();
        const QString sig = QString::fromLatin1(QCryptographicHash::hash(source, QCryptographicHash::Md5).toHex());
        full += "&sig=" + sig;
    }
    return QUrl(full);
}

QUrl tencentStaticMapUrl(const QJsonObject &station)
{
    const double lat = station.value("lat").toDouble();
    const double lng = station.value("lng").toDouble();
    const QString marker = QStringLiteral("size:large|color:red|label:S|%1,%2")
                               .arg(QString::number(lat, 'f', 6), QString::number(lng, 'f', 6));
    return tencentApiUrl(QStringLiteral("/ws/staticmap/v2/"),
                         {{QStringLiteral("center"), coordinateText(lat, lng)},
                          {QStringLiteral("zoom"), QStringLiteral("16")},
                          {QStringLiteral("size"), QStringLiteral("640*360")},
                          {QStringLiteral("scale"), QStringLiteral("2")},
                          {QStringLiteral("markers"), marker}});
}

QUrl tencentRouteUri(const QJsonObject &station, const QJsonObject &origin, const QString &mode)
{
    QUrl url(QStringLiteral("https://apis.map.qq.com/uri/v1/routeplan/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("type"), mode);
    query.addQueryItem(QStringLiteral("from"), u8("我的位置"));
    query.addQueryItem(QStringLiteral("fromcoord"),
                       coordinateText(origin.value("lat").toDouble(), origin.value("lng").toDouble()));
    query.addQueryItem(QStringLiteral("to"), station.value("name").toString());
    query.addQueryItem(QStringLiteral("tocoord"),
                       coordinateText(station.value("lat").toDouble(), station.value("lng").toDouble()));
    query.addQueryItem(QStringLiteral("referer"), QStringLiteral("ChargeHub"));
    url.setQuery(query);
    return url;
}

/** 腾讯路线 API 路径：步行/骑行/公交/驾车。 */
QString routeApiPath(const QString &mode)
{
    if (mode == QStringLiteral("walk"))
        return QStringLiteral("/ws/direction/v1/walking/");
    if (mode == QStringLiteral("bike"))
        return QStringLiteral("/ws/direction/v1/bicycling/");
    if (mode == QStringLiteral("bus"))
        return QStringLiteral("/ws/direction/v1/transit/");
    return QStringLiteral("/ws/direction/v1/driving/");
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

QNetworkRequest mapRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("ChargeHub/1.0"));
    return request;
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
    c->setMinimumHeight(36);
    c->setFocusPolicy(Qt::StrongFocus);
    c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    c->setMinimumContentsLength(4);
    auto *view = new QListView(c);
    view->setUniformItemSizes(true);
    view->setMinimumHeight(36);
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
    s->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    s->setWidget(inner);
    return s;
}

UserWindow::UserWindow(QWidget *parent) : QMainWindow(parent)
{
    setObjectName("desktop");
    setWindowTitle(u8("ChargeHub 用户端"));
    setMinimumSize(1080, 680);
    resize(1180, 760);
    root_ = new QStackedWidget;
    setCentralWidget(root_);
    mapNetwork_ = new QNetworkAccessManager(this);
    root_->addWidget(buildLogin());
    root_->addWidget(buildShell());

    connect(&controller_, &UserController::connected, this, [this] {
        loginPage_->setStatus(u8("已连接运营平台"));
        statusBar()->showMessage(u8("已连接  ") + loginPage_->serverHost() + ":"
                                 + QString::number(loginPage_->serverPort()));
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
    statusBar()->showMessage(u8("未连接服务器"));
    reconnect();
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

/** 主壳：导航 + 顶栏 + pages_。 */
QWidget *UserWindow::buildShell()
{
    auto *w = new QWidget;
    auto *lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto *side = new QFrame;
    side->setObjectName("sidebar");
    side->setFixedWidth(220);
    auto *sl = new QVBoxLayout(side);
    sl->setContentsMargins(16, 22, 16, 16);
    sl->setSpacing(4);
    auto *mark = new QLabel(QStringLiteral("CH"));
    mark->setObjectName("logoMark");
    mark->setFixedSize(36, 36);
    mark->setAlignment(Qt::AlignCenter);
    auto *brand = new QLabel(u8("ChargeHub"));
    brand->setObjectName("sideBrand");
    auto *sub = new QLabel(u8("充电用户端"));
    sub->setObjectName("sideSub");
    sl->addWidget(mark);
    sl->addSpacing(10);
    sl->addWidget(brand);
    sl->addWidget(sub);
    sl->addSpacing(18);
    nav_ = new QListWidget;
    nav_->setObjectName("nav");
    nav_->addItems({u8("附近电站"), u8("已预约"), u8("实时充电"), u8("我的订单"), u8("个人中心")});
    nav_->setCurrentRow(0);
    nav_->setFocusPolicy(Qt::NoFocus);
    nav_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    connect(nav_, &QListWidget::currentRowChanged, this, [this](int row) {
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
    sl->addWidget(nav_, 1);

    auto *content = new QWidget;
    auto *cl = new QVBoxLayout(content);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);
    auto *top = new QFrame;
    top->setObjectName("topbar");
    auto *tl = new QHBoxLayout(top);
    tl->setContentsMargins(24, 14, 24, 14);
    auto *titles = new QVBoxLayout;
    titles->setContentsMargins(0, 0, 0, 0);
    titles->setSpacing(2);
    pageTitle_ = new QLabel(u8("附近电站"));
    pageTitle_->setObjectName("pageTitle");
    pageSub_ = new QLabel(u8("按距离查看可用充电站"));
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
    cl->addWidget(top);
    cl->addWidget(pages_, 1);
    lay->addWidget(side);
    lay->addWidget(content, 1);
    return w;
}

/** 切业务页；订单/预约会再拉一次列表。 */
void UserWindow::switchTab(int i)
{
    pages_->setCurrentIndex(i);
    if (nav_) {
        nav_->blockSignals(true);
        if (i == 0 || i == 1 || i == 5)
            nav_->setCurrentRow(0);
        else if (i == 6)
            nav_->setCurrentRow(1);
        else if (i == 2)
            nav_->setCurrentRow(2);
        else if (i == 3)
            nav_->setCurrentRow(3);
        else if (i == 4)
            nav_->setCurrentRow(4);
        nav_->blockSignals(false);
    }
    if (pageTitle_ && pageSub_) {
        if (i == 0) {
            pageTitle_->setText(u8("附近电站"));
            pageSub_->setText(u8("填写住址，系统匹配最近电站和电桩"));
        } else if (i == 1) {
            pageTitle_->setText(u8("选择电桩"));
            pageSub_->setText(u8("预约占桩 15 分钟，或直接开始充电"));
        } else if (i == 2) {
            pageTitle_->setText(u8("实时充电"));
            pageSub_->setText(u8("进行中的订单、结束充电与结算"));
        } else if (i == 3) {
            pageTitle_->setText(u8("我的订单"));
            pageSub_->setText(u8("充电记录与待结算订单"));
        } else if (i == 6) {
            pageTitle_->setText(u8("已预约"));
            pageSub_->setText(u8("集中查看尚未到期的充电桩预约"));
        } else if (i == 5) {
            pageTitle_->setText(u8("电桩评价"));
            pageSub_->setText(u8("查看与撰写这根桩的充电体验"));
        } else {
            pageTitle_->setText(u8("个人中心"));
            pageSub_->setText(u8("资料、钱包与充值记录"));
        }
    }
    if (i == 0) {
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
    lay->setContentsMargins(24, 18, 24, 16);
    lay->setSpacing(14);
    auto *bar = new QFrame;
    bar->setObjectName("toolbar");
    auto *locRow = new QHBoxLayout(bar);
    locRow->setContentsMargins(14, 12, 14, 12);
    locRow->setSpacing(10);
    auto *loc = new QLabel(u8("我的位置"));
    loc->setObjectName("muted");
    addrEdit_ = new QLineEdit;
    addrEdit_->setPlaceholderText(u8("输入住址或地标，例如：中关村南大街5号"));
    addrEdit_->setMinimumWidth(220);
    auto *find = new QPushButton(u8("查找附近"));
    connect(find, &QPushButton::clicked, this, &UserWindow::queryStations);
    connect(addrEdit_, &QLineEdit::returnPressed, this, &UserWindow::queryStations);
    auto *radLab = new QLabel(u8("半径"));
    radLab->setObjectName("muted");
    radius_ = new QComboBox;
    radius_->addItems({u8("3 km"), u8("5 km"), u8("10 km"), u8("20 km")});
    radius_->setCurrentIndex(3);
    radius_->setMinimumWidth(110);
    radius_->setMaximumWidth(140);
    prepCombo(radius_);
    connect(radius_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { queryStations(); });
    locRow->addWidget(loc);
    locRow->addWidget(addrEdit_, 1);
    locRow->addWidget(find);
    locRow->addSpacing(8);
    locRow->addWidget(radLab);
    locRow->addWidget(radius_);
    lay->addWidget(bar);
    auto *chips = new QHBoxLayout;
    chips->setSpacing(8);
    auto *hintChip = new QLabel(u8("快捷："));
    hintChip->setObjectName("muted");
    chips->addWidget(hintChip);
    for (const auto &name : {u8("北京理工大学"), u8("中关村软件园"), u8("五道口")}) {
        auto *b = new QPushButton(name);
        b->setObjectName("ghost");
        connect(b, &QPushButton::clicked, this, [this, name] {
            if (addrEdit_)
                addrEdit_->setText(name);
            queryStations();
        });
        chips->addWidget(b);
    }
    chips->addStretch();
    lay->addLayout(chips);
    locMatch_ = new QLabel(u8("填写住址后点击查找，系统会匹配最近的电站和电桩"));
    locMatch_->setObjectName("muted");
    locMatch_->setWordWrap(true);
    lay->addWidget(locMatch_);
    auto *inner = new QWidget;
    stationBox_ = new QVBoxLayout(inner);
    stationBox_->setContentsMargins(0, 0, 8, 0);
    stationBox_->setSpacing(10);
    lay->addWidget(makeScroll(inner), 1);
    return w;
}

/** 桩列表页。 */
QWidget *UserWindow::buildPiles()
{
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(24, 18, 24, 16);
    lay->setSpacing(12);
    auto *back = new QPushButton(u8("← 返回电站列表"));
    back->setObjectName("ghost");
    back->setMaximumWidth(160);
    connect(back, &QPushButton::clicked, this, [this] { switchTab(0); });
    pileTitle_ = new QLabel;
    pileTitle_->setObjectName("title");
    pileMeta_ = new QLabel;
    pileMeta_->setObjectName("muted");
    pileMeta_->setWordWrap(true);
    lay->addWidget(back);
    pileType_ = new QComboBox;
    pileType_->addItems({u8("全部类型"), u8("快充"), u8("慢充")});
    pileType_->setMinimumWidth(140);
    pileType_->setMaximumWidth(200);
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
    outer->setContentsMargins(24, 10, 24, 12);
    outer->setSpacing(10);
    auto *back = new QPushButton(u8("← 返回电桩列表"));
    back->setObjectName("ghost");
    back->setMaximumWidth(160);
    connect(back, &QPushButton::clicked, this, [this] { switchTab(1); });
    outer->addWidget(back, 0, Qt::AlignLeft);

    auto *col = new QWidget;
    col->setMaximumWidth(760);
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
    reviewBody_->setMinimumHeight(140);
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

    auto *center = new QHBoxLayout;
    center->addStretch();
    center->addWidget(col, 1);
    center->addStretch();
    auto *page = new QWidget;
    page->setLayout(center);
    outer->addWidget(makeScroll(page), 1);
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
    auto *outer = new QHBoxLayout(w);
    outer->setContentsMargins(24, 20, 24, 16);
    outer->setSpacing(16);

    auto *form = new QWidget;
    auto *lay = new QVBoxLayout(form);
    lay->setContentsMargins(20, 18, 20, 24);
    lay->setSpacing(12);
    form->setMinimumHeight(720);

    auto *top = new QHBoxLayout;
    top->setSpacing(12);
    avatar_ = new QLabel(u8("用"));
    avatar_->setFixedSize(72, 72);
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
    col->addStretch();
    top->addWidget(avatar_, 0, Qt::AlignTop);
    top->addLayout(col, 1);
    lay->addLayout(top);

    auto *avRow = new QHBoxLayout;
    avRow->setSpacing(8);
    auto *pick = new QPushButton(u8("更换头像"));
    pick->setObjectName("ghost");
    auto *resetAv = new QPushButton(u8("恢复默认"));
    resetAv->setObjectName("ghost");
    connect(pick, &QPushButton::clicked, this, &UserWindow::pickAvatar);
    connect(resetAv, &QPushButton::clicked, this, &UserWindow::clearAvatar);
    avRow->addWidget(pick);
    avRow->addWidget(resetAv);
    avRow->addStretch();
    lay->addLayout(avRow);
    auto *avHint = new QLabel(u8("支持 jpg / png，服务端压缩后以二进制存入数据库。"));
    avHint->setObjectName("muted");
    avHint->setWordWrap(true);
    lay->addWidget(avHint);

    auto *nickLab = new QLabel(u8("修改昵称（1~20 字）"));
    nickEdit_ = new QLineEdit;
    auto *save = new QPushButton(u8("保存资料"));
    save->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(save, &QPushButton::clicked, this, [this] {
        const QString nick = nickEdit_->text().trimmed();
        if (nick.size() < 1 || nick.size() > 20) {
            uiWarn(this, u8("格式错误"), u8("昵称长度须为 1~20 个字符"));
            return;
        }
        controller_.request("UPDATE_PROFILE", QJsonObject{{"nickname", nick}});
    });
    lay->addSpacing(6);
    lay->addWidget(nickLab);
    lay->addWidget(nickEdit_);
    lay->addWidget(save);

    auto *payLab = new QLabel(u8("钱包充值（模拟支付，单笔 ≤ 10000 元）"));
    payEdit_ = new QLineEdit("20");
    auto *row = new QHBoxLayout;
    row->setSpacing(8);
    for (int a : {20, 50, 100, 200}) {
        auto *b = new QPushButton(QString("¥%1").arg(a));
        b->setObjectName("ghost");
        b->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        connect(b, &QPushButton::clicked, this, [this, a] { payEdit_->setText(QString::number(a)); });
        row->addWidget(b);
    }
    auto *pay = new QPushButton(u8("确认充值"));
    pay->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(pay, &QPushButton::clicked, this, [this] {
        const double a = payEdit_->text().toDouble();
        if (a <= 0 || a > 10000) {
            uiWarn(this, u8("金额错误"), u8("单笔充值须大于 0 且不超过 10000 元"));
            return;
        }
        controller_.request("RECHARGE", QJsonObject{{"amount", a}});
    });
    lay->addSpacing(6);
    lay->addWidget(payLab);
    lay->addLayout(row);
    lay->addWidget(payEdit_);
    lay->addWidget(pay);

    auto *logout = new QPushButton(u8("退出登录"));
    logout->setObjectName("ghost");
    logout->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(logout, &QPushButton::clicked, this, [this] {
        controller_.signOut();
        root_->setCurrentIndex(0);
        loginPage_->setStatus(u8("已安全退出，请重新登录"));
        statusBar()->showMessage(u8("已退出登录"));
    });
    auto *closeAcc = new QPushButton(u8("注销账号"));
    closeAcc->setObjectName("danger");
    closeAcc->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(closeAcc, &QPushButton::clicked, this, &UserWindow::closeMyAccount);
    auto *closeHint = new QLabel(u8("注销后不能登录，订单、评价、充值记录仍保留在库中，仅将账号标记为「注销」。"));
    closeHint->setObjectName("muted");
    closeHint->setWordWrap(true);
    closeHint->setMinimumHeight(48);
    closeHint->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    lay->addSpacing(8);
    lay->addWidget(closeHint);
    lay->addSpacing(4);
    lay->addWidget(closeAcc);
    lay->addSpacing(6);
    lay->addWidget(logout);
    lay->addSpacing(8);

    auto *left = new QFrame;
    left->setObjectName("card");
    auto *leftLay = new QVBoxLayout(left);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(0);
    leftLay->addWidget(makeScroll(form), 1);

    auto *right = new QFrame;
    right->setObjectName("card");
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(20, 20, 20, 16);
    auto *recTitle = new QLabel(u8("充值记录"));
    recTitle->setObjectName("title");
    rl->addWidget(recTitle);
    auto *rinner = new QWidget;
    rechargeBox_ = new QVBoxLayout(rinner);
    rechargeBox_->setContentsMargins(0, 0, 8, 0);
    rechargeBox_->setSpacing(8);
    rl->addWidget(makeScroll(rinner), 1);

    outer->addWidget(left, 1);
    outer->addWidget(right, 1);
    return w;
}

/** 按登录页地址重新拨号。 */
void UserWindow::reconnect()
{
    const QString host = loginPage_->serverHost();
    const quint16 port = loginPage_->serverPort();
    QSettings ini(QStringLiteral("ChargeHub"), QStringLiteral("UserClient"));
    ini.setValue("server", loginPage_->serverAddress());
    loginPage_->setStatus(u8("正在连接 ") + host + ":" + QString::number(port) + u8(" …"));
    controller_.connectTo(host, port);
}

/** 当前定位，给找站和导航。 */
QJsonObject UserWindow::coord() const
{
    return QJsonObject{{"lat", locLat_}, {"lng", locLng_}};
}

/** 发 QUERY_STATIONS。 */
void UserWindow::queryStations()
{
    if (!controller_.isAuthenticated())
        return;
    QJsonObject data = coord();
    const double radii[] = {3, 5, 10, 20};
    data["radiusKm"] = radii[qBound(0, radius_ ? radius_->currentIndex() : 3, 3)];
    if (addrEdit_)
        data["address"] = addrEdit_->text().trimmed();
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
/** 登录成功切到主壳并拉电站。 */
void UserWindow::showShell()
{
    root_->setCurrentIndex(1);
    switchTab(0);
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

/** 画附近电站卡片。 */
void UserWindow::renderStations(const QJsonObject &data)
{
    const auto loc = data.value("location").toObject();
    locLat_ = loc.value("lat").toDouble(locLat_);
    locLng_ = loc.value("lng").toDouble(locLng_);
    const QString matched = loc.value("matchedPlace").toString();
    const QString addr = loc.value("address").toString();
    if (loc.value("matched").toBool() && !matched.isEmpty()) {
        if (addr.isEmpty())
            locMatch_->setText(u8("已按%1匹配附近电站和电桩").arg(matched));
        else
            locMatch_->setText(u8("已定位：%1（%2）").arg(matched, addr));
    } else if (!addr.isEmpty()) {
        locMatch_->setText(u8("已按地址估算位置：%1").arg(addr));
    } else {
        locMatch_->setText(u8("未填写地址时默认按中关村匹配附近站点"));
    }

    clearBox(stationBox_);

    const auto nearby = data.value("nearbyPiles").toArray();
    if (!nearby.isEmpty()) {
        auto *h = new QLabel(u8("最近充电桩"));
        h->setObjectName("h2");
        stationBox_->addWidget(h);
        auto *hint = new QLabel(u8("按您填写的所在地自动匹配，距离由近到远"));
        hint->setObjectName("muted");
        stationBox_->addWidget(hint);
        for (const auto &v : nearby) {
            const auto p = v.toObject();
            auto *c = card();
            auto *cl = new QVBoxLayout(c);
            cl->setContentsMargins(14, 12, 14, 12);
            cl->setSpacing(6);
            auto *row = new QHBoxLayout;
            const QString code = p.value("code").toString().isEmpty()
                                     ? p.value("pileNo").toString()
                                     : p.value("code").toString();
            const QString stName = p.value("stationName").toString().isEmpty()
                                       ? p.value("station").toString()
                                       : p.value("stationName").toString();
            auto *name = new QLabel(code + "  " + stName);
            name->setObjectName("cardTitle");
            QString st = p.value("status").toString();
            if (st == u8("闲置"))
                st = u8("空闲");
            else if (st == u8("在用"))
                st = u8("占用");
            auto *pill = new QLabel(st);
            pill->setObjectName(st == u8("空闲") ? "pillOk" : (st == u8("故障") ? "pillOff" : "pillBusy"));
            row->addWidget(name, 1);
            row->addWidget(pill, 0, Qt::AlignRight);
            cl->addLayout(row);
            const QString ptype = p.value("pileType").toString().isEmpty()
                                      ? p.value("type").toString()
                                      : p.value("pileType").toString();
            auto *meta = new QLabel(QString::fromUtf8("%1  ·  %2  ·  %3 km  ·  ¥%4/度")
                                        .arg(p.value("stationAddress").toString())
                                        .arg(ptype)
                                        .arg(p.value("distanceKm").toDouble(), 0, 'f', 1)
                                        .arg(p.value("pricePerKwh").toDouble(), 0, 'f', 2));
            meta->setObjectName("muted");
            meta->setWordWrap(true);
            cl->addWidget(meta);
            auto *go = new QPushButton(u8("去该站选桩"));
            go->setMaximumWidth(140);
            const int sid = p.value("stationId").toInt();
            connect(go, &QPushButton::clicked, this, [this, sid] {
                controller_.request("QUERY_PILES", QJsonObject{{"stationId", sid}});
            });
            cl->addWidget(go, 0, Qt::AlignLeft);
            stationBox_->addWidget(c);
        }
    }

    auto *sh = new QLabel(u8("附近充电站"));
    sh->setObjectName("h2");
    stationBox_->addWidget(sh);

    const auto arr = data.value("stations").toArray();
    if (arr.isEmpty()) {
        auto *lab = new QLabel(u8("附近暂无充电站，请换一个更具体的地址试试"));
        lab->setObjectName("muted");
        stationBox_->addWidget(lab);
        stationBox_->addStretch();
        return;
    }
    for (const auto &v : arr) {
        const auto s = v.toObject();
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
        auto *meta = new QLabel(QString::fromUtf8("¥%1 / 度    %2 km    快充 %3  ·  慢充 %4    ★%5\n位置：%6, %7")
                                    .arg(s.value("pricePerKwh").toDouble(), 0, 'f', 2)
                                    .arg(s.value("distanceKm").toDouble(), 0, 'f', 1)
                                    .arg(s.value("fastPiles").toInt())
                                    .arg(s.value("slowPiles").toInt())
                                    .arg(s.value("score").toDouble(), 0, 'f', 1)
                                    .arg(s.value("lat").toDouble(), 0, 'f', 6)
                                    .arg(s.value("lng").toDouble(), 0, 'f', 6));
        meta->setObjectName("muted");
        cl->addWidget(meta);
        auto *btns = new QHBoxLayout;
        auto *nav = new QPushButton(u8("位置 / 导航"));
        nav->setObjectName("ghost");
        nav->setMaximumWidth(120);
        auto *go = new QPushButton(u8("选桩充电"));
        go->setMaximumWidth(120);
        connect(nav, &QPushButton::clicked, this, [this, s] { openNav(s); });
        connect(go, &QPushButton::clicked, this, [this, s] {
            currentStation_ = s;
            controller_.request("QUERY_PILES", QJsonObject{{"stationId", s.value("id").toInt()}});
        });
        btns->addWidget(nav);
        btns->addWidget(go);
        btns->addStretch();
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
        auto *hl = new QHBoxLayout(c);
        hl->setContentsMargins(16, 12, 16, 12);
        const QString st = p.value("status").toString();
        auto *infoCol = new QVBoxLayout;
        infoCol->setSpacing(4);
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
        infoCol->addWidget(info);
        infoCol->addWidget(stPill, 0, Qt::AlignLeft);
        const auto pileRevs = p.value("reviews").toArray();
        if (!pileRevs.isEmpty()) {
            const auto latest = pileRevs.at(0).toObject();
            auto *revLab = new QLabel(QString::fromUtf8("%1  ★%2  %3")
                                          .arg(latest.value("nickname").toString())
                                          .arg(latest.value("score").toInt())
                                          .arg(latest.value("comment").toString()));
            revLab->setObjectName("muted");
            revLab->setWordWrap(true);
            infoCol->addWidget(revLab);
        }
        auto *col = new QVBoxLayout;
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
        col->addWidget(rsv);
        col->addWidget(go);
        col->addWidget(rev);
        hl->addLayout(infoCol, 1);
        hl->addLayout(col);
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
        if (nav_) {
            nav_->blockSignals(true);
            nav_->setCurrentRow(2);
            nav_->blockSignals(false);
        }
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

/** 地图弹窗：静态图 + 路线，不经 Dispatch。 */
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
    dialog.polish(920, 760);

    auto *map = new QLabel(u8("正在加载腾讯地图…"));
    map->setAlignment(Qt::AlignCenter);
    map->setMinimumSize(820, 400);
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
    auto *open = new QPushButton(u8("打开腾讯地图"));
    open->setObjectName("ghost");
    routeBar->addWidget(open);
    routeBar->addStretch();
    dialog.body()->addLayout(routeBar);

    auto *routeResult = new QLabel(u8("选择出行方式后可查询距离和预计时间。"));
    routeResult->setObjectName("uiSheetHint");
    routeResult->setWordWrap(true);
    dialog.body()->addWidget(routeResult);
    dialog.addClose();

    const QJsonObject stationCopy = station;
    const QPointer<QLabel> mapGuard(map);
    if (mapNetwork_) {
        auto *reply = mapNetwork_->get(mapRequest(tencentStaticMapUrl(stationCopy)));
        connect(reply, &QNetworkReply::finished, this, [reply, mapGuard] {
            const QByteArray body = reply->readAll();
            if (mapGuard) {
                QPixmap pix;
                if (reply->error() == QNetworkReply::NoError && pix.loadFromData(body)) {
                    mapGuard->setPixmap(pix.scaled(mapGuard->size(), Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation));
                    mapGuard->setText(QString());
                } else {
                    mapGuard->setText(u8("腾讯地图图片暂时无法加载，可点击“打开腾讯地图”查看位置。"));
                }
            }
            reply->deleteLater();
        });
    }

    connect(route, &QPushButton::clicked, this, [this, stationCopy, mode, routeResult, route] {
        queryTencentRoute(stationCopy, mode->currentData().toString(), routeResult, route);
    });
    connect(open, &QPushButton::clicked, this, [this, stationCopy, mode] {
        const QUrl url = tencentRouteUri(stationCopy, coord(), mode->currentData().toString());
        if (!QDesktopServices::openUrl(url))
            QProcess::startDetached(QStringLiteral("xdg-open"), {url.toString()});
    });

    dialog.exec();
}

/** 问腾讯路线时间和距离，只改标签。 */
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
    if (tencentMapKey().isEmpty()) {
        resultLabel->setText(u8("未配置腾讯地图 Key；仍可点击“打开腾讯地图”进行导航。"));
        return;
    }
    if (queryButton)
        queryButton->setEnabled(false);
    resultLabel->setText(u8("正在查询腾讯地图路线…"));

    QList<QPair<QString, QString>> params{
        {QStringLiteral("from"), coordinateText(fromLat, fromLng)},
        {QStringLiteral("to"), coordinateText(toLat, toLng)},
        {QStringLiteral("output"), QStringLiteral("json")},
    };
    if (mode == QStringLiteral("bus"))
        params.append({QStringLiteral("policy"), QStringLiteral("LEAST_TIME")});
    const QUrl url = tencentApiUrl(routeApiPath(mode), params);
    auto *reply = mapNetwork_->get(mapRequest(url));
    const QPointer<QLabel> resultGuard(resultLabel);
    const QPointer<QPushButton> buttonGuard(queryButton);
    connect(reply, &QNetworkReply::finished, this, [reply, resultGuard, buttonGuard, mode] {
        if (resultGuard) {
            const QByteArray raw = reply->readAll();
            QJsonParseError error;
            const QJsonDocument doc = QJsonDocument::fromJson(raw, &error);
            if (reply->error() != QNetworkReply::NoError || error.error != QJsonParseError::NoError
                || !doc.isObject()) {
                resultGuard->setText(u8("路线查询失败：网络不可用或腾讯地图 API 未响应。"));
            } else {
                const QJsonObject root = doc.object();
                const int status = root.value("status").toInt(-1);
                const QString message = root.value("message").toString();
                const QJsonObject result = root.value("result").toObject();
                const QJsonArray routes = result.value("routes").toArray();
                if (status != 0 || routes.isEmpty()) {
                    resultGuard->setText(u8("%1路线查询失败：%2")
                                             .arg(routeModeName(mode), message.isEmpty() ? u8("暂无路线") : message));
                } else {
                    const QJsonObject first = routes.first().toObject();
                    const double distance = first.value("distance").toDouble();
                    const int duration = first.value("duration").toInt();
                    QString text = u8("%1：%2，距离约 %3 km")
                                       .arg(routeModeName(mode), durationText(duration))
                                       .arg(distance / 1000.0, 0, 'f', 1);
                    if (first.contains("taxi_fare"))
                        text += u8("，打车费约 ¥%1").arg(first.value("taxi_fare").toDouble(), 0, 'f', 2);
                    resultGuard->setText(text);
                }
            }
        }
        if (buttonGuard)
            buttonGuard->setEnabled(true);
        reply->deleteLater();
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
        // applyUser() calls refreshMe(), which requests LIST_RECHARGE once when logged in.
        applyUser(controller_.user());
    } else if (type == "UPDATE_PROFILE") {
        applyUser(controller_.user());
        uiInfo(this, u8("ChargeHub"), obj.value("message").toString());
    } else if (type == "LIST_RECHARGE") {
        renderRecharge(data.value("records").toArray());
    }
}
