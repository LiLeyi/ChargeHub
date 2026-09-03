/**
 * @file dispatch.cpp
 * @brief 全部业务规则：用户端协议与管理端同进程调用都走这里
 */
#include "dispatch.h"

#include <algorithm>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QIODevice>
#include <QPair>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMap>
#include <QRegularExpression>
#include <QUuid>
#include <QtMath>

static QRegularExpression phoneRe()
{
    return QRegularExpression(QStringLiteral("^1[3-9][0-9]{9}$"));
}

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

static double money(double v) { return qRound(v * 100.0) / 100.0; }

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

Dispatch::Dispatch(Database *db) : db_(db) {}

QJsonObject Dispatch::ok(const QString &type, int seq, const QString &msg, const QJsonObject &data) const
{
    return QJsonObject{{"type", type}, {"seq", seq}, {"code", 0}, {"message", msg}, {"data", data}};
}

QJsonObject Dispatch::fail(const QString &type, int seq, int code, const QString &msg) const
{
    return QJsonObject{{"type", type}, {"seq", seq}, {"code", code}, {"message", msg}, {"data", QJsonObject()}};
}

QString Dispatch::issueToken(int userId)
{
    const QString token = QUuid::createUuid().toString().remove('{').remove('}').remove('-');
    QMutexLocker locker(&sessionMutex_);
    tokenUser_.insert(token, userId);
    tokenAt_.insert(token, QDateTime::currentSecsSinceEpoch());
    return token;
}

int Dispatch::userIdByToken(const QString &token) const
{
    QMutexLocker locker(&sessionMutex_);
    if (!tokenUser_.contains(token))
        return 0;
    if (QDateTime::currentSecsSinceEpoch() - tokenAt_.value(token) > 30 * 60) {
        tokenUser_.remove(token);
        tokenAt_.remove(token);
        return 0;
    }
    tokenAt_.insert(token, QDateTime::currentSecsSinceEpoch());
    return tokenUser_.value(token);
}

void Dispatch::dropUser(int userId)
{
    QMutexLocker locker(&sessionMutex_);
    const auto keys = tokenUser_.keys();
    for (const QString &k : keys)
        if (tokenUser_.value(k) == userId)
            tokenUser_.remove(k);
}

QVariantMap Dispatch::requireUser(const QString &token, QString *err) const
{
    const int uid = userIdByToken(token);
    if (!uid) {
        *err = QString::fromUtf8("登录已失效，请重新登录");
        return {};
    }
    auto u = db_->one("SELECT * FROM user WHERE id=?", {uid});
    if (u.isEmpty()) {
        *err = QString::fromUtf8("登录已失效，请重新登录");
        return {};
    }
    if (u.value("status").toString() == QString::fromUtf8("注销")) {
        *err = QString::fromUtf8("账号已注销，历史数据已留档");
        return {};
    }
    if (u.value("status").toString() == QString::fromUtf8("冻结")) {
        *err = QString::fromUtf8("账号已冻结，请联系管理员");
        return {};
    }
    return u;
}

QJsonObject Dispatch::publicUser(const QVariantMap &u) const
{
    QJsonObject o{
        {"id", u.value("id").toInt()},
        {"phone", u.value("phone").toString()},
        {"nickname", u.value("nickname").toString()},
        {"avatarPath", u.value("avatar_path").toString()},
        {"hasAvatar", false},
        {"balance", money(u.value("balance").toDouble())},
        {"status", u.value("status").toString()},
        {"createdAt", u.value("created_at").toString()},
        {"address", u.value("address").toString()},
        {"lat", u.contains("loc_lat") ? u.value("loc_lat").toDouble() : 39.9644},
        {"lng", u.contains("loc_lng") ? u.value("loc_lng").toDouble() : 116.3473},
        {"closeReason", u.value("close_reason").toString()},
        {"closedAt", u.value("closed_at").toString()},
    };
    const auto av = db_->one("SELECT mime, data FROM user_avatar WHERE user_id=?", {u.value("id")});
    if (!av.isEmpty() && !av.value("data").toByteArray().isEmpty()) {
        o.insert("hasAvatar", true);
        o.insert("avatarMime", av.value("mime").toString());
        o.insert("avatarBase64", QString::fromLatin1(av.value("data").toByteArray().toBase64()));
    }
    return o;
}

