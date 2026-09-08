/**
 * @file dispatch.cpp
 * @brief Dispatch 实现。头文件写清职责与调用关系；这里是具体校验和 SQL。
 *
 * 读代码顺序建议：registerRoutes → startCharge / stopCharge / settle → calcLive。
 * 金额 fenOf/moneyFen 保证接口仍是元。详见 docs/模块与协作说明.md
 */
#include "dispatch.h"

#include "database.h"
#include "services/chargeservice.h"
#include "services/sessionservice.h"
#include "transport/requestdispatcher.h"

#include <algorithm>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QHash>
#include <QImage>
#include <QIODevice>
#include <QPair>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMap>
#include <QRegularExpression>
#include <QtMath>

/** 按星级和关键词做简易情感分析，只写评价表，不改订单。 */
static QJsonObject analyzeReview(int score, const QString &text)
{
    const QStringList pos = {QString::fromUtf8("快"), QString::fromUtf8("稳"), QString::fromUtf8("方便"),
                             QString::fromUtf8("好"), QString::fromUtf8("满意"), QString::fromUtf8("充足"),
                             QString::fromUtf8("安静"), QString::fromUtf8("清楚"), QString::fromUtf8("合适"),
                             QString::fromUtf8("满")};
    const QStringList neg = {QString::fromUtf8("排队"), QString::fromUtf8("故障"), QString::fromUtf8("贵"),
                             QString::fromUtf8("慢"), QString::fromUtf8("差"), QString::fromUtf8("等"),
                             QString::fromUtf8("掉线"), QString::fromUtf8("挤"), QString::fromUtf8("坏")};
    const QStringList keys = {QString::fromUtf8("快充"), QString::fromUtf8("慢充"), QString::fromUtf8("排队"),
                              QString::fromUtf8("功率"), QString::fromUtf8("价格"), QString::fromUtf8("车位"),
                              QString::fromUtf8("屏幕"), QString::fromUtf8("稳定"), QString::fromUtf8("故障"),
                              QString::fromUtf8("过夜"), QString::fromUtf8("高峰"), QString::fromUtf8("地铁")};
    int p = 0, n = 0;
    for (const auto &w : pos) {
        if (text.contains(w))
            ++p;
    }
    for (const auto &w : neg) {
        if (text.contains(w))
            ++n;
    }
    QJsonArray kw;
    for (const auto &w : keys) {
        if (text.contains(w))
            kw.append(w);
    }
    QString sent = QString::fromUtf8("中性");
    if (score >= 4 && n <= p)
        sent = QString::fromUtf8("正面");
    if (score <= 2 || n > p)
        sent = QString::fromUtf8("负面");
    const double s = qBound(-1.0, (score - 3) / 2.0 + 0.12 * (p - n), 1.0);
    return QJsonObject{{"sentiment", sent}, {"sentimentScore", s}, {"keywords", kw}};
}

static QString dumpDoc(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

/** 头像压到 256px JPEG，便于 BLOB 入库。 */
static QByteArray jpegAvatar(const QByteArray &raw, QString *err)
{
    QImage img;
    if (!img.loadFromData(raw)) {
        *err = QString::fromUtf8("无法识别图片，请选择 jpg / png");
        return {};
    }
    if (img.width() > 256 || img.height() > 256)
        img = img.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (img.format() != QImage::Format_RGB32 && img.format() != QImage::Format_RGB888)
        img = img.convertToFormat(QImage::Format_RGB32);
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    if (!img.save(&buf, "JPEG", 85)) {
        *err = QString::fromUtf8("图片编码失败");
        return {};
    }
    if (out.size() > 200 * 1024) {
        *err = QString::fromUtf8("图片过大，请换一张更小的图");
        return {};
    }
    return out;
}

static qint64 fenOf(double yuan) { return qRound(yuan * 100.0); }

static double money(double v) { return fenOf(v) / 100.0; }

static double moneyFen(qint64 fen) { return fen / 100.0; }

static QString nowStr()
{
    return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
}

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
    double lat = 39.9644;
    double lng = 116.3473;
    bool matched = false;
};

static GeoHit resolveAddress(const QString &raw, const QVector<QVariantMap> &stations)
{
    const QString t = raw.trimmed();
    GeoHit best{QString::fromUtf8("北京理工大学（默认）"), 39.9644, 116.3473, false};
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
    if (!best.matched)
        best.name = QString::fromUtf8("未精确匹配，已按海淀中关村一带检索");
    return best;
}

namespace {
ServiceResult legacyResult(QJsonObject body, const QString &message = QStringLiteral("ok"),
                           int defaultErrorCode = 400)
{
    if (body.contains("error"))
        return ServiceResult::fail(body.value("errorCode").toInt(defaultErrorCode),
                                   body.value("error").toString());
    body.remove("errorCode");
    return ServiceResult::ok(body, message);
}
}

Dispatch::Dispatch(Database *db)
    : db_(db),
      sessions_(std::make_unique<SessionService>(db)),
      charges_(std::make_unique<ChargeService>(db, sessions_.get())),
      requestDispatcher_(std::make_unique<RequestDispatcher>(sessions_.get()))
{
    registerRoutes();
}

Dispatch::~Dispatch() = default;

void Dispatch::registerRoutes()
{
    using SR = ServiceResult;
    requestDispatcher_->addPublicRoute("LOGIN", [this](const QVariantMap &, const QJsonObject &data) {
        return sessions_->login(data);
    });
    requestDispatcher_->addPublicRoute("REGISTER", [this](const QVariantMap &, const QJsonObject &data) {
        return sessions_->registerUser(data);
    });

    const auto add = [this](const QString &type, bool mutating,
                            const RequestDispatcher::Handler &handler) {
        requestDispatcher_->addAuthenticatedRoute(type, mutating, handler);
    };
    add("UPDATE_PROFILE", false, [this](const QVariantMap &u, const QJsonObject &d) {
        return legacyResult(updateProfile(u, d), QString::fromUtf8("保存成功"));
    });
    add("RECHARGE", true, [this](const QVariantMap &u, const QJsonObject &d) {
        return legacyResult(recharge(u, d), QString::fromUtf8("充值成功"));
    });
    add("QUERY_STATIONS", false, [this](const QVariantMap &u, const QJsonObject &d) {
        return SR::ok(queryStations(u, d));
    });
    add("CLOSE_ACCOUNT", false, [this](const QVariantMap &u, const QJsonObject &) {
        return legacyResult(closeAccount(u), QString::fromUtf8("账号已注销，历史订单与评价已留档"));
    });
    add("QUERY_PILES", false, [this](const QVariantMap &u, const QJsonObject &d) {
        return legacyResult(queryPiles(u, d), QStringLiteral("ok"), 404);
    });
    add("START_CHARGE", true, [this](const QVariantMap &u, const QJsonObject &d) {
        return legacyResult(charges_->start(u, d), QString::fromUtf8("充电已开始"));
    });
    add("CHARGE_STATUS", false, [this](const QVariantMap &u, const QJsonObject &) {
        const QJsonObject body = charges_->status(u);
        return SR::ok(body, body.value("order").isNull() ? QString::fromUtf8("无进行中订单")
                                                          : QStringLiteral("ok"));
    });
    add("STOP_CHARGE", true, [this](const QVariantMap &u, const QJsonObject &) {
        return legacyResult(charges_->stop(u), QString::fromUtf8("请结算订单"));
    });
    add("SETTLE_ORDER", true, [this](const QVariantMap &u, const QJsonObject &) {
        return legacyResult(charges_->settle(u), QString::fromUtf8("结算成功"));
    });
    add("LIST_ORDERS", false, [this](const QVariantMap &u, const QJsonObject &) {
        return SR::ok(charges_->listOrders(u));
    });
    add("LIST_RECHARGE", false, [this](const QVariantMap &u, const QJsonObject &) {
        return SR::ok(listRecharge(u));
    });
    add("LIST_RESERVATIONS", false, [this](const QVariantMap &u, const QJsonObject &) {
        return SR::ok(listReservations(u));
    });
    add("RESERVE_PILE", true, [this](const QVariantMap &u, const QJsonObject &d) {
        return legacyResult(reservePile(u, d), QString::fromUtf8("预约成功，15分钟内有效"));
    });
    add("CANCEL_RESERVE", true, [this](const QVariantMap &u, const QJsonObject &) {
        return legacyResult(cancelReserve(u), QString::fromUtf8("已取消预约"));
    });
    add("REVIEW_STATION", false, [this](const QVariantMap &u, const QJsonObject &d) {
        QJsonObject body = reviewStation(u, d);
        const QString message = body.value("updated").toBool()
            ? QString::fromUtf8("已更新你对这根桩的评价") : QString::fromUtf8("评价已提交");
        return legacyResult(body, message);
    });
    add("LIST_PILE_REVIEWS", false, [this](const QVariantMap &u, const QJsonObject &d) {
        return legacyResult(listPileReviews(u, d), QStringLiteral("ok"), 404);
    });
    add("HEARTBEAT", false, [](const QVariantMap &, const QJsonObject &) {
        return SR::ok();
    });
}

