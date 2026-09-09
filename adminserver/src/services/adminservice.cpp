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
    db_->execute("INSERT INTO admin(username,password_hash,created_at) VALUES(?,?,?)",
                 {user, QString::fromLatin1(hash), nowStr()});
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
    if (pile.value("status").toString() == QString::fromUtf8("故障")) {
        db_->execute("UPDATE pile SET status=? WHERE id=?", {QString::fromUtf8("闲置"), pileId});
        msg = QString::fromUtf8("重启指令已发送，故障已恢复为闲置");
    }
    db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                 {"admin", QString::fromUtf8("远程重启"), pile.value("pile_no"), QString::fromUtf8("成功"), nowStr()});
    return msg;
}

void AdminService::freezeUser(int userId, bool freeze)
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
        // QString() 是 null QString，QSQLITE 会将其绑定为 SQL NULL；而
        // close_reason/closed_at 均为 NOT NULL。必须传非 null 的空字符串，
        // 否则 UPDATE 整句失败，status 会继续停留在“冻结”。
        db_->execute("UPDATE user SET status=?, close_reason=?, closed_at=? WHERE id=?",
                     {QString::fromUtf8("正常"), QStringLiteral(""), QStringLiteral(""), userId});
        db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                     {"admin", QString::fromUtf8("解冻用户"), u.value("phone"), QString::fromUtf8("成功"), nowStr()});
    }
}

int AdminService::addStation(const QVariantMap &data)
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

QString AdminService::applyDefaultTariff(int stationId)
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

QString AdminService::adoptDispatchPlan(int planId)
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

QString AdminService::markPileFault(int pileId)
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
        "WHERE o.order_no LIKE ? OR u.phone LIKE ? ORDER BY o.id DESC LIMIT 80",
        {"%" + keyword + "%", "%" + keyword + "%"});
}

