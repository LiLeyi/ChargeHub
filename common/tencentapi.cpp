/**
 * @file tencentapi.cpp
 * @brief 高德 WebService 与公开降级服务查询；保留旧文件名兼容工程。
 */
#include "tencentapi.h"

#include <algorithm>
#include <cmath>
#include <QDate>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QtGlobal>
#include <QTimer>
#include <QUrlQuery>

#ifndef CHARGEHUB_AMAP_KEY
#define CHARGEHUB_AMAP_KEY "fa99e719d907e49c9eee8876d19079e9"
#endif

namespace {

QString encodedQuery(const QList<QPair<QString, QString>> &params)
{
    QList<QPair<QString, QString>> sorted = params;
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        if (a.first == b.first)
            return a.second < b.second;
        return a.first < b.first;
    });
    QStringList parts;
    parts.reserve(sorted.size());
    for (const auto &p : sorted) {
        parts.append(QString::fromLatin1(QUrl::toPercentEncoding(p.first)) + "="
                     + QString::fromLatin1(QUrl::toPercentEncoding(p.second)));
    }
    return parts.join('&');
}

QString wmoText(int code)
{
    if (code == 0)
        return QString::fromUtf8("晴");
    if (code <= 2)
        return QString::fromUtf8("多云");
    if (code == 3)
        return QString::fromUtf8("阴");
    if (code == 45 || code == 48)
        return QString::fromUtf8("雾");
    if (code >= 51 && code <= 57)
        return QString::fromUtf8("小雨");
    if (code >= 61 && code <= 67)
        return QString::fromUtf8("雨");
    if (code >= 71 && code <= 77)
        return QString::fromUtf8("雪");
    if (code >= 80 && code <= 82)
        return QString::fromUtf8("阵雨");
    if (code >= 95)
        return QString::fromUtf8("雷阵雨");
    return QString::fromUtf8("多云");
}

void tileXY(double lat, double lng, int zoom, int *x, int *y)
{
    const double n = static_cast<double>(1 << zoom);
    *x = static_cast<int>(std::floor((lng + 180.0) / 360.0 * n));
    const double latRad = lat * 3.14159265358979323846 / 180.0;
    *y = static_cast<int>(std::floor((1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad))
                                             / 3.14159265358979323846)
                                     / 2.0 * n));
}

double loadFactor(const QString &weather)
{
    if (weather.contains(QString::fromUtf8("雨")) || weather.contains(QString::fromUtf8("雪"))
        || weather.contains(QString::fromUtf8("雷")))
        return 0.90;
    if (QDate::currentDate().dayOfWeek() >= 6)
        return 0.88;
    return 1.08;
}

} // namespace

QString TencentApi::key()
{
    const QString env = qEnvironmentVariable("CHARGEHUB_AMAP_KEY").trimmed();
    return env.isEmpty() ? QString::fromLatin1(CHARGEHUB_AMAP_KEY) : env;
}

QUrl TencentApi::signedUrl(const QString &path, QList<QPair<QString, QString>> params)
{
    params.append({QStringLiteral("key"), key()});
    const QString query = encodedQuery(params);
    return QUrl(QStringLiteral("https://restapi.amap.com") + path + "?" + query);
}

QNetworkRequest TencentApi::request(const QUrl &url)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("ChargeHub/1.0 (campus training)"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    const QString host = url.host();
    if (host.contains(QLatin1String("autonavi")) || host.contains(QLatin1String("amap")))
        req.setRawHeader("Referer", "https://www.amap.com/");
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    req.setTransferTimeout(8000);
#endif
    return req;
}

QByteArray fetchUrl(const QUrl &url, int timeoutMs)
{
    if (!url.isValid())
        return {};
    QNetworkAccessManager nam;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QNetworkReply *reply = nam.get(TencentApi::request(url));
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(qMax(1000, timeoutMs));
    loop.exec();
    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        return {};
    }
    const QByteArray raw = reply->readAll();
    reply->deleteLater();
    return raw;
}

QJsonObject TencentApi::getUrlJson(const QUrl &url, int timeoutMs)
{
    return QJsonDocument::fromJson(fetchUrl(url, timeoutMs)).object();
}

