#ifndef CHARGEHUB_STATIONSERVICE_H
#define CHARGEHUB_STATIONSERVICE_H

#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QVariantMap>

class Database;
class ReservationService;

/** 用户侧电站检索、距离排序和电桩详情查询。可选 useGps+lat+lng+placeName。 */
class StationService {
public:
    /** @param db 数据库依赖。@param reservations 预约状态依赖；均不转移所有权。 */
    StationService(Database *db, ReservationService *reservations);
    /**
     * @param user 已认证用户行。
     * @param data 地址或 useGps/lat/lng、搜索半径。
     * @return 按球面距离排序的电站、地图轻量数据和附近空闲桩。
     */
    QJsonObject queryStations(const QVariantMap &user, const QJsonObject &data);
    /** @param user 已认证用户行。@param data 含 stationId。@return 电站及其全部电桩状态。 */
    QJsonObject queryPiles(const QVariantMap &user, const QJsonObject &data);
    /** @param address 地址或地标。@return 纬度、经度；优先本地匹配，失败时调用地理编码。 */
    QPair<double, double> coordinatesForAddress(const QString &address) const;

private:
    Database *db_;
    ReservationService *reservations_;
};

#endif
