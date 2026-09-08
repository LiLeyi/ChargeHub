#include "reservationspage.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
QString u8(const char *text) { return QString::fromUtf8(text); }
void clearLayout(QLayout *layout)
{
    while (layout->count()) {
        QLayoutItem *item = layout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}
QFrame *card()
{
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("card"));
    frame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    return frame;
}
}

ReservationsPage::ReservationsPage(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 18, 24, 16);
    layout->setSpacing(12);
    auto *bar = new QFrame;
    bar->setObjectName(QStringLiteral("toolbar"));
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(16, 12, 16, 12);
    row->setSpacing(10);
    auto *titleColumn = new QVBoxLayout;
    titleColumn->setContentsMargins(0, 0, 0, 0);
    titleColumn->setSpacing(2);
    auto *title = new QLabel(u8("已预约充电桩"));
    title->setObjectName(QStringLiteral("title"));
    auto *subtitle = new QLabel(u8("集中查看尚未到期的预约，避免在附近电站列表中反复查找"));
    subtitle->setObjectName(QStringLiteral("muted"));
    subtitle->setWordWrap(true);
    titleColumn->addWidget(title);
    titleColumn->addWidget(subtitle);
    auto *refreshButton = new QPushButton(u8("刷新"));
    refreshButton->setObjectName(QStringLiteral("ghost"));
    refreshButton->setMaximumWidth(96);
    connect(refreshButton, &QPushButton::clicked, this, &ReservationsPage::refreshRequested);
    row->addLayout(titleColumn, 1);
    row->addWidget(refreshButton);
    layout->addWidget(bar);
    auto *hint = new QLabel(u8("预约默认保留 15 分钟；到期后会自动失效。如需充电，可直接从这里进入对应充电桩。"));
    hint->setObjectName(QStringLiteral("muted"));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *inner = new QWidget;
    inner->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    listLayout_ = new QVBoxLayout(inner);
    listLayout_->setContentsMargins(0, 0, 8, 0);
    listLayout_->setSpacing(10);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    scroll->setWidget(inner);
    layout->addWidget(scroll, 1);
}

void ReservationsPage::setReservations(const QJsonArray &reservations)
{
    reservations_ = reservations;
    render();
}

void ReservationsPage::render()
{
    clearLayout(listLayout_);
    if (reservations_.isEmpty()) {
        auto *empty = new QLabel(u8("当前没有有效预约，去附近电站选择一个充电桩吧。"));
        empty->setObjectName(QStringLiteral("muted"));
        listLayout_->addWidget(empty);
        auto *findButton = new QPushButton(u8("去附近电站"));
        findButton->setObjectName(QStringLiteral("ghost"));
        findButton->setMaximumWidth(140);
        connect(findButton, &QPushButton::clicked, this, &ReservationsPage::findStationsRequested);
        listLayout_->addWidget(findButton, 0, Qt::AlignLeft);
        listLayout_->addStretch();
        return;
    }
    for (const QJsonValue &value : reservations_) {
        const QJsonObject reservation = value.toObject();
        auto *item = card();
        auto *itemLayout = new QVBoxLayout(item);
        itemLayout->setContentsMargins(16, 14, 16, 14);
        itemLayout->setSpacing(8);
        auto *header = new QHBoxLayout;
        auto *name = new QLabel(reservation.value("stationName").toString() + u8("  ·  ")
                                + reservation.value("pileNo").toString());
        name->setObjectName(QStringLiteral("cardTitle"));
        auto *status = new QLabel(u8("预约中"));
        status->setObjectName(QStringLiteral("pillOk"));
        header->addWidget(name, 1);
        header->addWidget(status, 0, Qt::AlignRight | Qt::AlignVCenter);
        itemLayout->addLayout(header);
        const int remaining = reservation.value("remainingSeconds").toInt();
        const QString remainingText = remaining > 0
            ? QString::fromUtf8("剩余 %1 分 %2 秒").arg(remaining / 60).arg(remaining % 60, 2, 10, QChar('0'))
            : u8("即将到期");
        auto *detail = new QLabel(QString::fromUtf8("%1\n%2  ·  %3  ·  %4 kW  ·  ¥%5 / 度\n预约到期：%6（%7）")
                                      .arg(reservation.value("address").toString())
                                      .arg(reservation.value("pileNo").toString())
                                      .arg(reservation.value("type").toString())
                                      .arg(reservation.value("powerKw").toDouble(), 0, 'f', 0)
                                      .arg(reservation.value("pricePerKwh").toDouble(), 0, 'f', 2)
                                      .arg(reservation.value("expireAt").toString(), remainingText));
        detail->setObjectName(QStringLiteral("muted"));
        detail->setWordWrap(true);
        itemLayout->addWidget(detail);
        const int pileId = reservation.value("pileId").toInt();
        const QJsonObject station{{"id", reservation.value("stationId").toInt()},
                                  {"name", reservation.value("stationName").toString()},
                                  {"address", reservation.value("address").toString()},
                                  {"lng", reservation.value("lng").toDouble()},
                                  {"lat", reservation.value("lat").toDouble()},
                                  {"pricePerKwh", reservation.value("pricePerKwh").toDouble()}};
        auto *actions = new QHBoxLayout;
        auto *viewButton = new QPushButton(u8("查看该站电桩"));
        viewButton->setObjectName(QStringLiteral("ghost"));
        viewButton->setMaximumWidth(140);
        connect(viewButton, &QPushButton::clicked, this, [this, station] { emit stationRequested(station); });
        auto *startButton = new QPushButton(u8("立即开始充电"));
        startButton->setMaximumWidth(140);
        startButton->setEnabled(reservation.value("pileStatus").toString() == u8("闲置"));
        connect(startButton, &QPushButton::clicked, this, [this, pileId] { emit chargeRequested(pileId); });
        auto *navigateButton = new QPushButton(u8("位置 / 导航"));
        navigateButton->setObjectName(QStringLiteral("ghost"));
        navigateButton->setMaximumWidth(120);
        connect(navigateButton, &QPushButton::clicked, this, [this, station] { emit navigationRequested(station); });
        auto *cancelButton = new QPushButton(u8("取消预约"));
        cancelButton->setObjectName(QStringLiteral("danger"));
        cancelButton->setMaximumWidth(120);
        connect(cancelButton, &QPushButton::clicked, this, &ReservationsPage::cancelRequested);
        actions->addWidget(viewButton);
        actions->addWidget(startButton);
        actions->addWidget(navigateButton);
        actions->addWidget(cancelButton);
        actions->addStretch();
        itemLayout->addLayout(actions);
        listLayout_->addWidget(item);
    }
    listLayout_->addStretch();
}
