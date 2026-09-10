#ifndef CHARGEHUB_RESERVATIONSPAGE_H
#define CHARGEHUB_RESERVATIONSPAGE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>

class QVBoxLayout;

class ReservationsPage : public QWidget
{
    Q_OBJECT
public:
    /** @param parent Qt 父对象；构造预约列表及刷新/找桩操作区。 */
    explicit ReservationsPage(QWidget *parent = nullptr);
    /** @param reservations 服务端返回的有效预约数组；保存后立即重绘。 */
    void setReservations(const QJsonArray &reservations);

signals:
    /** 用户请求重新拉取预约列表。 */
    void refreshRequested();
    /** 用户从空状态跳转到找桩页。 */
    void findStationsRequested();
    /** @param station 用户选择的预约所属电站快照。 */
    void stationRequested(QJsonObject station);
    /** @param pileId 用户希望开始充电的电桩主键。 */
    void chargeRequested(int pileId);
    /** 用户请求取消当前有效预约。 */
    void cancelRequested();
    /** @param station 需要查看位置或导航的电站快照。 */
    void navigationRequested(QJsonObject station);

private:
    /** 清空旧行并根据 reservations_ 重建预约状态和操作按钮。 */
    void render();
    QVBoxLayout *listLayout_ = nullptr;
    QJsonArray reservations_;
};

#endif