QJsonObject Dispatch::handle(const QJsonObject &request)
{
    return requestDispatcher_->handle(request);
}

int Dispatch::userIdOfToken(const QString &token) const
{
    return sessions_->userIdOfToken(token);
}

/** 改昵称和/或头像（JPEG BLOB）；clearAvatar 则删 user_avatar。 */
QJsonObject Dispatch::updateProfile(const QVariantMap &user, const QJsonObject &data)
{
    const int uid = user.value("id").toInt();
    if (data.contains("nickname")) {
        const QString nick = data.value("nickname").toString().trimmed();
        if (nick.size() < 1 || nick.size() > 20)
            return QJsonObject{{"error", QString::fromUtf8("昵称长度须为 1~20 个字符")}};
        db_->execute("UPDATE user SET nickname=? WHERE id=?", {nick, uid});
    }
    if (data.value("clearAvatar").toBool()) {
        db_->execute("DELETE FROM user_avatar WHERE user_id=?", {uid});
        db_->execute("UPDATE user SET avatar_path=? WHERE id=?", {QString(), uid});
    } else if (data.contains("avatarBase64")) {
        QString b64 = data.value("avatarBase64").toString().trimmed();
        if (b64.contains(QLatin1Char(',')))
            b64 = b64.section(QLatin1Char(','), -1);
        const QByteArray raw = QByteArray::fromBase64(b64.toLatin1());
        if (raw.isEmpty())
            return QJsonObject{{"error", QString::fromUtf8("请选择有效的头像图片")}};
        if (raw.size() > 2 * 1024 * 1024)
            return QJsonObject{{"error", QString::fromUtf8("图片不能超过 2MB")}};
        QString err;
        const QByteArray jpeg = jpegAvatar(raw, &err);
        if (jpeg.isEmpty())
            return QJsonObject{{"error", err}};
        db_->execute("INSERT OR REPLACE INTO user_avatar(user_id,mime,data,updated_at) VALUES(?,?,?,?)",
                     {uid, QStringLiteral("image/jpeg"), jpeg, nowStr()});
        db_->execute("UPDATE user SET avatar_path=? WHERE id=?", {QStringLiteral("db"), uid});
    } else if (data.contains("avatarPath")) {
        db_->execute("UPDATE user SET avatar_path=? WHERE id=?", {data.value("avatarPath").toString(), uid});
    }
    if (data.contains("address")) {
        const QString addr = data.value("address").toString().trimmed();
        if (addr.size() > 80)
            return QJsonObject{{"error", QString::fromUtf8("住址过长，请控制在 80 字以内")}};
        const auto hit = resolveAddress(addr, db_->query("SELECT * FROM station"));
        db_->execute("UPDATE user SET address=?, loc_lat=?, loc_lng=? WHERE id=?",
                     {addr, hit.lat, hit.lng, uid});
    }
    auto u = db_->one("SELECT * FROM user WHERE id=?", {uid});
    return QJsonObject{{"user", sessions_->publicUser(u)}};
}

/** 模拟充值，单笔 ≤ 10000 元，内部按分入账。 */
/** 事务：加余额 + 写 recharge_log。单笔不超过 10000 元。 */
QJsonObject Dispatch::recharge(const QVariantMap &user, const QJsonObject &data)
{
    bool ok = false;
    const double amount = data.value("amount").toVariant().toDouble(&ok);
    if (!ok || amount <= 0 || amount > 10000)
        return QJsonObject{{"error", QString::fromUtf8("单笔充值须大于 0 且不超过 10000 元")}};
    const double add = money(amount);
    const QString t = nowStr();
    int logId = 0;
    if (!db_->transaction([&] {
            const auto fresh = db_->one("SELECT balance FROM user WHERE id=?", {user.value("id")});
            const double nb = moneyFen(fenOf(fresh.value("balance").toDouble()) + fenOf(add));
            if (db_->execute("UPDATE user SET balance=? WHERE id=?", {nb, user.value("id")}) < 0)
                return false;
            logId = db_->execute("INSERT INTO recharge_log(user_id,amount,result,created_at) VALUES(?,?,?,?)",
                                 {user.value("id"), add, QString::fromUtf8("成功"), t});
            return logId > 0;
        })) {
        return QJsonObject{{"error", QString::fromUtf8("充值未写入，请重试")}};
    }
    const QString tradeNo = QString("RC%1").arg(logId, 8, 10, QChar('0'));
    auto u = db_->one("SELECT * FROM user WHERE id=?", {user.value("id")});
    return QJsonObject{{"user", sessions_->publicUser(u)}, {"tradeNo", tradeNo}, {"amount", add}};
}

/** 按地址关键字或半径列附近电站，只读 station。 */
QJsonObject Dispatch::queryStations(const QVariantMap &user, const QJsonObject &data)
{
    expireReservations();
    const auto stations = db_->query("SELECT * FROM station");
    QString address = data.value("address").toString().trimmed();
    if (address.isEmpty())
        address = user.value("address").toString().trimmed();
    GeoHit hit;
    if (!address.isEmpty()) {
        hit = resolveAddress(address, stations);
    } else if (user.contains("loc_lat")) {
        hit.lat = user.value("loc_lat").toDouble();
        hit.lng = user.contains("loc_lng") ? user.value("loc_lng").toDouble() : 116.3473;
        hit.name = QString::fromUtf8("上次定位");
        hit.matched = true;
    } else if (data.contains("lat") || data.contains("lng")) {
        hit.lat = data.value("lat").toDouble(39.9644);
        hit.lng = data.value("lng").toDouble(116.3473);
        hit.name = QString::fromUtf8("指定坐标");
        hit.matched = true;
    } else {
        hit = resolveAddress(QString(), stations);
    }
    if (!address.isEmpty()) {
        db_->execute("UPDATE user SET address=?, loc_lat=?, loc_lng=? WHERE id=?",
                     {address, hit.lat, hit.lng, user.value("id")});
    }
    const double lat = hit.lat;
    const double lng = hit.lng;
    const double radius = data.value("radiusKm").toDouble(20);
    QJsonArray arr;
    QVector<QJsonObject> tmp;
    QVector<QJsonObject> nearPiles;
    for (const auto &s : stations) {
        const auto piles = db_->query("SELECT * FROM pile WHERE station_id=?", {s.value("id")});
        int idle = 0, fast = 0, slow = 0;
        const double dist = qRound(haversine(lat, lng, s.value("lat").toDouble(), s.value("lng").toDouble()) * 10) / 10.0;
        for (const auto &p : piles) {
            if (pileIsIdle(p))
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
                    {"idle", pileIsIdle(p)},
                });
            }
        }
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

