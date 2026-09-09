/**
 * @file tencentapi.cpp
 * @brief 腾讯 WebService 签名与查询。凭证只读本机文件或环境变量。
 */
#include "tencentapi.h"

#if __has_include("tencent_credentials.h")
#include "tencent_credentials.h"
#endif

#include <algorithm>
#include <cmath>
#include <QCryptographicHash>
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

#ifndef CHARGEHUB_TENCENT_MAP_KEY
#define CHARGEHUB_TENCENT_MAP_KEY "2L2BZ-7WE6C-ZFP2W-A23GC-5WZK2-KHBGF"
#endif
#ifndef CHARGEHUB_TENCENT_MAP_SK
#define CHARGEHUB_TENCENT_MAP_SK ""
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
    const QString env = qEnvironmentVariable("CHARGEHUB_TENCENT_MAP_KEY").trimmed();
    return env.isEmpty() ? QString::fromLatin1(CHARGEHUB_TENCENT_MAP_KEY) : env;
}

QString TencentApi::sk()
{
    const QString env = qEnvironmentVariable("CHARGEHUB_TENCENT_MAP_SK").trimmed();
    return env.isEmpty() ? QString::fromLatin1(CHARGEHUB_TENCENT_MAP_SK) : env;
}

QUrl TencentApi::signedUrl(const QString &path, QList<QPair<QString, QString>> params)
{
    params.append({QStringLiteral("key"), key()});
    const QString query = encodedQuery(params);
    QString full = QStringLiteral("https://apis.map.qq.com") + path + "?" + query;
    const QString secret = sk();
    if (!secret.isEmpty()) {
        const QByteArray source = (path + "?" + query + secret).toUtf8();
        const QString sig = QString::fromLatin1(QCryptographicHash::hash(source, QCryptographicHash::Md5).toHex());
        full += "&sig=" + sig;
    }
    return QUrl(full);
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
    if (host.contains(QLatin1String("gtimg.com")))
        req.setRawHeader("Referer", "https://map.qq.com/");
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
    const QString center = QString::number(lat, 'f', 6) + "," + QString::number(lng, 'f', 6);
    const QString marker = QStringLiteral("size:large|color:red|label:S|%1").arg(center);
    return signedUrl(QStringLiteral("/ws/staticmap/v2/"),
                     {{QStringLiteral("center"), center},
                      {QStringLiteral("zoom"), QStringLiteral("16")},
                      {QStringLiteral("size"), QStringLiteral("640*360")},
                      {QStringLiteral("scale"), QStringLiteral("2")},
                      {QStringLiteral("markers"), marker}});
}

QUrl TencentApi::weatherNowUrl(double lat, double lng)
{
    const QString loc = QString::number(lat, 'f', 6) + "," + QString::number(lng, 'f', 6);
    return signedUrl(QStringLiteral("/ws/weather/v1/"),
                     {{QStringLiteral("location"), loc},
                      {QStringLiteral("type"), QStringLiteral("now")}});
}

TencentApi::Weather TencentApi::parseWeather(const QJsonObject &obj)
{
    Weather out;
    if (obj.value(QStringLiteral("status")).toInt(-1) != 0)
        return out;
    const QJsonArray realtime = obj.value(QStringLiteral("result")).toObject()
                                    .value(QStringLiteral("realtime"))
                                    .toArray();
    if (realtime.isEmpty())
        return out;
    const QJsonObject first = realtime.first().toObject();
    const QJsonObject infos = first.value(QStringLiteral("infos")).toObject();
    out.text = infos.value(QStringLiteral("weather")).toString();
    if (out.text.isEmpty())
        return out;
    out.ok = true;
    out.source = QString::fromUtf8("腾讯位置服务");
    out.factor = loadFactor(out.text);
    out.detail = out.text;
    if (infos.contains(QStringLiteral("temperature")))
        out.detail += QString::fromUtf8(" %1℃").arg(infos.value(QStringLiteral("temperature")).toInt());
    const QString district = first.value(QStringLiteral("district")).toString();
    if (!district.isEmpty())
        out.detail += QString::fromUtf8(" · ") + district;
    return out;
}

