#ifndef CHARGEHUB_TENCENTAPI_H
#define CHARGEHUB_TENCENTAPI_H

/**
 * @file tencentapi.h
 * @brief 外部 HTTP：腾讯位置服务为主，高德/腾讯瓦片、Open-Meteo、OSRM、IP 定位为兜底。
 *
 * 【职责】
 *   给用户端地图/导航/天气、管理端「刷新预测」的天气因子、找站 geocode 提供 URL 与解析。
 *   全部是出站 HTTP(S)，不经过 Dispatch，不改变 Socket 字段，不写 charge_order / 余额。
 *
 * 【凭证】
 *   Key/SK 优先级：环境变量 CHARGEHUB_TENCENT_MAP_KEY / CHARGEHUB_TENCENT_MAP_SK
 *   → 本机 common/tencent_credentials.h（不要提交）→ cpp 里的空/占位宏。
 *   注释和文档里禁止粘贴真实 Key/SK。签名算法见 signedUrl（WebService 文档：path+?+排序query+SK 的 MD5）。
 *
 * 【谁发 HTTP】
 *   - 同步（QEventLoop，默认约 4.5s）：getJson / getUrlJson / weatherNow / geocode / ipLocate。
 *     调用方：管理端 AnalyticsService、StationService 找站兜底。不要在 GUI 主路径长时间连打。
 *   - 异步：UserWindow 自己 QNetworkAccessManager::get(TencentApi::request(url))，
 *     用 weatherNowUrl / rasterTileUrlsXY / directionUrl / openMeteoUrl 等拼 URL。
 *
 * 【失败链路】（订单不受影响）
 *   天气：腾讯 /ws/weather/v1/ → Open-Meteo → ok=false 的模拟因子。
 *   地图瓦片：高德 webrd/wprd → 腾讯 rt*.map.gtimg.com；不走 OSM（国内常打不开）。
 *   路线：腾讯 /ws/direction/v1/（driving、walking 等）→ 用户端可再试 OSRM；失败只改提示。
 *   地址：只腾讯 /ws/geocoder/v1/，不用 Nominatim。
 *   IP 粗定位：ip-api.com → ipwho.is；桌面定位优先 Windows，这个只是城市级参考。
 *
 * 【坐标系】瓦片与世界像素均为 Web Mercator（EPSG:3857），zoom 夹在 [3,18]，瓦片 256px。
 * 【详见】docs/腾讯地图导航配置.md ；Socket 协议与此文件无关。
 */

#include <QJsonObject>
#include <QList>
#include <QNetworkRequest>
#include <QPair>
#include <QString>
#include <QUrl>

