#ifndef CHARGEHUB_CHARTWIDGET_H
#define CHARGEHUB_CHARTWIDGET_H

/**
 * @file chartwidget.h
 * @brief 管理端自绘折线 / 柱 / 饼，不依赖 Qt Charts，方便虚拟机编译。
 *
 * 数据来自 MainWindow::refresh → Dispatch 的营收点和桩状态。只负责画，不算钱。
 */
#include <QJsonArray>
#include <QWidget>

/** 营收趋势、分时负荷：横轴类别或序号，纵轴数值。 */
class LineChart : public QWidget {
    Q_OBJECT
public:
    /** @param parent Qt 父控件；初始化最小尺寸。 */
    explicit LineChart(QWidget *parent = nullptr);
    /**
     * 设置折线点。
     * @param pts  [{x,y},…] 或纯数字数组；空则画「暂无数据」
     * @param title 图标题，可空
     */
    void setPoints(const QJsonArray &pts, const QString &title = QString());
protected:
    /** @param e Qt 重绘事件；按点计算坐标轴范围，画折线、网格和标题。 */
    void paintEvent(QPaintEvent *e) override;
private:
    QJsonArray points_;
    QString title_;
};

/** 闲置桩等分类数量：一组 {name,value}。 */
class BarChart : public QWidget {
    Q_OBJECT
public:
    /** @param parent Qt 父控件；初始化最小尺寸。 */
    explicit BarChart(QWidget *parent = nullptr);
    /** @param bars `[{name,value},…]` 或数值数组；保存后触发重绘。 */
    void setBars(const QJsonArray &bars);
protected:
    /** @param e Qt 重绘事件；按最大值归一化柱高并绘制标签。 */
    void paintEvent(QPaintEvent *e) override;
private:
    QJsonArray bars_;
};

/** 电桩闲置/在用/故障占比。 */
class PieChart : public QWidget {
    Q_OBJECT
public:
    /** @param parent Qt 父控件；初始化最小尺寸。 */
    explicit PieChart(QWidget *parent = nullptr);
    /** @param slices `[{name,value},…]`；保存后按总和计算角度并重绘。 */
    void setSlices(const QJsonArray &slices);
protected:
    /** @param e Qt 重绘事件；将各值/总值映射为扇区角度并绘制图例。 */
    void paintEvent(QPaintEvent *e) override;
private:
    QJsonArray slices_;
};

#endif
