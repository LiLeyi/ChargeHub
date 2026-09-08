/**
 * @file adminservice.cpp
 * @brief 管理端运营用例实现；所有修改都会保留原有审计行为。
 */
#include "adminservice.h"
#include "chargeservice.h"
#include "database.h"
#include "sessionservice.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QRegularExpression>
#include <QtMath>

static qint64 fenOf(double yuan) { return qRound(yuan * 100.0); }
static double money(double value) { return fenOf(value) / 100.0; }
static QString nowStr() { return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"); }

static bool validStationValues(const QVariantMap &data)
{
    bool lngOk = false, latOk = false, priceOk = false;
    const double lng = data.value("lng").toDouble(&lngOk);
    const double lat = data.value("lat").toDouble(&latOk);
    const double price = data.value("pricePerKwh", 1.3).toDouble(&priceOk);
    return !data.value("name").toString().trimmed().isEmpty()
        && !data.value("address").toString().trimmed().isEmpty()
        && lngOk && latOk && priceOk && qIsFinite(lng) && qIsFinite(lat) && qIsFinite(price)
        && lng >= -180.0 && lng <= 180.0 && lat >= -90.0 && lat <= 90.0
        && price >= 0.10 && price <= 20.0;
}

AdminService::AdminService(Database *db, SessionService *sessions, ChargeService *charges)
    : db_(db), sessions_(sessions), charges_(charges) {}

QJsonObject AdminService::adminLogin(const QString &user, const QString &pwd)
{
    const QByteArray hash = QCryptographicHash::hash(pwd.toUtf8(), QCryptographicHash::Sha256).toHex();
    auto row = db_->one("SELECT * FROM admin WHERE username=?", {user});
    if (row.isEmpty() || row.value("password_hash").toString() != QString::fromLatin1(hash))
        return QJsonObject{{"ok", false}, {"message", QString::fromUtf8("账号或密码错误")}};
    return QJsonObject{{"ok", true}, {"username", user}};
}

QJsonObject AdminService::adminRegister(const QString &user, const QString &pwd)
{
    if (!QRegularExpression(QStringLiteral("^[A-Za-z][A-Za-z0-9_]{2,15}$")).match(user).hasMatch())
        return QJsonObject{{"ok", false}, {"message", QString::fromUtf8("账号须为 3~16 位，字母开头，可含数字和下划线")}};
    if (pwd.size() < 6 || pwd.size() > 20)
        return QJsonObject{{"ok", false}, {"message", QString::fromUtf8("密码长度须为 6~20 位")}};
    if (!db_->one("SELECT id FROM admin WHERE username=?", {user}).isEmpty())
        return QJsonObject{{"ok", false}, {"message", QString::fromUtf8("该账号已注册")}};
    const QByteArray hash = QCryptographicHash::hash(pwd.toUtf8(), QCryptographicHash::Sha256).toHex();
    if (db_->execute("INSERT INTO admin(username,password_hash,created_at) VALUES(?,?,?)",
                     {user, QString::fromLatin1(hash), nowStr()}) <= 0)
        return QJsonObject{{"ok", false}, {"message", QString::fromUtf8("注册失败，账号未写入，请重试")}};
    return QJsonObject{{"ok", true}, {"username", user}, {"registered", true}};
}

QVector<QVariantMap> AdminService::listPiles() const
{
    return db_->query("SELECT p.*, s.name AS station_name FROM pile p JOIN station s ON s.id=p.station_id ORDER BY p.pile_no");
}

QVector<QVariantMap> AdminService::listStations() const
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

QVector<QVariantMap> AdminService::listUsers(const QString &keyword) const
{
    const QString sql = QStringLiteral(
        "SELECT u.id, u.phone, u.nickname, u.balance, u.created_at, u.status, u.address, u.close_reason, "
        "CASE WHEN a.user_id IS NULL THEN 0 ELSE 1 END AS has_avatar "
        "FROM user u LEFT JOIN user_avatar a ON a.user_id=u.id %1 ORDER BY u.id DESC");
    if (keyword.isEmpty())
        return db_->query(sql.arg(QString()));
    return db_->query(sql.arg(QStringLiteral("WHERE u.phone LIKE ?")), {"%" + keyword + "%"});
}

QString AdminService::rebootPile(int pileId)
{
    auto pile = db_->one("SELECT * FROM pile WHERE id=?", {pileId});
    if (pile.isEmpty())
        return QString::fromUtf8("电桩不存在");
    if (pile.value("status").toString() == QString::fromUtf8("在用"))
        return QString::fromUtf8("充电中的电桩不可重启");
    QString msg = QString::fromUtf8("重启指令已发送");
    const bool faulty = pile.value("status").toString() == QString::fromUtf8("故障");
    if (faulty)
        msg = QString::fromUtf8("重启指令已发送，故障已恢复为闲置");
    const bool ok = db_->transaction([&] {
        if (faulty && db_->execute("UPDATE pile SET status=?, fault_code='', fault_at='' WHERE id=?",
                                   {QString::fromUtf8("闲置"), pileId}) < 0)
            return false;
        return db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                            {"admin", QString::fromUtf8("远程重启"), pile.value("pile_no"),
                             QString::fromUtf8("成功"), nowStr()}) >= 0;
    });
    return ok ? msg : QString::fromUtf8("重启失败，设备状态和审计记录未写入");
}

