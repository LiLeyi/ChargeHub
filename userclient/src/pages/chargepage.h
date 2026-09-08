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
    explicit ChargePage(QWidget *parent = nullptr);

    void setOrder(const QJsonObject &order);
    void clearOrder();

signals:
    void stopRequested();
    void settleRequested();
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