QJsonObject TencentApi::getJson(const QString &path, const QList<QPair<QString, QString>> &params,
                                int timeoutMs)
{
    if (key().isEmpty())
        return {};
    return getUrlJson(signedUrl(path, params), timeoutMs);
}

QUrl TencentApi::staticMapUrl(double lat, double lng)
{
    const QString center = QString::number(lng, 'f', 6) + "," + QString::number(lat, 'f', 6);
    const QString marker = QStringLiteral("mid,,A:%1").arg(center);
    return signedUrl(QStringLiteral("/v3/staticmap"),
                     {{QStringLiteral("location"), center},
                      {QStringLiteral("zoom"), QStringLiteral("16")},
                      {QStringLiteral("size"), QStringLiteral("640*360")},
                      {QStringLiteral("markers"), marker}});
}

QUrl TencentApi::weatherAdcodeUrl(double lat, double lng)
{
    const QString loc = QString::number(lng, 'f', 6) + "," + QString::number(lat, 'f', 6);
    return signedUrl(QStringLiteral("/v3/geocode/regeo"),
                     {{QStringLiteral("location"), loc},
                      {QStringLiteral("extensions"), QStringLiteral("base")}});
}

QString TencentApi::parseWeatherAdcode(const QJsonObject &obj)
{
    if (obj.value(QStringLiteral("status")).toString() != QLatin1String("1"))
        return {};
    return obj.value(QStringLiteral("regeocode")).toObject()
        .value(QStringLiteral("addressComponent")).toObject()
        .value(QStringLiteral("adcode")).toString();
}

QUrl TencentApi::weatherNowUrl(const QString &adcode)
{
    return signedUrl(QStringLiteral("/v3/weather/weatherInfo"),
                     {{QStringLiteral("city"), adcode},
                      {QStringLiteral("extensions"), QStringLiteral("base")}});
}

TencentApi::Weather TencentApi::parseWeather(const QJsonObject &obj)
{
    Weather out;
    if (obj.value(QStringLiteral("status")).toString() != QLatin1String("1"))
        return out;
    const QJsonArray realtime = obj.value(QStringLiteral("lives")).toArray();
    if (realtime.isEmpty())
        return out;
    const QJsonObject first = realtime.first().toObject();
    out.text = first.value(QStringLiteral("weather")).toString();
    if (out.text.isEmpty())
        return out;
    out.ok = true;
    out.factor = loadFactor(out.text);
    out.detail = out.text;
    out.source = QString::fromUtf8("高德地图");
    if (first.contains(QStringLiteral("temperature")))
        out.detail += QString::fromUtf8(" %1℃").arg(first.value(QStringLiteral("temperature")).toString());
    const QString city = first.value(QStringLiteral("city")).toString();
    if (!city.isEmpty())
        out.detail += QString::fromUtf8(" · ") + city;
    return out;
}

TencentApi::Weather TencentApi::weatherNow(double lat, double lng)
{
    Weather fallback;
    fallback.source = QString::fromUtf8("模拟");
    fallback.factor = loadFactor(QString());
    const QString adcode = parseWeatherAdcode(getUrlJson(weatherAdcodeUrl(lat, lng)));
    const Weather amap = adcode.isEmpty() ? Weather{} : parseWeather(getUrlJson(weatherNowUrl(adcode)));
    if (amap.ok)
        return amap;
    const Weather meteo = parseOpenMeteo(getUrlJson(openMeteoUrl(lat, lng)));
    return meteo.ok ? meteo : fallback;
}

void TencentApi::mapTile(double lat, double lng, int zoom, int *x, int *y)
{
    if (zoom < 3)
        zoom = 3;
    if (zoom > 18)
        zoom = 18;
    tileXY(lat, lng, zoom, x, y);
}

void TencentApi::tilePixelToLatLng(int tileX, int tileY, int zoom, double px, double py, int tilePx,
                                   double *lat, double *lng)
{
    if (!lat || !lng || tilePx <= 0)
        return;
    if (zoom < 0)
        zoom = 0;
    const double n = static_cast<double>(1 << zoom);
    const double fx = (static_cast<double>(tileX) + px / tilePx) / n;
    const double fy = (static_cast<double>(tileY) + py / tilePx) / n;
    *lng = fx * 360.0 - 180.0;
    const double latRad = std::atan(std::sinh(3.14159265358979323846 * (1.0 - 2.0 * fy)));
    *lat = latRad * 180.0 / 3.14159265358979323846;
}