/** 账号标注销、作废 token；历史订单留下，手机号不能再注册。 */
QJsonObject Dispatch::closeAccount(const QVariantMap &user)
{
    const int uid = user.value("id").toInt();
    auto open = charges_->openOrder(uid);
    if (!open.isEmpty())
        return QJsonObject{{"errorCode", 409},
                           {"error", QString::fromUtf8("请先结束充电并完成结算，再注销账号")}};
    auto res = db_->one("SELECT * FROM reservation WHERE user_id=? AND status='有效'", {uid});
    if (!res.isEmpty())
        db_->execute("UPDATE reservation SET status=? WHERE id=?", {QString::fromUtf8("已取消"), res.value("id")});
    const QString t = nowStr();
    db_->execute("UPDATE user SET status=?, close_reason=?, closed_at=? WHERE id=?",
                 {QString::fromUtf8("注销"), QString::fromUtf8("用户注销"), t, uid});
    db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                 {user.value("phone").toString(), QString::fromUtf8("用户注销"),
                  QString("uid=%1").arg(uid), QString::fromUtf8("留档禁用"), t});
    sessions_->dropUser(uid);
    return QJsonObject{{"closed", true}, {"userId", uid}};
}

/** 先过期预约，再列出某站的桩及是否可预约/可开充。 */
QJsonObject Dispatch::queryPiles(const QVariantMap &user, const QJsonObject &data)
{
    expireReservations();
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
        auto res = activeReserve(p.value("id").toInt());
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

/** 只查 admin 表，SHA256 比对；失败文案统一「账号或密码错误」。 */
QJsonObject Dispatch::adminLogin(const QString &user, const QString &pwd)
{
    const QByteArray hash = QCryptographicHash::hash(pwd.toUtf8(), QCryptographicHash::Sha256).toHex();
    auto row = db_->one("SELECT * FROM admin WHERE username=?", {user});
    if (row.isEmpty() || row.value("password_hash").toString() != QString::fromLatin1(hash))
        return QJsonObject{{"ok", false}, {"message", QString::fromUtf8("账号或密码错误")}};
    return QJsonObject{{"ok", true}, {"username", user}};
}

/** 写入 admin；用户名 3~16 位字母开头。 */
QJsonObject Dispatch::adminRegister(const QString &user, const QString &pwd)
{
    if (!QRegularExpression(QStringLiteral("^[A-Za-z][A-Za-z0-9_]{2,15}$")).match(user).hasMatch())
        return QJsonObject{{"ok", false}, {"message", QString::fromUtf8("账号须为 3~16 位，字母开头，可含数字和下划线")}};
    if (pwd.size() < 6 || pwd.size() > 20)
        return QJsonObject{{"ok", false}, {"message", QString::fromUtf8("密码长度须为 6~20 位")}};
    if (!db_->one("SELECT id FROM admin WHERE username=?", {user}).isEmpty())
        return QJsonObject{{"ok", false}, {"message", QString::fromUtf8("该账号已注册")}};
    const QByteArray hash = QCryptographicHash::hash(pwd.toUtf8(), QCryptographicHash::Sha256).toHex();
    db_->execute("INSERT INTO admin(username,password_hash,created_at) VALUES(?,?,?)",
                 {user, QString::fromLatin1(hash), nowStr()});
    return QJsonObject{{"ok", true}, {"username", user}, {"registered", true}};
}

/** 今日/本月/累计营收与电量，给 KPI 和折线。 */
QJsonObject Dispatch::salesSummary() const
{
    const QString today = QDate::currentDate().toString("yyyy-MM-dd");
    const QString month = QDate::currentDate().toString("yyyy-MM");
    auto sumWhere = [this](const QString &w, const QVariantList &a) {
        auto r = db_->one("SELECT IFNULL(SUM(amount),0) AS s FROM charge_order WHERE status='已完成' AND " + w, a);
        return money(r.value("s").toDouble());
    };
    QJsonArray trend;
    for (int i = 29; i >= 0; --i) {
        const QString d = QDate::currentDate().addDays(-i).toString("yyyy-MM-dd");
        trend.append(QJsonObject{{"date", d}, {"amount", sumWhere("substr(start_time,1,10)=?", {d})}});
    }
    return QJsonObject{
        {"today", sumWhere("substr(start_time,1,10)=?", {today})},
        {"month", sumWhere("substr(start_time,1,7)=?", {month})},
        {"total", sumWhere("1=1", {})},
        {"trend", trend},
    };
}

/** 闲置/在用/故障计数，给饼图。 */
QJsonObject Dispatch::pileStatusStats() const
{
    const auto rows = db_->query("SELECT status, COUNT(*) AS n FROM pile GROUP BY status");
    QMap<QString, int> counts{{QString::fromUtf8("在用"), 0}, {QString::fromUtf8("闲置"), 0}, {QString::fromUtf8("故障"), 0}};
    int total = 0;
    for (const auto &r : rows) {
        counts[r.value("status").toString()] = r.value("n").toInt();
        total += r.value("n").toInt();
    }
    if (total == 0)
        total = 1;
    QJsonArray items;
    const QStringList order = {QString::fromUtf8("在用"), QString::fromUtf8("闲置"), QString::fromUtf8("故障")};
    double acc = 0;
    for (int i = 0; i < order.size(); ++i) {
        const int n = counts.value(order[i]);
        double pct = qRound(n * 1000.0 / total) / 10.0;
        if (i == order.size() - 1)
            pct = qRound((100.0 - acc) * 10) / 10.0;
        else
            acc += pct;
        items.append(QJsonObject{{"status", order[i]}, {"count", n}, {"percent", pct}});
    }
    return QJsonObject{{"total", total}, {"items", items}};
}

/** 全部电桩含站名。 */
QVector<QVariantMap> Dispatch::listPiles() const
{
    return db_->query("SELECT p.*, s.name AS station_name FROM pile p JOIN station s ON s.id=p.station_id ORDER BY p.pile_no");
}

/** 全部电站。 */
QVector<QVariantMap> Dispatch::listStations() const
{
    auto stations = db_->query("SELECT * FROM station ORDER BY id");
    QVector<QVariantMap> out;
    for (auto s : stations) {
        auto piles = db_->query("SELECT * FROM pile WHERE station_id=?", {s.value("id")});
        int online = 0;
        for (const auto &p : piles)
            if (p.value("status").toString() != QString::fromUtf8("故障"))
                ++online;
        s.insert("totalPiles", piles.size());
        s.insert("onlineRate", piles.isEmpty() ? 0 : qRound(1000.0 * online / piles.size()) / 10.0);
        out.append(s);
    }
    return out;
}

/** 按手机号或昵称检索用户。 */
QVector<QVariantMap> Dispatch::listUsers(const QString &keyword) const
{
    const QString sql = QStringLiteral(
        "SELECT u.id, u.phone, u.nickname, u.balance, u.created_at, u.status, u.address, u.close_reason, "
        "CASE WHEN a.user_id IS NULL THEN 0 ELSE 1 END AS has_avatar "
        "FROM user u LEFT JOIN user_avatar a ON a.user_id=u.id %1 ORDER BY u.id DESC");
    if (keyword.isEmpty())
        return db_->query(sql.arg(QString()));
    return db_->query(sql.arg(QStringLiteral("WHERE u.phone LIKE ?")), {"%" + keyword + "%"});
}

/** 故障桩改回闲置，写审计「远程重启」。 */
QString Dispatch::rebootPile(int pileId)
{
    auto pile = db_->one("SELECT * FROM pile WHERE id=?", {pileId});
    if (pile.isEmpty())
        return QString::fromUtf8("电桩不存在");
    if (pile.value("status").toString() == QString::fromUtf8("在用"))
        return QString::fromUtf8("充电中的电桩不可重启");
    QString msg = QString::fromUtf8("重启指令已发送");
    if (pile.value("status").toString() == QString::fromUtf8("故障")) {
        db_->execute("UPDATE pile SET status=? WHERE id=?", {QString::fromUtf8("闲置"), pileId});
        msg = QString::fromUtf8("重启指令已发送，故障已恢复为闲置");
    }
    db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                 {"admin", QString::fromUtf8("远程重启"), pile.value("pile_no"), QString::fromUtf8("成功"), nowStr()});
    return msg;
}