QJsonObject Dispatch::handle(const QJsonObject &req)
{
    const QString type = req.value("type").toString();
    const int seq = req.value("seq").toInt();
    const QJsonObject data = req.value("data").toObject();
    if (type == "LOGIN") {
        const QString phone = data.value("phone").toString().trimmed();
        const QString pwd = data.value("password").toString();
        if (!phoneRe().match(phone).hasMatch())
            return fail(type, seq, 400, QString::fromUtf8("请输入正确的手机号格式"));
        if (pwd.size() < 6 || pwd.size() > 20)
            return fail(type, seq, 400, QString::fromUtf8("密码长度须为 6~20 位"));
        auto user = db_->one("SELECT * FROM user WHERE phone=?", {phone});
        const QString hash = QString::fromLatin1(
            QCryptographicHash::hash(pwd.toUtf8(), QCryptographicHash::Sha256).toHex());
        if (user.isEmpty() || user.value("password_hash").toString() != hash)
            return fail(type, seq, 401, QString::fromUtf8("账号或密码错误"));
        if (user.value("status").toString() == QString::fromUtf8("注销"))
            return fail(type, seq, 403, QString::fromUtf8("账号已注销，历史数据已留档，无法登录"));
        if (user.value("status").toString() == QString::fromUtf8("冻结"))
            return fail(type, seq, 403, QString::fromUtf8("账号已冻结，请联系管理员"));
        const QString token = issueToken(user.value("id").toInt());
        return ok(type, seq, QString::fromUtf8("登录成功"),
                  QJsonObject{{"user", publicUser(user)}, {"token", token}, {"isNew", false}});
    }
    if (type == "REGISTER") {
        const QString phone = data.value("phone").toString().trimmed();
        const QString pwd = data.value("password").toString();
        if (!phoneRe().match(phone).hasMatch())
            return fail(type, seq, 400, QString::fromUtf8("请输入正确的手机号格式"));
        if (pwd.size() < 6 || pwd.size() > 20)
            return fail(type, seq, 400, QString::fromUtf8("密码长度须为 6~20 位"));
        if (!db_->one("SELECT id FROM user WHERE phone=?", {phone}).isEmpty()) {
            auto old = db_->one("SELECT status FROM user WHERE phone=?", {phone});
            if (old.value("status").toString() == QString::fromUtf8("注销"))
                return fail(type, seq, 409, QString::fromUtf8("该手机号已注销留档，无法再次注册，请联系管理员"));
            return fail(type, seq, 409, QString::fromUtf8("该账号已注册"));
        }
        const QString hash = QString::fromLatin1(
            QCryptographicHash::hash(pwd.toUtf8(), QCryptographicHash::Sha256).toHex());
        const QString nick = QString::fromUtf8("用户") + phone.right(4);
        const int uid = db_->execute(
            "INSERT INTO user(phone,nickname,avatar_path,password_hash,balance,status,created_at) VALUES(?,?,?,?,?,?,?)",
            {phone, nick, "", hash, 0.0, QString::fromUtf8("正常"), nowStr()});
        auto user = db_->one("SELECT * FROM user WHERE id=?", {uid});
        const QString token = issueToken(uid);
        return ok(type, seq, QString::fromUtf8("注册成功"),
                  QJsonObject{{"user", publicUser(user)}, {"token", token}, {"isNew", true}});
    }
    QString err;
    const auto user = requireUser(req.value("token").toString(), &err);
    if (user.isEmpty())
        return fail(type, seq,
                     (err.contains(QString::fromUtf8("冻结")) || err.contains(QString::fromUtf8("注销"))) ? 403 : 401,
                     err);

    QJsonObject body;
    QString message = "ok";
    if (type == "UPDATE_PROFILE") {
        body = updateProfile(user, data);
        if (body.contains("error"))
            return fail(type, seq, 400, body.value("error").toString());
        message = QString::fromUtf8("保存成功");
    } else if (type == "RECHARGE") {
        body = recharge(user, data);
        if (body.contains("error"))
            return fail(type, seq, 400, body.value("error").toString());
        message = QString::fromUtf8("充值成功");
    } else if (type == "QUERY_STATIONS") {
        body = queryStations(user, data);
    } else if (type == "CLOSE_ACCOUNT") {
        body = closeAccount(user);
        if (body.contains("error"))
            return fail(type, seq, body.value("errorCode").toInt(400), body.value("error").toString());
        message = QString::fromUtf8("账号已注销，历史订单与评价已留档");
    } else if (type == "QUERY_PILES") {
        body = queryPiles(user, data);
        if (body.contains("error"))
            return fail(type, seq, 404, body.value("error").toString());
    } else if (type == "START_CHARGE") {
        body = startCharge(user, data);
        if (body.contains("errorCode"))
            return fail(type, seq, body.value("errorCode").toInt(), body.value("error").toString());
        message = QString::fromUtf8("充电已开始");
    } else if (type == "CHARGE_STATUS") {
        body = chargeStatus(user);
        message = body.value("order").isNull() ? QString::fromUtf8("无进行中订单") : "ok";
    } else if (type == "STOP_CHARGE") {
        body = stopCharge(user);
        if (body.contains("errorCode"))
            return fail(type, seq, body.value("errorCode").toInt(), body.value("error").toString());
        message = QString::fromUtf8("请结算订单");
    } else if (type == "SETTLE_ORDER") {
        body = settle(user);
        if (body.contains("errorCode"))
            return fail(type, seq, body.value("errorCode").toInt(), body.value("error").toString());
        message = QString::fromUtf8("结算成功");
    } else if (type == "LIST_ORDERS") {
        body = listOrders(user);
    } else if (type == "LIST_RECHARGE") {
        body = listRecharge(user);
    } else if (type == "RESERVE_PILE") {
        body = reservePile(user, data);
        if (body.contains("errorCode"))
            return fail(type, seq, body.value("errorCode").toInt(), body.value("error").toString());
        message = QString::fromUtf8("预约成功，15分钟内有效");
    } else if (type == "CANCEL_RESERVE") {
        body = cancelReserve(user);
        if (body.contains("errorCode"))
            return fail(type, seq, body.value("errorCode").toInt(), body.value("error").toString());
        message = QString::fromUtf8("已取消预约");
    } else if (type == "REVIEW_STATION") {
        body = reviewStation(user, data);
        if (body.contains("error"))
            return fail(type, seq, 400, body.value("error").toString());
        message = body.value("updated").toBool() ? QString::fromUtf8("已更新你对这根桩的评价")
                                                 : QString::fromUtf8("评价已提交");
    } else if (type == "LIST_PILE_REVIEWS") {
        body = listPileReviews(user, data);
        if (body.contains("error"))
            return fail(type, seq, 404, body.value("error").toString());
        message = "ok";
    } else if (type == "HEARTBEAT") {
        body = QJsonObject();
    } else {
        return fail(type, seq, 400, QString::fromUtf8("未知请求类型"));
    }
    body.remove("error");
    body.remove("errorCode");
    return ok(type, seq, message, body);
}

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
    return QJsonObject{{"user", publicUser(u)}};
}