QUrl TencentApi::fallbackMapUrl(double lat, double lng, int zoom)
{
    const QList<QUrl> urls = rasterTileUrls(lat, lng, zoom);
    return urls.isEmpty() ? QUrl() : urls.first();
}

int TencentApi::clampTileZoom(int zoom)
{
    if (zoom < 3)
        return 3;
    if (zoom > 18)
        return 18;
    return zoom;
}

void TencentApi::latLngToWorldPixel(double lat, double lng, int zoom, double *x, double *y)
{
    if (!x || !y)
        return;
    zoom = clampTileZoom(zoom);
    if (lat > 85.05112878)
        lat = 85.05112878;
    if (lat < -85.05112878)
        lat = -85.05112878;
    const double n = static_cast<double>(1 << zoom);
    *x = (lng + 180.0) / 360.0 * n * 256.0;
    const double latRad = lat * 3.14159265358979323846 / 180.0;
    *y = (1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad))
                    / 3.14159265358979323846)
         / 2.0 * n * 256.0;
}

void TencentApi::worldPixelToLatLng(double x, double y, int zoom, double *lat, double *lng)
{
    if (!lat || !lng)
        return;
    zoom = clampTileZoom(zoom);
    const double n = static_cast<double>(1 << zoom);
    const double span = n * 256.0;
    *lng = x / span * 360.0 - 180.0;
    const double fy = y / span;
    const double latRad = std::atan(std::sinh(3.14159265358979323846 * (1.0 - 2.0 * fy)));
    *lat = latRad * 180.0 / 3.14159265358979323846;
}

QList<QUrl> TencentApi::rasterTileUrlsXY(int tileX, int tileY, int zoom)
{
    zoom = clampTileZoom(zoom);
    const int maxT = 1 << zoom;
    if (maxT <= 0)
        return {};
    tileX %= maxT;
    if (tileX < 0)
        tileX += maxT;
    if (tileY < 0 || tileY >= maxT)
        return {};
    const int slot = (tileX + tileY) % 4 + 1;
    QList<QUrl> urls;
    urls.append(QUrl(QStringLiteral("https://webrd0%1.is.autonavi.com/appmaptile?lang=zh_cn&size=1&scale=1&style=8&x=%2&y=%3&z=%4")
                         .arg(slot)
                         .arg(tileX)
                         .arg(tileY)
                         .arg(zoom)));
    urls.append(QUrl(QStringLiteral("https://wprd0%1.is.autonavi.com/appmaptile?x=%2&y=%3&z=%4&lang=zh_cn&size=1&scl=1&style=7")
                         .arg(slot)
                         .arg(tileX)
                         .arg(tileY)
                         .arg(zoom)));
    return urls;
}

QList<QUrl> TencentApi::rasterTileUrls(double lat, double lng, int zoom)
{
    zoom = clampTileZoom(zoom);
    int x = 0, y = 0;
    tileXY(lat, lng, zoom, &x, &y);
    return rasterTileUrlsXY(x, y, zoom);
}

int TencentApi::fitTileZoom(double lat1, double lng1, double lat2, double lng2)
{
    const double midLat = (lat1 + lat2) / 2.0;
    const double midLng = (lng1 + lng2) / 2.0;
    for (int z = 16; z >= 11; --z) {
        int tx = 0, ty = 0;
        tileXY(midLat, midLng, z, &tx, &ty);
        double px1 = 0, py1 = 0, px2 = 0, py2 = 0;
        latLngToTilePixel(lat1, lng1, z, tx, ty, 256, &px1, &py1);
        latLngToTilePixel(lat2, lng2, z, tx, ty, 256, &px2, &py2);
        const double pad = 32.0;
        auto inside = [pad](double px, double py) {
            return px >= pad && px <= 256.0 - pad && py >= pad && py <= 256.0 - pad;
        };
        if (inside(px1, py1) && inside(px2, py2))
            return z;
    }
    return 11;
}