/** 冻结则 dropUser；注销账号不能再改状态。 */
void Dispatch::freezeUser(int userId, bool freeze)
{
    auto u = db_->one("SELECT * FROM user WHERE id=?", {userId});
    if (u.isEmpty() || u.value("status").toString() == QString::fromUtf8("注销"))
        return;
    if (freeze) {
        auto order = charges_->openOrder(userId);
        if (!order.isEmpty() && order.value("status").toString() == QString::fromUtf8("充电中"))
            charges_->stop(u);
        db_->execute("UPDATE user SET status=?, close_reason=?, closed_at=? WHERE id=?",
                     {QString::fromUtf8("冻结"), QString::fromUtf8("管理员冻结"), nowStr(), userId});
        sessions_->dropUser(userId);
        db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                     {"admin", QString::fromUtf8("冻结用户"), u.value("phone"), QString::fromUtf8("成功"), nowStr()});
    } else {
        db_->execute("UPDATE user SET status=?, close_reason=?, closed_at=? WHERE id=?",
                     {QString::fromUtf8("正常"), QString(), QString(), userId});
        db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                     {"admin", QString::fromUtf8("解冻用户"), u.value("phone"), QString::fromUtf8("成功"), nowStr()});
    }
}

/** 插 station，再按 pileCount 生成闲置桩。 */
int Dispatch::addStation(const QVariantMap &data)
{
    const QString name = data.value("name").toString().trimmed();
    const QString address = data.value("address").toString().trimmed();
    int sid = 0;
    const int n = qBound(1, data.value("pileCount", 4).toInt(), 20);
    db_->transaction([&] {
        sid = db_->execute(
            "INSERT INTO station(name,address,lng,lat,price_per_kwh) VALUES(?,?,?,?,?)",
            {name, address, data.value("lng"), data.value("lat"), data.value("pricePerKwh", 1.3)});
        if (sid <= 0)
            return false;
        for (int i = 1; i <= n; ++i) {
            const bool fast = i <= qMax(1, n / 2);
            if (db_->execute("INSERT INTO pile(pile_no,station_id,type,power_kw,status) VALUES(?,?,?,?,?)",
                             {QString("ST%1-P%2").arg(sid, 2, 10, QChar('0')).arg(i, 2, 10, QChar('0')),
                              sid, fast ? QString::fromUtf8("快充") : QString::fromUtf8("慢充"),
                              fast ? 60.0 : 7.0, QString::fromUtf8("闲置")})
                <= 0)
                return false;
        }
        return true;
    });
    if (sid > 0)
        applyDefaultTariff(sid);
    return sid;
}

/** 谷 0–7/22–24、平 7–17、峰 17–22，系数乘站点标价。 */
/** 写入谷 0–7/22–24、平 7–17、峰 17–22。 */
QString Dispatch::applyDefaultTariff(int stationId)
{
    auto st = db_->one("SELECT * FROM station WHERE id=?", {stationId});
    if (st.isEmpty())
        return QString::fromUtf8("电站不存在");
    const double base = st.value("price_per_kwh").toDouble();
    const double valley = money(base * 0.85);
    const double peak = money(base * 1.25);
    db_->transaction([&] {
        if (db_->execute("DELETE FROM tariff_rule WHERE station_id=?", {stationId}) < 0)
            return false;
        const struct { int a, b; double p; const char *lab; } rows[] = {
            {0, 7, valley, "谷"}, {7, 17, base, "平"}, {17, 22, peak, "峰"}, {22, 24, valley, "谷"},
        };
        for (const auto &r : rows) {
            if (db_->execute("INSERT INTO tariff_rule(station_id,start_hour,end_hour,price_per_kwh,label) VALUES(?,?,?,?,?)",
                             {stationId, r.a, r.b, r.p, QString::fromUtf8(r.lab)})
                <= 0)
                return false;
        }
        return true;
    });
    db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                 {"admin", QString::fromUtf8("启用分时电价"), st.value("name"),
                  QString::fromUtf8("谷/平/峰"), nowStr()});
    return QString::fromUtf8("已按基准电价启用谷 0.85 / 平 1.0 / 峰 1.25");
}

/** 采纳建议：保证有默认分时，再把该站峰价上浮并标 adopted。 */
QString Dispatch::adoptDispatchPlan(int planId)
{
    auto plan = db_->one("SELECT * FROM dispatch_plan WHERE id=?", {planId});
    if (plan.isEmpty())
        return QString::fromUtf8("没有这条调度建议");
    auto st = db_->one("SELECT * FROM station WHERE name=?", {plan.value("station")});
    if (st.isEmpty())
        return QString::fromUtf8("对不上电站名，请先刷新分析");
    applyDefaultTariff(st.value("id").toInt());
    const double peak = money(st.value("price_per_kwh").toDouble() * 1.35);
    db_->execute("UPDATE tariff_rule SET price_per_kwh=? WHERE station_id=? AND label=?",
                 {peak, st.value("id"), QString::fromUtf8("峰")});
    db_->execute("UPDATE dispatch_plan SET adopted=1, adopted_at=? WHERE id=?", {nowStr(), planId});
    db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                 {"admin", QString::fromUtf8("采纳调度"), plan.value("station"),
                  QString::fromUtf8("峰段上浮"), nowStr()});
    return QString::fromUtf8("已采纳：该站峰时段电价上浮，引导错峰");
}

/** 推送用：等价于该用户的 chargeStatus。 */
QJsonObject Dispatch::chargePushFor(int userId) const
{
    return charges_->pushFor(userId);
}

/** 闲置桩标故障；充电中须先强制结束。 */
QString Dispatch::markPileFault(int pileId)
{
    auto pile = db_->one("SELECT * FROM pile WHERE id=?", {pileId});
    if (pile.isEmpty())
        return QString::fromUtf8("电桩不存在");
    if (pile.value("status").toString() == QString::fromUtf8("在用"))
        return QString::fromUtf8("充电中的电桩请先强制结束订单，再标故障");
    const QString t = nowStr();
    db_->execute("UPDATE pile SET status=?, fault_code=?, fault_at=? WHERE id=?",
                 {QString::fromUtf8("故障"), QString::fromUtf8("ADMIN"), t, pileId});
    db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                 {"admin", QString::fromUtf8("标记故障"), pile.value("pile_no"), QString::fromUtf8("成功"), t});
    return QString::fromUtf8("已标记为故障，用户端不可再开充");
}

/** 与 rebootPile 相同：故障→闲置。 */
QString Dispatch::restorePile(int pileId)
{
    return rebootPile(pileId);
}