QJsonObject Dispatch::recharge(const QVariantMap &user, const QJsonObject &data)
{
    bool ok = false;
    const double amount = data.value("amount").toVariant().toDouble(&ok);
    if (!ok || amount <= 0 || amount > 10000)
        return QJsonObject{{"error", QString::fromUtf8("单笔充值须大于 0 且不超过 10000 元")}};
    const double nb = money(user.value("balance").toDouble() + amount);
    const QString t = nowStr();
    db_->execute("UPDATE user SET balance=? WHERE id=?", {nb, user.value("id")});
    db_->execute("INSERT INTO recharge_log(user_id,amount,result,created_at) VALUES(?,?,?,?)",
                 {user.value("id"), money(amount), QString::fromUtf8("成功"), t});
    const auto last = db_->one("SELECT id FROM recharge_log WHERE user_id=? ORDER BY id DESC LIMIT 1",
                               {user.value("id")});
    const QString tradeNo = QString("RC%1").arg(last.value("id").toInt(), 8, 10, QChar('0'));
    auto u = db_->one("SELECT * FROM user WHERE id=?", {user.value("id")});
    return QJsonObject{{"user", publicUser(u)}, {"tradeNo", tradeNo}, {"amount", money(amount)}};
}

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

QJsonObject Dispatch::closeAccount(const QVariantMap &user)
{
    const int uid = user.value("id").toInt();
    auto open = openOrder(uid);
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
    dropUser(uid);
    return QJsonObject{{"closed", true}, {"userId", uid}};
}

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

