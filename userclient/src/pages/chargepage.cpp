#include "chargepage.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ChargePage::ChargePage(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(24, 24, 24, 24);

    auto *panel = new QFrame;
    panel->setObjectName(QStringLiteral("card"));
    panel->setMaximumWidth(640);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(28, 24, 28, 24);

    status_ = new QLabel(QString::fromUtf8("暂无进行中的充电"));
    status_->setObjectName(QStringLiteral("title"));
    time_ = new QLabel(QStringLiteral("00:00"));
    time_->setObjectName(QStringLiteral("kpi"));
    time_->setAlignment(Qt::AlignCenter);
    info_ = new QLabel(QString::fromUtf8(
        "在“附近电站”选择电站和空闲桩后开始充电。支持预约占桩 15 分钟。"));
    info_->setObjectName(QStringLiteral("muted"));
    info_->setWordWrap(true);

    stopButton_ = new QPushButton(QString::fromUtf8("结束充电"));
    stopButton_->setObjectName(QStringLiteral("danger"));
    settleButton_ = new QPushButton(QString::fromUtf8("立即结算"));
    auto *homeButton = new QPushButton(QString::fromUtf8("去找桩"));
    homeButton->setObjectName(QStringLiteral("ghost"));

    connect(stopButton_, &QPushButton::clicked, this, &ChargePage::stopRequested);
    connect(settleButton_, &QPushButton::clicked, this, &ChargePage::settleRequested);
    connect(homeButton, &QPushButton::clicked, this, &ChargePage::findPileRequested);

    stopButton_->hide();
    settleButton_->hide();
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(stopButton_);
    buttons->addWidget(settleButton_);
    buttons->addWidget(homeButton);
    buttons->addStretch();

    layout->addWidget(status_);
    layout->addSpacing(8);
    layout->addWidget(time_);
    layout->addSpacing(12);
    layout->addWidget(info_);
    layout->addStretch();
    layout->addLayout(buttons);
    outer->addWidget(panel, 1);
    outer->addStretch(1);
}

void ChargePage::setOrder(const QJsonObject &order)
{
    const QString status = order.value("status").toString();
    status_->setText(status.isEmpty() ? QString::fromUtf8("暂无进行中的充电") : status);
    const int seconds = order.value("seconds").toInt();
    time_->setText(QString("%1:%2")
                       .arg(seconds / 60, 2, 10, QChar('0'))
                       .arg(seconds % 60, 2, 10, QChar('0')));
    info_->setText(QString::fromUtf8("%1  %2\n订单 %3\n电量 %4 kWh\n费用 ¥%5\n%6 kW × %7 元/度")
                       .arg(order.value("stationName").toString(), order.value("pileNo").toString())
                       .arg(order.value("orderNo").toString())
                       .arg(order.value("energyKwh").toDouble(), 0, 'f', 3)
                       .arg(order.value("amount").toDouble(), 0, 'f', 2)
                       .arg(order.value("powerKw").toDouble(), 0, 'f', 0)
                       .arg(order.value("pricePerKwh").toDouble(), 0, 'f', 2));
    stopButton_->setVisible(status == QString::fromUtf8("充电中"));
    settleButton_->setVisible(status == QString::fromUtf8("待结算"));
}

void ChargePage::clearOrder()
{
    status_->setText(QString::fromUtf8("暂无进行中的充电"));
    time_->setText(QStringLiteral("00:00"));
    info_->setText(QString::fromUtf8(
        "在“附近电站”选择电站和空闲桩后开始充电。支持预约占桩 15 分钟。"));
    stopButton_->hide();
    settleButton_->hide();
}
