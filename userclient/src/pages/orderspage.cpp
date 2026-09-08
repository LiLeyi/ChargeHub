#include "orderspage.h"

#include <algorithm>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QListView>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QVector>

namespace {

void clearLayout(QLayout *layout)
{
    while (layout->count()) {
        QLayoutItem *item = layout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
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

} // namespace

OrdersPage::OrdersPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 8, 16, 8);
    layout->setSpacing(10);

    filter_ = new QComboBox;
    filter_->addItems({QString::fromUtf8("全部订单"), QString::fromUtf8("待结算优先"),
                       QString::fromUtf8("已完成"), QString::fromUtf8("充电中")});
    filter_->setMinimumHeight(32);
    filter_->setMaxVisibleItems(8);
    auto *view = new QListView(filter_);
    view->setUniformItemSizes(true);
    filter_->setView(view);
    connect(filter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { render(); });
    layout->addWidget(filter_);

    auto *inner = new QWidget;
    inner->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    ordersLayout_ = new QVBoxLayout(inner);
    ordersLayout_->setContentsMargins(0, 0, 8, 0);
    ordersLayout_->setSpacing(10);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(inner);
    layout->addWidget(scroll, 1);
}

void OrdersPage::setOrders(const QJsonArray &orders)
{
    orders_ = orders;
    render();
}

void OrdersPage::render()
{
    clearLayout(ordersLayout_);
    QVector<QJsonObject> rows;
    for (const auto &value : orders_)
        rows.append(value.toObject());

    const int mode = filter_->currentIndex();
    if (mode == 1) {
        std::sort(rows.begin(), rows.end(), [](const QJsonObject &a, const QJsonObject &b) {
            auto rank = [](const QString &status) {
                if (status == QString::fromUtf8("待结算")) return 0;
                if (status == QString::fromUtf8("充电中")) return 1;
                return 2;
            };
            return rank(a.value("status").toString()) < rank(b.value("status").toString());
        });
    }
    if (orders_.isEmpty()) {
        auto *label = new QLabel(QString::fromUtf8("暂无订单，去首页找桩充电吧"));
        label->setObjectName(QStringLiteral("muted"));
        ordersLayout_->addWidget(label);
        ordersLayout_->addStretch();
        return;
    }

    int shown = 0;
    for (const auto &order : rows) {
        const QString status = order.value("status").toString();
        if (mode == 2 && status != QString::fromUtf8("已完成"))
            continue;
        if (mode == 3 && status != QString::fromUtf8("充电中"))
            continue;
        ++shown;

        auto *item = card();
        auto *itemLayout = new QVBoxLayout(item);
        itemLayout->setContentsMargins(16, 12, 16, 12);
        itemLayout->setSpacing(6);
        auto *header = new QHBoxLayout;
        auto *number = new QLabel(order.value("orderNo").toString());
        number->setObjectName(QStringLiteral("cardTitle"));
        auto *badge = new QLabel(status);
        if (status == QString::fromUtf8("已完成"))
            badge->setObjectName(QStringLiteral("pillOk"));
        else if (status == QString::fromUtf8("充电中"))
            badge->setObjectName(QStringLiteral("pillWarn"));
        else
            badge->setObjectName(QStringLiteral("pillOff"));
        header->addWidget(number, 1);
        header->addWidget(badge);
        itemLayout->addLayout(header);
        auto *detail = new QLabel(QString("%1  %2\n电量 %3 kWh    ¥%4")
                                      .arg(order.value("stationName").toString(),
                                           order.value("pileNo").toString())
                                      .arg(order.value("energyKwh").toDouble(), 0, 'f', 3)
                                      .arg(order.value("amount").toDouble(), 0, 'f', 2));
        detail->setObjectName(QStringLiteral("muted"));
        itemLayout->addWidget(detail);
        ordersLayout_->addWidget(item);
    }
    if (shown == 0) {
        auto *label = new QLabel(QString::fromUtf8("当前筛选下暂无订单"));
        label->setObjectName(QStringLiteral("muted"));
        ordersLayout_->addWidget(label);
    }
    ordersLayout_->addStretch();
}
