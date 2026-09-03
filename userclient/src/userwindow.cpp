/**
 * @file userwindow.cpp
 * @brief 用户端页面与交互，所有写操作只发 Socket 请求
 */
#include "userwindow.h"

#include <algorithm>
#include <QAbstractItemView>
#include <QButtonGroup>
#include <QVector>
#include <QBuffer>
#include <QDesktopServices>
#include <QDebug>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QIODevice>
#include <QJsonArray>
#include <QListView>
#include <QListWidget>
#include <QComboBox>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProcess>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QStatusBar>
#include <QStringList>
#include <QStyle>
#include <QTextCursor>
#include <QUrl>
#include <QSettings>

static QString u8(const char *s) { return QString::fromUtf8(s); }

static bool isWalletMessage(const QString &type)
{
    return type == QStringLiteral("RECHARGE")
        || type == QStringLiteral("QUERY_RECHARGE")
        || type == QStringLiteral("QUERY_WALLET")
        || type == QStringLiteral("LIST_RECHARGE");
}

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
    root_->addWidget(buildLogin());
    root_->addWidget(buildShell());

    connect(&client_, &Client::connected, this, [this] {
        loginHint_->setText(u8("已连接运营平台"));
        statusBar()->showMessage(u8("已连接  ") + serverHost() + ":" + QString::number(serverPort()));
        sendPendingAuth();
    });
    connect(&client_, &Client::failed, this, [this](const QString &m) {
        loginHint_->setText(m);
        statusBar()->showMessage(m);
        if (walletController.isBusy()) {
            walletTimeout.stop();
            walletController.markFailure(QStringLiteral("NETWORK_ERROR"));
            qWarning().noquote()
                << QStringLiteral("wallet errorCode=NETWORK_ERROR requestId=%1")
                       .arg(walletController.pendingRequestId().isEmpty()
                                ? QStringLiteral("<none>")
                                : walletController.pendingRequestId());
            updateWalletUi();
        }
    });
    connect(&client_, &Client::responded, this, &UserWindow::onResp);
    connect(&poll_, &QTimer::timeout, this, &UserWindow::pollCharge);
    walletTimeout.setSingleShot(true);
    walletTimeout.setInterval(6000);
    connect(&walletTimeout, &QTimer::timeout, this, [this] {
        const WalletState previousState = walletController.state();
        walletController.markTimeout();
        qWarning().noquote()
            << QStringLiteral("wallet errorCode=TIMEOUT requestId=%1")
                   .arg(walletController.pendingRequestId().isEmpty()
                            ? QStringLiteral("<none>")
                            : walletController.pendingRequestId());
        updateWalletUi();
        if (previousState == WalletState::Submitting || previousState == WalletState::Querying)
            statusBar()->showMessage(u8("充值结果暂不确定，请查询原请求"));
        else
            statusBar()->showMessage(u8("余额暂不可用"));
    });
    statusBar()->showMessage(u8("未连接服务器"));
    reconnect();
}

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
    auto *w = new QWidget;
    w->setObjectName("loginRoot");
    auto *outer = new QHBoxLayout(w);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *brandPane = new QFrame;
    brandPane->setObjectName("brandPane");
    brandPane->setMinimumWidth(380);
    auto *bl = new QVBoxLayout(brandPane);
    bl->setContentsMargins(48, 56, 48, 48);
    auto *mark = new QLabel(QStringLiteral("CH"));
    mark->setObjectName("logoMark");
    mark->setFixedSize(48, 48);
    mark->setAlignment(Qt::AlignCenter);
    auto *brand = new QLabel(u8("ChargeHub"));
    brand->setObjectName("brandMark");
    auto *sub = new QLabel(u8("电动汽车充电综合服务平台\n桌面用户端"));
    sub->setObjectName("brandSub");
    sub->setWordWrap(true);
    auto *feat = new QLabel(u8("附近电站  ·  预约占桩\n充电结算  ·  钱包充值"));
    feat->setObjectName("brandFeat");
    feat->setWordWrap(true);
    bl->addStretch();
    bl->addWidget(mark);
    bl->addSpacing(16);
    bl->addWidget(brand);
    bl->addSpacing(10);
    bl->addWidget(sub);
    bl->addSpacing(28);
    bl->addWidget(feat);
    bl->addStretch();

    auto *formPane = new QWidget;
    formPane->setObjectName("formPane");
    auto *fl = new QVBoxLayout(formPane);
    fl->setContentsMargins(48, 36, 48, 28);
    fl->setSpacing(8);
    auto *title = new QLabel(u8("账号登录"));
    title->setObjectName("title");
    auto *desc = new QLabel(u8("使用手机号登录或注册。先连接管理端，再登录。"));
    desc->setObjectName("muted");
    desc->setWordWrap(true);
    phone_ = new QLineEdit("13800138000");
    phone_->setPlaceholderText(u8("手机号（11 位）"));
    pwd_ = new QLineEdit("123456");
    pwd_->setEchoMode(QLineEdit::Password);
    pwd_->setPlaceholderText(u8("密码（6~20 位）"));
    pwd2_ = new QLineEdit("123456");
    pwd2_->setEchoMode(QLineEdit::Password);
    pwd2_->setPlaceholderText(u8("确认密码（仅注册）"));
    hostEdit_ = new QLineEdit;
    QSettings ini(QStringLiteral("ChargeHub"), QStringLiteral("UserClient"));
    hostEdit_->setText(ini.value("server", QStringLiteral("127.0.0.1:8888")).toString());
    hostEdit_->setPlaceholderText(u8("服务器地址，例如 192.168.1.8:8888"));
    auto *conn = new QPushButton(u8("连接"));
    conn->setObjectName("ghost");
    conn->setMaximumWidth(100);
    connect(conn, &QPushButton::clicked, this, &UserWindow::reconnect);
    auto *hostRow = new QHBoxLayout;
    hostRow->addWidget(hostEdit_, 1);
    hostRow->addWidget(conn);
    auto *pwdRow = new QHBoxLayout;
    pwdRow->addWidget(pwd_);
    pwdRow->addWidget(pwd2_);
    auto *login = new QPushButton(u8("登  录"));
    login->setObjectName("primary");
    login->setDefault(true);
    login->setAutoDefault(true);
    login->setMinimumHeight(46);
    auto *reg = new QPushButton(u8("注册新账号"));
    reg->setObjectName("ghost");
    connect(login, &QPushButton::clicked, this, &UserWindow::doLogin);
    connect(reg, &QPushButton::clicked, this, &UserWindow::doRegister);
    loginHint_ = new QLabel(u8("请先连接服务器"));
    loginHint_->setObjectName("muted");
    auto *tips = new QLabel(u8("本机填 127.0.0.1:8888。演示账号 13800138000 / 123456"));
    tips->setObjectName("muted");
    tips->setWordWrap(true);
    fl->addWidget(title);
    fl->addWidget(desc);
    fl->addSpacing(8);
    fl->addWidget(new QLabel(u8("手机号")));
    fl->addWidget(phone_);
    fl->addWidget(new QLabel(u8("密码 / 确认密码")));
    fl->addLayout(pwdRow);
    fl->addWidget(new QLabel(u8("服务器地址")));
    fl->addLayout(hostRow);
    fl->addWidget(loginHint_);
    fl->addStretch(1);
    fl->addWidget(login);
    fl->addWidget(reg);
    fl->addWidget(tips);

    outer->addWidget(brandPane, 4);
    outer->addWidget(formPane, 5);
    return w;
}

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
    nav_->addItems({u8("附近电站"), u8("实时充电"), u8("我的订单"), u8("个人中心")});
    nav_->setCurrentRow(0);
    nav_->setFocusPolicy(Qt::NoFocus);
    nav_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    connect(nav_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row == 0)
            switchTab(0);
        else if (row == 1)
            switchTab(2);
        else if (row == 2)
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
    cl->addWidget(top);
    cl->addWidget(pages_, 1);
    lay->addWidget(side);
    lay->addWidget(content, 1);
    return w;
}