/** 改站名/地址/经纬/基准电价，写审计。 */
QString Dispatch::updateStation(int stationId, const QVariantMap &data)
{
    auto st = db_->one("SELECT * FROM station WHERE id=?", {stationId});
    if (st.isEmpty())
        return QString::fromUtf8("电站不存在");
    const QString name = data.value("name").toString().trimmed();
    const QString address = data.value("address").toString().trimmed();
    const double price = data.value("pricePerKwh").toDouble();
    if (name.isEmpty() || address.isEmpty() || price <= 0)
        return QString::fromUtf8("站名、地址不能空，电价须大于 0");
    db_->execute("UPDATE station SET name=?, address=?, lng=?, lat=?, price_per_kwh=? WHERE id=?",
                 {name, address, data.value("lng", st.value("lng")), data.value("lat", st.value("lat")),
                  money(price), stationId});
    db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                 {"admin", QString::fromUtf8("修改电站"), name, QString::fromUtf8("成功"), nowStr()});
    return QString::fromUtf8("电站已更新");
}

/** 运营指定订单走 stopCharge，并写审计。 */
QString Dispatch::forceStopOrder(int orderId)
{
    return charges_->forceStop(orderId);
}

/** 充电中则先强制停，再 settle 扣款，写审计。 */
QString Dispatch::forceSettleOrder(int orderId)
{
    return charges_->forceSettle(orderId);
}

/** 最近若干条 audit_log，最多 200。 */
QVector<QVariantMap> Dispatch::listAudit(int limit) const
{
    return db_->query("SELECT * FROM audit_log ORDER BY id DESC LIMIT ?", {qBound(1, limit, 200)});
}

/** 断线超时：对该用户充电中订单走 stopCharge，审计写「断线释放」。 */
void Dispatch::releaseStaleSession(int userId)
{
    charges_->releaseStaleSession(userId);
}

/** 过期仍「有效」的预约改为已取消并标 no_show。 */
void Dispatch::expireReservations() const
{
    db_->execute("UPDATE reservation SET status=?, no_show=1 WHERE status=? AND expire_at < ?",
                 {QString::fromUtf8("已取消"), QString::fromUtf8("有效"), nowStr()});
}

/** 该桩当前仍有效的预约；没有则空。 */
QVariantMap Dispatch::activeReserve(int pileId) const
{
    return db_->one(
        "SELECT * FROM reservation WHERE pile_id=? AND status='有效' ORDER BY id DESC LIMIT 1", {pileId});
}

/** 闲置且没有任何有效预约。 */
bool Dispatch::pileIsIdle(const QVariantMap &pile) const
{
    if (pile.value("status").toString() != QString::fromUtf8("闲置"))
        return false;
    return activeReserve(pile.value("id").toInt()).isEmpty();
}

/** 当前用户充值流水。 */
QJsonObject Dispatch::listRecharge(const QVariantMap &user)
{
    const auto rows = db_->query(
        "SELECT * FROM recharge_log WHERE user_id=? ORDER BY id DESC LIMIT 30",
        {user.value("id")});
    QJsonArray arr;
    for (const auto &r : rows) {
        arr.append(QJsonObject{
            {"id", r.value("id").toInt()},
            {"tradeNo", QString("RC%1").arg(r.value("id").toInt(), 8, 10, QChar('0'))},
            {"amount", r.value("amount").toDouble()},
            {"result", r.value("result").toString()},
            {"createdAt", r.value("created_at").toString()},
        });
    }
    return QJsonObject{{"records", arr}, {"balance", money(user.value("balance").toDouble())}};
}

/** 先过期处理，再返回该用户预约。 */
QJsonObject Dispatch::listReservations(const QVariantMap &user)
{
    expireReservations();
    const auto rows = db_->query(
        "SELECT r.id, r.pile_id, r.status, r.expire_at, r.created_at, "
        "p.pile_no, p.type, p.power_kw, p.status AS pile_status, "
        "s.id AS station_id, s.name AS station_name, s.address, s.lng, s.lat, s.price_per_kwh "
        "FROM reservation r "
        "JOIN pile p ON p.id=r.pile_id "
        "JOIN station s ON s.id=p.station_id "
        "WHERE r.user_id=? AND r.status='有效' "
        "ORDER BY r.expire_at, r.id DESC",
        {user.value("id")});

    QJsonArray arr;
    const QDateTime now = QDateTime::currentDateTime();
    for (const auto &r : rows) {
        const QDateTime expire =
            QDateTime::fromString(r.value("expire_at").toString(), "yyyy-MM-dd HH:mm:ss");
        const int remaining = expire.isValid() ? qMax(0, int(now.secsTo(expire))) : 0;
        arr.append(QJsonObject{
            {"id", r.value("id").toInt()},
            {"pileId", r.value("pile_id").toInt()},
            {"status", r.value("status").toString()},
            {"expireAt", r.value("expire_at").toString()},
            {"createdAt", r.value("created_at").toString()},
            {"remainingSeconds", remaining},
            {"pileNo", r.value("pile_no").toString()},
            {"type", r.value("type").toString()},
            {"powerKw", r.value("power_kw").toDouble()},
            {"pileStatus", r.value("pile_status").toString()},
            {"stationId", r.value("station_id").toInt()},
            {"stationName", r.value("station_name").toString()},
            {"address", r.value("address").toString()},
            {"lng", r.value("lng").toDouble()},
            {"lat", r.value("lat").toDouble()},
            {"pricePerKwh", r.value("price_per_kwh").toDouble()},
        });
    }
    return QJsonObject{{"reservations", arr}};
}

/** 闲置且无他人预约才插入 reservation(有效)。 */
QJsonObject Dispatch::reservePile(const QVariantMap &user, const QJsonObject &data)
{
    expireReservations();
    const int pileId = data.value("pileId").toInt();
    auto pile = db_->one("SELECT * FROM pile WHERE id=?", {pileId});
    if (pile.isEmpty())
        return QJsonObject{{"errorCode", 404}, {"error", QString::fromUtf8("电桩不存在")}};
    if (pile.value("status").toString() != QString::fromUtf8("闲置"))
        return QJsonObject{{"errorCode", 409}, {"error", QString::fromUtf8("仅闲置电桩可预约")}};
    if (!activeReserve(pileId).isEmpty())
        return QJsonObject{{"errorCode", 409}, {"error", QString::fromUtf8("该桩已被预约")}};
    auto mine = db_->one("SELECT * FROM reservation WHERE user_id=? AND status='有效'", {user.value("id")});
    if (!mine.isEmpty())
        return QJsonObject{{"errorCode", 409}, {"error", QString::fromUtf8("您已有有效预约，请先取消或履约")}};
    const QString expire = QDateTime::currentDateTime().addSecs(15 * 60).toString("yyyy-MM-dd HH:mm:ss");
    const int rid = db_->execute(
        "INSERT INTO reservation(user_id,pile_id,status,expire_at,created_at) VALUES(?,?,?,?,?)",
        {user.value("id"), pileId, QString::fromUtf8("有效"), expire, nowStr()});
    return QJsonObject{{"reservation", QJsonObject{{"id", rid}, {"pileId", pileId}, {"expireAt", expire}}}};
}

/** 取消本人当前有效预约。 */
QJsonObject Dispatch::cancelReserve(const QVariantMap &user)
{
    auto row = db_->one("SELECT * FROM reservation WHERE user_id=? AND status='有效' ORDER BY id DESC LIMIT 1",
                        {user.value("id")});
    if (row.isEmpty())
        return QJsonObject{{"errorCode", 404}, {"error", QString::fromUtf8("没有有效预约")}};
    db_->execute("UPDATE reservation SET status=? WHERE id=?", {QString::fromUtf8("已取消"), row.value("id")});
    return QJsonObject{};
}