void TencentApi::latLngToTilePixel(double lat, double lng, int zoom, int tileX, int tileY, int tilePx,
                                   double *px, double *py)
{
    if (!px || !py || tilePx <= 0)
        return;
    if (zoom < 0)
        zoom = 0;
    const double n = static_cast<double>(1 << zoom);
    const double fx = (lng + 180.0) / 360.0 * n;
    const double latRad = lat * 3.14159265358979323846 / 180.0;
    const double fy = (1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad))
                                 / 3.14159265358979323846)
                      / 2.0 * n;
    *px = (fx - static_cast<double>(tileX)) * tilePx;
    *py = (fy - static_cast<double>(tileY)) * tilePx;
}

QUrl TencentApi::overviewStaticMapUrl(double fromLat, double fromLng, double toLat, double toLng)
{
    if (key().isEmpty())
        return {};
    const double cLat = (fromLat + toLat) / 2.0;
    const double cLng = (fromLng + toLng) / 2.0;
    const QString center = QString::number(cLng, 'f', 6) + "," + QString::number(cLat, 'f', 6);
    const QString from = QString::number(fromLng, 'f', 6) + "," + QString::number(fromLat, 'f', 6);
    const QString to = QString::number(toLng, 'f', 6) + "," + QString::number(toLat, 'f', 6);
    const QString markers = QStringLiteral("mid,,A:%1|mid,,B:%2").arg(from, to);
    return signedUrl(QStringLiteral("/v3/staticmap"),
                     {{QStringLiteral("location"), center},
                      {QStringLiteral("zoom"), QString::number(fitTileZoom(fromLat, fromLng, toLat, toLng))},
                      {QStringLiteral("size"), QStringLiteral("640*360")},
                      {QStringLiteral("markers"), markers}});
}

QUrl TencentApi::ipLocateUrl()
{
    return QUrl(QStringLiteral("http://ip-api.com/json/?fields=status,lat,lon,city,regionName&lang=zh-CN"));
}

QUrl TencentApi::ipLocateFallbackUrl()
{
    return QUrl(QStringLiteral("https://ipwho.is/?fields=success,latitude,longitude,city,region"));
}

TencentApi::Geo TencentApi::parseIpLocate(const QJsonObject &obj)
{
    Geo out;
    if (obj.value(QStringLiteral("status")).toString() == QLatin1String("success")) {
        out.lat = obj.value(QStringLiteral("lat")).toDouble();
        out.lng = obj.value(QStringLiteral("lon")).toDouble();
        const QString city = obj.value(QStringLiteral("city")).toString();
        const QString region = obj.value(QStringLiteral("regionName")).toString();
        out.name = region.isEmpty() ? city : (city.isEmpty() ? region : region + " " + city);
        out.ok = out.lat != 0.0 || out.lng != 0.0;
        return out;
    }
    if (obj.value(QStringLiteral("success")).toBool()) {
        out.lat = obj.value(QStringLiteral("latitude")).toDouble();
        out.lng = obj.value(QStringLiteral("longitude")).toDouble();
        const QString city = obj.value(QStringLiteral("city")).toString();
        const QString region = obj.value(QStringLiteral("region")).toString();
        out.name = region.isEmpty() ? city : (city.isEmpty() ? region : region + " " + city);
        out.ok = out.lat != 0.0 || out.lng != 0.0;
    }
    return out;
}

TencentApi::Geo TencentApi::ipLocate()
{
    Geo a = parseIpLocate(getUrlJson(ipLocateUrl(), 4000));
    if (a.ok)
        return a;
    return parseIpLocate(getUrlJson(ipLocateFallbackUrl(), 4000));
}

QUrl TencentApi::openMeteoUrl(double lat, double lng)
{
    return QUrl(QStringLiteral("https://api.open-meteo.com/v1/forecast?latitude=%1&longitude=%2"
                               "&current=temperature_2m,weather_code&timezone=auto")
                    .arg(lat, 0, 'f', 6)
                    .arg(lng, 0, 'f', 6));
}