QVariantMap Dispatch::openOrder(int userId) const
{
    return db_->one(
        "SELECT * FROM charge_order WHERE user_id=? AND status IN ('充电中','待结算') ORDER BY id DESC LIMIT 1",
        {userId});
}

QJsonObject Dispatch::calcLive(const QVariantMap &order, const QVariantMap &pile, const QVariantMap &station) const
{
    const QDateTime start = QDateTime::fromString(order.value("start_time").toString(), "yyyy-MM-dd HH:mm:ss");
    QDateTime end = QDateTime::currentDateTime();
    if (order.value("status").toString() != QString::fromUtf8("充电中") && !order.value("end_time").toString().isEmpty())
        end = QDateTime::fromString(order.value("end_time").toString(), "yyyy-MM-dd HH:mm:ss");
    const int seconds = qMax(0, int(start.secsTo(end)));
    const double energy = qRound(pile.value("power_kw").toDouble() * (seconds / 3600.0) * 1000) / 1000.0;
    const double amount = money(energy * station.value("price_per_kwh").toDouble());
    return QJsonObject{
        {"seconds", seconds},
        {"energyKwh", energy},
        {"amount", amount},
        {"powerKw", pile.value("power_kw").toDouble()},
        {"pricePerKwh", station.value("price_per_kwh").toDouble()},
    };
}

QJsonObject Dispatch::publicOrder(const QVariantMap &o, const QVariantMap &p, const QVariantMap &s, const QJsonObject &live) const
{
    QJsonObject r{
        {"id", o.value("id").toInt()},
        {"orderNo", o.value("order_no").toString()},
        {"status", o.value("status").toString()},
        {"startTime", o.value("start_time").toString()},
        {"endTime", o.value("end_time").toString()},
        {"pileNo", p.value("pile_no").toString()},
        {"stationName", s.value("name").toString()},
        {"type", p.value("type").toString()},
    };
    for (auto it = live.begin(); it != live.end(); ++it)
        r.insert(it.key(), it.value());
    return r;
}

