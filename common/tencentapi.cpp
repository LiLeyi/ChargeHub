/**
 * @file tencentapi.cpp
 * @brief 高德 WebService 查询。保留旧文件名以兼容现有 qmake 工程。
 */
#include "tencentapi.h"

#if __has_include("tencent_credentials.h")
#include "tencent_credentials.h"
#endif

#include <algorithm>
#include <cmath>
#include <QDate>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
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
    out.source = QString::fromUtf8("高德地图");
    out.factor = loadFactor(out.text);
    out.detail = out.text;
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
    if (zoom < 3)
        zoom = 3;
    if (zoom > 18)
        zoom = 18;
    int x = 0, y = 0;
    tileXY(lat, lng, zoom, &x, &y);
    return QUrl(QStringLiteral("https://a.basemaps.cartocdn.com/rastertiles/voyager/%1/%2/%3@2x.png")
                    .arg(zoom)
                    .arg(x)
                    .arg(y));
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
    QUrl nom(QStringLiteral("https://nominatim.openstreetmap.org/search"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), text);
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("1"));
    nom.setQuery(q);
    const QJsonArray hits = QJsonDocument::fromJson(fetchUrl(nom, 4500)).array();
    if (hits.isEmpty())
        return out;
    const QJsonObject first = hits.first().toObject();
    out.lat = first.value(QStringLiteral("lat")).toString().toDouble();
    out.lng = first.value(QStringLiteral("lon")).toString().toDouble();
    out.name = first.value(QStringLiteral("display_name")).toString();
    if (out.name.isEmpty())
        out.name = text;
    out.ok = out.lat != 0.0 || out.lng != 0.0;
    return out;
}