/** 必须有文字；写 station_review 并浅层情感写入 review_doc。 */
QJsonObject Dispatch::reviewStation(const QVariantMap &user, const QJsonObject &data)
{
    const int sid = data.value("stationId").toInt();
    const int pileId = data.value("pileId").toInt();
    const int score = data.value("score").toInt();
    const QString comment = data.value("comment").toString().trimmed();
    if (score < 1 || score > 5)
        return QJsonObject{{"error", QString::fromUtf8("评分须为 1~5 分")}};
    if (comment.size() < 2 || comment.size() > 300)
        return QJsonObject{{"error", QString::fromUtf8("评语须为 2~300 个字，不能只打分")}};
    auto pile = db_->one("SELECT id, station_id FROM pile WHERE id=?", {pileId});
    if (pile.isEmpty())
        return QJsonObject{{"error", QString::fromUtf8("电桩不存在")}};
    const int stationId = pile.value("station_id").toInt();
    if (sid > 0 && sid != stationId)
        return QJsonObject{{"error", QString::fromUtf8("电桩与电站不匹配")}};
    const int uid = user.value("id").toInt();
    const QString t = nowStr();
    const QJsonObject nlp = analyzeReview(score, comment);
    QJsonObject doc{
        {"schema", QStringLiteral("chargehub.review.v1")},
        {"userId", uid},
        {"pileId", pileId},
        {"stationId", stationId},
        {"score", score},
        {"comment", comment},
        {"createdAt", t},
        {"updatedAt", t},
        {"nlp", nlp},
    };
    auto old = db_->one("SELECT id FROM station_review WHERE user_id=? AND pile_id=?", {uid, pileId});
    auto oldDoc = db_->one("SELECT id, doc FROM review_doc WHERE user_id=? AND pile_id=?", {uid, pileId});
    if (!oldDoc.isEmpty()) {
        auto prev = QJsonDocument::fromJson(oldDoc.value("doc").toString().toUtf8()).object();
        if (!prev.value("createdAt").toString().isEmpty())
            doc["createdAt"] = prev.value("createdAt");
    }
    if (!old.isEmpty()) {
        db_->execute("UPDATE station_review SET score=?, comment=?, created_at=? WHERE id=?",
                     {score, comment, t, old.value("id")});
        if (!oldDoc.isEmpty())
            db_->execute("UPDATE review_doc SET doc=?, created_at=? WHERE id=?",
                         {dumpDoc(doc), t, oldDoc.value("id")});
        else
            db_->execute("INSERT INTO review_doc(pile_id,user_id,doc,created_at) VALUES(?,?,?,?)",
                         {pileId, uid, dumpDoc(doc), t});
        return QJsonObject{{"updated", true}, {"nlp", nlp}};
    }
    db_->execute(
        "INSERT INTO station_review(user_id,station_id,pile_id,score,comment,created_at) VALUES(?,?,?,?,?,?)",
        {uid, stationId, pileId, score, comment, t});
    db_->execute("INSERT INTO review_doc(pile_id,user_id,doc,created_at) VALUES(?,?,?,?)",
                 {pileId, uid, dumpDoc(doc), t});
    return QJsonObject{{"updated", false}, {"nlp", nlp}};
}

/** 某桩评价列表、均分和情感摘要。 */
QJsonObject Dispatch::listPileReviews(const QVariantMap &user, const QJsonObject &data)
{
    const int pileId = data.value("pileId").toInt();
    auto pile = db_->one("SELECT * FROM pile WHERE id=?", {pileId});
    if (pile.isEmpty())
        return QJsonObject{{"error", QString::fromUtf8("电桩不存在")}};
    auto station = db_->one("SELECT * FROM station WHERE id=?", {pile.value("station_id")});
    auto docs = db_->query(
        "SELECT d.id, d.doc, u.nickname FROM review_doc d JOIN user u ON u.id=d.user_id "
        "WHERE d.pile_id=? ORDER BY d.id DESC",
        {pileId});
    if (docs.isEmpty()) {
        const auto rows = db_->query(
            "SELECT r.id, r.score, r.comment, r.created_at, r.user_id, u.nickname FROM station_review r "
            "JOIN user u ON u.id=r.user_id WHERE r.pile_id=? ORDER BY r.id DESC",
            {pileId});
        for (const auto &r : rows) {
            QJsonObject d{
                {"schema", QStringLiteral("chargehub.review.v1")},
                {"userId", r.value("user_id").toInt()},
                {"pileId", pileId},
                {"score", r.value("score").toInt()},
                {"comment", r.value("comment").toString()},
                {"createdAt", r.value("created_at").toString()},
                {"nlp", analyzeReview(r.value("score").toInt(), r.value("comment").toString())},
            };
            docs.append(QVariantMap{
                {"id", r.value("id")},
                {"doc", dumpDoc(d)},
                {"nickname", r.value("nickname")},
            });
        }
    }
    QJsonArray reviews;
    double sum = 0;
    int pos = 0, neu = 0, neg = 0;
    QMap<QString, int> kwc;
    QJsonObject mine;
    const int uid = user.value("id").toInt();
    for (const auto &r : docs) {
        auto o = QJsonDocument::fromJson(r.value("doc").toString().toUtf8()).object();
        if (!o.contains("nlp"))
            o.insert("nlp", analyzeReview(o.value("score").toInt(), o.value("comment").toString()));
        const auto nlp = o.value("nlp").toObject();
        const QString sent = nlp.value("sentiment").toString();
        if (sent == QString::fromUtf8("正面"))
            ++pos;
        else if (sent == QString::fromUtf8("负面"))
            ++neg;
        else
            ++neu;
        sum += o.value("score").toInt();
        for (const auto &k : nlp.value("keywords").toArray())
            kwc[k.toString()] += 1;
        const QJsonObject item{
            {"id", r.value("id").toInt()},
            {"nickname", r.value("nickname").toString()},
            {"score", o.value("score").toInt()},
            {"comment", o.value("comment").toString()},
            {"createdAt", o.value("createdAt").toString(o.value("updatedAt").toString())},
            {"sentiment", sent},
            {"keywords", nlp.value("keywords")},
            {"mine", o.value("userId").toInt() == uid},
        };
        reviews.append(item);
        if (o.value("userId").toInt() == uid)
            mine = item;
    }
    QJsonArray topKw;
    QList<QPair<int, QString>> ranked;
    for (auto it = kwc.begin(); it != kwc.end(); ++it)
        ranked.append({it.value(), it.key()});
    std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    for (int i = 0; i < qMin(6, ranked.size()); ++i)
        topKw.append(QJsonObject{{"word", ranked[i].second}, {"n", ranked[i].first}});
    const int n = reviews.size();
    return QJsonObject{
        {"pile", QJsonObject{
             {"id", pile.value("id").toInt()},
             {"pileNo", pile.value("pile_no").toString()},
             {"type", pile.value("type").toString()},
             {"powerKw", pile.value("power_kw").toDouble()},
             {"status", pile.value("status").toString()},
             {"stationId", pile.value("station_id").toInt()},
         }},
        {"station", QJsonObject{
             {"id", station.value("id").toInt()},
             {"name", station.value("name").toString()},
             {"address", station.value("address").toString()},
             {"pricePerKwh", station.value("price_per_kwh").toDouble()},
         }},
        {"nlp", QJsonObject{
             {"count", n},
             {"avgScore", n ? sum / n : 0},
             {"positive", pos},
             {"neutral", neu},
             {"negative", neg},
             {"keywords", topKw},
         }},
        {"reviews", reviews},
        {"mine", mine},
    };
}