namespace TencentApi {

/**
 * 当前腾讯 WebService Key。空串时 signedUrl 仍能拼 URL，但服务端会拒；getJson 会直接返回 {}。
 * 只从环境变量或编译期宏读取，不要在日志里打印返回值。
 */
QString key();

/**
 * WebService 签名 SK。空串则 URL 不带 sig=（控制台未开签名校验时可以）。
 * 同样禁止打印。
 */
QString sk();

/**
 * 拼 https://apis.map.qq.com + path + 排序后的 query；有 SK 则追加 sig。
 *
 * @param path    必须以 / 开头，例如 "/ws/geocoder/v1/"。不要带 host。
 * @param params  业务参数（不要自己放 key/sig，本函数会加 key）。
 * @return        可直接给 QNetworkAccessManager 的绝对 URL。
 *
 * 签名串 = path + "?" + encodedQuery(params含key) + SK，MD5 小写 hex。
 * 参数按 key 升序，同 key 再按 value；键值都做百分号编码。
 */
QUrl signedUrl(const QString &path, QList<QPair<QString, QString>> params);

/**
 * 给任意外部 URL 填 User-Agent、8s 传输超时、跟随不太降级的重定向。
 * 高德瓦片补 Referer: amap.com；腾讯 gtimg 瓦片补 Referer: map.qq.com（防防盗链）。
 */
QNetworkRequest request(const QUrl &url);

/**
 * 同步 GET 腾讯 JSON：signedUrl(path,params) → getUrlJson。
 * Key 为空立即返回 {}。timeoutMs 内未完成则空对象（不抛异常）。
 */
QJsonObject getJson(const QString &path, const QList<QPair<QString, QString>> &params,
                    int timeoutMs = 4500);

/**
 * 腾讯静态图：640×360@2x，中心 lat,lng，红标 S。配额用尽时用户端改用瓦片，不要挡开充。
 */
QUrl staticMapUrl(double lat, double lng);

/** 实况天气解析结果。factor 给负荷预测当天气折扣，不是给用户看的电价。 */
struct Weather {
    bool ok = false;
    QString text;   ///< 晴 / 小雨 等短词
    QString detail; ///< 「晴 26℃ · 海淀区」展示串
    double factor = 1.0; ///< 雨雪雷≈0.90，周末≈0.88，否则≈1.08
    QString source; ///< 「腾讯位置服务」/「Open-Meteo」/「模拟」
};

/** GET /ws/weather/v1/ location=lat,lng type=now 的签名 URL，供异步请求。 */
QUrl weatherNowUrl(double lat, double lng);

/**
 * 解析腾讯天气 JSON。status!=0 或无 realtime[].infos.weather 则 ok=false。
 * 不发起网络。
 */
Weather parseWeather(const QJsonObject &obj);

/**
 * 同步拉实况：腾讯成功则返回；否则 Open-Meteo；再失败 ok=false、source=模拟、仍带 factor。
 * 管理端刷新预测用这条，避免 UI 自己拼两套 URL。
 */
Weather weatherNow(double lat, double lng);

/** 地址转 WGS84 坐标。只用腾讯 geocoder；空地址或 status!=0 则 ok=false。 */
struct Geo {
    bool ok = false;
    QString name;  ///< title 或 address 或原始输入
    double lat = 0;
    double lng = 0;
};

/** 同步 geocode。失败不抛；调用方应继续用本地地标 / 默认海淀点。 */
Geo geocode(const QString &address);

/**
 * 某经纬度对应瓦片列表的第一张（高德）。无网时 URL 仍可拼出，加载失败由调用方换下一张。
 */
QUrl fallbackMapUrl(double lat, double lng, int zoom = 15);

/**
 * 该 lat/lng 所在瓦片的候选 URL：高德矢量、高德影像风、腾讯 rt 瓦片。
 * 用户端 fetchFirstPixmap 按顺序试到第一张能解码的图。
 */
QList<QUrl> rasterTileUrls(double lat, double lng, int zoom = 15);

/**
 * 指定瓦片号 (x,y,z) 的同样三套 URL。x 会对 2^z 取模（允许经度环绕）；y 出界返回空列表。
 * 3×3 马赛克地图的核心：每个格子自己的 x/y，针脚在 widget 坐标画，不烤进瓦片。
 */
QList<QUrl> rasterTileUrlsXY(int tileX, int tileY, int zoom);

/** 把缩放级限制在 [3,18]，与地图滚轮一致。 */
int clampTileZoom(int zoom);

/**
 * 经纬度 → 世界像素（zoom 级整张地图的 256*2^z 坐标系）。
 * 纬度会夹在 Web Mercator 合法范围 ±85.05112878。空指针直接返回。
 */
void latLngToWorldPixel(double lat, double lng, int zoom, double *x, double *y);

/** 世界像素 → 经纬度。与 latLngToWorldPixel 互逆（忽略夹紧误差）。 */
void worldPixelToLatLng(double x, double y, int zoom, double *lat, double *lng);

/**
 * 一张静态图上同时标 A 起点、B 充电桩。无 Key 返回空 URL。
 * 交互地图不再依赖它，保留给需要整图 URL 的旧调用。
 */
QUrl overviewStaticMapUrl(double fromLat, double fromLng, double toLat, double toLng);

/**
 * 让两点都落在同一张 256 瓦片内（边距 32px）的最大缩放，搜索 16→11，不行则 11。
 */
int fitTileZoom(double lat1, double lng1, double lat2, double lng2);

/** Web Mercator 整数瓦片号。zoom 会先夹到 [3,18]。 */
void mapTile(double lat, double lng, int zoom, int *x, int *y);

/**
 * 瓦片内像素 → 经纬度。tilePx 通常 256。用于点击地图选点。
 */
void tilePixelToLatLng(int tileX, int tileY, int zoom, double px, double py, int tilePx,
                       double *lat, double *lng);

/** 经纬度 → 某张瓦片内的像素坐标，供画「我」「桩」针脚。 */
void latLngToTilePixel(double lat, double lng, int zoom, int tileX, int tileY, int tilePx,
                       double *px, double *py);

/** Open-Meteo 当前气温与 WMO weather_code。腾讯天气失败时的异步/同步兜底。 */
QUrl openMeteoUrl(double lat, double lng);

/** 解析 Open-Meteo forecast JSON 的 current 对象。缺 current 则 ok=false。 */
Weather parseOpenMeteo(const QJsonObject &obj);

/**
 * 公共 OSRM：profile 为 driving/foot/bike。query 顺序是 lng,lat（与腾讯 lat,lng 相反）。
 * 校园网可能被墙，只作腾讯路线失败后的备选。
 */
QUrl osrmUrl(const QString &mode, double fromLat, double fromLng, double toLat, double toLng);

/**
 * OSRM code==Ok 时写入第一段 routes[0] 的 distance(米) 与 duration(秒)。
 * @return 解析是否成功；失败不改输出指针。
 */
bool parseOsrm(const QJsonObject &obj, double *meters, int *seconds);

/**
 * 腾讯路线规划 URL。mode: 缺省驾车；walk / bike / bus 换 path。
 * from/to 为 "lat,lng"。公交走 transit/export。
 */
QUrl directionUrl(const QString &mode, double fromLat, double fromLng, double toLat, double toLng);

/**
 * 解析腾讯 direction JSON。status==0 且有 routes[0] 则写出 distance/duration。
 * duration 腾讯为秒；公交结构若缺 distance 仍可能 true。
 */
bool parseDirection(const QJsonObject &obj, double *meters, int *seconds);

/**
 * 同步 GET 任意绝对 URL 并 fromJson 成对象。非 JSON 或超时得到 {}。
 * 给 Open-Meteo / IP 定位 / 已签好名的腾讯 URL 用。
 */
QJsonObject getUrlJson(const QUrl &url, int timeoutMs = 4500);

/** ip-api.com 城市级定位（HTTP）。字段 status/lat/lon/city/regionName。 */
QUrl ipLocateUrl();

/** ipwho.is HTTPS 兜底。字段 success/latitude/longitude/city/region。 */
QUrl ipLocateFallbackUrl();

/** 同时能吃 ip-api 与 ipwho 两种 JSON。坐标全 0 视为失败。 */
Geo parseIpLocate(const QJsonObject &obj);

/**
 * 按公网 IP 粗定位：先 ip-api 4s，再 ipwho 4s。
 * WSL/桌面没有 GPS 时的参考点；正式找桩优先 Windows 定位和地图选点。
 */
Geo ipLocate();

} // namespace TencentApi

#endif