TencentApi::Weather TencentApi::parseOpenMeteo(const QJsonObject &obj)
{
    Weather out;
    const QJsonObject cur = obj.value(QStringLiteral("current")).toObject();
    if (cur.isEmpty())
        return out;
    out.text = wmoText(cur.value(QStringLiteral("weather_code")).toInt());
    out.ok = true;
    out.source = QStringLiteral("Open-Meteo");
    out.factor = loadFactor(out.text);
    out.detail = out.text;
    if (cur.contains(QStringLiteral("temperature_2m")))
        out.detail += QString::fromUtf8(" %1℃").arg(qRound(cur.value(QStringLiteral("temperature_2m")).toDouble()));
    return out;
}

QUrl TencentApi::osrmUrl(const QString &mode, double fromLat, double fromLng, double toLat, double toLng)
{
    QString profile = QStringLiteral("driving");
    if (mode == QStringLiteral("walk"))
        profile = QStringLiteral("foot");
    else if (mode == QStringLiteral("bike"))
        profile = QStringLiteral("bike");
    return QUrl(QStringLiteral("https://router.project-osrm.org/route/v1/%1/%2,%3;%4,%5?overview=false")
                    .arg(profile)
                    .arg(fromLng, 0, 'f', 6)
                    .arg(fromLat, 0, 'f', 6)
                    .arg(toLng, 0, 'f', 6)
                    .arg(toLat, 0, 'f', 6));
}

bool TencentApi::parseOsrm(const QJsonObject &obj, double *meters, int *seconds)
{
    if (obj.value(QStringLiteral("code")).toString() != QLatin1String("Ok"))
        return false;
    const QJsonArray routes = obj.value(QStringLiteral("routes")).toArray();
    if (routes.isEmpty())
        return false;
    const QJsonObject first = routes.first().toObject();
    if (meters)
        *meters = first.value(QStringLiteral("distance")).toDouble();
    if (seconds)
        *seconds = qRound(first.value(QStringLiteral("duration")).toDouble());
    return true;
}

TencentApi::Geo TencentApi::geocode(const QString &address)
{
    Geo out;
    const QString text = address.trimmed();
    if (text.isEmpty())
        return out;
    const QJsonObject obj = getJson(QStringLiteral("/v3/geocode/geo"),
                                    {{QStringLiteral("address"), text}});
    if (obj.value(QStringLiteral("status")).toString() == QLatin1String("1")) {
        const QJsonArray geocodes = obj.value(QStringLiteral("geocodes")).toArray();
        const QJsonObject result = geocodes.isEmpty() ? QJsonObject{} : geocodes.first().toObject();
        const QStringList coordinate = result.value(QStringLiteral("location")).toString().split(',');
        if (coordinate.size() == 2) {
            out.lng = coordinate.at(0).toDouble();
            out.lat = coordinate.at(1).toDouble();
        }
        out.name = result.value(QStringLiteral("formatted_address")).toString();
        if (out.name.isEmpty())
            out.name = text;
        out.ok = out.lat != 0.0 || out.lng != 0.0;
        if (out.ok)
            return out;
    }
    return out;
}

QUrl TencentApi::amapGeocodeUrl(const QString &address)
{
    const QString env = qEnvironmentVariable("CHARGEHUB_AMAP_KEY").trimmed();
    const QString apiKey = env.isEmpty() ? QString::fromLatin1(CHARGEHUB_AMAP_KEY) : env;
    QUrl url(QStringLiteral("https://restapi.amap.com/v3/geocode/geo"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("address"), address.trimmed());
    query.addQueryItem(QStringLiteral("key"), apiKey);
    url.setQuery(query);
    return url;
}

QJsonArray TencentApi::parseAmapGeocodes(const QJsonObject &obj)
{
    if (obj.value(QStringLiteral("status")).toString() != QLatin1String("1"))
        return {};
    QJsonArray valid;
    const QJsonArray geocodes = obj.value(QStringLiteral("geocodes")).toArray();
    for (const QJsonValue &value : geocodes) {
        const QJsonObject hit = value.toObject();
        const QStringList coordinate = hit.value(QStringLiteral("location")).toString().split(',');
        if (coordinate.size() == 2)
            valid.append(hit);
    }
    return valid;
}
