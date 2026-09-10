#include "stationservice.h"

#include "database.h"
#include "reservationservice.h"
#include "tencentapi.h"

#include <algorithm>
#include <QHash>
#include <QJsonArray>
#include <QtMath>

static double haversine(double lat1, double lng1, double lat2, double lng2)
{
    const double r = 6371.0;
    const double p1 = qDegreesToRadians(lat1);
    const double p2 = qDegreesToRadians(lat2);
    const double dphi = qDegreesToRadians(lat2 - lat1);
    const double dl = qDegreesToRadians(lng2 - lng1);
    const double a = qSin(dphi / 2) * qSin(dphi / 2)
        + qCos(p1) * qCos(p2) * qSin(dl / 2) * qSin(dl / 2);
    return r * 2 * qAtan2(qSqrt(a), qSqrt(1 - a));
}

struct GeoHit {
    QString name;
    double lat = 39.728167;
    double lng = 116.170492;
    bool matched = false;
};

static GeoHit resolveAddress(const QString &raw, const QVector<QVariantMap> &stations)
{
    const QString t = raw.trimmed();
    GeoHit best{QString::fromUtf8("北京理工大学良乡校区（默认）"),
                39.728167, 116.170492, false};
    if (t.isEmpty())
        return best;
    int score = 0;
    auto consider = [&](const QString &key, const QString &name, double lat, double lng) {
        if (key.isEmpty() || !t.contains(key))
            return;
        const int s = key.size();
        if (s > score) {
            score = s;
            best = {name, lat, lng, true};
        }
    };
    for (const auto &s : stations) {
        consider(s.value("name").toString(), s.value("name").toString(),
                 s.value("lat").toDouble(), s.value("lng").toDouble());
        consider(s.value("address").toString(), s.value("name").toString(),
                 s.value("lat").toDouble(), s.value("lng").toDouble());
    }
    struct Lm { const char *k, *n; double lat, lng; } lms[] = {
        {"北京理工大学", "北京理工大学", 39.9644, 116.3473},
        {"北理工", "北京理工大学", 39.9644, 116.3473},
        {"中关村南大街", "北京理工大学 / 中关村南大街", 39.9644, 116.3473},
        {"中关村软件园", "中关村软件园", 39.9832, 116.3105},
        {"软件园", "中关村软件园", 39.9832, 116.3105},
        {"东北旺", "中关村软件园 / 东北旺", 39.9832, 116.3105},
        {"五道口", "五道口", 39.9928, 116.3382},
        {"成府路", "五道口 / 成府路", 39.9928, 116.3382},
        {"中关村", "中关村", 39.9832, 116.3105},
        {"清华大学", "清华大学", 40.0030, 116.3269},
        {"清华", "清华大学", 40.0030, 116.3269},
        {"北京大学", "北京大学", 39.9869, 116.3059},
        {"北大", "北京大学", 39.9869, 116.3059},
        {"人民大学", "中国人民大学", 39.9704, 116.3180},
        {"上地", "上地", 40.0324, 116.3067},
        {"西二旗", "西二旗", 40.0530, 116.3062},
        {"海淀黄庄", "海淀黄庄", 39.9756, 116.3173},
        {"知春路", "知春路", 39.9764, 116.3398},
    };
    for (const auto &lm : lms)
        consider(QString::fromUtf8(lm.k), QString::fromUtf8(lm.n), lm.lat, lm.lng);
    if (!best.matched && !t.isEmpty()) {
        const TencentApi::Geo geo = TencentApi::geocode(t);
        if (geo.ok)
            return {geo.name.isEmpty() ? t : geo.name, geo.lat, geo.lng, true};
        best.name = QString::fromUtf8("未精确匹配，已按海淀中关村一带检索");
    }
    return best;
}

/** 按地址关键字或半径列附近电站，只读 station。 */
StationService::StationService(Database *db, ReservationService *reservations)
    : db_(db), reservations_(reservations)
{
}

QPair<double, double> StationService::coordinatesForAddress(const QString &address) const
{
    const auto hit = resolveAddress(address, db_->query("SELECT * FROM station"));
    return {hit.lat, hit.lng};
}

