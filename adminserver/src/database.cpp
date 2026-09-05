/**
 * @file database.cpp
 * @brief 建表、旧库加列、演示数据。open() 可重复执行，已有联调库只迁移不覆盖。
 *
 * 演示账号：管理员 admin/123456；用户 13800138000/123456。
 * 调用者只有 Dispatch 和 main.cpp 的 Database::open。
 */
#include "database.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlRecord>
#include <QThread>
#include <QtGlobal>

static const char *kSchema = R"SQL(
PRAGMA foreign_keys = ON;
CREATE TABLE IF NOT EXISTS admin (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS user (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    phone TEXT NOT NULL UNIQUE,
    nickname TEXT NOT NULL,
    avatar_path TEXT NOT NULL DEFAULT '',
    password_hash TEXT NOT NULL DEFAULT '',
    balance REAL NOT NULL DEFAULT 0.00,
    status TEXT NOT NULL DEFAULT '正常',
    created_at TEXT NOT NULL,
    address TEXT NOT NULL DEFAULT '',
    loc_lat REAL NOT NULL DEFAULT 39.9644,
    loc_lng REAL NOT NULL DEFAULT 116.3473,
    close_reason TEXT NOT NULL DEFAULT '',
    closed_at TEXT NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS user_avatar (
    user_id INTEGER PRIMARY KEY,
    mime TEXT NOT NULL,
    data BLOB NOT NULL,
    updated_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS station (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    address TEXT NOT NULL,
    lng REAL NOT NULL,
    lat REAL NOT NULL,
    price_per_kwh REAL NOT NULL
);
CREATE TABLE IF NOT EXISTS pile (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    pile_no TEXT NOT NULL UNIQUE,
    station_id INTEGER NOT NULL,
    type TEXT NOT NULL,
    power_kw REAL NOT NULL,
    status TEXT NOT NULL DEFAULT '闲置',
    total_charge_count INTEGER NOT NULL DEFAULT 0,
    total_charge_minutes INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS charge_order (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    order_no TEXT NOT NULL UNIQUE,
    user_id INTEGER NOT NULL,
    pile_id INTEGER NOT NULL,
    status TEXT NOT NULL,
    start_time TEXT NOT NULL,
    end_time TEXT,
    energy_kwh REAL NOT NULL DEFAULT 0,
    amount REAL NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS recharge_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    amount REAL NOT NULL,
    result TEXT NOT NULL,
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS audit_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    actor TEXT NOT NULL,
    action TEXT NOT NULL,
    target TEXT NOT NULL,
    result TEXT NOT NULL,
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS load_forecast (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id INTEGER NOT NULL,
    horizon_hours INTEGER NOT NULL,
    pred_kwh REAL NOT NULL,
    pred_idle INTEGER NOT NULL,
    peak_hour TEXT NOT NULL,
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS reservation (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    pile_id INTEGER NOT NULL,
    status TEXT NOT NULL DEFAULT '有效',
    expire_at TEXT NOT NULL,
    created_at TEXT NOT NULL,
    no_show INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS station_review (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    station_id INTEGER NOT NULL,
    pile_id INTEGER NOT NULL DEFAULT 0,
    score INTEGER NOT NULL,
    comment TEXT NOT NULL DEFAULT '',
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS review_doc (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    pile_id INTEGER NOT NULL,
    user_id INTEGER NOT NULL,
    doc TEXT NOT NULL,
    created_at TEXT NOT NULL
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_review_doc_user_pile ON review_doc(user_id, pile_id);
CREATE TABLE IF NOT EXISTS hourly_load (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id INTEGER NOT NULL,
    hour INTEGER NOT NULL,
    pred_kwh REAL NOT NULL,
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS fault_risk (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    pile_no TEXT NOT NULL,
    station TEXT NOT NULL,
    score REAL NOT NULL,
    level TEXT NOT NULL,
    reason TEXT NOT NULL,
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS analysis_alert (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    level TEXT NOT NULL,
    title TEXT NOT NULL,
    detail TEXT NOT NULL,
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS dispatch_plan (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    station TEXT NOT NULL,
    recommend REAL NOT NULL,
    priority INTEGER NOT NULL,
    reason TEXT NOT NULL,
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS tariff_rule (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id INTEGER NOT NULL,
    start_hour INTEGER NOT NULL,
    end_hour INTEGER NOT NULL,
    price_per_kwh REAL NOT NULL,
    label TEXT NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS session (
    token TEXT PRIMARY KEY,
    user_id INTEGER NOT NULL,
    updated_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS analysis_report (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    model_version TEXT NOT NULL,
    mae REAL NOT NULL,
    rmse REAL NOT NULL,
    sample_n INTEGER NOT NULL,
    weather TEXT NOT NULL,
    created_at TEXT NOT NULL
);
)SQL";

Database::Database(const QString &path) : path_(path) {}

Database::~Database()
{
    const QString name = QString("db_%1").arg(quintptr(QThread::currentThreadId()));
    if (QSqlDatabase::contains(name)) {
        QSqlDatabase::database(name).close();
        QSqlDatabase::removeDatabase(name);
    }
}

QSqlDatabase Database::conn()
{
    const QString name = QString("db_%1").arg(quintptr(QThread::currentThreadId()));
    if (!QSqlDatabase::contains(name)) {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(path_);
        db.open();
    }
    return QSqlDatabase::database(name);
}

/** 建表、补列、演示账号；已有联调库只做兼容迁移，不覆盖数据。 */
bool Database::open()
{
    QMutexLocker locker(&mutex_);
    QDir().mkpath(QFileInfo(path_).absolutePath());
    QSqlDatabase db = conn();
    if (!db.isOpen())
        return false;
    db.exec("PRAGMA foreign_keys = ON");
    db.exec("PRAGMA busy_timeout = 5000");
    db.exec("PRAGMA journal_mode = WAL");
    const QStringList stmts = QString::fromUtf8(kSchema).split(QLatin1Char(';'));
    for (const QString &s : stmts) {
        const QString t = s.trimmed();
        if (!t.isEmpty())
            db.exec(t);
    }
    QSqlQuery q(db);
    q.exec("PRAGMA table_info(user)");
    bool hasPwd = false;
    while (q.next()) {
        if (q.value(1).toString() == QLatin1String("password_hash"))
            hasPwd = true;
    }
    if (!hasPwd)
        db.exec("ALTER TABLE user ADD COLUMN password_hash TEXT NOT NULL DEFAULT ''");
    auto addUserCol = [&](const char *name, const char *def) {
        q.exec("PRAGMA table_info(user)");
        bool has = false;
        while (q.next()) {
            if (q.value(1).toString() == QLatin1String(name))
                has = true;
        }
        if (!has)
            db.exec(QString("ALTER TABLE user ADD COLUMN %1 %2").arg(QLatin1String(name), QLatin1String(def)));
    };
    addUserCol("address", "TEXT NOT NULL DEFAULT ''");
    addUserCol("loc_lat", "REAL NOT NULL DEFAULT 39.9644");
    addUserCol("loc_lng", "REAL NOT NULL DEFAULT 116.3473");
    addUserCol("close_reason", "TEXT NOT NULL DEFAULT ''");
    addUserCol("closed_at", "TEXT NOT NULL DEFAULT ''");
    q.exec("PRAGMA table_info(station_review)");
    bool hasPile = false;
    while (q.next()) {
        if (q.value(1).toString() == QLatin1String("pile_id"))
            hasPile = true;
    }
    if (!hasPile)
        db.exec("ALTER TABLE station_review ADD COLUMN pile_id INTEGER NOT NULL DEFAULT 0");
    auto addCol = [&](const char *table, const char *name, const char *def) {
        q.exec(QString("PRAGMA table_info(%1)").arg(QLatin1String(table)));
        bool has = false;
        while (q.next()) {
            if (q.value(1).toString() == QLatin1String(name))
                has = true;
        }
        if (!has)
            db.exec(QString("ALTER TABLE %1 ADD COLUMN %2 %3")
                        .arg(QLatin1String(table), QLatin1String(name), QLatin1String(def)));
    };
    addCol("reservation", "no_show", "INTEGER NOT NULL DEFAULT 0");
    addCol("pile", "last_seen_at", "TEXT NOT NULL DEFAULT ''");
    addCol("pile", "fault_code", "TEXT NOT NULL DEFAULT ''");
    addCol("pile", "fault_at", "TEXT NOT NULL DEFAULT ''");
    addCol("dispatch_plan", "adopted", "INTEGER NOT NULL DEFAULT 0");
    addCol("dispatch_plan", "adopted_at", "TEXT NOT NULL DEFAULT ''");
    db.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_order_user_open ON charge_order(user_id) "
            "WHERE status IN ('充电中','待结算')");
    db.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_order_pile_charging ON charge_order(pile_id) "
            "WHERE status='充电中'");
    db.exec("CREATE INDEX IF NOT EXISTS idx_reservation_pile_status ON reservation(pile_id, status)");
    db.exec("CREATE INDEX IF NOT EXISTS idx_audit_created ON audit_log(created_at)");
    db.exec("UPDATE station_review SET pile_id=("
            "SELECT p.id FROM pile p WHERE p.station_id=station_review.station_id ORDER BY p.id LIMIT 1"
            ") WHERE IFNULL(pile_id,0)=0");
    const QByteArray defHash = QCryptographicHash::hash(QByteArray("123456"), QCryptographicHash::Sha256).toHex();
    q.prepare("UPDATE user SET password_hash=? WHERE password_hash='' OR password_hash IS NULL");
    q.addBindValue(QString::fromLatin1(defHash));
    q.exec();

    q.exec("SELECT COUNT(*) FROM admin");
    q.next();
    if (q.value(0).toInt() == 0) {
        const QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        const QByteArray hash = QCryptographicHash::hash(QByteArray("123456"), QCryptographicHash::Sha256).toHex();
        q.prepare("INSERT INTO admin(username,password_hash,created_at) VALUES(?,?,?)");
        q.addBindValue("admin");
        q.addBindValue(QString::fromLatin1(hash));
        q.addBindValue(now);
        q.exec();
        q.prepare("INSERT INTO user(phone,nickname,avatar_path,password_hash,balance,status,created_at) VALUES(?,?,?,?,?,?,?)");
        q.addBindValue("13800138000");
        q.addBindValue(QString::fromUtf8("用户8000"));
        q.addBindValue("");
        q.addBindValue(QString::fromLatin1(hash));
        q.addBindValue(80.0);
        q.addBindValue(QString::fromUtf8("正常"));
        q.addBindValue(now);
        q.exec();
        struct ExtraU { const char *p, *n; double b; const char *st; } extras[] = {
            {"13912345678", "用户5678", 36.5, "正常"},
            {"18611112222", "用户2222", 12.0, "正常"},
            {"17700009999", "用户9999", 5.0, "冻结"},
        };
        for (const auto &eu : extras) {
            q.prepare("INSERT INTO user(phone,nickname,avatar_path,password_hash,balance,status,created_at) VALUES(?,?,?,?,?,?,?)");
            q.addBindValue(QString::fromLatin1(eu.p));
            q.addBindValue(QString::fromUtf8(eu.n));
            q.addBindValue("");
            q.addBindValue(QString::fromLatin1(hash));
            q.addBindValue(eu.b);
            q.addBindValue(QString::fromUtf8(eu.st));
            q.addBindValue(now);
            q.exec();
        }
        struct St { const char *n, *a; double lng, lat, p; } sts[] = {
            {"北京理工大学充电站", "北京市海淀区中关村南大街5号", 116.3473, 39.9644, 1.28},
            {"中关村软件园充电站", "北京市海淀区东北旺西路8号", 116.3105, 39.9832, 1.35},
            {"五道口地铁充电站", "北京市海淀区成府路五道口", 116.3382, 39.9928, 1.20},
        };
        for (int i = 0; i < 3; ++i) {
            q.prepare("INSERT INTO station(name,address,lng,lat,price_per_kwh) VALUES(?,?,?,?,?)");
            q.addBindValue(QString::fromUtf8(sts[i].n));
            q.addBindValue(QString::fromUtf8(sts[i].a));
            q.addBindValue(sts[i].lng);
            q.addBindValue(sts[i].lat);
            q.addBindValue(sts[i].p);
            q.exec();
            const int sid = q.lastInsertId().toInt();
            for (int j = 1; j <= 4; ++j) {
                const bool fast = j <= 2;
                const QString status = (i == 2 && j == 4) ? QString::fromUtf8("故障") : QString::fromUtf8("闲置");
                q.prepare("INSERT INTO pile(pile_no,station_id,type,power_kw,status) VALUES(?,?,?,?,?)");
                q.addBindValue(QString("ST%1-P%2").arg(sid, 2, 10, QChar('0')).arg(j, 2, 10, QChar('0')));
                q.addBindValue(sid);
                q.addBindValue(fast ? QString::fromUtf8("快充") : QString::fromUtf8("慢充"));
                q.addBindValue(fast ? 60.0 : 7.0);
                q.addBindValue(status);
                q.exec();
            }
        }
    }

    q.exec("SELECT COUNT(*) FROM station_review");
    q.next();
    if (q.value(0).toInt() == 0) {
        const QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        struct Rv { int uid, sid, pid, score; const char *c; } rvs[] = {
            {1, 1, 1, 5, "P01 快充功率稳，下课过来很快就满"},
            {2, 1, 2, 4, "P02 高峰要排队，充满后记得挪车"},
            {3, 1, 3, 5, "P03 慢充很安静，适合停一整节课"},
            {1, 2, 5, 5, "软件园这根快充车位宽，办公充电方便"},
            {2, 2, 6, 4, "价格略高，但桩体屏幕清楚好操作"},
            {3, 2, 7, 5, "慢充一夜充满，早上取车正好"},
            {1, 3, 9, 4, "地铁口方便，晚高峰这根桩偶尔要等"},
            {2, 3, 10, 5, "快充稳定，从五道口过来很合适"},
            {3, 3, 11, 4, "慢充过夜没掉线，适合住附近的人"},
        };
        for (const auto &r : rvs) {
            q.prepare("INSERT INTO station_review(user_id,station_id,pile_id,score,comment,created_at) VALUES(?,?,?,?,?,?)");
            q.addBindValue(r.uid);
            q.addBindValue(r.sid);
            q.addBindValue(r.pid);
            q.addBindValue(r.score);
            q.addBindValue(QString::fromUtf8(r.c));
            q.addBindValue(now);
            q.exec();
        }
    }

    q.exec("SELECT COUNT(*) FROM review_doc");
    q.next();
    if (q.value(0).toInt() == 0) {
        const auto rows = query("SELECT user_id, station_id, pile_id, score, comment, created_at FROM station_review");
        for (const auto &r : rows) {
            QJsonObject doc{
                {"schema", QStringLiteral("chargehub.review.v1")},
                {"userId", r.value("user_id").toInt()},
                {"pileId", r.value("pile_id").toInt()},
                {"stationId", r.value("station_id").toInt()},
                {"score", r.value("score").toInt()},
                {"comment", r.value("comment").toString()},
                {"createdAt", r.value("created_at").toString()},
                {"updatedAt", r.value("created_at").toString()},
            };
            execute("INSERT INTO review_doc(pile_id,user_id,doc,created_at) VALUES(?,?,?,?)",
                    {r.value("pile_id"), r.value("user_id"),
                     QString::fromUtf8(QJsonDocument(doc).toJson(QJsonDocument::Compact)),
                     r.value("created_at")});
        }
    }

    q.exec("SELECT COUNT(*) FROM audit_log WHERE action='SEED_HISTORY'");
    q.next();
    if (q.value(0).toInt() == 0) {
        const QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        const auto piles = query("SELECT id, station_id, power_kw FROM pile WHERE status!='故障'");
        const auto prices = query("SELECT id, price_per_kwh FROM station");
        QHash<int, double> priceMap;
        for (const auto &s : prices)
            priceMap.insert(s.value("id").toInt(), s.value("price_per_kwh").toDouble());
        if (!piles.isEmpty()) {
            int oid = 1;
            for (int day = 14; day >= 1; --day) {
                const int n = 6 + (day % 5);
                for (int k = 0; k < n; ++k) {
                    const auto &pile = piles[(day * 7 + k) % piles.size()];
                    const int minutes = 20 + ((day + k) % 6) * 10;
                    const QDateTime start = QDateTime::currentDateTime().addDays(-day).addSecs(-((8 + k) * 3600));
                    const double energy = qRound(pile.value("power_kw").toDouble() * (minutes / 60.0) * 1000) / 1000.0;
                    const double amount = qRound(energy * priceMap.value(pile.value("station_id").toInt(), 1.3) * 100) / 100.0;
                    const QString no = QString("CH%1%2").arg(start.toString("yyyyMMdd")).arg(oid, 5, 10, QChar('0'));
                    execute(
                        "INSERT INTO charge_order(order_no,user_id,pile_id,status,start_time,end_time,energy_kwh,amount,created_at) "
                        "VALUES(?,?,?,?,?,?,?,?,?)",
                        {no, 1 + (k % 3), pile.value("id"), QString::fromUtf8("已完成"),
                         start.toString("yyyy-MM-dd HH:mm:ss"),
                         start.addSecs(minutes * 60).toString("yyyy-MM-dd HH:mm:ss"),
                         energy, amount, start.toString("yyyy-MM-dd HH:mm:ss")});
                    execute("UPDATE pile SET total_charge_count=total_charge_count+1, total_charge_minutes=total_charge_minutes+? WHERE id=?",
                            {minutes, pile.value("id")});
                    ++oid;
                }
            }
            execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                    {"system", "SEED_HISTORY", "orders", QString::fromUtf8("成功"), now});
        }
    }

    q.exec("SELECT COUNT(*) FROM load_forecast");
    q.next();
    if (q.value(0).toInt() == 0) {
        const QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        const auto stations = query("SELECT id FROM station");
        for (const auto &s : stations) {
            const auto piles = query("SELECT status FROM pile WHERE station_id=?", {s.value("id")});
            int idle = 0;
            for (const auto &p : piles)
                if (p.value("status").toString() == QString::fromUtf8("闲置"))
                    ++idle;
            const int horizons[] = {1, 6, 24};
            for (int h : horizons) {
                execute(
                    "INSERT INTO load_forecast(station_id,horizon_hours,pred_kwh,pred_idle,peak_hour,created_at) VALUES(?,?,?,?,?,?)",
                    {s.value("id"), h, idle * 18.0 * h / 24.0, qMax(0, idle - h / 8), QStringLiteral("18:00"), now});
            }
        }
    }

    q.exec("SELECT COUNT(*) FROM tariff_rule");
    q.next();
    if (q.value(0).toInt() == 0) {
        const auto stations = query("SELECT id, price_per_kwh FROM station");
        for (const auto &s : stations) {
            const int sid = s.value("id").toInt();
            const double base = s.value("price_per_kwh").toDouble();
            const double valley = qRound(base * 0.85 * 100) / 100.0;
            const double peak = qRound(base * 1.25 * 100) / 100.0;
            execute("INSERT INTO tariff_rule(station_id,start_hour,end_hour,price_per_kwh,label) VALUES(?,?,?,?,?)",
                    {sid, 0, 7, valley, QString::fromUtf8("谷")});
            execute("INSERT INTO tariff_rule(station_id,start_hour,end_hour,price_per_kwh,label) VALUES(?,?,?,?,?)",
                    {sid, 7, 17, base, QString::fromUtf8("平")});
            execute("INSERT INTO tariff_rule(station_id,start_hour,end_hour,price_per_kwh,label) VALUES(?,?,?,?,?)",
                    {sid, 17, 22, peak, QString::fromUtf8("峰")});
            execute("INSERT INTO tariff_rule(station_id,start_hour,end_hour,price_per_kwh,label) VALUES(?,?,?,?,?)",
                    {sid, 22, 24, valley, QString::fromUtf8("谷")});
        }
    }
    return true;
}

QVector<QVariantMap> Database::query(const QString &sql, const QVariantList &args)
{
    QMutexLocker locker(&mutex_);
    QSqlQuery q(conn());
    q.prepare(sql);
    for (const QVariant &a : args)
        q.addBindValue(a);
    q.exec();
    QVector<QVariantMap> rows;
    while (q.next()) {
        QVariantMap row;
        for (int i = 0; i < q.record().count(); ++i)
            row.insert(q.record().fieldName(i), q.value(i));
        rows.append(row);
    }
    return rows;
}

QVariantMap Database::one(const QString &sql, const QVariantList &args)
{
    const auto rows = query(sql, args);
    return rows.isEmpty() ? QVariantMap() : rows.first();
}

int Database::execute(const QString &sql, const QVariantList &args)
{
    QMutexLocker locker(&mutex_);
    QSqlQuery q(conn());
    if (!q.prepare(sql)) {
        lastError_ = q.lastError().text();
        return -1;
    }
    for (const QVariant &a : args)
        q.addBindValue(a);
    if (!q.exec()) {
        lastError_ = q.lastError().text();
        return -1;
    }
    lastError_.clear();
    return q.lastInsertId().toInt();
}

/** fn 返回 true 才 COMMIT，否则 ROLLBACK。开充/停充/结算/充值都走这里。 */
bool Database::transaction(const std::function<bool()> &fn)
{
    QMutexLocker locker(&mutex_);
    QSqlDatabase db = conn();
    if (!db.transaction()) {
        lastError_ = db.lastError().text();
        return false;
    }
    const bool ok = fn();
    if (ok && db.commit()) {
        lastError_.clear();
        return true;
    }
    lastError_ = lastError_.isEmpty() ? db.lastError().text() : lastError_;
    db.rollback();
    return false;
}
