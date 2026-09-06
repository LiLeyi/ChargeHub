/**
 * @file chartwidget.cpp
 * @brief 管理端营收折线、空闲柱状、电桩状态饼图
 */
#include "chartwidget.h"

#include <algorithm>
#include <QColor>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QSizePolicy>
#include <QVector>

static QString u8(const char *s) { return QString::fromUtf8(s); }

LineChart::LineChart(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(168);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

/** 记下点与标题，触发重绘。 */
void LineChart::setPoints(const QJsonArray &pts, const QString &title)
{
    points_ = pts;
    title_ = title;
    update();
}

/** 画折线、网格和标题；空数据画「暂无数据」。 */
void LineChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#18181B"));
    p.setPen(QColor("#5EEAD4"));
    p.drawText(QRect(8, 4, width() - 16, 18), Qt::AlignLeft, title_.isEmpty() ? u8("营收趋势") : title_);
    if (points_.size() < 2) {
        p.setPen(QColor("#A1A1AA"));
        p.drawText(rect(), Qt::AlignCenter, u8("暂无数据"));
        return;
    }
    QVector<double> vals;
    QStringList labs;
    for (const auto &v : points_) {
        const auto o = v.toObject();
        vals.append(o.value("amount").toDouble(o.value("y").toDouble()));
        labs.append(o.value("date").toString(o.value("x").toString()));
    }
    double lo = *std::min_element(vals.begin(), vals.end());
    double hi = *std::max_element(vals.begin(), vals.end());
    if (hi - lo < 1e-6)
        hi = lo + 1;
    const QRect plot(40, 26, qMax(20, width() - 52), qMax(20, height() - 48));
    p.setPen(QPen(QColor("#27272A"), 1));
    p.drawRect(plot);
    p.setPen(QColor("#A1A1AA"));
    p.drawText(4, plot.top() + 10, QString::number(hi, 'f', 0));
    p.drawText(4, plot.bottom(), QString::number(lo, 'f', 0));
    QPainterPath path;
    for (int i = 0; i < vals.size(); ++i) {
        const double x = plot.left() + i * double(plot.width()) / (vals.size() - 1);
        const double y = plot.bottom() - (vals[i] - lo) / (hi - lo) * plot.height();
        if (i == 0)
            path.moveTo(x, y);
        else
            path.lineTo(x, y);
    }
    QPainterPath fill = path;
    fill.lineTo(plot.right(), plot.bottom());
    fill.lineTo(plot.left(), plot.bottom());
    fill.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 184, 166, 50));
    p.drawPath(fill);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor("#2DD4BF"), 2.4));
    p.drawPath(path);
    p.setPen(QColor("#71717A"));
    const int step = qMax(1, vals.size() / 6);
    for (int i = 0; i < labs.size(); i += step)
        p.drawText(plot.left() + i * plot.width() / (vals.size() - 1) - 12, height() - 6, labs[i].right(5));
}

BarChart::BarChart(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(168);
    setMaximumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

/** 记下柱数据并重绘。 */
void BarChart::setBars(const QJsonArray &bars)
{
    bars_ = bars;
    update();
}

/** 按 name/value 画柱。 */
void BarChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#18181B"));
    p.setPen(QColor("#5EEAD4"));
    p.drawText(8, 16, u8("各站空闲桩"));
    if (bars_.isEmpty())
        return;
    double hi = 1;
    for (const auto &v : bars_)
        hi = qMax(hi, v.toObject().value("value").toDouble());
    const int n = bars_.size();
    const QRect plot(28, 26, width() - 40, height() - 50);
    const int gap = 8;
    const int bw = qMax(8, (plot.width() - gap * n) / n);
    for (int i = 0; i < n; ++i) {
        const auto o = bars_.at(i).toObject();
        const int h = int(o.value("value").toDouble() / hi * plot.height());
        const QRect r(plot.left() + i * (bw + gap), plot.bottom() - h, bw, h);
        p.fillRect(r, QColor("#14B8A6"));
        p.setPen(QColor("#CCFBF1"));
        p.drawText(QRect(r.x() - 6, plot.bottom() + 2, bw + 12, 16), Qt::AlignCenter, o.value("name").toString().left(4));
    }
}

PieChart::PieChart(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(168);
    setMaximumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

/** 记下扇区并重绘。 */
void PieChart::setSlices(const QJsonArray &slices)
{
    slices_ = slices;
    update();
}

/** 按总和算角度画饼。 */
void PieChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#18181B"));
    p.setPen(QColor("#5EEAD4"));
    p.drawText(8, 16, u8("电桩状态"));
    double sum = 0;
    for (const auto &v : slices_)
        sum += v.toObject().value("value").toDouble();
    if (sum <= 0)
        return;
    const QRect pie(width() / 2 - 54, 28, 108, 108);
    int start = 90 * 16;
    const QColor cols[] = {QColor("#2DD4BF"), QColor("#38BDF8"), QColor("#F87171"), QColor("#FBBF24")};
    int i = 0;
    int y = 28;
    for (const auto &v : slices_) {
        const auto o = v.toObject();
        const int span = int(o.value("value").toDouble() / sum * 360 * 16);
        p.setBrush(cols[i % 4]);
        p.setPen(Qt::NoPen);
        p.drawPie(pie, start, -span);
        start -= span;
        p.setPen(QColor("#E8F5E9"));
        p.drawText(12, y + 12, QString("%1 %2").arg(o.value("name").toString()).arg(int(o.value("value").toDouble())));
        y += 18;
        ++i;
    }
}