/** 运营侧评价情感汇总。 */
QVector<QVariantMap> Dispatch::listReviewNlp() const
{
    struct Agg {
        int n = 0, pos = 0, neu = 0, neg = 0;
        double sum = 0;
        QString pileNo, station;
        QMap<QString, int> kw;
    };
    QHash<int, Agg> m;
    const auto docs = db_->query(
        "SELECT d.pile_id, d.doc, p.pile_no, s.name AS station FROM review_doc d "
        "JOIN pile p ON p.id=d.pile_id JOIN station s ON s.id=p.station_id");
    for (const auto &r : docs) {
        auto o = QJsonDocument::fromJson(r.value("doc").toString().toUtf8()).object();
        auto nlp = o.value("nlp").toObject();
        if (nlp.isEmpty())
            nlp = analyzeReview(o.value("score").toInt(), o.value("comment").toString());
        Agg &a = m[r.value("pile_id").toInt()];
        a.pileNo = r.value("pile_no").toString();
        a.station = r.value("station").toString();
        a.n += 1;
        a.sum += o.value("score").toInt();
        const QString sent = nlp.value("sentiment").toString();
        if (sent == QString::fromUtf8("正面"))
            ++a.pos;
        else if (sent == QString::fromUtf8("负面"))
            ++a.neg;
        else
            ++a.neu;
        for (const auto &k : nlp.value("keywords").toArray())
            a.kw[k.toString()] += 1;
    }
    QVector<QVariantMap> out;
    for (auto it = m.begin(); it != m.end(); ++it) {
        const Agg &a = it.value();
        QList<QPair<int, QString>> ranked;
        for (auto k = a.kw.begin(); k != a.kw.end(); ++k)
            ranked.append({k.value(), k.key()});
        std::sort(ranked.begin(), ranked.end(), [](const auto &x, const auto &y) { return x.first > y.first; });
        QStringList top;
        for (int i = 0; i < qMin(4, ranked.size()); ++i)
            top.append(ranked[i].second);
        out.append(QVariantMap{
            {"pile_no", a.pileNo},
            {"station", a.station},
            {"count", a.n},
            {"avg", a.n ? a.sum / a.n : 0},
            {"positive", a.pos},
            {"neutral", a.neu},
            {"negative", a.neg},
            {"keywords", top.join(QString::fromUtf8(" · "))},
        });
    }
    std::sort(out.begin(), out.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value("count").toInt() > b.value("count").toInt();
    });
    return out;
}

/** 营收 + 桩状态 + 最近分析报告，一次给驾驶舱。 */
QJsonObject Dispatch::cockpit() const
{
    QJsonObject s = salesSummary();
    QJsonObject st = pileStatusStats();
    QJsonArray idleRank;
    for (const auto &station : listStations()) {
        const auto piles = db_->query("SELECT status FROM pile WHERE station_id=?", {station.value("id")});
        int idle = 0;
        for (const auto &p : piles)
            if (p.value("status").toString() == QString::fromUtf8("闲置"))
                ++idle;
        idleRank.append(QJsonObject{
            {"name", station.value("name").toString()},
            {"idle", idle},
            {"total", piles.size()},
        });
    }
    s.insert("piles", st.value("items"));
    s.insert("idleRank", idleRank);
    return s;
}

/** 读 load_forecast。 */
QVector<QVariantMap> Dispatch::listForecasts() const
{
    return db_->query(
        "SELECT f.*, s.name FROM load_forecast f JOIN station s ON s.id=f.station_id "
        "ORDER BY f.station_id, f.horizon_hours");
}

/** 读 hourly_load。 */
QVector<QVariantMap> Dispatch::listHourlyLoad() const
{
    return db_->query(
        "SELECT h.hour, h.pred_kwh, s.name FROM hourly_load h "
        "JOIN station s ON s.id=h.station_id ORDER BY s.id, h.hour");
}

/** 读 fault_risk。 */
QVector<QVariantMap> Dispatch::listFaultRisks() const
{
    return db_->query("SELECT * FROM fault_risk ORDER BY score DESC");
}

/** 读 analysis_alert。 */
QVector<QVariantMap> Dispatch::listAlerts() const
{
    return db_->query("SELECT * FROM analysis_alert ORDER BY id DESC LIMIT 20");
}

/** 读 dispatch_plan（含是否已采纳）。 */
QVector<QVariantMap> Dispatch::listDispatchPlan() const
{
    return db_->query("SELECT * FROM dispatch_plan ORDER BY priority");
}

/** 运营侧按单号/手机/桩号筛订单。 */
QVector<QVariantMap> Dispatch::listAdminOrders(const QString &keyword) const
{
    if (keyword.isEmpty())
        return db_->query(
            "SELECT o.*, u.phone, p.pile_no, s.name AS station_name FROM charge_order o "
            "JOIN user u ON u.id=o.user_id JOIN pile p ON p.id=o.pile_id "
            "JOIN station s ON s.id=p.station_id ORDER BY o.id DESC LIMIT 80");
    return db_->query(
        "SELECT o.*, u.phone, p.pile_no, s.name AS station_name FROM charge_order o "
        "JOIN user u ON u.id=o.user_id JOIN pile p ON p.id=o.pile_id "
        "JOIN station s ON s.id=p.station_id "
        "WHERE o.order_no LIKE ? OR u.phone LIKE ? ORDER BY o.id DESC LIMIT 80",
        {"%" + keyword + "%", "%" + keyword + "%"});
}

/** 最近一行 analysis_report。 */
QVariantMap Dispatch::latestReport() const
{
    return db_->one("SELECT * FROM analysis_report ORDER BY id DESC LIMIT 1");
}

