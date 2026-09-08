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
    explicit ReservationsPage(QWidget *parent = nullptr);
    void setReservations(const QJsonArray &reservations);

signals:
    void refreshRequested();
    void findStationsRequested();
    void stationRequested(QJsonObject station);
    void chargeRequested(int pileId);
    void cancelRequested();
    void navigationRequested(QJsonObject station);

private:
    void render();
    QVBoxLayout *listLayout_ = nullptr;
    QJsonArray reservations_;
};

#endif