TencentApi::Weather TencentApi::weatherNow(double lat, double lng)
{
    Weather fallback;
    fallback.source = QString::fromUtf8("模拟");
    fallback.factor = loadFactor(QString());
    const Weather tencent = parseWeather(getJson(QStringLiteral("/ws/weather/v1/"),
                                                 {{QStringLiteral("location"),
                                                   QString::number(lat, 'f', 6) + ","
                                                       + QString::number(lng, 'f', 6)},
                                                  {QStringLiteral("type"), QStringLiteral("now")}}));
    if (tencent.ok)
        return tencent;
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
    urls.append(QUrl(QStringLiteral("https://rt%1.map.gtimg.com/tile?z=%2&x=%3&y=%4&styleid=0&scene=0")
                         .arg((tileX + tileY) % 4)
                         .arg(zoom)
                         .arg(tileX)
                         .arg(tileY)));
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
    const QString center = QString::number(cLat, 'f', 6) + "," + QString::number(cLng, 'f', 6);
    const QString from = QString::number(fromLat, 'f', 6) + "," + QString::number(fromLng, 'f', 6);
    const QString to = QString::number(toLat, 'f', 6) + "," + QString::number(toLng, 'f', 6);
    const QString markers =
        QStringLiteral("size:large|color:0x2563EB|label:A|%1|size:large|color:0xDC2626|label:B|%2")
            .arg(from)
            .arg(to);
    return signedUrl(QStringLiteral("/ws/staticmap/v2/"),
                     {{QStringLiteral("center"), center},
                      {QStringLiteral("zoom"), QString::number(fitTileZoom(fromLat, fromLng, toLat, toLng))},
                      {QStringLiteral("size"), QStringLiteral("640*360")},
                      {QStringLiteral("scale"), QStringLiteral("2")},
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
    const QJsonObject obj = getJson(QStringLiteral("/ws/geocoder/v1/"),
                                    {{QStringLiteral("address"), text}});
    if (obj.value(QStringLiteral("status")).toInt(-1) == 0) {
        const QJsonObject result = obj.value(QStringLiteral("result")).toObject();
        const QJsonObject loc = result.value(QStringLiteral("location")).toObject();
        out.lat = loc.value(QStringLiteral("lat")).toDouble();
        out.lng = loc.value(QStringLiteral("lng")).toDouble();
        out.name = result.value(QStringLiteral("title")).toString();
        if (out.name.isEmpty())
            out.name = result.value(QStringLiteral("address")).toString();
        if (out.name.isEmpty())
            out.name = text;
        out.ok = out.lat != 0.0 || out.lng != 0.0;
        if (out.ok)
            return out;
    }
    return out;
}

QUrl TencentApi::directionUrl(const QString &mode, double fromLat, double fromLng, double toLat, double toLng)
{
    QString path = QStringLiteral("/ws/direction/v1/driving/");
    if (mode == QStringLiteral("walk"))
        path = QStringLiteral("/ws/direction/v1/walking/");
    else if (mode == QStringLiteral("bike"))
        path = QStringLiteral("/ws/direction/v1/bicycling/");
    else if (mode == QStringLiteral("bus"))
        path = QStringLiteral("/ws/direction/v1/transit/export");
    const QString from = QString::number(fromLat, 'f', 6) + "," + QString::number(fromLng, 'f', 6);
    const QString to = QString::number(toLat, 'f', 6) + "," + QString::number(toLng, 'f', 6);
    return signedUrl(path, {{QStringLiteral("from"), from}, {QStringLiteral("to"), to}});
}

bool TencentApi::parseDirection(const QJsonObject &obj, double *meters, int *seconds)
{
    if (obj.value(QStringLiteral("status")).toInt(-1) != 0)
        return false;
    const QJsonArray routes = obj.value(QStringLiteral("result")).toObject()
                                  .value(QStringLiteral("routes"))
                                  .toArray();
    if (routes.isEmpty())
        return false;
    const QJsonObject first = routes.first().toObject();
    const double dist = first.value(QStringLiteral("distance")).toDouble();
    const int dur = first.value(QStringLiteral("duration")).toInt();
    if (meters)
        *meters = dist;
    if (seconds)
        *seconds = dur;
    return dist > 0 || first.contains(QStringLiteral("distance"));
}