QJsonObject Dispatch::startCharge(const QVariantMap &user, const QJsonObject &data)
{
    expireReservations();
    if (!openOrder(user.value("id").toInt()).isEmpty())
        return QJsonObject{{"errorCode", 409}, {"error", QString::fromUtf8("您有未完成的充电订单，请先结算")}};
    if (user.value("balance").toDouble() <= 0)
        return QJsonObject{{"errorCode", 402}, {"error", QString::fromUtf8("余额不足，请先充值")}};
    const int pileId = data.value("pileId").toInt();
    auto pile = db_->one("SELECT * FROM pile WHERE id=?", {pileId});
    if (pile.isEmpty())
        return QJsonObject{{"errorCode", 404}, {"error", QString::fromUtf8("电桩不存在")}};
    const QString st = pile.value("status").toString();
    if (st == QString::fromUtf8("故障"))
        return QJsonObject{{"errorCode", 409}, {"error", QString::fromUtf8("电桩故障，请选择其他电桩")}};
    if (st == QString::fromUtf8("在用"))
        return QJsonObject{{"errorCode", 409}, {"error", QString::fromUtf8("电桩正在使用中")}};
    auto res = activeReserve(pileId);
    if (!res.isEmpty() && res.value("user_id").toInt() != user.value("id").toInt())
        return QJsonObject{{"errorCode", 409}, {"error", QString::fromUtf8("该桩已被他人预约")}};
    auto station = db_->one("SELECT * FROM station WHERE id=?", {pile.value("station_id")});
    const QString t = nowStr();
    const QString no = "CH" + QDateTime::currentDateTime().toString("yyyyMMddHHmmss") + QString::number(user.value("id").toInt());
    const int oid = db_->execute(
        "INSERT INTO charge_order(order_no,user_id,pile_id,status,start_time,energy_kwh,amount,created_at) VALUES(?,?,?,?,?,?,?,?)",
        {no, user.value("id"), pileId, QString::fromUtf8("充电中"), t, 0, 0, t});
    db_->execute("UPDATE pile SET status=? WHERE id=?", {QString::fromUtf8("在用"), pileId});
    if (!res.isEmpty())
        db_->execute("UPDATE reservation SET status=? WHERE id=?", {QString::fromUtf8("已履约"), res.value("id")});
    auto order = db_->one("SELECT * FROM charge_order WHERE id=?", {oid});
    return QJsonObject{{"order", publicOrder(order, pile, station, calcLive(order, pile, station))}};
}

QJsonObject Dispatch::chargeStatus(const QVariantMap &user)
{
    auto order = openOrder(user.value("id").toInt());
    if (order.isEmpty())
        return QJsonObject{{"order", QJsonValue::Null}};
    auto pile = db_->one("SELECT * FROM pile WHERE id=?", {order.value("pile_id")});
    auto station = db_->one("SELECT * FROM station WHERE id=?", {pile.value("station_id")});
    return QJsonObject{{"order", publicOrder(order, pile, station, calcLive(order, pile, station))}};
}

QJsonObject Dispatch::stopCharge(const QVariantMap &user)
{
    auto order = db_->one(
        "SELECT * FROM charge_order WHERE user_id=? AND status='充电中' ORDER BY id DESC LIMIT 1",
        {user.value("id")});
    if (order.isEmpty())
        return QJsonObject{{"errorCode", 404}, {"error", QString::fromUtf8("没有正在进行的充电")}};
    auto pile = db_->one("SELECT * FROM pile WHERE id=?", {order.value("pile_id")});
    auto station = db_->one("SELECT * FROM station WHERE id=?", {pile.value("station_id")});
    auto live = calcLive(order, pile, station);
    const int minutes = qMax(1, live.value("seconds").toInt() / 60);
    db_->execute("UPDATE charge_order SET status=?, end_time=?, energy_kwh=?, amount=? WHERE id=?",
                 {QString::fromUtf8("待结算"), nowStr(), live.value("energyKwh").toDouble(),
                  live.value("amount").toDouble(), order.value("id")});
    db_->execute(
        "UPDATE pile SET status=?, total_charge_count=total_charge_count+1, total_charge_minutes=total_charge_minutes+? WHERE id=?",
        {QString::fromUtf8("闲置"), minutes, pile.value("id")});
    order = db_->one("SELECT * FROM charge_order WHERE id=?", {order.value("id")});
    return QJsonObject{{"order", publicOrder(order, pile, station, calcLive(order, pile, station))}};
}