QString AdminService::freezeUser(int userId, bool freeze)
{
    auto u = db_->one("SELECT * FROM user WHERE id=?", {userId});
    if (u.isEmpty() || u.value("status").toString() == QString::fromUtf8("注销"))
        return QString::fromUtf8("用户不存在或账号已注销");
    if (freeze) {
        auto order = charges_->openOrder(userId);
        if (!order.isEmpty() && order.value("status").toString() == QString::fromUtf8("充电中")) {
            const QJsonObject stopped = charges_->stop(u);
            if (stopped.contains("error"))
                return QString::fromUtf8("冻结失败：%1").arg(stopped.value("error").toString());
        }
        const bool ok = db_->transaction([&] {
            if (db_->execute("UPDATE user SET status=?, close_reason=?, closed_at=? WHERE id=?",
                             {QString::fromUtf8("冻结"), QString::fromUtf8("管理员冻结"), nowStr(), userId}) < 0)
                return false;
            return db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                                {"admin", QString::fromUtf8("冻结用户"), u.value("phone"),
                                 QString::fromUtf8("成功"), nowStr()}) >= 0;
        });
        if (!ok)
            return QString::fromUtf8("冻结失败，数据库未写入");
        sessions_->dropUser(userId);
        return QString::fromUtf8("用户已冻结");
    } else {
        // QString() 是 null QString，QSQLITE 会将其绑定为 SQL NULL；而
        // close_reason/closed_at 均为 NOT NULL。必须传非 null 的空字符串，
        // 否则 UPDATE 整句失败，status 会继续停留在“冻结”。
        const bool ok = db_->transaction([&] {
            if (db_->execute("UPDATE user SET status=?, close_reason=?, closed_at=? WHERE id=?",
                             {QString::fromUtf8("正常"), QStringLiteral(""), QStringLiteral(""), userId}) < 0)
                return false;
            return db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                                {"admin", QString::fromUtf8("解冻用户"), u.value("phone"),
                                 QString::fromUtf8("成功"), nowStr()}) >= 0;
        });
        return ok ? QString::fromUtf8("用户已解冻") : QString::fromUtf8("解冻失败，数据库未写入");
    }
}

int AdminService::addStation(const QVariantMap &data)
{
    const QString name = data.value("name").toString().trimmed();
    const QString address = data.value("address").toString().trimmed();
    const double lng = data.value("lng").toDouble();
    const double lat = data.value("lat").toDouble();
    const double price = data.value("pricePerKwh", 1.3).toDouble();
    if (!validStationValues(data))
        return 0;
    int sid = 0;
    const int n = qBound(1, data.value("pileCount", 4).toInt(), 20);
    const bool ok = db_->transaction([&] {
        sid = db_->execute(
            "INSERT INTO station(name,address,lng,lat,price_per_kwh) VALUES(?,?,?,?,?)",
            {name, address, lng, lat, money(price)});
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
        if (!replaceTariff(sid, money(price)))
            return false;
        return db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                            {"admin", QString::fromUtf8("启用分时电价"), name,
                             QString::fromUtf8("谷/平/峰"), nowStr()}) >= 0;
    });
    return ok ? sid : 0;
}

bool AdminService::replaceTariff(int stationId, double base, double peakFactor)
{
    if (!qIsFinite(base) || base < 0.10 || base > 20.0)
        return false;
    if (db_->execute("DELETE FROM tariff_rule WHERE station_id=?", {stationId}) < 0)
        return false;
    const double valley = money(base * 0.85);
    const double peak = money(base * peakFactor);
    const struct { int a, b; double p; const char *lab; } rows[] = {
        {0, 7, valley, "谷"}, {7, 17, base, "平"}, {17, 22, peak, "峰"}, {22, 24, valley, "谷"},
    };
    for (const auto &r : rows) {
        if (db_->execute("INSERT INTO tariff_rule(station_id,start_hour,end_hour,price_per_kwh,label) VALUES(?,?,?,?,?)",
                         {stationId, r.a, r.b, r.p, QString::fromUtf8(r.lab)}) <= 0)
            return false;
    }
    return true;
}

