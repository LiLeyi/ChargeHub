/**
 * @file database.cpp
 * @brief 建表、旧库迁移、演示电站与账号
 */
#include "database.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
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

namespace {

QString sqlOperation(const QString &sql)
{
    const QString trimmed = sql.trimmed();
    int separator = 0;
    while (separator < trimmed.size() && !trimmed.at(separator).isSpace())
        ++separator;
    const QString operation = trimmed.left(separator).toUpper();
    if (operation == QLatin1String("SELECT") || operation == QLatin1String("INSERT")
        || operation == QLatin1String("UPDATE") || operation == QLatin1String("DELETE")
        || operation == QLatin1String("PRAGMA") || operation == QLatin1String("CREATE")
        || operation == QLatin1String("ALTER") || operation == QLatin1String("DROP")
        || operation == QLatin1String("REPLACE")
        || operation == QLatin1String("WITH")) {
        return operation;
    }
    return QStringLiteral("UNKNOWN");
}

void logSqlError(const QString &context, const QSqlError &error, const QString &sql)
{
    // Do not log SQL text or bound values: callers may pass passwords or tokens.
    qCritical().noquote() << context << "failed:" << error.text()
                          << "(SQL operation:" << sqlOperation(sql) + QLatin1Char(')');
}

Database::ErrorKind errorKindFor(const QSqlError &error)
{
    bool parsed = false;
    const int nativeCode = error.nativeErrorCode().toInt(&parsed);
    const int primaryCode = parsed ? (nativeCode & 0xff) : 0;
    const QString text = error.text().toLower();
    if (primaryCode == 5 || primaryCode == 6 || text.contains(QLatin1String("locked"))
        || text.contains(QLatin1String("busy"))) {
        return Database::ErrorKind::Busy;
    }
    if (primaryCode == 19 || text.contains(QLatin1String("constraint")))
        return Database::ErrorKind::Constraint;
    return Database::ErrorKind::Other;
}

bool execSql(QSqlQuery &query, const QString &sql, const QString &context)
{
    if (query.exec(sql))
        return true;
    logSqlError(context, query.lastError(), sql);
    return false;
}

bool execSql(QSqlDatabase &database, const QString &sql, const QString &context)
{
    QSqlQuery query(database);
    return execSql(query, sql, context);
}

bool prepareSql(QSqlQuery &query, const QString &sql, const QString &context)
{
    if (query.prepare(sql))
        return true;
    logSqlError(context, query.lastError(), sql);
    return false;
}

bool execPrepared(QSqlQuery &query, const QString &sql, const QString &context)
{
    if (query.exec())
        return true;
    logSqlError(context, query.lastError(), sql);
    return false;
}

bool tableHasColumn(QSqlDatabase &database, const QString &table, const QString &column,
                    bool *found)
{
    const QString sql = QStringLiteral("PRAGMA table_info(%1)").arg(table);
    QSqlQuery query(database);
    if (!execSql(query, sql, QStringLiteral("Inspect table %1").arg(table)))
        return false;

    *found = false;
    while (query.next()) {
        if (query.value(1).toString() == column) {
            *found = true;
            break;
        }
    }
    if (query.lastError().isValid()) {
        logSqlError(QStringLiteral("Read table metadata for %1").arg(table), query.lastError(), sql);
        return false;
    }
    return true;
}

bool queryCount(QSqlDatabase &database, const QString &sql, const QString &context, int *count)
{
    QSqlQuery query(database);
    if (!execSql(query, sql, context))
        return false;
    if (!query.next()) {
        if (query.lastError().isValid())
            logSqlError(context, query.lastError(), sql);
        else
            qCritical().noquote() << context << "failed: count query returned no row";
        return false;
    }
    *count = query.value(0).toInt();
    return true;
}

bool hasNoUniquenessConflict(QSqlDatabase &database, const QString &sql,
                             const QString &description)
{
    QSqlQuery query(database);
    if (!execSql(query, sql, QStringLiteral("Check existing %1 conflicts").arg(description)))
        return false;
    if (query.next()) {
        qCritical().noquote()
            << "Cannot install database uniqueness constraints: existing database contains"
            << description << "conflicts. No business data was changed.";
        return false;
    }
    if (query.lastError().isValid()) {
        logSqlError(QStringLiteral("Read existing %1 conflicts").arg(description),
                    query.lastError(), sql);
        return false;
    }
    return true;
}

bool validateBusinessUniqueness(QSqlDatabase &database)
{
    return hasNoUniquenessConflict(
               database,
               QStringLiteral("SELECT 1 FROM reservation WHERE status='有效' "
                              "GROUP BY pile_id HAVING COUNT(*)>1 LIMIT 1"),
               QStringLiteral("duplicate active reservations for one pile"))
        && hasNoUniquenessConflict(
            database,
            QStringLiteral("SELECT 1 FROM reservation WHERE status='有效' "
                           "GROUP BY user_id HAVING COUNT(*)>1 LIMIT 1"),
            QStringLiteral("duplicate active reservations for one user"))
        && hasNoUniquenessConflict(
            database,
            QStringLiteral("SELECT 1 FROM charge_order WHERE status IN ('充电中','待结算') "
                           "GROUP BY user_id HAVING COUNT(*)>1 LIMIT 1"),
            QStringLiteral("duplicate unfinished orders for one user"))
        && hasNoUniquenessConflict(
            database,
            QStringLiteral("SELECT 1 FROM charge_order WHERE status='充电中' "
                           "GROUP BY pile_id HAVING COUNT(*)>1 LIMIT 1"),
            QStringLiteral("duplicate charging orders for one pile"));
}

} // namespace

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
    status TEXT NOT NULL DEFAULT '正常' CHECK (status IN ('正常', '冻结', '注销')),
    created_at TEXT NOT NULL,
    address TEXT NOT NULL DEFAULT '',
    loc_lat REAL NOT NULL DEFAULT 39.9644,
    loc_lng REAL NOT NULL DEFAULT 116.3473,
    close_reason TEXT NOT NULL DEFAULT '',
    closed_at TEXT NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS user_avatar (
    user_id INTEGER PRIMARY KEY REFERENCES user(id),
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
    price_per_kwh REAL NOT NULL CHECK (price_per_kwh > 0)
);
CREATE TABLE IF NOT EXISTS pile (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    pile_no TEXT NOT NULL UNIQUE,
    station_id INTEGER NOT NULL REFERENCES station(id),
    type TEXT NOT NULL CHECK (type IN ('快充', '慢充')),
    power_kw REAL NOT NULL CHECK (power_kw > 0),
    status TEXT NOT NULL DEFAULT '闲置' CHECK (status IN ('闲置', '在用', '故障')),
    total_charge_count INTEGER NOT NULL DEFAULT 0,
    total_charge_minutes INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS charge_order (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    order_no TEXT NOT NULL UNIQUE,
    user_id INTEGER NOT NULL REFERENCES user(id),
    pile_id INTEGER NOT NULL REFERENCES pile(id),
    status TEXT NOT NULL CHECK (status IN ('充电中', '待结算', '已完成', '已取消')),
    start_time TEXT NOT NULL,
    end_time TEXT,
    energy_kwh REAL NOT NULL DEFAULT 0,
    amount REAL NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS recharge_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES user(id),
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
    station_id INTEGER NOT NULL REFERENCES station(id),
    horizon_hours INTEGER NOT NULL,
    pred_kwh REAL NOT NULL,
    pred_idle INTEGER NOT NULL,
    peak_hour TEXT NOT NULL,
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS reservation (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES user(id),
    pile_id INTEGER NOT NULL REFERENCES pile(id),
    status TEXT NOT NULL DEFAULT '有效' CHECK (status IN ('有效', '已取消', '已履约')),
    expire_at TEXT NOT NULL,
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS station_review (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES user(id),
    station_id INTEGER NOT NULL REFERENCES station(id),
    pile_id INTEGER NOT NULL DEFAULT 0,
    score INTEGER NOT NULL CHECK (score BETWEEN 1 AND 5),
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
CREATE TABLE IF NOT EXISTS analysis_report (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    model_version TEXT NOT NULL,
    mae REAL NOT NULL,
    rmse REAL NOT NULL,
    sample_n INTEGER NOT NULL,
    weather TEXT NOT NULL,
    created_at TEXT NOT NULL
);
CREATE UNIQUE INDEX IF NOT EXISTS uq_reservation_active_pile
ON reservation(pile_id) WHERE status='有效';
CREATE UNIQUE INDEX IF NOT EXISTS uq_reservation_active_user
ON reservation(user_id) WHERE status='有效';
CREATE UNIQUE INDEX IF NOT EXISTS uq_order_open_user
ON charge_order(user_id) WHERE status IN ('充电中','待结算');
CREATE UNIQUE INDEX IF NOT EXISTS uq_order_charging_pile
ON charge_order(pile_id) WHERE status='充电中';
DROP INDEX IF EXISTS idx_order_created_at;
DROP INDEX IF EXISTS idx_recharge_user_created;
DROP INDEX IF EXISTS idx_order_status;
DROP INDEX IF EXISTS idx_reservation_user_status;
CREATE INDEX IF NOT EXISTS idx_order_user_status ON charge_order(user_id, status);
CREATE INDEX IF NOT EXISTS idx_order_user_id ON charge_order(user_id, id DESC);
CREATE INDEX IF NOT EXISTS idx_order_status_start ON charge_order(status, start_time);
CREATE INDEX IF NOT EXISTS idx_order_start ON charge_order(start_time);
CREATE INDEX IF NOT EXISTS idx_pile_station ON pile(station_id);
CREATE INDEX IF NOT EXISTS idx_pile_status ON pile(status);
CREATE INDEX IF NOT EXISTS idx_reservation_user_status_expire_id
ON reservation(user_id, status, expire_at, id DESC);
CREATE INDEX IF NOT EXISTS idx_reservation_pile_status ON reservation(pile_id, status);
CREATE INDEX IF NOT EXISTS idx_reservation_status_expire ON reservation(status, expire_at);
CREATE INDEX IF NOT EXISTS idx_station_review_station ON station_review(station_id, id DESC);
CREATE INDEX IF NOT EXISTS idx_station_review_user_pile ON station_review(user_id, pile_id);
CREATE INDEX IF NOT EXISTS idx_station_review_pile_id ON station_review(pile_id, id DESC);
CREATE INDEX IF NOT EXISTS idx_review_doc_pile_id ON review_doc(pile_id, id DESC);
CREATE INDEX IF NOT EXISTS idx_recharge_user_id ON recharge_log(user_id, id DESC);
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
    QSqlDatabase db;
    if (!QSqlDatabase::contains(name)) {
        db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(path_);
    } else {
        db = QSqlDatabase::database(name, false);
    }
    if (!db.isValid()) {
        qCritical().noquote() << "Create SQLite connection failed: QSQLITE driver is unavailable";
        return db;
    }
    bool openedNow = false;
    if (!db.isOpen()) {
        if (!db.open()) {
            qCritical().noquote() << "Open SQLite database failed:" << db.lastError().text()
                                  << "database path:" << path_;
            return db;
        }
        openedNow = true;
    }
    if (openedNow
        && (!execSql(db, QStringLiteral("PRAGMA foreign_keys = ON"), QStringLiteral("Enable foreign keys"))
            || !execSql(db, QStringLiteral("PRAGMA busy_timeout = 5000"), QStringLiteral("Set busy timeout")))) {
        db.close();
    }
    return db;
}

bool Database::open()
{
    QMutexLocker locker(&mutex_);
    const QString databaseDir = QFileInfo(path_).absolutePath();
    if (!QDir().mkpath(databaseDir)) {
        qCritical().noquote() << "Create database directory failed:" << databaseDir;
        return false;
    }
    QSqlDatabase db = conn();
    if (!db.isValid() || !db.isOpen())
        return false;
    if (!execSql(db, QStringLiteral("PRAGMA journal_mode = WAL"), QStringLiteral("Enable WAL mode"))) {
        return false;
    }
    const QStringList stmts = QString::fromUtf8(kSchema).split(QLatin1Char(';'));
    bool uniquenessValidated = false;
    for (int i = 0; i < stmts.size(); ++i) {
        const QString &s = stmts.at(i);
        const QString t = s.trimmed();
        if (!uniquenessValidated
            && t.startsWith(QLatin1String("CREATE UNIQUE INDEX IF NOT EXISTS uq_"))) {
            if (!validateBusinessUniqueness(db))
                return false;
            uniquenessValidated = true;
        }
        if (!t.isEmpty()
            && !execSql(db, t, QStringLiteral("Initialize schema statement %1").arg(i + 1))) {
            return false;
        }
    }
    QSqlQuery q(db);
    bool hasPwd = false;
    if (!tableHasColumn(db, QStringLiteral("user"), QStringLiteral("password_hash"), &hasPwd))
        return false;
    if (!hasPwd
        && !execSql(db, QStringLiteral("ALTER TABLE user ADD COLUMN password_hash TEXT NOT NULL DEFAULT ''"),
                    QStringLiteral("Add user.password_hash column"))) {
        return false;
    }
    auto addUserCol = [&](const char *name, const char *def) {
        bool has = false;
        if (!tableHasColumn(db, QStringLiteral("user"), QLatin1String(name), &has))
            return false;
        if (has)
            return true;
        const QString sql = QStringLiteral("ALTER TABLE user ADD COLUMN %1 %2")
                                .arg(QLatin1String(name), QLatin1String(def));
        return execSql(db, sql, QStringLiteral("Add user.%1 column").arg(QLatin1String(name)));
    };
    if (!addUserCol("address", "TEXT NOT NULL DEFAULT ''")
        || !addUserCol("loc_lat", "REAL NOT NULL DEFAULT 39.9644")
        || !addUserCol("loc_lng", "REAL NOT NULL DEFAULT 116.3473")
        || !addUserCol("close_reason", "TEXT NOT NULL DEFAULT ''")
        || !addUserCol("closed_at", "TEXT NOT NULL DEFAULT ''")) {
        return false;
    }
    bool hasPile = false;
    if (!tableHasColumn(db, QStringLiteral("station_review"), QStringLiteral("pile_id"), &hasPile))
        return false;
    if (!hasPile
        && !execSql(db, QStringLiteral("ALTER TABLE station_review ADD COLUMN pile_id INTEGER NOT NULL DEFAULT 0"),
                    QStringLiteral("Add station_review.pile_id column"))) {
        return false;
    }
    const QString reviewPileBackfill = QStringLiteral(
        "UPDATE station_review SET pile_id=("
        "SELECT p.id FROM pile p WHERE p.station_id=station_review.station_id ORDER BY p.id LIMIT 1"
        ") WHERE IFNULL(pile_id,0)=0");
    if (!execSql(db, reviewPileBackfill, QStringLiteral("Backfill station review pile IDs")))
        return false;
    const QByteArray defHash = QCryptographicHash::hash(QByteArray("123456"), QCryptographicHash::Sha256).toHex();
    const QString updatePasswords = QStringLiteral(
        "UPDATE user SET password_hash=? WHERE password_hash='' OR password_hash IS NULL");
    if (!prepareSql(q, updatePasswords, QStringLiteral("Prepare default password hash update")))
        return false;
    q.addBindValue(QString::fromLatin1(defHash));
    if (!execPrepared(q, updatePasswords, QStringLiteral("Update default password hashes")))
        return false;

    int adminCount = 0;
    if (!queryCount(db, QStringLiteral("SELECT COUNT(*) FROM admin"),
                    QStringLiteral("Count administrator records"), &adminCount)) {
        return false;
    }
    if (adminCount == 0) {
        const QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        const QByteArray hash = QCryptographicHash::hash(QByteArray("123456"), QCryptographicHash::Sha256).toHex();
        const QString insertAdmin = QStringLiteral(
            "INSERT INTO admin(username,password_hash,created_at) VALUES(?,?,?)");
        if (!prepareSql(q, insertAdmin, QStringLiteral("Prepare administrator seed")))
            return false;
        q.addBindValue("admin");
        q.addBindValue(QString::fromLatin1(hash));
        q.addBindValue(now);
        if (!execPrepared(q, insertAdmin, QStringLiteral("Insert administrator seed")))
            return false;
        const QString insertUser = QStringLiteral(
            "INSERT INTO user(phone,nickname,avatar_path,password_hash,balance,status,created_at) "
            "VALUES(?,?,?,?,?,?,?)");
        if (!prepareSql(q, insertUser, QStringLiteral("Prepare user seed")))
            return false;
        q.addBindValue("13800138000");
        q.addBindValue(QString::fromUtf8("用户8000"));
        q.addBindValue("");
        q.addBindValue(QString::fromLatin1(hash));
        q.addBindValue(80.0);
        q.addBindValue(QString::fromUtf8("正常"));
        q.addBindValue(now);
        if (!execPrepared(q, insertUser, QStringLiteral("Insert user seed")))
            return false;
        struct ExtraU { const char *p, *n; double b; const char *st; } extras[] = {
            {"13912345678", "用户5678", 36.5, "正常"},
            {"18611112222", "用户2222", 12.0, "正常"},
            {"17700009999", "用户9999", 5.0, "冻结"},
        };
        for (const auto &eu : extras) {
            if (!prepareSql(q, insertUser, QStringLiteral("Prepare additional user seed")))
                return false;
            q.addBindValue(QString::fromLatin1(eu.p));
            q.addBindValue(QString::fromUtf8(eu.n));
            q.addBindValue("");
            q.addBindValue(QString::fromLatin1(hash));
            q.addBindValue(eu.b);
            q.addBindValue(QString::fromUtf8(eu.st));
            q.addBindValue(now);
            if (!execPrepared(q, insertUser, QStringLiteral("Insert additional user seed")))
                return false;
        }
        struct St { const char *n, *a; double lng, lat, p; } sts[] = {
            {"北京理工大学充电站", "北京市海淀区中关村南大街5号", 116.3473, 39.9644, 1.28},
            {"中关村软件园充电站", "北京市海淀区东北旺西路8号", 116.3105, 39.9832, 1.35},
            {"五道口地铁充电站", "北京市海淀区成府路五道口", 116.3382, 39.9928, 1.20},
        };
        const QString insertStation = QStringLiteral(
            "INSERT INTO station(name,address,lng,lat,price_per_kwh) VALUES(?,?,?,?,?)");
        const QString insertPile = QStringLiteral(
            "INSERT INTO pile(pile_no,station_id,type,power_kw,status) VALUES(?,?,?,?,?)");
        for (int i = 0; i < 3; ++i) {
            if (!prepareSql(q, insertStation, QStringLiteral("Prepare station seed")))
                return false;
            q.addBindValue(QString::fromUtf8(sts[i].n));
            q.addBindValue(QString::fromUtf8(sts[i].a));
            q.addBindValue(sts[i].lng);
            q.addBindValue(sts[i].lat);
            q.addBindValue(sts[i].p);
            if (!execPrepared(q, insertStation, QStringLiteral("Insert station seed")))
                return false;
            const int sid = q.lastInsertId().toInt();
            for (int j = 1; j <= 4; ++j) {
                const bool fast = j <= 2;
                const QString status = (i == 2 && j == 4) ? QString::fromUtf8("故障") : QString::fromUtf8("闲置");
                if (!prepareSql(q, insertPile, QStringLiteral("Prepare pile seed")))
                    return false;
                q.addBindValue(QString("ST%1-P%2").arg(sid, 2, 10, QChar('0')).arg(j, 2, 10, QChar('0')));
                q.addBindValue(sid);
                q.addBindValue(fast ? QString::fromUtf8("快充") : QString::fromUtf8("慢充"));
                q.addBindValue(fast ? 60.0 : 7.0);
                q.addBindValue(status);
                if (!execPrepared(q, insertPile, QStringLiteral("Insert pile seed")))
                    return false;
            }
        }
    }

    int reviewCount = 0;
    if (!queryCount(db, QStringLiteral("SELECT COUNT(*) FROM station_review"),
                    QStringLiteral("Count station reviews"), &reviewCount)) {
        return false;
    }
    if (reviewCount == 0) {
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
        const QString insertReview = QStringLiteral(
            "INSERT INTO station_review(user_id,station_id,pile_id,score,comment,created_at) "
            "VALUES(?,?,?,?,?,?)");
        for (const auto &r : rvs) {
            if (!prepareSql(q, insertReview, QStringLiteral("Prepare station review seed")))
                return false;
            q.addBindValue(r.uid);
            q.addBindValue(r.sid);
            q.addBindValue(r.pid);
            q.addBindValue(r.score);
            q.addBindValue(QString::fromUtf8(r.c));
            q.addBindValue(now);
            if (!execPrepared(q, insertReview, QStringLiteral("Insert station review seed")))
                return false;
        }
    }

    int reviewDocCount = 0;
    if (!queryCount(db, QStringLiteral("SELECT COUNT(*) FROM review_doc"),
                    QStringLiteral("Count review documents"), &reviewDocCount)) {
        return false;
    }
    if (reviewDocCount == 0) {
        const auto reviews = queryChecked(
            "SELECT user_id, station_id, pile_id, score, comment, created_at FROM station_review");
        if (!reviews.ok)
            return false;
        for (const auto &r : reviews.rows) {
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
            const auto inserted = executeChecked(
                "INSERT INTO review_doc(pile_id,user_id,doc,created_at) VALUES(?,?,?,?)",
                {r.value("pile_id"), r.value("user_id"),
                 QString::fromUtf8(QJsonDocument(doc).toJson(QJsonDocument::Compact)),
                 r.value("created_at")});
            if (!inserted.ok || inserted.insertId <= 0)
                return false;
        }
    }

    int historySeedCount = 0;
    if (!queryCount(db, QStringLiteral("SELECT COUNT(*) FROM audit_log WHERE action='SEED_HISTORY'"),
                    QStringLiteral("Count history seed markers"), &historySeedCount)) {
        return false;
    }
    if (historySeedCount == 0) {
        const QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        const auto pileRows = queryChecked("SELECT id, station_id, power_kw FROM pile WHERE status!='故障'");
        const auto priceRows = queryChecked("SELECT id, price_per_kwh FROM station");
        if (!pileRows.ok || !priceRows.ok)
            return false;
        QHash<int, double> priceMap;
        for (const auto &s : priceRows.rows)
            priceMap.insert(s.value("id").toInt(), s.value("price_per_kwh").toDouble());
        if (!pileRows.rows.isEmpty()) {
            int oid = 1;
            for (int day = 14; day >= 1; --day) {
                const int n = 6 + (day % 5);
                for (int k = 0; k < n; ++k) {
                    const auto &pile = pileRows.rows[(day * 7 + k) % pileRows.rows.size()];
                    const int minutes = 20 + ((day + k) % 6) * 10;
                    const QDateTime start = QDateTime::currentDateTime().addDays(-day).addSecs(-((8 + k) * 3600));
                    const double energy = qRound(pile.value("power_kw").toDouble() * (minutes / 60.0) * 1000) / 1000.0;
                    const double amount = qRound(energy * priceMap.value(pile.value("station_id").toInt(), 1.3) * 100) / 100.0;
                    const QString no = QString("CH%1%2").arg(start.toString("yyyyMMdd")).arg(oid, 5, 10, QChar('0'));
                    const auto inserted = executeChecked(
                        "INSERT INTO charge_order(order_no,user_id,pile_id,status,start_time,end_time,energy_kwh,amount,created_at) "
                        "VALUES(?,?,?,?,?,?,?,?,?)",
                        {no, 1 + (k % 3), pile.value("id"), QString::fromUtf8("已完成"),
                         start.toString("yyyy-MM-dd HH:mm:ss"),
                         start.addSecs(minutes * 60).toString("yyyy-MM-dd HH:mm:ss"),
                         energy, amount, start.toString("yyyy-MM-dd HH:mm:ss")});
                    if (!inserted.ok || inserted.insertId <= 0)
                        return false;
                    const auto updated = executeChecked(
                        "UPDATE pile SET total_charge_count=total_charge_count+1, "
                        "total_charge_minutes=total_charge_minutes+? WHERE id=?",
                        {minutes, pile.value("id")});
                    if (!updated.ok || updated.rowsAffected != 1)
                        return false;
                    ++oid;
                }
            }
            const auto audit = executeChecked(
                "INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                {"system", "SEED_HISTORY", "orders", QString::fromUtf8("成功"), now});
            if (!audit.ok || audit.insertId <= 0)
                return false;
        }
    }

    int forecastCount = 0;
    if (!queryCount(db, QStringLiteral("SELECT COUNT(*) FROM load_forecast"),
                    QStringLiteral("Count load forecasts"), &forecastCount)) {
        return false;
    }
    if (forecastCount == 0) {
        const QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        const auto stations = queryChecked("SELECT id FROM station");
        if (!stations.ok)
            return false;
        for (const auto &s : stations.rows) {
            const auto piles = queryChecked("SELECT status FROM pile WHERE station_id=?", {s.value("id")});
            if (!piles.ok)
                return false;
            int idle = 0;
            for (const auto &p : piles.rows)
                if (p.value("status").toString() == QString::fromUtf8("闲置"))
                    ++idle;
            const int horizons[] = {1, 6, 24};
            for (int h : horizons) {
                const auto inserted = executeChecked(
                    "INSERT INTO load_forecast(station_id,horizon_hours,pred_kwh,pred_idle,peak_hour,created_at) VALUES(?,?,?,?,?,?)",
                    {s.value("id"), h, idle * 18.0 * h / 24.0, qMax(0, idle - h / 8), QStringLiteral("18:00"), now});
                if (!inserted.ok || inserted.insertId <= 0)
                    return false;
            }
        }
    }
    return true;
}

QVector<QVariantMap> Database::query(const QString &sql, const QVariantList &args)
{
    const QueryResult result = queryChecked(sql, args);
    return result.ok ? result.rows : QVector<QVariantMap>();
}

QVariantMap Database::one(const QString &sql, const QVariantList &args)
{
    const auto rows = query(sql, args);
    return rows.isEmpty() ? QVariantMap() : rows.first();
}

int Database::execute(const QString &sql, const QVariantList &args)
{
    const WriteResult result = executeChecked(sql, args);
    return result.ok ? int(result.insertId) : 0;
}

Database::QueryResult Database::queryChecked(const QString &sql, const QVariantList &args)
{
    QMutexLocker locker(&mutex_);
    QSqlDatabase db = conn();
    QueryResult result;
    if (!db.isValid() || !db.isOpen()) {
        result.errorKind = db.lastError().isValid() ? errorKindFor(db.lastError()) : ErrorKind::Other;
        return result;
    }
    QSqlQuery q(db);
    if (!prepareSql(q, sql, QStringLiteral("Prepare database query"))) {
        result.errorKind = errorKindFor(q.lastError());
        return result;
    }
    for (const QVariant &a : args)
        q.addBindValue(a);
    if (!execPrepared(q, sql, QStringLiteral("Execute database query"))) {
        result.errorKind = errorKindFor(q.lastError());
        return result;
    }
    while (q.next()) {
        QVariantMap row;
        for (int i = 0; i < q.record().count(); ++i)
            row.insert(q.record().fieldName(i), q.value(i));
        result.rows.append(row);
    }
    if (q.lastError().isValid()) {
        logSqlError(QStringLiteral("Read database query results"), q.lastError(), sql);
        result.rows.clear();
        result.errorKind = errorKindFor(q.lastError());
        return result;
    }
    result.ok = true;
    result.errorKind = ErrorKind::None;
    return result;
}

Database::WriteResult Database::executeChecked(const QString &sql, const QVariantList &args)
{
    QMutexLocker locker(&mutex_);
    QSqlDatabase db = conn();
    WriteResult result;
    if (!db.isValid() || !db.isOpen()) {
        result.errorKind = db.lastError().isValid() ? errorKindFor(db.lastError()) : ErrorKind::Other;
        return result;
    }
    QSqlQuery q(db);
    if (!prepareSql(q, sql, QStringLiteral("Prepare database command"))) {
        result.errorKind = errorKindFor(q.lastError());
        return result;
    }
    for (const QVariant &a : args)
        q.addBindValue(a);
    if (!execPrepared(q, sql, QStringLiteral("Execute database command"))) {
        result.errorKind = errorKindFor(q.lastError());
        return result;
    }
    result.ok = true;
    result.rowsAffected = q.numRowsAffected();
    const QVariant insertId = q.lastInsertId();
    result.insertId = insertId.isValid() ? insertId.toLongLong() : 0;
    result.errorKind = ErrorKind::None;
    return result;
}

bool Database::runTransaction(const std::function<bool()> &operation, ErrorKind *errorKind)
{
    QMutexLocker locker(&mutex_);
    if (errorKind)
        *errorKind = ErrorKind::None;

    QSqlDatabase db = conn();
    if (!db.isValid() || !db.isOpen()) {
        if (errorKind)
            *errorKind = db.lastError().isValid() ? errorKindFor(db.lastError()) : ErrorKind::Other;
        return false;
    }

    const QString beginSql = QStringLiteral("BEGIN IMMEDIATE");
    QSqlQuery begin(db);
    if (!execSql(begin, beginSql, QStringLiteral("Begin database transaction"))) {
        if (errorKind)
            *errorKind = errorKindFor(begin.lastError());
        return false;
    }

    bool operationOk = false;
    try {
        operationOk = operation();
    } catch (...) {
        if (errorKind)
            *errorKind = ErrorKind::Other;
        qCritical() << "Database transaction callback raised an exception";
    }

    if (!operationOk) {
        QSqlQuery rollback(db);
        execSql(rollback, QStringLiteral("ROLLBACK"), QStringLiteral("Rollback database transaction"));
        if (errorKind && *errorKind == ErrorKind::None)
            *errorKind = ErrorKind::Other;
        return false;
    }

    const QString commitSql = QStringLiteral("COMMIT");
    QSqlQuery commit(db);
    if (execSql(commit, commitSql, QStringLiteral("Commit database transaction")))
        return true;

    if (errorKind)
        *errorKind = errorKindFor(commit.lastError());
    QSqlQuery rollback(db);
    execSql(rollback, QStringLiteral("ROLLBACK"), QStringLiteral("Rollback failed commit"));
    return false;
}