/** 清空分析表后按已完成订单重算预测、风险、告警、调度建议。不改账。 */
int Dispatch::refreshForecast()
{
    const QString t = nowStr();
    const int nowH = QDateTime::currentDateTime().time().hour();
    const int wd = QDateTime::currentDateTime().date().dayOfWeek();
    const bool weekend = wd >= 6;
    const QStringList weathers = {QString::fromUtf8("晴"), QString::fromUtf8("多云"), QString::fromUtf8("小雨")};
    const QString weather = weathers[QDate::currentDate().dayOfYear() % 3];
    const double wFactor = weather == QString::fromUtf8("小雨") ? 0.90 : (weekend ? 0.88 : 1.08);

    db_->execute("DELETE FROM hourly_load");
    db_->execute("DELETE FROM load_forecast");
    db_->execute("DELETE FROM fault_risk");
    db_->execute("DELETE FROM analysis_alert");
    db_->execute("DELETE FROM dispatch_plan");
    db_->execute("DELETE FROM analysis_report");

    const auto hist = db_->query(
        "SELECT p.station_id AS sid, CAST(substr(o.start_time,12,2) AS INTEGER) AS hh, "
        "AVG(o.energy_kwh) AS a, COUNT(*) AS n FROM charge_order o "
        "JOIN pile p ON p.id=o.pile_id WHERE o.status='已完成' GROUP BY sid, hh");
    QHash<int, QVector<double>> hourMean;
    int sampleN = 0;
    for (const auto &r : hist) {
        const int sid = r.value("sid").toInt();
        if (!hourMean.contains(sid))
            hourMean.insert(sid, QVector<double>(24, 0));
        hourMean[sid][qBound(0, r.value("hh").toInt(), 23)] = r.value("a").toDouble();
        sampleN += r.value("n").toInt();
    }

    const auto daily = db_->query(
        "SELECT p.station_id AS sid, substr(o.start_time,1,10) AS d, SUM(o.energy_kwh) AS s "
        "FROM charge_order o JOIN pile p ON p.id=o.pile_id WHERE o.status='已完成' "
        "GROUP BY sid, d ORDER BY d");
    QHash<int, QVector<double>> series;
    for (const auto &r : daily)
        series[r.value("sid").toInt()].append(r.value("s").toDouble());
    double mae = 0, rmse = 0;
    int evalN = 0;
    for (auto it = series.begin(); it != series.end(); ++it) {
        auto v = it.value();
        if (v.size() < 5)
            continue;
        const int cut = qMax(2, int(v.size() * 0.8));
        double mean = 0;
        for (int i = 0; i < cut; ++i)
            mean += v[i];
        mean /= cut;
        for (int i = cut; i < v.size(); ++i) {
            const double e = v[i] - mean;
            mae += qAbs(e);
            rmse += e * e;
            ++evalN;
        }
    }
    if (evalN) {
        mae /= evalN;
        rmse = qSqrt(rmse / evalN);
    }

    int n = 0;
    QVector<QPair<QString, double>> loads;
    for (const auto &s : db_->query("SELECT id, name FROM station")) {
        const int sid = s.value("id").toInt();
        const QString name = s.value("name").toString();
        const auto piles = db_->query("SELECT * FROM pile WHERE station_id=?", {sid});
        int idle = 0;
        double cap = 0;
        for (const auto &p : piles) {
            cap += p.value("power_kw").toDouble();
            if (p.value("status").toString() == QString::fromUtf8("闲置"))
                ++idle;
        }
        if (cap < 1)
            cap = 60;
        QVector<double> curve(24);
        for (int h = 0; h < 24; ++h) {
            double base = hourMean.value(sid).value(h, 0);
            if (base <= 0)
                base = idle * 2.4 * (h >= 17 && h <= 21 ? 1.4 : (h >= 7 && h <= 9 ? 1.15 : 0.7));
            if (h >= 17 && h <= 21)
                base *= 1.12;
            curve[h] = qMax(0.0, base * wFactor);
            db_->execute("INSERT INTO hourly_load(station_id,hour,pred_kwh,created_at) VALUES(?,?,?,?)",
                         {sid, h, qRound(curve[h] * 100) / 100.0, t});
        }
        auto sumH = [&](int from, int len) {
            double x = 0;
            for (int i = 0; i < len; ++i)
                x += curve[(from + i) % 24];
            return x;
        };
        int peakH = 0;
        double peakV = -1;
        for (int h = 0; h < 24; ++h) {
            if (curve[h] > peakV) {
                peakV = curve[h];
                peakH = h;
            }
        }
        int peakStart = peakH, peakEnd = peakH;
        const double peakCut = qMax(peakV * 0.8, cap * 0.55);
        while (peakStart > 0 && curve[peakStart - 1] >= peakCut)
            --peakStart;
        while (peakEnd < 23 && curve[peakEnd + 1] >= peakCut)
            ++peakEnd;
        const QString peakWin = QString("%1:00-%2:00")
                                    .arg(peakStart, 2, 10, QChar('0'))
                                    .arg(peakEnd, 2, 10, QChar('0'));
        const int hs[] = {1, 6, 24};
        for (int h : hs) {
            const double pred = sumH(nowH, h);
            const int predIdle = qBound(0, int(qRound(piles.size() - pred / qMax(7.0, cap / qMax(1, piles.size())))), piles.size());
            db_->execute(
                "INSERT INTO load_forecast(station_id,horizon_hours,pred_kwh,pred_idle,peak_hour,created_at) VALUES(?,?,?,?,?,?)",
                {sid, h, qRound(pred * 100) / 100.0, predIdle, peakWin, t});
            ++n;
        }
        loads.append({name, sumH(nowH, 6)});
        const double rate = peakV / cap;
        if (rate >= 0.8)
            db_->execute("INSERT INTO analysis_alert(level,title,detail,created_at) VALUES(?,?,?,?)",
                         {QString::fromUtf8("严重"), QString::fromUtf8("高峰过载风险 · ") + name,
                          QString::fromUtf8("预测高峰负荷率 %1%，建议引导分流或增开慢充").arg(qRound(rate * 1000) / 10.0), t});
        else if (rate >= 0.55)
            db_->execute("INSERT INTO analysis_alert(level,title,detail,created_at) VALUES(?,?,?,?)",
                         {QString::fromUtf8("一般"), QString::fromUtf8("晚高峰需关注 · ") + name,
                          QString::fromUtf8("预测高峰约 %1 kWh，空闲桩约 %2").arg(peakV, 0, 'f', 1).arg(idle), t});
    }

    std::sort(loads.begin(), loads.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    int pri = 1;
    for (const auto &L : loads) {
        db_->execute("INSERT INTO dispatch_plan(station,recommend,priority,reason,created_at) VALUES(?,?,?,?,?)",
                     {L.first, qRound(L.second * 10) / 10.0, pri,
                      pri == 1 ? QString::fromUtf8("未来6小时负荷最高，优先保障运维值班")
                               : QString::fromUtf8("按负荷从高到低分配巡检与引导资源"),
                      t});
        ++pri;
    }

    for (const auto &p : db_->query(
             "SELECT p.*, s.name AS station_name FROM pile p JOIN station s ON s.id=p.station_id")) {
        double score = 0.08;
        QString reason = QString::fromUtf8("运行平稳");
        if (p.value("status").toString() == QString::fromUtf8("故障")) {
            score = 0.93;
            reason = QString::fromUtf8("当前故障，需现场检修");
        } else {
            if (p.value("total_charge_count").toInt() > 40) {
                score += 0.22;
                reason = QString::fromUtf8("累计次数偏高，建议保养");
            }
            if (p.value("total_charge_minutes").toInt() > 1800) {
                score += 0.18;
                reason = QString::fromUtf8("累计时长偏高，关注接触器温升");
            }
            if (p.value("status").toString() == QString::fromUtf8("在用"))
                score += 0.08;
        }
        score = qBound(0.0, score, 0.99);
        const QString level = score >= 0.8 ? QString::fromUtf8("高风险")
                                           : (score >= 0.5 ? QString::fromUtf8("需关注") : QString::fromUtf8("正常"));
        db_->execute("INSERT INTO fault_risk(pile_no,station,score,level,reason,created_at) VALUES(?,?,?,?,?,?)",
                     {p.value("pile_no"), p.value("station_name"), qRound(score * 100) / 100.0, level, reason, t});
        if (score >= 0.8)
            db_->execute("INSERT INTO analysis_alert(level,title,detail,created_at) VALUES(?,?,?,?)",
                         {QString::fromUtf8("严重"), QString::fromUtf8("设备故障预警 ") + p.value("pile_no").toString(),
                          reason, t});
    }

    if (sampleN < 20)
        db_->execute("INSERT INTO analysis_alert(level,title,detail,created_at) VALUES(?,?,?,?)",
                     {QString::fromUtf8("提示"), QString::fromUtf8("样本偏少"),
                      QString::fromUtf8("历史完成订单不足，预测置信度有限，已回退到小时均值+时段因子"), t});
    db_->execute("INSERT INTO analysis_alert(level,title,detail,created_at) VALUES(?,?,?,?)",
                 {QString::fromUtf8("提示"), QString::fromUtf8("天气因子已融合"),
                  QString::fromUtf8("当前模拟天气：") + weather + QString::fromUtf8("，负荷系数 ") + QString::number(wFactor, 'f', 2), t});

    db_->execute(
        "INSERT INTO analysis_report(model_version,mae,rmse,sample_n,weather,created_at) VALUES(?,?,?,?,?,?)",
        {QStringLiteral("hour-mean-v1.2"), qRound(mae * 1000) / 1000.0, qRound(rmse * 1000) / 1000.0, sampleN, weather, t});
    return n;
}
