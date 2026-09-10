#ifndef CHARGEHUB_ORDERSPAGE_H
#define CHARGEHUB_ORDERSPAGE_H

#include <QJsonArray>
#include <QWidget>

class QComboBox;
class QVBoxLayout;

class OrdersPage : public QWidget
{
    Q_OBJECT
public:
    /** @param parent Qt 父对象；构造状态筛选器和订单列表容器。 */
    explicit OrdersPage(QWidget *parent = nullptr);

    /**
     * 替换订单快照并按当前筛选条件重绘列表。
     * @param orders LIST_ORDERS 返回的订单数组。
     */
    void setOrders(const QJsonArray &orders);

private:
    /** 先清空旧卡片，再按筛选状态逐项生成订单卡片。无输入和返回值。 */
    void render();

    QComboBox *filter_ = nullptr;
    QVBoxLayout *ordersLayout_ = nullptr;
    QJsonArray orders_;
};

#endif
