#ifndef CHARGEHUB_CHARGEPAGE_H
#define CHARGEHUB_CHARGEPAGE_H

#include <QJsonObject>
#include <QWidget>

class QLabel;
class QPushButton;

class ChargePage : public QWidget
{
    Q_OBJECT
public:
    /** @param parent Qt 父对象；构造空状态充电详情和操作按钮。 */
    explicit ChargePage(QWidget *parent = nullptr);

    /**
     * 用服务端订单快照刷新状态、时长、电量、金额和按钮可用性。
     * @param order CHARGE_STATUS/PUSH_CHARGE 返回的 order 对象。
     */
    void setOrder(const QJsonObject &order);
    /** 清空当前订单并恢复“暂无充电”状态。无输入和返回值。 */
    void clearOrder();

signals:
    /** 用户点击停止充电时发出。 */
    void stopRequested();
    /** 用户点击结算时发出。 */
    void settleRequested();
    /** 空状态下用户请求返回找桩页时发出。 */
    void findPileRequested();

private:
    QLabel *status_ = nullptr;
    QLabel *time_ = nullptr;
    QLabel *energy_ = nullptr;
    QLabel *fee_ = nullptr;
    QLabel *info_ = nullptr;
    QPushButton *stopButton_ = nullptr;
    QPushButton *settleButton_ = nullptr;
};

#endif