QJsonObject Dispatch::settle(const QVariantMap &userIn)
{
    auto order = db_->one(
        "SELECT * FROM charge_order WHERE user_id=? AND status='待结算' ORDER BY id DESC LIMIT 1",
        {userIn.value("id")});
    if (order.isEmpty())
        return QJsonObject{{"errorCode", 404}, {"error", QString::fromUtf8("没有待结算订单")}};
    auto user = db_->one("SELECT * FROM user WHERE id=?", {userIn.value("id")});
    if (user.value("balance").toDouble() + 1e-6 < order.value("amount").toDouble())
        return QJsonObject{{"errorCode", 402}, {"error", QString::fromUtf8("余额不足，请先充值后再结算")}};
    const double nb = money(user.value("balance").toDouble() - order.value("amount").toDouble());
    db_->execute("UPDATE user SET balance=? WHERE id=?", {nb, user.value("id")});
    db_->execute("UPDATE charge_order SET status=? WHERE id=?", {QString::fromUtf8("已完成"), order.value("id")});
    user = db_->one("SELECT * FROM user WHERE id=?", {user.value("id")});
    auto pile = db_->one("SELECT * FROM pile WHERE id=?", {order.value("pile_id")});
    auto station = db_->one("SELECT * FROM station WHERE id=?", {pile.value("station_id")});
    auto live = calcLive(order, pile, station);
    return QJsonObject{{"order", publicOrder(order, pile, station, live)}, {"user", publicUser(user)}};
}

QJsonObject Dispatch::adminLogin(const QString &user, const QString &pwd)
{
    const QByteArray hash = QCryptographicHash::hash(pwd.toUtf8(), QCryptographicHash::Sha256).toHex();
    auto row = db_->one("SELECT * FROM admin WHERE username=?", {user});
    if (row.isEmpty() || row.value("password_hash").toString() != QString::fromLatin1(hash))
        return QJsonObject{{"ok", false}, {"message", QString::fromUtf8("账号或密码错误")}};
    return QJsonObject{{"ok", true}, {"username", user}};
}

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

QVector<QVariantMap> Dispatch::listPiles() const
{
    return db_->query("SELECT p.*, s.name AS station_name FROM pile p JOIN station s ON s.id=p.station_id ORDER BY p.pile_no");
}

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

void Dispatch::freezeUser(int userId, bool freeze)
{
    auto u = db_->one("SELECT * FROM user WHERE id=?", {userId});
    if (u.isEmpty() || u.value("status").toString() == QString::fromUtf8("注销"))
        return;
    if (freeze) {
        auto order = openOrder(userId);
        if (!order.isEmpty() && order.value("status").toString() == QString::fromUtf8("充电中"))
            stopCharge(u);
        db_->execute("UPDATE user SET status=?, close_reason=?, closed_at=? WHERE id=?",
                     {QString::fromUtf8("冻结"), QString::fromUtf8("管理员冻结"), nowStr(), userId});
        dropUser(userId);
        db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                     {"admin", QString::fromUtf8("冻结用户"), u.value("phone"), QString::fromUtf8("成功"), nowStr()});
    } else {
        db_->execute("UPDATE user SET status=?, close_reason=?, closed_at=? WHERE id=?",
                     {QString::fromUtf8("正常"), QString(), QString(), userId});
        db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                     {"admin", QString::fromUtf8("解冻用户"), u.value("phone"), QString::fromUtf8("成功"), nowStr()});
    }
}

int Dispatch::addStation(const QVariantMap &data)
{
    const QString name = data.value("name").toString().trimmed();
    const QString address = data.value("address").toString().trimmed();
    const int sid = db_->execute(
        "INSERT INTO station(name,address,lng,lat,price_per_kwh) VALUES(?,?,?,?,?)",
        {name, address, data.value("lng"), data.value("lat"), data.value("pricePerKwh", 1.3)});
    const int n = data.value("pileCount", 4).toInt();
    for (int i = 1; i <= n; ++i) {
        const bool fast = i <= qMax(1, n / 2);
        db_->execute("INSERT INTO pile(pile_no,station_id,type,power_kw,status) VALUES(?,?,?,?,?)",
                     {QString("ST%1-P%2").arg(sid, 2, 10, QChar('0')).arg(i, 2, 10, QChar('0')),
                      sid, fast ? QString::fromUtf8("快充") : QString::fromUtf8("慢充"),
                      fast ? 60.0 : 7.0, QString::fromUtf8("闲置")});
    }
    return sid;
}