void UserWindow::switchTab(int i)
{
    pages_->setCurrentIndex(i);
    if (nav_) {
        nav_->blockSignals(true);
        if (i == 0 || i == 1 || i == 5)
            nav_->setCurrentRow(0);
        else if (i == 2)
            nav_->setCurrentRow(1);
        else if (i == 3)
            nav_->setCurrentRow(2);
        else if (i == 4)
            nav_->setCurrentRow(3);
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
        if (!token_.isEmpty())
            client_.request("CHARGE_STATUS", {}, token_);
    } else if (i == 3) {
        if (!token_.isEmpty())
            client_.request("LIST_ORDERS", {}, token_);
    } else if (i == 4) {
        refreshMe();
        loadWallet();
    }
}

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

QWidget *UserWindow::buildCharge()
{
    auto *w = new QWidget;
    auto *outer = new QHBoxLayout(w);
    outer->setContentsMargins(24, 24, 24, 24);
    auto *panel = new QFrame;
    panel->setObjectName("card");
    panel->setMaximumWidth(640);
    auto *lay = new QVBoxLayout(panel);
    lay->setContentsMargins(28, 24, 28, 24);
    chStatus_ = new QLabel(u8("暂无进行中的充电"));
    chStatus_->setObjectName("title");
    chTime_ = new QLabel(u8("00:00"));
    chTime_->setObjectName("kpi");
    chTime_->setAlignment(Qt::AlignCenter);
    chInfo_ = new QLabel(u8("在「附近电站」选择电站和空闲桩后开始充电。支持预约占桩 15 分钟。"));
    chInfo_->setObjectName("muted");
    chInfo_->setWordWrap(true);
    stopBtn_ = new QPushButton(u8("结束充电"));
    stopBtn_->setObjectName("danger");
    settleBtn_ = new QPushButton(u8("立即结算"));
    auto *home = new QPushButton(u8("去找桩"));
    home->setObjectName("ghost");
    connect(stopBtn_, &QPushButton::clicked, this, [this] { client_.request("STOP_CHARGE", {}, token_); });
    connect(settleBtn_, &QPushButton::clicked, this, [this] { client_.request("SETTLE_ORDER", {}, token_); });
    connect(home, &QPushButton::clicked, this, [this] { switchTab(0); });
    stopBtn_->hide();
    settleBtn_->hide();
    auto *btns = new QHBoxLayout;
    btns->addWidget(stopBtn_);
    btns->addWidget(settleBtn_);
    btns->addWidget(home);
    btns->addStretch();
    lay->addWidget(chStatus_);
    lay->addSpacing(8);
    lay->addWidget(chTime_);
    lay->addSpacing(12);
    lay->addWidget(chInfo_);
    lay->addStretch();
    lay->addLayout(btns);
    outer->addWidget(panel, 1);
    outer->addStretch(1);
    return w;
}

