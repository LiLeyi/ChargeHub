#include "chargepage.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ChargePage::ChargePage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 8, 16, 10);
    layout->setSpacing(8);

    auto *panel = new QFrame;
    panel->setObjectName(QStringLiteral("card"));
    auto *inner = new QVBoxLayout(panel);
    inner->setContentsMargins(14, 14, 14, 14);
    inner->setSpacing(8);

    status_ = new QLabel(QString::fromUtf8("暂无进行中的充电"));
    status_->setObjectName(QStringLiteral("title"));
    status_->setAlignment(Qt::AlignCenter);
    time_ = new QLabel(QStringLiteral("00:00"));
    time_->setObjectName(QStringLiteral("kpi"));
    time_->setAlignment(Qt::AlignCenter);

    auto *stats = new QHBoxLayout;
    stats->setSpacing(10);
    energy_ = new QLabel(QString::fromUtf8("0.000 kWh"));
    energy_->setObjectName(QStringLiteral("kpiSub"));
    energy_->setAlignment(Qt::AlignCenter);
    fee_ = new QLabel(QStringLiteral("¥0.00"));
    fee_->setObjectName(QStringLiteral("kpiSub"));
    fee_->setAlignment(Qt::AlignCenter);
    stats->addWidget(energy_);
    stats->addWidget(fee_);

    info_ = new QLabel(QString::fromUtf8("在「找桩」选空闲桩后开始充电，预约占桩 15 分钟。"));
    info_->setObjectName(QStringLiteral("muted"));
    info_->setWordWrap(true);
    info_->setAlignment(Qt::AlignCenter);

    inner->addWidget(status_);
    inner->addWidget(time_);
    inner->addLayout(stats);
    inner->addWidget(info_);

    stopButton_ = new QPushButton(QString::fromUtf8("结束充电"));
    stopButton_->setObjectName(QStringLiteral("danger"));
    settleButton_ = new QPushButton(QString::fromUtf8("立即结算"));
    settleButton_->setObjectName(QStringLiteral("primary"));
    auto *homeButton = new QPushButton(QString::fromUtf8("去找桩"));
    homeButton->setObjectName(QStringLiteral("ghost"));

    connect(stopButton_, &QPushButton::clicked, this, &ChargePage::stopRequested);
    connect(settleButton_, &QPushButton::clicked, this, &ChargePage::settleRequested);
    connect(homeButton, &QPushButton::clicked, this, &ChargePage::findPileRequested);

    stopButton_->hide();
    settleButton_->hide();

    layout->addWidget(panel, 1);
    layout->addWidget(stopButton_);
    layout->addWidget(settleButton_);
    layout->addWidget(homeButton);
}

void ChargePage::setOrder(const QJsonObject &order)
{
    const QString status = order.value("status").toString();
    status_->setText(status.isEmpty() ? QString::fromUtf8("暂无进行中的充电") : status);
    const int seconds = order.value("seconds").toInt();
    time_->setText(QString("%1:%2")
                       .arg(seconds / 60, 2, 10, QChar('0'))
                       .arg(seconds % 60, 2, 10, QChar('0')));
    energy_->setText(QString::fromUtf8("%1 kWh").arg(order.value("energyKwh").toDouble(), 0, 'f', 3));
    fee_->setText(QString::fromUtf8("¥%1").arg(order.value("amount").toDouble(), 0, 'f', 2));
    const double startupFee = order.value("startupFee").toDouble();
    const QString feeDetail = startupFee > 0
        ? QString::fromUtf8("（含起步价 ¥%1）").arg(startupFee, 0, 'f', 2)
        : QString();
    info_->setText(QString::fromUtf8("%1  ·  %2\n订单 %3\n%4 kW  ×  %5 元/度%6")
                       .arg(order.value("stationName").toString(), order.value("pileNo").toString())
                       .arg(order.value("orderNo").toString())
                       .arg(order.value("powerKw").toDouble(), 0, 'f', 0)
                       .arg(order.value("pricePerKwh").toDouble(), 0, 'f', 2)
                       .arg(feeDetail));
    stopButton_->setVisible(status == QString::fromUtf8("充电中"));
    settleButton_->setVisible(status == QString::fromUtf8("待结算"));
}

void ChargePage::clearOrder()
{
    status_->setText(QString::fromUtf8("暂无进行中的充电"));
    time_->setText(QStringLiteral("00:00"));
    energy_->setText(QString::fromUtf8("0.000 kWh"));
    fee_->setText(QStringLiteral("¥0.00"));
    info_->setText(QString::fromUtf8("在「找桩」选空闲桩后开始充电，预约占桩 15 分钟。"));
    stopButton_->hide();
    settleButton_->hide();
}