QString AdminService::applyDefaultTariff(int stationId)
{
    auto st = db_->one("SELECT * FROM station WHERE id=?", {stationId});
    if (st.isEmpty())
        return QString::fromUtf8("电站不存在");
    const bool ok = db_->transaction([&] {
        if (!replaceTariff(stationId, st.value("price_per_kwh").toDouble()))
            return false;
        return db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                            {"admin", QString::fromUtf8("启用分时电价"), st.value("name"),
                             QString::fromUtf8("谷/平/峰"), nowStr()}) >= 0;
    });
    if (!ok)
        return QString::fromUtf8("启用失败，原有电价保持不变");
    return QString::fromUtf8("已按基准电价启用谷 0.85 / 平 1.0 / 峰 1.25");
}

QString AdminService::adoptDispatchPlan(int planId)
{
    auto plan = db_->one("SELECT * FROM dispatch_plan WHERE id=?", {planId});
    if (plan.isEmpty())
        return QString::fromUtf8("没有这条调度建议");
    if (plan.value("adopted").toInt())
        return QString::fromUtf8("该建议已采纳，无需重复操作");
    const auto stations = db_->query("SELECT * FROM station WHERE name=?", {plan.value("station")});
    if (stations.isEmpty())
        return QString::fromUtf8("对不上电站名，请先刷新分析");
    if (stations.size() != 1)
        return QString::fromUtf8("存在同名电站，无法确定改价对象，请先区分站名并刷新分析");
    const auto st = stations.first();
    const bool ok = db_->transaction([&] {
        if (!replaceTariff(st.value("id").toInt(), st.value("price_per_kwh").toDouble(), 1.35))
            return false;
        if (db_->execute("UPDATE dispatch_plan SET adopted=1, adopted_at=? WHERE id=?", {nowStr(), planId}) < 0)
            return false;
        return db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                            {"admin", QString::fromUtf8("采纳调度"), plan.value("station"),
                             QString::fromUtf8("峰段上浮"), nowStr()}) >= 0;
    });
    if (!ok)
        return QString::fromUtf8("采纳失败，电价和建议状态保持不变");
    return QString::fromUtf8("已采纳：该站峰时段电价上浮，引导错峰");
}

QString AdminService::markPileFault(int pileId)
{
    auto pile = db_->one("SELECT * FROM pile WHERE id=?", {pileId});
    if (pile.isEmpty())
        return QString::fromUtf8("电桩不存在");
    if (pile.value("status").toString() == QString::fromUtf8("在用"))
        return QString::fromUtf8("充电中的电桩请先强制结束订单，再标故障");
    const QString t = nowStr();
    const bool ok = db_->transaction([&] {
        if (db_->execute("UPDATE pile SET status=?, fault_code=?, fault_at=? WHERE id=?",
                         {QString::fromUtf8("故障"), QString::fromUtf8("ADMIN"), t, pileId}) < 0)
            return false;
        return db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                            {"admin", QString::fromUtf8("标记故障"), pile.value("pile_no"), QString::fromUtf8("成功"), t}) >= 0;
    });
    if (!ok)
        return QString::fromUtf8("标记失败，设备状态和审计记录未写入");
    return QString::fromUtf8("已标记为故障，用户端不可再开充");
}

QString AdminService::restorePile(int pileId)
{
    return rebootPile(pileId);
}

QString AdminService::updateStation(int stationId, const QVariantMap &data)
{
    auto st = db_->one("SELECT * FROM station WHERE id=?", {stationId});
    if (st.isEmpty())
        return QString::fromUtf8("电站不存在");
    const QString name = data.value("name").toString().trimmed();
    const QString address = data.value("address").toString().trimmed();
    QVariantMap values = data;
    values.insert("lng", data.value("lng", st.value("lng")));
    values.insert("lat", data.value("lat", st.value("lat")));
    if (!validStationValues(values))
        return QString::fromUtf8("站名、地址不能空，经纬度须有效，电价须在 0.10～20.00 元之间");
    const bool ok = db_->transaction([&] {
        if (db_->execute("UPDATE station SET name=?, address=?, lng=?, lat=?, price_per_kwh=? WHERE id=?",
                         {name, address, values.value("lng").toDouble(), values.value("lat").toDouble(),
                          money(values.value("pricePerKwh", 1.3).toDouble()), stationId}) < 0)
            return false;
        return db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                            {"admin", QString::fromUtf8("修改电站"), name, QString::fromUtf8("成功"), nowStr()}) >= 0;
    });
    if (!ok)
        return QString::fromUtf8("保存失败，电站信息保持不变");
    return QString::fromUtf8("电站已更新");
}

QVector<QVariantMap> AdminService::listAudit(int limit) const
{
    return db_->query("SELECT * FROM audit_log ORDER BY id DESC LIMIT ?", {qBound(1, limit, 200)});
}

QVector<QVariantMap> AdminService::listAdminOrders(const QString &keyword) const
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
        "WHERE o.order_no LIKE ? OR u.phone LIKE ? OR p.pile_no LIKE ? ORDER BY o.id DESC LIMIT 80",
        {"%" + keyword + "%", "%" + keyword + "%", "%" + keyword + "%"});
}