QJsonObject StationService::queryStations(const QVariantMap &user, const QJsonObject &data)
{
    reservations_->expire();
    const auto stations = db_->query("SELECT * FROM station");
    QString address = data.value("address").toString().trimmed();
    if (address.isEmpty())
        address = user.value("address").toString().trimmed();
    GeoHit hit;
    const bool useGps = data.value("useGps").toBool()
                        && data.contains("lat") && data.contains("lng");
    if (useGps) {
        hit.lat = data.value("lat").toDouble();
        hit.lng = data.value("lng").toDouble();
        hit.name = data.value("placeName").toString();
        if (hit.name.isEmpty())
            hit.name = QString::fromUtf8("GPS 定位");
        hit.matched = true;
        db_->execute("UPDATE user SET loc_lat=?, loc_lng=? WHERE id=?",
                     {hit.lat, hit.lng, user.value("id")});
    } else if (!address.isEmpty()) {
        hit = resolveAddress(address, stations);
    } else if (user.contains("loc_lat")) {
        hit.lat = user.value("loc_lat").toDouble();
        hit.lng = user.contains("loc_lng") ? user.value("loc_lng").toDouble() : 116.170492;
        hit.name = QString::fromUtf8("上次定位");
        hit.matched = true;
    } else if (data.contains("lat") || data.contains("lng")) {
        hit.lat = data.value("lat").toDouble(39.728167);
        hit.lng = data.value("lng").toDouble(116.170492);
        hit.name = QString::fromUtf8("指定坐标");
        hit.matched = true;
    } else {
        hit = resolveAddress(QString(), stations);
    }
    if (!useGps && !address.isEmpty()) {
        db_->execute("UPDATE user SET address=?, loc_lat=?, loc_lng=? WHERE id=?",
                     {address, hit.lat, hit.lng, user.value("id")});
    }
    const double lat = hit.lat;
    const double lng = hit.lng;
    const double radius = data.value("radiusKm").toDouble(20);
    QJsonArray arr;
    QJsonArray mapStations;
    QVector<QJsonObject> tmp;
    QVector<QJsonObject> nearPiles;
    for (const auto &s : stations) {
        const auto piles = db_->query("SELECT * FROM pile WHERE station_id=?", {s.value("id")});
        int idle = 0, fast = 0, slow = 0;
        const double dist = qRound(haversine(lat, lng, s.value("lat").toDouble(), s.value("lng").toDouble()) * 10) / 10.0;
        for (const auto &p : piles) {
            if (reservations_->pileIsIdle(p))
                ++idle;
            if (p.value("type").toString() == QString::fromUtf8("快充"))
                ++fast;
            else
                ++slow;
            if (radius <= 0 || dist <= radius) {
                QString disp = p.value("status").toString();
                if (disp == QString::fromUtf8("闲置"))
                    disp = QString::fromUtf8("空闲");
                else if (disp == QString::fromUtf8("在用"))
                    disp = QString::fromUtf8("占用");
                nearPiles.append(QJsonObject{
                    {"id", p.value("id").toInt()},
                    {"code", p.value("pile_no").toString()},
                    {"pileNo", p.value("pile_no").toString()},
                    {"type", p.value("type").toString()},
                    {"pileType", p.value("type").toString()},
                    {"powerKw", p.value("power_kw").toDouble()},
                    {"status", disp},
                    {"stationId", s.value("id").toInt()},
                    {"stationName", s.value("name").toString()},
                    {"station", s.value("name").toString()},
                    {"stationAddress", s.value("address").toString()},
                    {"distanceKm", dist},
                    {"pricePerKwh", s.value("price_per_kwh").toDouble()},
                    {"idle", reservations_->pileIsIdle(p)},
                });
            }
        }
        mapStations.append(QJsonObject{
            {"id", s.value("id").toInt()},
            {"name", s.value("name").toString()},
            {"lat", s.value("lat").toDouble()},
            {"lng", s.value("lng").toDouble()},
            {"idlePiles", idle},
            {"totalPiles", piles.size()},
        });
        if (radius > 0 && dist > radius)
            continue;
        auto rev = db_->one("SELECT IFNULL(AVG(score),0) AS a, COUNT(*) AS n FROM station_review WHERE station_id=?",
                            {s.value("id")});
        const int nrev = rev.value("n").toInt();
        tmp.append(QJsonObject{
            {"id", s.value("id").toInt()},
            {"name", s.value("name").toString()},
            {"address", s.value("address").toString()},
            {"lng", s.value("lng").toDouble()},
            {"lat", s.value("lat").toDouble()},
            {"pricePerKwh", s.value("price_per_kwh").toDouble()},
            {"totalPiles", piles.size()},
            {"idlePiles", idle},
            {"fastPiles", fast},
            {"slowPiles", slow},
            {"distanceKm", dist},
            {"score", nrev ? qRound(rev.value("a").toDouble() * 10) / 10.0 : 4.8},
            {"reviewCount", nrev},
        });
    }
    std::sort(tmp.begin(), tmp.end(), [](const QJsonObject &a, const QJsonObject &b) {
        const double da = a.value("distanceKm").toDouble();
        const double db = b.value("distanceKm").toDouble();
        if (qAbs(da - db) > 1e-6)
            return da < db;
        return a.value("idlePiles").toInt() > b.value("idlePiles").toInt();
    });
    std::sort(nearPiles.begin(), nearPiles.end(), [](const QJsonObject &a, const QJsonObject &b) {
        const double da = a.value("distanceKm").toDouble();
        const double db = b.value("distanceKm").toDouble();
        if (qAbs(da - db) > 1e-6)
            return da < db;
        return a.value("idle").toBool() && !b.value("idle").toBool();
    });
    for (const auto &o : tmp)
        arr.append(o);
    QJsonArray nearby;
    for (int i = 0; i < qMin(8, nearPiles.size()); ++i)
        nearby.append(nearPiles[i]);
    return QJsonObject{
        {"stations", arr},
        {"mapStations", mapStations},
        {"nearbyPiles", nearby},
        {"location", QJsonObject{
             {"address", address},
             {"lat", lat},
             {"lng", lng},
             {"matchedPlace", hit.name},
             {"matched", hit.matched},
         }},
    };
}