QWidget *UserWindow::buildOrders()
{
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(24, 18, 24, 16);
    lay->setSpacing(12);
    orderFilter_ = new QComboBox;
    orderFilter_->addItems({u8("全部订单"), u8("待结算优先"), u8("已完成"), u8("充电中")});
    orderFilter_->setMinimumWidth(160);
    orderFilter_->setMaximumWidth(220);
    prepCombo(orderFilter_);
    connect(orderFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        renderOrders(lastOrders_);
    });
    lay->addWidget(orderFilter_);
    auto *inner = new QWidget;
    orderBox_ = new QVBoxLayout(inner);
    orderBox_->setContentsMargins(0, 0, 8, 0);
    orderBox_->setSpacing(10);
    lay->addWidget(makeScroll(inner), 1);
    return w;
}

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
            QMessageBox::warning(this, u8("格式错误"), u8("昵称长度须为 1~20 个字符"));
            return;
        }
        client_.request("UPDATE_PROFILE", QJsonObject{{"nickname", nick}}, token_);
    });
    lay->addSpacing(6);
    lay->addWidget(nickLab);
    lay->addWidget(nickEdit_);
    lay->addWidget(save);

    auto *payLab = new QLabel(u8("钱包充值（模拟支付，单笔 ≤ 10000 元）"));
    payEdit_ = new QLineEdit("20");
    payEdit_->setPlaceholderText(u8("输入0.01至10000.00，最多两位小数"));
    connect(payEdit_, &QLineEdit::textEdited, this, [this] {
        walletController.beginEditing();
        updateWalletUi();
    });
    auto *row = new QHBoxLayout;
    row->setSpacing(8);
    for (int a : {20, 50, 100, 200}) {
        auto *b = new QPushButton(QString("¥%1").arg(a));
        b->setObjectName("ghost");
        b->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        connect(b, &QPushButton::clicked, this, [this, a] {
            payEdit_->setText(QString::number(a));
            walletController.beginEditing();
            updateWalletUi();
        });
        rechargeQuickButtons.append(b);
        row->addWidget(b);
    }
    rechargeButton = new QPushButton(u8("确认充值"));
    rechargeButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(rechargeButton, &QPushButton::clicked, this, &UserWindow::submitRecharge);
    connect(payEdit_, &QLineEdit::returnPressed, this, &UserWindow::submitRecharge);
    newRechargeButton = new QPushButton(u8("新建一笔充值"));
    newRechargeButton->setObjectName("ghost");
    newRechargeButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    newRechargeButton->hide();
    connect(newRechargeButton, &QPushButton::clicked, this, &UserWindow::startNewRecharge);
    walletHint = new QLabel(u8("余额尚未加载"));
    walletHint->setObjectName("muted");
    walletHint->setWordWrap(true);
    lay->addSpacing(6);
    lay->addWidget(payLab);
    lay->addWidget(walletHint);
    lay->addLayout(row);
    lay->addWidget(payEdit_);
    lay->addWidget(rechargeButton);
    lay->addWidget(newRechargeButton);

    auto *logout = new QPushButton(u8("退出登录"));
    logout->setObjectName("ghost");
    logout->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(logout, &QPushButton::clicked, this, [this] {
        token_.clear();
        poll_.stop();
        walletTimeout.stop();
        walletController = WalletController();
        walletBlocked = false;
        root_->setCurrentIndex(0);
        loginHint_->setText(u8("已安全退出，请重新登录"));
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

void UserWindow::reconnect()
{
    const QString host = serverHost();
    const quint16 port = serverPort();
    QSettings ini(QStringLiteral("ChargeHub"), QStringLiteral("UserClient"));
    ini.setValue("server", hostEdit_ ? hostEdit_->text().trimmed() : QString("%1:%2").arg(host).arg(port));
    loginHint_->setText(u8("正在连接 ") + host + ":" + QString::number(port) + u8(" …"));
    client_.connectTo(host, port);
}

QString UserWindow::serverHost() const
{
    QString raw = hostEdit_ ? hostEdit_->text().trimmed() : QStringLiteral("127.0.0.1");
    if (raw.isEmpty())
        raw = QStringLiteral("127.0.0.1");
    if (raw.contains(QLatin1String("://")))
        raw = raw.section(QLatin1String("://"), 1, 1);
    raw = raw.section('/', 0, 0);
    if (raw.count('.') >= 1 && raw.contains(':'))
        return raw.section(':', 0, -2);
    return raw;
}

quint16 UserWindow::serverPort() const
{
    QString raw = hostEdit_ ? hostEdit_->text().trimmed() : QString();
    if (raw.contains(':')) {
        const QString p = raw.section(':', -1);
        bool ok = false;
        const int n = p.toInt(&ok);
        if (ok && n > 0 && n < 65536)
            return quint16(n);
    }
    return 8888;
}

void UserWindow::sendPendingAuth()
{
    if (pendingAuth_.isEmpty() || !client_.isConnected())
        return;
    const QString act = pendingAuth_;
    pendingAuth_.clear();
    loginHint_->setText(act == QLatin1String("REGISTER") ? u8("正在注册…") : u8("正在登录…"));
    client_.request(act, QJsonObject{{"phone", pendingPhone_}, {"password", pendingPwd_}}, QString());
}

void UserWindow::doLogin()
{
    const QString phone = phone_->text().trimmed();
    const QString pwd = pwd_->text();
    if (!QRegularExpression(QStringLiteral("^1[3-9][0-9]{9}$")).match(phone).hasMatch()) {
        QMessageBox::warning(this, u8("格式错误"), u8("请输入正确的手机号格式"));
        return;
    }
    if (pwd.size() < 6 || pwd.size() > 20) {
        QMessageBox::warning(this, u8("格式错误"), u8("密码长度须为 6~20 位"));
        return;
    }
    pendingAuth_ = QStringLiteral("LOGIN");
    pendingPhone_ = phone;
    pendingPwd_ = pwd;
    if (client_.isConnected()) {
        sendPendingAuth();
        return;
    }
    loginHint_->setText(u8("尚未连接，正在连接服务器…"));
    reconnect();
}

void UserWindow::doRegister()
{
    const QString phone = phone_->text().trimmed();
    const QString pwd = pwd_->text();
    if (!QRegularExpression(QStringLiteral("^1[3-9][0-9]{9}$")).match(phone).hasMatch()) {
        QMessageBox::warning(this, u8("格式错误"), u8("请输入正确的手机号格式"));
        return;
    }
    if (pwd.size() < 6 || pwd.size() > 20) {
        QMessageBox::warning(this, u8("格式错误"), u8("密码长度须为 6~20 位"));
        return;
    }
    if (pwd != pwd2_->text()) {
        QMessageBox::warning(this, u8("格式错误"), u8("两次输入的密码不一致"));
        return;
    }
    pendingAuth_ = QStringLiteral("REGISTER");
    pendingPhone_ = phone;
    pendingPwd_ = pwd;
    if (client_.isConnected()) {
        sendPendingAuth();
        return;
    }
    loginHint_->setText(u8("尚未连接，正在连接服务器…"));
    reconnect();
}

QJsonObject UserWindow::coord() const
{
    return QJsonObject{{"lat", locLat_}, {"lng", locLng_}};
}

void UserWindow::queryStations()
{
    if (token_.isEmpty())
        return;
    QJsonObject data = coord();
    const double radii[] = {3, 5, 10, 20};
    data["radiusKm"] = radii[qBound(0, radius_ ? radius_->currentIndex() : 3, 3)];
    if (addrEdit_)
        data["address"] = addrEdit_->text().trimmed();
    client_.request("QUERY_STATIONS", data, token_);
}

void UserWindow::applyUser(const QJsonObject &u)
{
    user_ = u;
    if (u.value("balanceCents").isDouble())
        walletController.acceptWallet(QJsonObject{{"balanceCents", u.value("balanceCents")}});
    if (u.contains("lat"))
        locLat_ = u.value("lat").toDouble(locLat_);
    if (u.contains("lng"))
        locLng_ = u.value("lng").toDouble(locLng_);
    if (addrEdit_ && addrEdit_->text().trimmed().isEmpty() && !u.value("address").toString().isEmpty())
        addrEdit_->setText(u.value("address").toString());
    refreshMe();
}

void UserWindow::showShell()
{
    root_->setCurrentIndex(1);
    switchTab(0);
}

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

void UserWindow::pickAvatar()
{
    if (token_.isEmpty())
        return;
    const QString path = QFileDialog::getOpenFileName(this, u8("选择头像"), QString(),
                                                      u8("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp)"));
    if (path.isEmpty())
        return;
    QImage img(path);
    if (img.isNull()) {
        QMessageBox::warning(this, u8("无法打开"), u8("请选择有效的 jpg / png 图片"));
        return;
    }
    if (img.width() > 512 || img.height() > 512)
        img = img.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    if (!img.save(&buf, "JPEG", 88)) {
        QMessageBox::warning(this, u8("无法处理"), u8("图片编码失败，请换一张"));
        return;
    }
    if (bytes.size() > 400 * 1024) {
        QMessageBox::warning(this, u8("图片过大"), u8("请选择更小的图片（压缩后需小于 400KB）"));
        return;
    }
    client_.request("UPDATE_PROFILE",
                    QJsonObject{{"avatarBase64", QString::fromLatin1(bytes.toBase64())}}, token_);
}

void UserWindow::clearAvatar()
{
    if (token_.isEmpty())
        return;
    client_.request("UPDATE_PROFILE", QJsonObject{{"clearAvatar", true}}, token_);
}

void UserWindow::refreshMe()
{
    if (user_.isEmpty())
        return;
    const QString nick = user_.value("nickname").toString();
    meName_->setText(nick);
    nickEdit_->setText(nick);
    showAvatar(user_);
    updateWalletUi();
}

void UserWindow::loadWallet()
{
    if (token_.isEmpty() || walletController.isBusy()
        || walletController.state() == WalletState::Uncertain) {
        return;
    }
    walletController.beginLoading();
    updateWalletUi();
    walletTimeout.start();
    client_.request("QUERY_WALLET", {}, token_);
}

void UserWindow::submitRecharge()
{
    if (walletController.canQuery()) {
        queryPendingRecharge();
        return;
    }
    if (!walletController.canSubmit() || walletBlocked)
        return;
    const AmountParseResult amount = WalletController::parseAmountCents(payEdit_->text());
    if (!amount.valid) {
        QMessageBox::warning(this, u8("金额错误"),
                             u8("请输入0.01至10000.00元，最多两位小数"));
        payEdit_->setFocus();
        payEdit_->selectAll();
        return;
    }
    if (!walletController.beginConfirmation(amount.cents))
        return;
    updateWalletUi();
    const auto answer = QMessageBox::question(
        this, u8("确认充值"),
        u8("本次模拟充值金额：%1\n确认继续吗？")
            .arg(WalletController::formatCents(amount.cents)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        walletController.cancelConfirmation();
        updateWalletUi();
        return;
    }
    if (!client_.isConnected()) {
        walletController.cancelConfirmation();
        walletController.markFailure(QStringLiteral("NETWORK_ERROR"));
        updateWalletUi();
        QMessageBox::warning(this, u8("网络错误"), u8("未连接到服务器，余额未改变"));
        return;
    }
    const WalletRequest request = walletController.submitConfirmed();
    if (!request.isValid())
        return;
    updateWalletUi();
    walletTimeout.start();
    client_.request(request.type, request.data, token_);
}

void UserWindow::queryPendingRecharge()
{
    if (!client_.isConnected()) {
        walletController.markFailure(QStringLiteral("NETWORK_ERROR"));
        updateWalletUi();
        return;
    }
    const WalletRequest request = walletController.queryPending();
    if (!request.isValid())
        return;
    updateWalletUi();
    walletTimeout.start();
    client_.request(request.type, request.data, token_);
}

void UserWindow::startNewRecharge()
{
    walletController.startNewRecharge();
    walletBlocked = false;
    payEdit_->clear();
    updateWalletUi();
    payEdit_->setFocus();
}

void UserWindow::updateWalletUi()
{
    const bool trusted = walletController.hasTrustedBalance();
    QString balanceStatus;
    if (trusted)
        balanceStatus = walletController.balanceText();
    else if (walletController.state() == WalletState::Loading)
        balanceStatus = u8("余额加载中");
    else
        balanceStatus = u8("余额暂不可用");
    if (headBal_)
        headBal_->setText(trusted ? u8("余额 ") + balanceStatus : balanceStatus);
    if (meBal_ && !user_.isEmpty())
        meBal_->setText(user_.value("phone").toString() + u8("  ·  ") + balanceStatus);

    QString hint;
    switch (walletController.state()) {
    case WalletState::Idle: hint = u8("余额尚未加载"); break;
    case WalletState::Loading: hint = u8("余额加载中…"); break;
    case WalletState::Ready: hint = u8("余额已与服务器同步"); break;
    case WalletState::Editing: hint = u8("请输入充值金额"); break;
    case WalletState::Confirming: hint = u8("请确认本次充值金额"); break;
    case WalletState::Submitting: hint = u8("充值请求提交中，请勿重复操作…"); break;
    case WalletState::Succeeded: hint = u8("充值成功，正在刷新钱包…"); break;
    case WalletState::Failed:
        hint = trusted ? u8("操作失败，已保留上次可信余额") : u8("余额暂不可用");
        break;
    case WalletState::Uncertain:
        hint = u8("充值结果暂不确定，可查询原请求或新建一笔");
        break;
    case WalletState::Querying: hint = u8("正在查询原充值请求…"); break;
    }
    if (walletHint)
        walletHint->setText(hint);

    const bool uncertain = walletController.canQuery();
    const bool busy = walletController.isBusy() || walletController.state() == WalletState::Confirming;
    if (rechargeButton) {
        rechargeButton->setText(uncertain ? u8("查询充值结果") : u8("确认充值"));
        rechargeButton->setEnabled(!walletBlocked && (uncertain || walletController.canSubmit()));
    }
    if (newRechargeButton) {
        newRechargeButton->setVisible(uncertain);
        newRechargeButton->setEnabled(!busy);
    }
    if (payEdit_)
        payEdit_->setEnabled(!walletBlocked && !busy && !uncertain);
    for (QPushButton *button : rechargeQuickButtons)
        button->setEnabled(!walletBlocked && !busy && !uncertain);
}

void UserWindow::handleWalletError(const QString &type, const QJsonObject &response)
{
    walletTimeout.stop();
    QString errorCode = response.value("errorCode").toString();
    if (errorCode.isEmpty())
        errorCode = QStringLiteral("PROTOCOL_ERROR");
    walletController.markFailure(errorCode);
    if (errorCode == QStringLiteral("USER_FROZEN"))
        walletBlocked = true;
    if (errorCode == QStringLiteral("INVALID_AMOUNT") && payEdit_) {
        payEdit_->setFocus();
        payEdit_->selectAll();
    }
    qWarning().noquote()
        << QStringLiteral("wallet action=%1 errorCode=%2 requestId=%3")
               .arg(type, errorCode,
                    walletController.pendingRequestId().isEmpty()
                        ? QStringLiteral("<none>")
                        : walletController.pendingRequestId());
    updateWalletUi();
    const QString message = response.value("message").toString(u8("钱包请求失败"));
    QMessageBox::warning(this, u8("钱包提示"), message);
    if (errorCode == QStringLiteral("USER_NOT_FOUND")
        || errorCode == QStringLiteral("AUTH_REQUIRED")) {
        token_.clear();
        root_->setCurrentIndex(0);
        loginHint_->setText(u8("请重新登录"));
    }
}

void UserWindow::closeMyAccount()
{
    const auto ret = QMessageBox::question(
        this,
        u8("注销账号"),
        u8("注销后账号将被禁用并留档，订单、评价、充值记录均保留以便追溯。\n"
           "同一手机号不能再注册。确定注销吗？"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;
    client_.request("CLOSE_ACCOUNT", {}, token_);
}

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
                client_.request("QUERY_PILES", QJsonObject{{"stationId", sid}}, token_);
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
        auto *meta = new QLabel(QString::fromUtf8("¥%1 / 度    %2 km    快充 %3  ·  慢充 %4    ★%5")
                                    .arg(s.value("pricePerKwh").toDouble(), 0, 'f', 2)
                                    .arg(s.value("distanceKm").toDouble(), 0, 'f', 1)
                                    .arg(s.value("fastPiles").toInt())
                                    .arg(s.value("slowPiles").toInt())
                                    .arg(s.value("score").toDouble(), 0, 'f', 1));
        meta->setObjectName("muted");
        cl->addWidget(meta);
        auto *btns = new QHBoxLayout;
        auto *nav = new QPushButton(u8("导航"));
        nav->setObjectName("ghost");
        nav->setMaximumWidth(88);
        auto *go = new QPushButton(u8("选桩充电"));
        go->setMaximumWidth(120);
        connect(nav, &QPushButton::clicked, this, [this, s] { openNav(s); });
        connect(go, &QPushButton::clicked, this, [this, s] {
            currentStation_ = s;
            client_.request("QUERY_PILES", QJsonObject{{"stationId", s.value("id").toInt()}}, token_);
        });
        btns->addWidget(nav);
        btns->addWidget(go);
        btns->addStretch();
        cl->addLayout(btns);
        stationBox_->addWidget(c);
    }
    stationBox_->addStretch();
}

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
                client_.request("CANCEL_RESERVE", {}, token_);
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

void UserWindow::renderOrders(const QJsonArray &arr)
{
    lastOrders_ = arr;
    clearBox(orderBox_);
    QVector<QJsonObject> rows;
    for (const auto &v : arr)
        rows.append(v.toObject());
    const int mode = orderFilter_ ? orderFilter_->currentIndex() : 0;
    if (mode == 1)
        std::sort(rows.begin(), rows.end(), [](const QJsonObject &a, const QJsonObject &b) {
            auto rank = [](const QString &s) {
                if (s == QString::fromUtf8("待结算")) return 0;
                if (s == QString::fromUtf8("充电中")) return 1;
                return 2;
            };
            return rank(a.value("status").toString()) < rank(b.value("status").toString());
        });
    if (arr.isEmpty()) {
        auto *lab = new QLabel(u8("暂无订单，去首页找桩充电吧"));
        lab->setObjectName("muted");
        orderBox_->addWidget(lab);
        orderBox_->addStretch();
        return;
    }
    int shown = 0;
    for (const auto &o : rows) {
        const QString st = o.value("status").toString();
        if (mode == 2 && st != u8("已完成"))
            continue;
        if (mode == 3 && st != u8("充电中"))
            continue;
        ++shown;
        auto *c = card();
        auto *cl = new QVBoxLayout(c);
        cl->setContentsMargins(16, 12, 16, 12);
        cl->setSpacing(6);
        auto *head = new QHBoxLayout;
        auto *no = new QLabel(o.value("orderNo").toString());
        no->setObjectName("cardTitle");
        auto *stPill = new QLabel(st);
        if (st == u8("已完成"))
            stPill->setObjectName("pillOk");
        else if (st == u8("充电中"))
            stPill->setObjectName("pillWarn");
        else
            stPill->setObjectName("pillOff");
        head->addWidget(no, 1);
        head->addWidget(stPill);
        cl->addLayout(head);
        auto *detail = new QLabel(QString("%1  %2\n电量 %3 kWh    ¥%4")
                                      .arg(o.value("stationName").toString(), o.value("pileNo").toString())
                                      .arg(o.value("energyKwh").toDouble(), 0, 'f', 3)
                                      .arg(o.value("amount").toDouble(), 0, 'f', 2));
        detail->setObjectName("muted");
        cl->addWidget(detail);
        orderBox_->addWidget(c);
    }
    if (shown == 0) {
        auto *lab = new QLabel(u8("当前筛选下暂无订单"));
        lab->setObjectName("muted");
        orderBox_->addWidget(lab);
    }
    orderBox_->addStretch();
}

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
        const QString amount = r.value("amountCents").isDouble()
            ? WalletController::formatCents(r.value("amountCents").toVariant().toLongLong())
            : QStringLiteral("--");
        cl->addWidget(new QLabel(QStringLiteral("%1    %2")
                                     .arg(amount, r.value("result").toString())));
        auto *m = new QLabel(r.value("tradeNo").toString() + "  " + r.value("createdAt").toString());
        m->setObjectName("muted");
        cl->addWidget(m);
        rechargeBox_->addWidget(c);
    }
    rechargeBox_->addStretch();
}

void UserWindow::showCharge(const QJsonObject &order)
{
    currentOrder_ = order;
    const QString st = order.value("status").toString();
    chStatus_->setText(st.isEmpty() ? u8("暂无进行中的充电") : st);
    const int sec = order.value("seconds").toInt();
    chTime_->setText(QString("%1:%2").arg(sec / 60, 2, 10, QChar('0')).arg(sec % 60, 2, 10, QChar('0')));
    chInfo_->setText(QString::fromUtf8("%1  %2\n订单 %3\n电量 %4 kWh\n费用 ¥%5\n%6 kW × %7 元/度")
                         .arg(order.value("stationName").toString(), order.value("pileNo").toString())
                         .arg(order.value("orderNo").toString())
                         .arg(order.value("energyKwh").toDouble(), 0, 'f', 3)
                         .arg(order.value("amount").toDouble(), 0, 'f', 2)
                         .arg(order.value("powerKw").toDouble(), 0, 'f', 0)
                         .arg(order.value("pricePerKwh").toDouble(), 0, 'f', 2));
    stopBtn_->setVisible(st == u8("充电中"));
    settleBtn_->setVisible(st == u8("待结算"));
    if (st == u8("充电中") || st == u8("待结算")) {
        pages_->setCurrentIndex(2);
        if (nav_) {
            nav_->blockSignals(true);
            nav_->setCurrentRow(1);
            nav_->blockSignals(false);
        }
    }
}

void UserWindow::tryStart(int pileId)
{
    pendingPile_ = pileId;
    wantStart_ = true;
    client_.request("CHARGE_STATUS", {}, token_);
}

void UserWindow::doReserve(int pileId)
{
    client_.request("RESERVE_PILE", QJsonObject{{"pileId", pileId}}, token_);
}

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
    client_.request("LIST_PILE_REVIEWS", QJsonObject{{"pileId", pileId}}, token_);
}

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

