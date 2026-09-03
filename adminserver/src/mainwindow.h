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
    void refresh();
    void rebootPile();
    void freeze(bool on);
    void addStation();
    void genForecast();
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
    QLineEdit *userKw_ = nullptr;
    QLineEdit *orderKw_ = nullptr;
};

#endif