/** 先过期预约，再列出某站的桩及是否可预约/可开充。 */
QJsonObject StationService::queryPiles(const QVariantMap &user, const QJsonObject &data)
{
    reservations_->expire();
    const int sid = data.value("stationId").toInt();
    const int uid = user.value("id").toInt();
    auto station = db_->one("SELECT * FROM station WHERE id=?", {sid});
    if (station.isEmpty())
        return QJsonObject{{"error", QString::fromUtf8("该站暂无电桩数据")}};
    const QString typeFilter = data.value("type").toString().trimmed();
    const auto piles = db_->query("SELECT * FROM pile WHERE station_id=? ORDER BY pile_no", {sid});
    QJsonArray arr;
    for (const auto &p : piles) {
        if (!typeFilter.isEmpty() && p.value("type").toString() != typeFilter)
            continue;
        auto res = reservations_->activeForPile(p.value("id").toInt());
        const bool mine = !res.isEmpty() && res.value("user_id").toInt() == uid;
        QString status = p.value("status").toString();
        if (status == QString::fromUtf8("闲置") && !res.isEmpty() && !mine)
            status = QString::fromUtf8("已预约");
        arr.append(QJsonObject{
            {"id", p.value("id").toInt()},
            {"pileNo", p.value("pile_no").toString()},
            {"stationId", p.value("station_id").toInt()},
            {"type", p.value("type").toString()},
            {"powerKw", p.value("power_kw").toDouble()},
            {"status", status},
            {"totalChargeCount", p.value("total_charge_count").toInt()},
            {"totalChargeMinutes", p.value("total_charge_minutes").toInt()},
            {"reserved", !res.isEmpty()},
            {"reservedByMe", mine},
        });
    }
    QHash<int, QJsonArray> byPile;
    QJsonArray reviews;
    const auto rws = db_->query(
        "SELECT r.score, r.comment, r.pile_id, u.nickname, IFNULL(p.pile_no,'') AS pile_no "
        "FROM station_review r JOIN user u ON u.id=r.user_id "
        "LEFT JOIN pile p ON p.id=r.pile_id "
        "WHERE r.station_id=? ORDER BY r.id DESC LIMIT 40",
        {sid});
    for (const auto &r : rws) {
        const QJsonObject item{
            {"score", r.value("score").toInt()},
            {"comment", r.value("comment").toString()},
            {"nickname", r.value("nickname").toString()},
            {"pileId", r.value("pile_id").toInt()},
            {"pileNo", r.value("pile_no").toString()},
        };
        reviews.append(item);
        byPile[r.value("pile_id").toInt()].append(item);
    }
    QJsonArray pilesOut;
    for (const auto &v : arr) {
        QJsonObject o = v.toObject();
        const auto mineRev = byPile.value(o.value("id").toInt());
        o.insert("reviews", mineRev);
        pilesOut.append(o);
    }
    return QJsonObject{
        {"station", QJsonObject{
             {"id", station.value("id").toInt()},
             {"name", station.value("name").toString()},
             {"pricePerKwh", station.value("price_per_kwh").toDouble()},
             {"lat", station.value("lat").toDouble()},
             {"lng", station.value("lng").toDouble()},
             {"address", station.value("address").toString()},
         }},
        {"piles", pilesOut},
        {"reviews", reviews},
    };
}
