#ifndef CHARGEHUB_MAINWINDOW_H
#define CHARGEHUB_MAINWINDOW_H

/**
 * @file mainwindow.h
 * @brief 运营管理端主窗口：电站、电桩、用户、订单、智能分析。
 *
 * 与 TcpServer 同进程，直接调 Dispatch，不向 8888 再连一次。
 * 打开 Web 大屏只是启动 Flask 并打开浏览器，大屏自己只读 SQLite。
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
    MainWindow(Dispatch *dispatch, QWidget *parent = nullptr);
private slots:
    void refresh();          ///< 刷新表格、KPI、图表
    void rebootPile();       ///< 远程重启选中桩
    void markFault();        ///< 标记选中桩故障
    void freeze(bool on);    ///< 冻结 / 解冻选中用户
    void addStation();       ///< 弹窗新建电站
    void editStation();      ///< 弹窗修改选中电站
    void enableTariff();     ///< 为选中站启用默认分时电价
    void adoptPlan();        ///< 采纳调度建议表中选中行
    void forceStop();        ///< 强制结束选中订单充电
    void forceSettle();      ///< 代结算选中订单
    void genForecast();      ///< 重算智能分析
    void openDash();         ///< 启动只读 Web 大屏
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