void Dispatch::expireReservations() const
{
    db_->execute("UPDATE reservation SET status=? WHERE status=? AND expire_at < ?",
                 {QString::fromUtf8("已取消"), QString::fromUtf8("有效"), nowStr()});
}

QVariantMap Dispatch::activeReserve(int pileId) const
{
    return db_->one(
        "SELECT * FROM reservation WHERE pile_id=? AND status='有效' ORDER BY id DESC LIMIT 1", {pileId});
}

bool Dispatch::pileIsIdle(const QVariantMap &pile) const
{
    if (pile.value("status").toString() != QString::fromUtf8("闲置"))
        return false;
    return activeReserve(pile.value("id").toInt()).isEmpty();
}

QJsonObject Dispatch::listOrders(const QVariantMap &user)
{
    const auto rows = db_->query(
        "SELECT o.*, p.pile_no, p.type, p.power_kw, s.name, s.price_per_kwh "
        "FROM charge_order o JOIN pile p ON p.id=o.pile_id JOIN station s ON s.id=p.station_id "
        "WHERE o.user_id=? ORDER BY o.id DESC LIMIT 40",
        {user.value("id")});
    QJsonArray arr;
    for (const auto &o : rows) {
        QJsonObject live;
        if (o.value("status").toString() == QString::fromUtf8("充电中")
            || o.value("status").toString() == QString::fromUtf8("待结算")) {
            live = calcLive(o, o, o);
        } else {
            live = QJsonObject{
                {"seconds", 0},
                {"energyKwh", o.value("energy_kwh").toDouble()},
                {"amount", o.value("amount").toDouble()},
                {"powerKw", o.value("power_kw").toDouble()},
                {"pricePerKwh", o.value("price_per_kwh").toDouble()},
            };
        }
        arr.append(publicOrder(o, o, o, live));
    }
    return QJsonObject{{"orders", arr}};
}

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

QJsonObject Dispatch::cancelReserve(const QVariantMap &user)
{
    auto row = db_->one("SELECT * FROM reservation WHERE user_id=? AND status='有效' ORDER BY id DESC LIMIT 1",
                        {user.value("id")});
    if (row.isEmpty())
        return QJsonObject{{"errorCode", 404}, {"error", QString::fromUtf8("没有有效预约")}};
    db_->execute("UPDATE reservation SET status=? WHERE id=?", {QString::fromUtf8("已取消"), row.value("id")});
    return QJsonObject{};
}

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

QVector<QVariantMap> Dispatch::listForecasts() const
{
    return db_->query(
        "SELECT f.*, s.name FROM load_forecast f JOIN station s ON s.id=f.station_id "
        "ORDER BY f.station_id, f.horizon_hours");
}

QVector<QVariantMap> Dispatch::listHourlyLoad() const
{
    return db_->query(
        "SELECT h.hour, h.pred_kwh, s.name FROM hourly_load h "
        "JOIN station s ON s.id=h.station_id ORDER BY s.id, h.hour");
}

QVector<QVariantMap> Dispatch::listFaultRisks() const
{
    return db_->query("SELECT * FROM fault_risk ORDER BY score DESC");
}

QVector<QVariantMap> Dispatch::listAlerts() const
{
    return db_->query("SELECT * FROM analysis_alert ORDER BY id DESC LIMIT 20");
}

QVector<QVariantMap> Dispatch::listDispatchPlan() const
{
    return db_->query("SELECT * FROM dispatch_plan ORDER BY priority");
}

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

QVariantMap Dispatch::latestReport() const
{
    return db_->one("SELECT * FROM analysis_report ORDER BY id DESC LIMIT 1");
}

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
