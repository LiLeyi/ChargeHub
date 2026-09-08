#ifndef CHARGEHUB_TENCENTAPI_H
#define CHARGEHUB_TENCENTAPI_H

/**
 * @file tencentapi.h
 * @brief 腾讯位置服务：静态图、路线、天气、地址解析。Key/SK 来自环境变量或本机凭证文件。
 *
 * 用户端自己 HTTP 拉图/路线/天气，不经 Dispatch。
 * 管理端 refreshForecast / 找站兜底走同步查询。
 * 腾讯配额用尽时：天气改 Open-Meteo，地图默认 Carto/OSM 瓦片（无日调用上限），路线改 OSRM，地址改 Nominatim。
 * 用户端定位优先 Windows 定位服务，其次地图选点；公网 IP 只作城市级参考。
 */

#include <QJsonObject>
#include <QList>
#include <QNetworkRequest>
#include <QPair>
#include <QString>
#include <QUrl>

namespace TencentApi {

QString key();
QString sk();

/** 拼 https://apis.map.qq.com + path，参数按 key 排序；有 SK 则带 sig。 */
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

QUrl weatherNowUrl(double lat, double lng);
Weather parseWeather(const QJsonObject &obj);

/** 实况天气。失败 ok=false，调用方自己用模拟值。 */
Weather weatherNow(double lat, double lng);

struct Geo {
    bool ok = false;
    QString name;
    double lat = 0;
    double lng = 0;
};

/** 地址转坐标。失败 ok=false。腾讯失败时再试 Nominatim。 */
Geo geocode(const QString &address);

QUrl fallbackMapUrl(double lat, double lng, int zoom = 15);
/** Web Mercator 瓦片号，给地图选点换算点击坐标。 */
void mapTile(double lat, double lng, int zoom, int *x, int *y);
void tilePixelToLatLng(int tileX, int tileY, int zoom, double px, double py, int tilePx,
                       double *lat, double *lng);
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