void UserWindow::submitReview()
{
    const int pileId = currentPile_.value("id").toInt();
    if (pileId <= 0) {
        QMessageBox::warning(this, u8("无法评价"), u8("请从电桩列表进入对应桩的评价页"));
        return;
    }
    const QString comment = reviewBody_ ? reviewBody_->toPlainText().trimmed() : QString();
    if (comment.size() < 2 || comment.size() > 300) {
        QMessageBox::warning(this, u8("评语不完整"), u8("请写下 2~300 字的充电体验，不能只打分"));
        return;
    }
    client_.request("REVIEW_STATION",
                    QJsonObject{{"stationId", currentStation_.value("id").toInt()},
                                {"pileId", pileId},
                                {"score", reviewStars_},
                                {"comment", comment}},
                    token_);
}

void UserWindow::openNav(const QJsonObject &station)
{
    const auto c = coord();
    const QString dest = station.value("name").toString();
    const QString url = QStringLiteral(
                            "https://map.qq.com/nav/drive#routes/page?sword=%1&spointx=%2&spointy=%3&eword=%4&epointx=%5&epointy=%6")
                            .arg(QString::fromUtf8(QUrl::toPercentEncoding(u8("当前位置"))))
                            .arg(c.value("lng").toDouble(), 0, 'f', 6)
                            .arg(c.value("lat").toDouble(), 0, 'f', 6)
                            .arg(QString::fromUtf8(QUrl::toPercentEncoding(dest)))
                            .arg(station.value("lng").toDouble(), 0, 'f', 6)
                            .arg(station.value("lat").toDouble(), 0, 'f', 6);
    if (!QDesktopServices::openUrl(QUrl(url)))
        QProcess::startDetached(QStringLiteral("xdg-open"), {url});
}

