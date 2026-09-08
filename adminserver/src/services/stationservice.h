#ifndef CHARGEHUB_STATIONSERVICE_H
#define CHARGEHUB_STATIONSERVICE_H

#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QVariantMap>

class Database;
class ReservationService;

/** 用户侧电站检索、距离排序和电桩详情查询。 */
class StationService {
public:
    StationService(Database *db, ReservationService *reservations);
    QJsonObject queryStations(const QVariantMap &user, const QJsonObject &data);
    QJsonObject queryPiles(const QVariantMap &user, const QJsonObject &data);
    QPair<double, double> coordinatesForAddress(const QString &address) const;

private:
    Database *db_;
    ReservationService *reservations_;
};

#endif
