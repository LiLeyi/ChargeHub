#ifndef CHARGEHUB_CHARTWIDGET_H
#define CHARGEHUB_CHARTWIDGET_H

/**
 * @file chartwidget.h
 * @brief 管理端自绘折线 / 柱状 / 饼图，不依赖 Qt Charts。
 */
#include <QJsonArray>
#include <QWidget>
class LineChart : public QWidget {
    Q_OBJECT
public:
    explicit LineChart(QWidget *parent = nullptr);
    void setPoints(const QJsonArray &pts, const QString &title = QString()); ///< pts: [{x,y}] 或数值数组
protected:
    void paintEvent(QPaintEvent *e) override;
private:
    QJsonArray points_;
    QString title_;
};

class BarChart : public QWidget {
    Q_OBJECT
public:
    explicit BarChart(QWidget *parent = nullptr);
    void setBars(const QJsonArray &bars);
protected:
    void paintEvent(QPaintEvent *e) override;
private:
    QJsonArray bars_;
};

class PieChart : public QWidget {
    Q_OBJECT
public:
    explicit PieChart(QWidget *parent = nullptr);
    void setSlices(const QJsonArray &slices);
protected:
    void paintEvent(QPaintEvent *e) override;
private:
    QJsonArray slices_;
};

#endif
