/**
 * @file mainwindow.cpp
 * @brief 管理端界面：导航、表格、图表刷新与运营操作
 */
#include "mainwindow.h"

#include <algorithm>
#include <QAbstractItemView>
#include <QVector>
#include <QColor>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QMap>
#include <QListView>
#include <QMessageBox>
#include <QPair>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

static QString u8(const char *s) { return QString::fromUtf8(s); }

static void prepCombo(QComboBox *c)
{
    c->setMaxVisibleItems(8);
    c->setMinimumHeight(36);
    c->setFocusPolicy(Qt::StrongFocus);
    c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    c->setMinimumContentsLength(4);
    c->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *view = new QListView(c);
    view->setUniformItemSizes(true);
    view->setMinimumHeight(36);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setTextElideMode(Qt::ElideRight);
    c->setView(view);
}

static void asPage(QWidget *w)
{
    w->setObjectName("page");
    w->setAttribute(Qt::WA_StyledBackground, true);
    w->setAutoFillBackground(true);
}

static QTableWidget *makeTable(const QStringList &headers)
{
    auto *t = new QTableWidget(0, headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setAlternatingRowColors(true);
    t->verticalHeader()->setVisible(false);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->verticalHeader()->setDefaultSectionSize(36);
    t->setShowGrid(false);
    t->setMinimumHeight(160);
    t->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    t->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    t->setAttribute(Qt::WA_StyledBackground, true);
    t->viewport()->setAutoFillBackground(true);
    return t;
}

static QLabel *kpi(const QString &text)
{
    auto *k = new QLabel(text);
    k->setObjectName("kpi");
    k->setWordWrap(true);
    return k;
}

static QFrame *panel(const QString &title, QWidget *inner, int maxH = 0)
{
    auto *f = new QFrame;
    f->setObjectName("card");
    f->setAttribute(Qt::WA_StyledBackground, true);
    f->setAutoFillBackground(true);
    auto *l = new QVBoxLayout(f);
    l->setContentsMargins(12, 10, 12, 10);
    l->setSpacing(8);
    auto *t = new QLabel(title);
    t->setObjectName("title");
    l->addWidget(t);
    if (maxH > 0)
        inner->setMaximumHeight(maxH);
    l->addWidget(inner, 1);
    return f;
}

MainWindow::MainWindow(Dispatch *dispatch, QWidget *parent)
    : QMainWindow(parent), dispatch_(dispatch)
{
    setWindowTitle(u8("ChargeHub · 充电桩运营管理平台"));
    resize(1400, 860);
    auto *root = new QWidget;
    root->setObjectName("root");
    setCentralWidget(root);
    auto *body = new QHBoxLayout(root);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    auto *side = new QFrame;
    side->setObjectName("sidebar");
    side->setFixedWidth(220);
    auto *sidel = new QVBoxLayout(side);
    sidel->setContentsMargins(16, 22, 12, 16);
    sidel->setSpacing(4);
    auto *brand = new QLabel(u8("ChargeHub"));
    brand->setObjectName("brandMark");
    auto *sub = new QLabel(u8("运营后台"));
    sub->setObjectName("brandSub");
    sidel->addWidget(brand);
    sidel->addWidget(sub);
    sidel->addSpacing(16);
    nav_ = new QListWidget;
    nav_->setObjectName("nav");
    nav_->setFocusPolicy(Qt::NoFocus);
    nav_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    nav_->addItems({u8("销售业绩"), u8("电桩状态"), u8("充电桩管理"), u8("充电站管理"),
                    u8("用户管理"), u8("订单跟踪"), u8("运营决策大屏"), u8("智能分析")});
    sidel->addWidget(nav_, 1);
    stack_ = new QStackedWidget;
    body->addWidget(side);
    body->addWidget(stack_, 1);
    connect(nav_, &QListWidget::currentRowChanged, stack_, &QStackedWidget::setCurrentIndex);

    auto *sales = new QWidget;
    auto *sl = new QVBoxLayout(sales);
    sl->setContentsMargins(24, 20, 24, 20);
    sl->setSpacing(14);
    auto *sbar = new QFrame;
    sbar->setObjectName("toolbar");
    auto *sbl = new QHBoxLayout(sbar);
    sbl->setContentsMargins(16, 12, 16, 12);
    auto *stitle = new QLabel(u8("销售业绩"));
    stitle->setObjectName("title");
    auto *sdesc = new QLabel(u8("仅统计已完成订单"));
    sdesc->setObjectName("muted");
    range_ = new QComboBox;
    range_->addItems({u8("近7日"), u8("近30日")});
    range_->setMinimumWidth(120);
    range_->setMaximumWidth(140);
    prepCombo(range_);
    connect(range_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::refresh);
    sbl->addWidget(stitle);
    sbl->addSpacing(10);
    sbl->addWidget(sdesc);
    sbl->addStretch(1);
    sbl->addWidget(range_);
    sl->addWidget(sbar);
    auto *krow = new QHBoxLayout;
    krow->setSpacing(12);
    kToday_ = kpi(u8("今日营收"));
    kMonth_ = kpi(u8("本月营收"));
    kTotal_ = kpi(u8("累计营收"));
    for (auto *k : {kToday_, kMonth_, kTotal_})
        krow->addWidget(k);
    sl->addLayout(krow);
    chart_ = new LineChart;
    sl->addWidget(chart_, 1);
    asPage(sales);
    stack_->addWidget(sales);

    auto *st = new QWidget;
    auto *stl = new QHBoxLayout(st);
    auto *left = new QVBoxLayout;
    left->addWidget(new QLabel(u8("电桩状态分布 · 设备健康度")));
    statusTable_ = makeTable({u8("状态"), u8("数量"), u8("占比")});
    left->addWidget(statusTable_);
    stl->addLayout(left, 1);
    statusPie_ = new PieChart;
    stl->addWidget(statusPie_, 1);
    asPage(st);
    stack_->addWidget(st);

    auto *pl = new QWidget;
    auto *pll = new QVBoxLayout(pl);
    auto *pbar = new QFrame;
    pbar->setObjectName("toolbar");
    auto *pbl = new QHBoxLayout(pbar);
    pbl->setContentsMargins(16, 10, 16, 10);
    auto *plab = new QLabel(u8("充电桩管理"));
    plab->setObjectName("title");
    pileFilter_ = new QComboBox;
    pileFilter_->addItems({u8("全部状态"), u8("闲置"), u8("在用"), u8("故障"), u8("快充"), u8("慢充")});
    pileFilter_->setMinimumWidth(140);
    pileFilter_->setMaximumWidth(180);
    prepCombo(pileFilter_);
    connect(pileFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::refresh);
    pbl->addWidget(plab);
    pbl->addStretch(1);
    pbl->addWidget(pileFilter_);
    auto *reboot = new QPushButton(u8("远程重启选中电桩"));
    connect(reboot, &QPushButton::clicked, this, &MainWindow::rebootPile);
    pbl->addWidget(reboot);
    pll->addWidget(pbar);
    pileTable_ = makeTable({u8("编号"), u8("电站"), u8("类型"), u8("功率kW"), u8("状态"), u8("累计次数"), u8("累计时长(分)")});
    pll->addWidget(pileTable_);
    asPage(pl);
    stack_->addWidget(pl);

    auto *sn = new QWidget;
    auto *snl = new QVBoxLayout(sn);
    auto *stationBar = new QHBoxLayout;
    stationBar->addWidget(new QLabel(u8("充电站管理")));
    stationBar->addStretch();
    auto *add = new QPushButton(u8("新增电站"));
    connect(add, &QPushButton::clicked, this, &MainWindow::addStation);
    stationBar->addWidget(add);
    snl->addLayout(stationBar);
    stationTable_ = makeTable({"ID", u8("站名"), u8("地址"), u8("经纬度"), u8("桩数"), u8("在线率")});
    snl->addWidget(stationTable_);
    asPage(sn);
    stack_->addWidget(sn);

    auto *us = new QWidget;
    auto *usl = new QVBoxLayout(us);
    auto *ubar = new QHBoxLayout;
    userKw_ = new QLineEdit;
    userKw_->setPlaceholderText(u8("手机号模糊搜索"));
    auto *search = new QPushButton(u8("查询"));
    auto *fz = new QPushButton(u8("冻结"));
    auto *uf = new QPushButton(u8("解冻"));
    uf->setObjectName("ghost");
    connect(search, &QPushButton::clicked, this, &MainWindow::refresh);
    connect(fz, &QPushButton::clicked, this, [this] { freeze(true); });
    connect(uf, &QPushButton::clicked, this, [this] { freeze(false); });
    ubar->addWidget(new QLabel(u8("用户管理")));
    ubar->addWidget(userKw_, 1);
    ubar->addWidget(search);
    ubar->addWidget(fz);
    ubar->addWidget(uf);
    usl->addLayout(ubar);
    userTable_ = makeTable({"ID", u8("手机号"), u8("昵称"), u8("头像"), u8("余额"), u8("住址"), u8("状态"), u8("禁用原因")});
    usl->addWidget(userTable_);
    asPage(us);
    stack_->addWidget(us);

    auto *od = new QWidget;
    auto *ol = new QVBoxLayout(od);
    auto *obar = new QHBoxLayout;
    orderKw_ = new QLineEdit;
    orderKw_->setPlaceholderText(u8("订单号 / 手机号"));
    auto *os = new QPushButton(u8("查询订单"));
    connect(os, &QPushButton::clicked, this, &MainWindow::refresh);
    obar->addWidget(new QLabel(u8("充电订单跟踪")));
    obar->addWidget(orderKw_, 1);
    obar->addWidget(os);
    ol->addLayout(obar);
    orderTable_ = makeTable({u8("订单号"), u8("手机号"), u8("电站"), u8("电桩"), u8("状态"), u8("电量"), u8("金额"), u8("开始时间")});
    ol->addWidget(orderTable_);
    asPage(od);
    stack_->addWidget(od);

    auto *dash = new QWidget;
    auto *dl = new QVBoxLayout(dash);
    auto *dbar = new QHBoxLayout;
    dbar->addWidget(new QLabel(u8("运营决策大屏（内嵌精简视图，完整图表见 Web）")));
    dbar->addStretch();
    auto *open = new QPushButton(u8("打开 Web 大屏"));
    connect(open, &QPushButton::clicked, this, &MainWindow::openDash);
    dbar->addWidget(open);
    dl->addLayout(dbar);
    dashChart_ = new LineChart;
    dashChart_->setMaximumHeight(230);
    dashPie_ = new PieChart;
    dashPie_->setMaximumHeight(230);
    idleBar_ = new BarChart;
    idleBar_->setMaximumHeight(230);
    auto *g = new QGridLayout;
    g->addWidget(dashChart_, 0, 0);
    g->addWidget(dashPie_, 0, 1);
    g->addWidget(idleBar_, 1, 0);
    idleTable_ = makeTable({u8("电站"), u8("空闲桩"), u8("总桩数")});
    g->addWidget(idleTable_, 1, 1);
    dl->addLayout(g);
    asPage(dash);
    stack_->addWidget(dash);

    auto *scroll = new QScrollArea;
    scroll->setObjectName("mlScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setAttribute(Qt::WA_StyledBackground, true);
    scroll->setAutoFillBackground(true);
    scroll->viewport()->setAutoFillBackground(true);
    scroll->viewport()->setObjectName("page");
    auto *ml = new QWidget;
    asPage(ml);
    ml->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    scroll->setWidget(ml);
    auto *mlay = new QVBoxLayout(ml);
    mlay->setContentsMargins(20, 16, 20, 16);
    mlay->setSpacing(12);
    auto *mbar = new QFrame;
    mbar->setObjectName("toolbar");
    mbar->setAttribute(Qt::WA_StyledBackground, true);
    auto *mbl = new QHBoxLayout(mbar);
    mbl->setContentsMargins(16, 10, 16, 10);
    auto *mt = new QLabel(u8("智能分析"));
    mt->setObjectName("title");
    auto *ms = new QLabel(u8("负荷预测 · 故障风险 · 评价 NLP，均在管理端 C++ 计算"));
    ms->setObjectName("muted");
    auto *gen = new QPushButton(u8("刷新分析"));
    connect(gen, &QPushButton::clicked, this, &MainWindow::genForecast);
    mbl->addWidget(mt);
    mbl->addSpacing(10);
    mbl->addWidget(ms, 1);
    mbl->addWidget(gen);
    mlay->addWidget(mbar);
    auto *mk = new QHBoxLayout;
    mk->setSpacing(12);
    mlMae_ = kpi(u8("MAE"));
    mlRmse_ = kpi(u8("RMSE"));
    mlWeather_ = kpi(u8("天气因子"));
    mk->addWidget(mlMae_);
    mk->addWidget(mlRmse_);
    mk->addWidget(mlWeather_);
    mlay->addLayout(mk);
    mlHint_ = new QLabel;
    mlHint_->setObjectName("muted");
    mlHint_->setWordWrap(true);
    mlay->addWidget(mlHint_);

    auto *nlpCard = new QFrame;
    nlpCard->setObjectName("card");
    nlpCard->setAttribute(Qt::WA_StyledBackground, true);
    nlpCard->setAutoFillBackground(true);
    auto *nlpLay = new QVBoxLayout(nlpCard);
    nlpLay->setContentsMargins(14, 12, 14, 12);
    nlpLay->setSpacing(10);
    auto *nlpTitle = new QLabel(u8("用户评价 NLP"));
    nlpTitle->setObjectName("title");
    auto *nlpSub = new QLabel(u8("读取 review_doc 文档库，词典情感 + 领域关键词统计（不改订单与余额）"));
    nlpSub->setObjectName("muted");
    nlpSub->setWordWrap(true);
    nlpLay->addWidget(nlpTitle);
    nlpLay->addWidget(nlpSub);
    auto *nk = new QHBoxLayout;
    nk->setSpacing(12);
    nlpCount_ = kpi(u8("评价文档"));
    nlpPos_ = kpi(u8("正面"));
    nlpNeu_ = kpi(u8("中性"));
    nlpNeg_ = kpi(u8("负面"));
    nk->addWidget(nlpCount_);
    nk->addWidget(nlpPos_);
    nk->addWidget(nlpNeu_);
    nk->addWidget(nlpNeg_);
    nlpLay->addLayout(nk);
    nlpKeys_ = new QLabel(u8("高频词 --"));
    nlpKeys_->setObjectName("muted");
    nlpKeys_->setWordWrap(true);
    nlpLay->addWidget(nlpKeys_);
    nlpTable_ = makeTable({u8("桩号"), u8("电站"), u8("评价数"), u8("均分"), u8("正面"), u8("中性"), u8("负面"), u8("关键词")});
    nlpTable_->setMaximumHeight(240);
    nlpLay->addWidget(nlpTable_);
    mlay->addWidget(nlpCard);

    hourlyChart_ = new LineChart;
    hourlyChart_->setMinimumHeight(200);
    hourlyChart_->setMaximumHeight(240);
    alertTable_ = makeTable({u8("级别"), u8("标题"), u8("详情"), u8("时间")});
    forecastTable_ = makeTable({u8("电站"), u8("窗口"), u8("预测电量kWh"), u8("预测空闲"), u8("高峰"), u8("时间")});
    riskTable_ = makeTable({u8("桩号"), u8("电站"), u8("分数"), u8("等级"), u8("原因")});
    planTable_ = makeTable({u8("优先级"), u8("电站"), u8("6h负荷"), u8("建议")});
    auto *mlg = new QGridLayout;
    mlg->setHorizontalSpacing(12);
    mlg->setVerticalSpacing(12);
    mlg->setColumnStretch(0, 3);
    mlg->setColumnStretch(1, 2);
    mlg->addWidget(panel(u8("全站 24 小时负荷"), hourlyChart_), 0, 0);
    mlg->addWidget(panel(u8("综合告警"), alertTable_, 220), 0, 1);
    mlg->addWidget(panel(u8("1 / 6 / 24 小时负荷预测"), forecastTable_, 220), 1, 0, 1, 2);
    mlg->addWidget(panel(u8("设备故障风险"), riskTable_, 200), 2, 0);
    mlg->addWidget(panel(u8("需求调度建议"), planTable_, 200), 2, 1);
    mlay->addLayout(mlg);
    asPage(scroll);
    stack_->addWidget(scroll);

    nav_->setCurrentRow(0);
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::refresh);
    timer->start(4000);
    dispatch_->refreshForecast();
    refresh();
}

void MainWindow::refresh()
{
    const QJsonObject s = dispatch_->salesSummary();
    kToday_->setText(u8("今日营收\n¥ ") + QString::number(s.value("today").toDouble(), 'f', 2));
    kMonth_->setText(u8("本月营收\n¥ ") + QString::number(s.value("month").toDouble(), 'f', 2));
    kTotal_->setText(u8("累计营收\n¥ ") + QString::number(s.value("total").toDouble(), 'f', 2));
    const QJsonArray trend = s.value("trend").toArray();
    QJsonArray slice;
    const int start = range_->currentIndex() == 0 ? qMax(0, trend.size() - 7) : 0;
    for (int i = start; i < trend.size(); ++i)
        slice.append(trend.at(i));
    chart_->setPoints(slice, range_->currentText() + u8("营收"));
    dashChart_->setPoints(slice, u8("营收趋势"));

    const QJsonArray items = dispatch_->pileStatusStats().value("items").toArray();
    statusTable_->setRowCount(items.size());
    QJsonArray pies;
    for (int i = 0; i < items.size(); ++i) {
        const auto o = items.at(i).toObject();
        statusTable_->setItem(i, 0, new QTableWidgetItem(o.value("status").toString()));
        statusTable_->setItem(i, 1, new QTableWidgetItem(QString::number(o.value("count").toInt())));
        statusTable_->setItem(i, 2, new QTableWidgetItem(QString::number(o.value("percent").toDouble(), 'f', 1) + "%"));
        pies.append(QJsonObject{{"name", o.value("status")}, {"value", o.value("count")}});
    }
    statusPie_->setSlices(pies);
    dashPie_->setSlices(pies);

    const QString filt = pileFilter_->currentIndex() == 0 ? QString() : pileFilter_->currentText();
    const auto piles = dispatch_->listPiles();
    QVector<QVariantMap> shown;
    for (const auto &p : piles) {
        if (filt.isEmpty() || p.value("status").toString() == filt || p.value("type").toString() == filt)
            shown.append(p);
    }
    pileTable_->setRowCount(shown.size());
    for (int i = 0; i < shown.size(); ++i) {
        const auto &p = shown[i];
        pileTable_->setItem(i, 0, new QTableWidgetItem(p.value("pile_no").toString()));
        pileTable_->item(i, 0)->setData(Qt::UserRole, p.value("id"));
        pileTable_->setItem(i, 1, new QTableWidgetItem(p.value("station_name").toString()));
        pileTable_->setItem(i, 2, new QTableWidgetItem(p.value("type").toString()));
        pileTable_->setItem(i, 3, new QTableWidgetItem(QString::number(p.value("power_kw").toDouble())));
        pileTable_->setItem(i, 4, new QTableWidgetItem(p.value("status").toString()));
        pileTable_->setItem(i, 5, new QTableWidgetItem(QString::number(p.value("total_charge_count").toInt())));
        pileTable_->setItem(i, 6, new QTableWidgetItem(QString::number(p.value("total_charge_minutes").toInt())));
    }

    const auto stations = dispatch_->listStations();
    stationTable_->setRowCount(stations.size());
    idleTable_->setRowCount(stations.size());
    QJsonArray bars;
    for (int i = 0; i < stations.size(); ++i) {
        const auto &r = stations[i];
        stationTable_->setItem(i, 0, new QTableWidgetItem(r.value("id").toString()));
        stationTable_->setItem(i, 1, new QTableWidgetItem(r.value("name").toString()));
        stationTable_->setItem(i, 2, new QTableWidgetItem(r.value("address").toString()));
        stationTable_->setItem(i, 3, new QTableWidgetItem(
            QString("%1,%2").arg(r.value("lng").toDouble(), 0, 'f', 4).arg(r.value("lat").toDouble(), 0, 'f', 4)));
        stationTable_->setItem(i, 4, new QTableWidgetItem(r.value("totalPiles").toString()));
        stationTable_->setItem(i, 5, new QTableWidgetItem(QString::number(r.value("onlineRate").toDouble(), 'f', 1) + "%"));
        int idle = 0, total = 0;
        for (const auto &p : piles) {
            if (p.value("station_id").toInt() != r.value("id").toInt())
                continue;
            ++total;
            if (p.value("status").toString() == u8("闲置"))
                ++idle;
        }
        idleTable_->setItem(i, 0, new QTableWidgetItem(r.value("name").toString()));
        idleTable_->setItem(i, 1, new QTableWidgetItem(QString::number(idle)));
        idleTable_->setItem(i, 2, new QTableWidgetItem(QString::number(total)));
        bars.append(QJsonObject{{"name", r.value("name").toString()}, {"value", idle}});
    }
    idleBar_->setBars(bars);

    const auto users = dispatch_->listUsers(userKw_->text().trimmed());
    userTable_->setRowCount(users.size());
    for (int i = 0; i < users.size(); ++i) {
        const auto &u = users[i];
        userTable_->setItem(i, 0, new QTableWidgetItem(u.value("id").toString()));
        userTable_->setItem(i, 1, new QTableWidgetItem(u.value("phone").toString()));
        userTable_->setItem(i, 2, new QTableWidgetItem(u.value("nickname").toString()));
        userTable_->setItem(i, 3, new QTableWidgetItem(u.value("has_avatar").toInt() ? u8("已上传") : u8("默认")));
        userTable_->setItem(i, 4, new QTableWidgetItem(QString::number(u.value("balance").toDouble(), 'f', 2)));
        userTable_->setItem(i, 5, new QTableWidgetItem(u.value("address").toString()));
        userTable_->setItem(i, 6, new QTableWidgetItem(u.value("status").toString()));
        userTable_->setItem(i, 7, new QTableWidgetItem(u.value("close_reason").toString()));
    }

    const auto orders = dispatch_->listAdminOrders(orderKw_->text().trimmed());
    orderTable_->setRowCount(orders.size());
    for (int i = 0; i < orders.size(); ++i) {
        const auto &o = orders[i];
        orderTable_->setItem(i, 0, new QTableWidgetItem(o.value("order_no").toString()));
        orderTable_->setItem(i, 1, new QTableWidgetItem(o.value("phone").toString()));
        orderTable_->setItem(i, 2, new QTableWidgetItem(o.value("station_name").toString()));
        orderTable_->setItem(i, 3, new QTableWidgetItem(o.value("pile_no").toString()));
        orderTable_->setItem(i, 4, new QTableWidgetItem(o.value("status").toString()));
        orderTable_->setItem(i, 5, new QTableWidgetItem(QString::number(o.value("energy_kwh").toDouble(), 'f', 3)));
        orderTable_->setItem(i, 6, new QTableWidgetItem(QString::number(o.value("amount").toDouble(), 'f', 2)));
        orderTable_->setItem(i, 7, new QTableWidgetItem(o.value("start_time").toString()));
    }

    const auto fcs = dispatch_->listForecasts();
    forecastTable_->setRowCount(fcs.size());
    for (int i = 0; i < fcs.size(); ++i) {
        const auto &f = fcs[i];
        forecastTable_->setItem(i, 0, new QTableWidgetItem(f.value("name").toString()));
        forecastTable_->setItem(i, 1, new QTableWidgetItem(QString::number(f.value("horizon_hours").toInt()) + "h"));
        forecastTable_->setItem(i, 2, new QTableWidgetItem(QString::number(f.value("pred_kwh").toDouble(), 'f', 2)));
        forecastTable_->setItem(i, 3, new QTableWidgetItem(QString::number(f.value("pred_idle").toInt())));
        forecastTable_->setItem(i, 4, new QTableWidgetItem(f.value("peak_hour").toString()));
        forecastTable_->setItem(i, 5, new QTableWidgetItem(f.value("created_at").toString()));
    }

    QMap<int, double> hourSum;
    for (const auto &h : dispatch_->listHourlyLoad())
        hourSum[h.value("hour").toInt()] += h.value("pred_kwh").toDouble();
    QJsonArray hourly;
    for (int i = 0; i < 24; ++i)
        hourly.append(QJsonObject{{"date", QString("%1:00").arg(i, 2, 10, QChar('0'))}, {"amount", hourSum.value(i)}});
    hourlyChart_->setPoints(hourly, u8("全站未来24小时负荷曲线 (kWh)"));

    const auto risks = dispatch_->listFaultRisks();
    riskTable_->setRowCount(qMin(12, risks.size()));
    for (int i = 0; i < riskTable_->rowCount(); ++i) {
        const auto &f = risks[i];
        riskTable_->setItem(i, 0, new QTableWidgetItem(f.value("pile_no").toString()));
        riskTable_->setItem(i, 1, new QTableWidgetItem(f.value("station").toString()));
        riskTable_->setItem(i, 2, new QTableWidgetItem(QString::number(f.value("score").toDouble(), 'f', 2)));
        riskTable_->setItem(i, 3, new QTableWidgetItem(f.value("level").toString()));
        riskTable_->setItem(i, 4, new QTableWidgetItem(f.value("reason").toString()));
    }

    const auto plans = dispatch_->listDispatchPlan();
    planTable_->setRowCount(plans.size());
    for (int i = 0; i < plans.size(); ++i) {
        const auto &f = plans[i];
        planTable_->setItem(i, 0, new QTableWidgetItem(QString::number(f.value("priority").toInt())));
        planTable_->setItem(i, 1, new QTableWidgetItem(f.value("station").toString()));
        planTable_->setItem(i, 2, new QTableWidgetItem(QString::number(f.value("recommend").toDouble(), 'f', 1)));
        planTable_->setItem(i, 3, new QTableWidgetItem(f.value("reason").toString()));
    }

    const auto alerts = dispatch_->listAlerts();
    alertTable_->setRowCount(alerts.size());
    for (int i = 0; i < alerts.size(); ++i) {
        const auto &f = alerts[i];
        alertTable_->setItem(i, 0, new QTableWidgetItem(f.value("level").toString()));
        alertTable_->setItem(i, 1, new QTableWidgetItem(f.value("title").toString()));
        alertTable_->setItem(i, 2, new QTableWidgetItem(f.value("detail").toString()));
        alertTable_->setItem(i, 3, new QTableWidgetItem(f.value("created_at").toString()));
    }

    if (nlpTable_) {
        const auto nlp = dispatch_->listReviewNlp();
        nlpTable_->setRowCount(nlp.size());
        int docs = 0, pos = 0, neu = 0, neg = 0;
        QMap<QString, int> kwc;
        for (int i = 0; i < nlp.size(); ++i) {
            const auto &f = nlp[i];
            nlpTable_->setItem(i, 0, new QTableWidgetItem(f.value("pile_no").toString()));
            nlpTable_->setItem(i, 1, new QTableWidgetItem(f.value("station").toString()));
            nlpTable_->setItem(i, 2, new QTableWidgetItem(QString::number(f.value("count").toInt())));
            nlpTable_->setItem(i, 3, new QTableWidgetItem(QString::number(f.value("avg").toDouble(), 'f', 1)));
            nlpTable_->setItem(i, 4, new QTableWidgetItem(QString::number(f.value("positive").toInt())));
            nlpTable_->setItem(i, 5, new QTableWidgetItem(QString::number(f.value("neutral").toInt())));
            nlpTable_->setItem(i, 6, new QTableWidgetItem(QString::number(f.value("negative").toInt())));
            nlpTable_->setItem(i, 7, new QTableWidgetItem(f.value("keywords").toString()));
            docs += f.value("count").toInt();
            pos += f.value("positive").toInt();
            neu += f.value("neutral").toInt();
            neg += f.value("negative").toInt();
            const auto parts = f.value("keywords").toString().split(QString::fromUtf8("·"), Qt::SkipEmptyParts);
            for (auto w : parts) {
                w = w.trimmed();
                if (!w.isEmpty())
                    kwc[w] += 1;
            }
        }
        if (nlpCount_)
            nlpCount_->setText(u8("评价文档\n") + QString::number(docs) + u8(" 条 / ")
                               + QString::number(nlp.size()) + u8(" 桩"));
        if (nlpPos_)
            nlpPos_->setText(u8("正面\n") + QString::number(pos)
                             + (docs ? QString("  (%1%)").arg(qRound(100.0 * pos / docs)) : QString()));
        if (nlpNeu_)
            nlpNeu_->setText(u8("中性\n") + QString::number(neu)
                             + (docs ? QString("  (%1%)").arg(qRound(100.0 * neu / docs)) : QString()));
        if (nlpNeg_)
            nlpNeg_->setText(u8("负面\n") + QString::number(neg)
                             + (docs ? QString("  (%1%)").arg(qRound(100.0 * neg / docs)) : QString()));
        if (nlpKeys_) {
            QList<QPair<int, QString>> ranked;
            for (auto it = kwc.begin(); it != kwc.end(); ++it)
                ranked.append({it.value(), it.key()});
            std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
            QStringList top;
            for (int i = 0; i < qMin(8, ranked.size()); ++i)
                top.append(QString("%1 ×%2").arg(ranked[i].second).arg(ranked[i].first));
            nlpKeys_->setText(top.isEmpty() ? u8("高频词  暂无（用户评价后自动聚合）")
                                            : (u8("高频词  ") + top.join(u8("   ·   "))));
        }
    }

    const auto rep = dispatch_->latestReport();
    if (!rep.isEmpty()) {
        mlMae_->setText(u8("MAE\n") + QString::number(rep.value("mae").toDouble(), 'f', 3));
        mlRmse_->setText(u8("RMSE\n") + QString::number(rep.value("rmse").toDouble(), 'f', 3));
        mlWeather_->setText(u8("天气 / 模型\n") + rep.value("weather").toString() + "  " + rep.value("model_version").toString());
        mlHint_->setText(u8("样本 %1 条 · 时间序 8:2 验证 · 禁止随机打乱 · 只写分析表，不改订单与余额")
                             .arg(rep.value("sample_n").toInt()));
    }
}

void MainWindow::rebootPile()
{
    const int row = pileTable_->currentRow();
    if (row < 0) {
        QMessageBox::information(this, u8("提示"), u8("请先选中电桩"));
        return;
    }
    const int id = pileTable_->item(row, 0)->data(Qt::UserRole).toInt();
    QMessageBox::information(this, u8("远程重启"), dispatch_->rebootPile(id));
    refresh();
}

void MainWindow::freeze(bool on)
{
    const int row = userTable_->currentRow();
    if (row < 0) {
        QMessageBox::information(this, u8("提示"), u8("请先选中用户"));
        return;
    }
    if (userTable_->item(row, 6) && userTable_->item(row, 6)->text() == u8("注销")) {
        QMessageBox::information(this, u8("提示"), u8("该账号已注销留档，不能再冻结或解冻"));
        return;
    }
    dispatch_->freezeUser(userTable_->item(row, 0)->text().toInt(), on);
    refresh();
}

void MainWindow::addStation()
{
    QDialog dlg(this);
    dlg.setWindowTitle(u8("新增电站"));
    auto *form = new QFormLayout(&dlg);
    auto *name = new QLineEdit;
    auto *addr = new QLineEdit;
    auto *lng = new QLineEdit("116.35");
    auto *lat = new QLineEdit("39.96");
    auto *n = new QSpinBox;
    n->setRange(1, 20);
    n->setValue(4);
    form->addRow(u8("站名"), name);
    form->addRow(u8("地址"), addr);
    form->addRow(u8("经度"), lng);
    form->addRow(u8("纬度"), lat);
    form->addRow(u8("电桩数"), n);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QVariantMap data;
    data["name"] = name->text();
    data["address"] = addr->text();
    data["lng"] = lng->text().toDouble();
    data["lat"] = lat->text().toDouble();
    data["pileCount"] = n->value();
    data["pricePerKwh"] = 1.30;
    dispatch_->addStation(data);
    refresh();
}

void MainWindow::genForecast()
{
    const int n = dispatch_->refreshForecast();
    QMessageBox::information(this, u8("智能分析"), u8("已更新 %1 条窗口预测，并重算高峰、故障风险与调度建议").arg(n));
    refresh();
}

void MainWindow::openDash()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString dbPath = QDir(appDir).absoluteFilePath(QStringLiteral("data/chargehub.db"));
    const QStringList scripts{
        QDir(appDir).absoluteFilePath(QStringLiteral("../dashboard/app.py")),
        QDir::homePath() + QStringLiteral("/projects/ChargeHub/dashboard/app.py"),
        QStringLiteral("/home/bit/projects/ChargeHub/dashboard/app.py"),
    };
    QString py;
    for (const auto &p : scripts) {
        if (QFileInfo::exists(p)) {
            py = p;
            break;
        }
    }
    if (!py.isEmpty()) {
        QProcess proc;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("CHARGEHUB_DB"), dbPath);
        proc.setProcessEnvironment(env);
        proc.setWorkingDirectory(QFileInfo(py).absolutePath());
        proc.setProgram(QStringLiteral("python3"));
        proc.setArguments({py});
        proc.startDetached();
    }
    QTimer::singleShot(700, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("http://127.0.0.1:5000")));
    });
}
