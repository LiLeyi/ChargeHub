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
    explicit OrdersPage(QWidget *parent = nullptr);

    void setOrders(const QJsonArray &orders);

private:
    void render();

    QComboBox *filter_ = nullptr;
    QVBoxLayout *ordersLayout_ = nullptr;
    QJsonArray orders_;
};

#endif