void UserWindow::pollCharge()
{
    if (!token_.isEmpty() && currentOrder_.value("status").toString() == u8("充电中"))
        client_.request("CHARGE_STATUS", {}, token_);
}

void UserWindow::onResp(QJsonObject obj)
{
    const QString type = obj.value("type").toString();
    const int code = obj.value("code").toInt();
    if (code != 0) {
        if (isWalletMessage(type)) {
            handleWalletError(type, obj);
            wantStart_ = false;
            return;
        }
        const QString msg = obj.value("message").toString();
        loginHint_->setText(msg.isEmpty() ? u8("请求失败") : msg);
        QMessageBox::warning(this, u8("提示"), msg.isEmpty() ? (type + u8(" 失败")) : msg);
        // 只有登录/注册失败才停在登录页；进首页后的接口失败不得把人踢回去
        if (type == "LOGIN" || type == "REGISTER") {
            token_.clear();
            root_->setCurrentIndex(0);
        }
        wantStart_ = false;
        return;
    }
    const QJsonObject data = obj.value("data").toObject();
    if (type == "LOGIN" || type == "REGISTER") {
        walletTimeout.stop();
        walletController = WalletController();
        walletBlocked = false;
        token_ = data.value("token").toString();
        showShell();
        applyUser(data.value("user").toObject());
        loadWallet();
    } else if (type == "CLOSE_ACCOUNT") {
        poll_.stop();
        walletTimeout.stop();
        token_.clear();
        user_ = {};
        walletController = WalletController();
        walletBlocked = false;
        locLat_ = 39.9644;
        locLng_ = 116.3473;
        if (addrEdit_)
            addrEdit_->clear();
        QMessageBox::information(this, u8("账号已注销"),
                                 obj.value("message").toString(u8("账号已禁用留档，历史记录可追溯。")));
        loginHint_->setText(u8("账号已注销留档，同一手机号不能再注册"));
        root_->setCurrentIndex(0);
    } else if (type == "QUERY_STATIONS") {
        renderStations(data);
    } else if (type == "QUERY_PILES") {
        renderPiles(data);
    } else if (type == "RESERVE_PILE" || type == "CANCEL_RESERVE") {
        QMessageBox::information(this, u8("ChargeHub"), obj.value("message").toString());
        if (currentStation_.value("id").toInt() > 0)
            client_.request("QUERY_PILES", QJsonObject{{"stationId", currentStation_.value("id").toInt()}}, token_);
        else
            queryStations();
    } else if (type == "REVIEW_STATION") {
        QMessageBox::information(this, u8("ChargeHub"), obj.value("message").toString());
        const int pileId = currentPile_.value("id").toInt();
        if (pages_->currentIndex() == 5 && pileId > 0)
            client_.request("LIST_PILE_REVIEWS", QJsonObject{{"pileId", pileId}}, token_);
        else if (currentStation_.value("id").toInt() > 0)
            client_.request("QUERY_PILES", QJsonObject{{"stationId", currentStation_.value("id").toInt()}}, token_);
        else
            queryStations();
    } else if (type == "LIST_PILE_REVIEWS") {
        renderPileReview(data);
    } else if (type == "CHARGE_STATUS") {
        const QJsonObject order = data.value("order").toObject();
        if (wantStart_) {
            wantStart_ = false;
            if (!order.isEmpty() && order.value("id").toInt() > 0) {
                QMessageBox::warning(this, u8("无法开始充电"), u8("您有未完成的充电订单，请先结算"));
                showCharge(order);
                if (order.value("status").toString() == u8("充电中"))
                    poll_.start(1000);
                return;
            }
            client_.request("START_CHARGE", QJsonObject{{"pileId", pendingPile_}}, token_);
            return;
        }
        if (!order.isEmpty() && order.value("id").toInt() > 0) {
            showCharge(order);
            if (order.value("status").toString() == u8("充电中"))
                poll_.start(1000);
            else
                poll_.stop();
        } else if (pages_->currentIndex() == 2) {
            poll_.stop();
            currentOrder_ = {};
            chStatus_->setText(u8("暂无进行中的充电"));
            chTime_->setText("00:00");
            stopBtn_->hide();
            settleBtn_->hide();
        }
    } else if (type == "START_CHARGE" || type == "STOP_CHARGE") {
        showCharge(data.value("order").toObject());
        if (type == "START_CHARGE")
            poll_.start(1000);
        else
            poll_.stop();
    } else if (type == "SETTLE_ORDER") {
        poll_.stop();
        applyUser(data.value("user").toObject());
        const auto o = data.value("order").toObject();
        QMessageBox::information(this, u8("结算成功"),
                                 u8("订单 %1\n电量 %2 kWh\n费用 ¥%3\n余额 %4")
                                     .arg(o.value("orderNo").toString())
                                     .arg(o.value("energyKwh").toDouble(), 0, 'f', 3)
                                     .arg(o.value("amount").toDouble(), 0, 'f', 2)
                                     .arg(walletController.balanceText()));
        currentOrder_ = {};
        switchTab(3);
    } else if (type == "LIST_ORDERS") {
        renderOrders(data.value("orders").toArray());
    } else if (type == "RECHARGE" || type == "QUERY_RECHARGE") {
        const WalletResponseStatus responseStatus = walletController.acceptRecharge(data);
        if (responseStatus == WalletResponseStatus::Ignored
            || responseStatus == WalletResponseStatus::Duplicate) {
            return;
        }
        walletTimeout.stop();
        if (responseStatus == WalletResponseStatus::ProtocolError) {
            walletController.markFailure(QStringLiteral("PROTOCOL_ERROR"));
            updateWalletUi();
            QMessageBox::warning(this, u8("钱包提示"), u8("服务器响应不完整，余额未更新"));
            return;
        }
        user_.insert("balanceCents", QJsonValue::fromVariant(walletController.balanceCents()));
        user_.insert("balance", walletController.balanceCents() / 100.0);
        refreshMe();
        QMessageBox::information(this, u8("充值成功"),
                                 u8("流水号 %1\n金额 %2\n余额 %3")
                                     .arg(data.value("tradeNo").toString())
                                     .arg(WalletController::formatCents(
                                         data.value("amountCents").toVariant().toLongLong()))
                                     .arg(walletController.balanceText()));
        loadWallet();
    } else if (type == "QUERY_WALLET" || type == "LIST_RECHARGE") {
        const WalletResponseStatus responseStatus = walletController.acceptWallet(data);
        if (responseStatus == WalletResponseStatus::Ignored)
            return;
        walletTimeout.stop();
        if (responseStatus == WalletResponseStatus::ProtocolError) {
            updateWalletUi();
            QMessageBox::warning(this, u8("钱包提示"), u8("服务器余额响应不完整，已拒绝更新"));
            return;
        }
        user_.insert("balanceCents", QJsonValue::fromVariant(walletController.balanceCents()));
        user_.insert("balance", walletController.balanceCents() / 100.0);
        refreshMe();
        renderRecharge(data.value("records").toArray());
    } else if (type == "UPDATE_PROFILE") {
        applyUser(data.value("user").toObject());
        QMessageBox::information(this, u8("ChargeHub"), obj.value("message").toString());
    }
}
