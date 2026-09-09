#ifndef CHARGEHUB_TENCENTAPI_H
#define CHARGEHUB_TENCENTAPI_H

/**
 * @file tencentapi.h
 * @brief 高德位置服务适配：地图、天气和地址解析；保留旧文件名兼容工程。
 *
 * 用户端自己 HTTP 拉图/路线/天气，不经 Dispatch。
 * 管理端 refreshForecast / 找站兜底走同步查询。
 * 高德请求失败时：天气回退 Open-Meteo，路线距离使用 OSRM。
 * 用户端定位优先 Windows 定位服务，其次地图选点；公网 IP 只作城市级参考。
 */

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QNetworkRequest>
#include <QPair>
#include <QString>
#include <QUrl>

namespace TencentApi {

QString key();

/** 拼接高德 WebService URL 并附加 key。 */
QUrl signedUrl(const QString &path, QList<QPair<QString, QString>> params);

QNetworkRequest request(const QUrl &url);

/** 同步 GET JSON。timeoutMs 内无响应返回空对象。 */
QJsonObject getJson(const QString &path, const QList<QPair<QString, QString>> &params,
                    int timeoutMs = 4500);

QUrl staticMapUrl(double lat, double lng);

struct Weather {
    bool ok = false;
    QString text;   ///< 晴 / 小雨
    QString detail; ///< 晴 26℃ · 海淀区
    double factor = 1.0;
    QString source;
};

QUrl weatherAdcodeUrl(double lat, double lng);
QString parseWeatherAdcode(const QJsonObject &obj);
QUrl weatherNowUrl(const QString &adcode);
Weather parseWeather(const QJsonObject &obj);

/** 实况天气。失败 ok=false，调用方自己用模拟值。 */
Weather weatherNow(double lat, double lng);

struct Geo {
    bool ok = false;
    QString name;
    double lat = 0;
    double lng = 0;
};

/** 地址转坐标。失败 ok=false。 */
Geo geocode(const QString &address);

/** 高德地理编码候选，用于地图选点页的异步地址搜索。 */
QUrl amapGeocodeUrl(const QString &address);
QJsonArray parseAmapGeocodes(const QJsonObject &obj);

QUrl fallbackMapUrl(double lat, double lng, int zoom = 15);
/** 国内高德瓦片。 */
QList<QUrl> rasterTileUrls(double lat, double lng, int zoom = 15);
QList<QUrl> rasterTileUrlsXY(int tileX, int tileY, int zoom);
int clampTileZoom(int zoom);
void latLngToWorldPixel(double lat, double lng, int zoom, double *x, double *y);
void worldPixelToLatLng(double x, double y, int zoom, double *lat, double *lng);
/** 同时标出起点和充电桩。无 Key 时返回空 URL。 */
QUrl overviewStaticMapUrl(double fromLat, double fromLng, double toLat, double toLng);
/** 让两点落在同一张 256 瓦片内的最大缩放。 */
int fitTileZoom(double lat1, double lng1, double lat2, double lng2);
/** Web Mercator 瓦片号，给地图选点换算点击坐标。 */
void mapTile(double lat, double lng, int zoom, int *x, int *y);
void tilePixelToLatLng(int tileX, int tileY, int zoom, double px, double py, int tilePx,
                       double *lat, double *lng);
void latLngToTilePixel(double lat, double lng, int zoom, int tileX, int tileY, int tilePx,
                       double *px, double *py);
QUrl openMeteoUrl(double lat, double lng);
Weather parseOpenMeteo(const QJsonObject &obj);
QUrl osrmUrl(const QString &mode, double fromLat, double fromLng, double toLat, double toLng);
bool parseOsrm(const QJsonObject &obj, double *meters, int *seconds);
QJsonObject getUrlJson(const QUrl &url, int timeoutMs = 4500);

QUrl ipLocateUrl();
QUrl ipLocateFallbackUrl();
Geo parseIpLocate(const QJsonObject &obj);
/** 按公网 IP 粗定位。桌面/WSL 通常没有 GPS 芯片时用这个。 */
Geo ipLocate();

} // namespace TencentApi

#endif
