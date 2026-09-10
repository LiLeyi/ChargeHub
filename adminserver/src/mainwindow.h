#ifndef CHARGEHUB_MAINWINDOW_H
#define CHARGEHUB_MAINWINDOW_H

/**
 * @file mainwindow.h
 * @brief 运营桌面：电站 / 电桩 / 用户 / 订单 / 智能分析 / 审计。
 *
 * 【职责】展示 Dispatch 查出来的表，把按钮转成 Dispatch 调用。
 * 【原理】与 TcpServer 同进程，不连 8888。refresh() 定时拉 KPI 和表格。
 *         弹窗用 UiSheet，只改外观，不改接口字段。
 * 【协作】openDash() 只启动 Flask；大屏自己只读库，不经本窗口写单。
 * 【详见】docs/模块与协作说明.md
 */
#include "chartwidget.h"
#include "dispatch.h"

#include <QComboBox>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    /**
     * 搭导航和各页表格、接按钮到下面这些槽。
     * dispatch 必须已构造（且 TcpServer 已在听）。
     * @param dispatch 业务门面，必须非空且生命周期覆盖窗口。
     * @param parent Qt 父窗口。
     */
    MainWindow(Dispatch *dispatch, QWidget *parent = nullptr);

private slots:
    /**
     * 向 Dispatch 要营收、桩/站/用户/订单、预测、审计，填 KPI、表格和自绘图表。
     * 定时器与手动操作成功后都会调用。
     */
    void refresh();
    /** 对电桩表当前行调用 Dispatch::rebootPile，成功再 refresh。 */
    void rebootPile();
    /** 对选中桩 Dispatch::markPileFault（占用中会先停充）。 */
    void markFault();
    /** @param on true 冻结选中用户，false 解冻；注销用户由 Dispatch 拒绝。 */
    void freeze(bool on);
    /** UiSheet 表单：站名/地址/经纬/电价/桩数 → Dispatch::addStation。 */
    void addStation();
    /** 用选中行填表单，Dispatch::updateStation。 */
    void editStation();
    /** 选中站写入默认谷平峰，Dispatch::applyDefaultTariff。 */
    void enableTariff();
    /** 调度建议表选中行，Dispatch::adoptDispatchPlan（峰价上浮）。 */
    void adoptPlan();
    /** 订单表选中行，Dispatch::forceStopOrder。 */
    void forceStop();
    /** 订单表选中行，Dispatch::forceSettleOrder。 */
    void forceSettle();
    /** Dispatch::refreshForecast 重算分析表，再 refresh 界面。 */
    void genForecast();
    /**
     * 「打开 Web 大屏」：若 5000 未监听则启动 dashboard/app.py，再打开浏览器。
     * 大屏自己只读同一份 SQLite，不经本窗口写单。
     */
    void openDash();

private:
    Dispatch *dispatch_;
    QListWidget *nav_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    QLabel *kToday_ = nullptr;
    QLabel *kMonth_ = nullptr;
    QLabel *kTotal_ = nullptr;
    QLabel *mlMae_ = nullptr;
    QLabel *mlRmse_ = nullptr;
    QLabel *mlWeather_ = nullptr;
    QLabel *mlHint_ = nullptr;
    QLabel *nlpCount_ = nullptr;
    QLabel *nlpPos_ = nullptr;
    QLabel *nlpNeu_ = nullptr;
    QLabel *nlpNeg_ = nullptr;
    QLabel *nlpKeys_ = nullptr;
    LineChart *chart_ = nullptr;
    LineChart *dashChart_ = nullptr;
    LineChart *hourlyChart_ = nullptr;
    PieChart *statusPie_ = nullptr;
    PieChart *dashPie_ = nullptr;
    BarChart *idleBar_ = nullptr;
    QComboBox *range_ = nullptr;
    QComboBox *pileFilter_ = nullptr;
    QTableWidget *statusTable_ = nullptr;
    QTableWidget *pileTable_ = nullptr;
    QTableWidget *stationTable_ = nullptr;
    QTableWidget *userTable_ = nullptr;
    QTableWidget *forecastTable_ = nullptr;
    QTableWidget *idleTable_ = nullptr;
    QTableWidget *riskTable_ = nullptr;
    QTableWidget *planTable_ = nullptr;
    QTableWidget *alertTable_ = nullptr;
    QTableWidget *nlpTable_ = nullptr;
    QTableWidget *orderTable_ = nullptr;
    QTableWidget *auditTable_ = nullptr;
    QLineEdit *userKw_ = nullptr;
    QLineEdit *orderKw_ = nullptr;
};

#endif
