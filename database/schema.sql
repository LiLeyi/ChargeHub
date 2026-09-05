-- ChargeHub SQLite 表结构（权威说明，与 C++ 启动建表一致）
-- 写权限：仅 PC 管理端 adminserver。用户端禁止打开本文件。
-- 大屏 Flask 只读；ml/forecast.py 只写分析相关表。
-- 库文件位置：管理端可执行文件旁 data/chargehub.db
-- 关系：admin 独立；user 1—1 user_avatar；station 1—* pile；
--       user/pile 1—* charge_order / reservation / station_review

PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;

CREATE TABLE IF NOT EXISTS admin (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    username      TEXT    NOT NULL UNIQUE,
    password_hash TEXT    NOT NULL,
    created_at    TEXT    NOT NULL
);

CREATE TABLE IF NOT EXISTS user (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    phone         TEXT    NOT NULL UNIQUE,
    nickname      TEXT    NOT NULL,
    avatar_path   TEXT    NOT NULL DEFAULT '',
    password_hash TEXT    NOT NULL DEFAULT '',
    balance       REAL    NOT NULL DEFAULT 0.00,
    status        TEXT    NOT NULL DEFAULT '正常' CHECK (status IN ('正常', '冻结', '注销')),
    created_at    TEXT    NOT NULL,
    address       TEXT    NOT NULL DEFAULT '',
    loc_lat       REAL    NOT NULL DEFAULT 39.9644,
    loc_lng       REAL    NOT NULL DEFAULT 116.3473,
    close_reason  TEXT    NOT NULL DEFAULT '',
    closed_at     TEXT    NOT NULL DEFAULT ''
);

-- 用户头像二进制，仅管理端写入；user.avatar_path='db' 表示已有头像
CREATE TABLE IF NOT EXISTS user_avatar (
    user_id    INTEGER PRIMARY KEY REFERENCES user(id),
    mime       TEXT    NOT NULL,
    data       BLOB    NOT NULL,
    updated_at TEXT    NOT NULL
);

CREATE TABLE IF NOT EXISTS station (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    name          TEXT    NOT NULL,
    address       TEXT    NOT NULL,
    lng           REAL    NOT NULL,
    lat           REAL    NOT NULL,
    price_per_kwh REAL    NOT NULL CHECK (price_per_kwh > 0)
);

CREATE TABLE IF NOT EXISTS pile (
    id                   INTEGER PRIMARY KEY AUTOINCREMENT,
    pile_no              TEXT    NOT NULL UNIQUE,
    station_id           INTEGER NOT NULL REFERENCES station(id),
    type                 TEXT    NOT NULL CHECK (type IN ('快充', '慢充')),
    power_kw             REAL    NOT NULL CHECK (power_kw > 0),
    status               TEXT    NOT NULL DEFAULT '闲置' CHECK (status IN ('闲置', '在用', '故障')),
    total_charge_count   INTEGER NOT NULL DEFAULT 0,
    total_charge_minutes INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS charge_order (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    order_no    TEXT    NOT NULL UNIQUE,
    user_id     INTEGER NOT NULL REFERENCES user(id),
    pile_id     INTEGER NOT NULL REFERENCES pile(id),
    status      TEXT    NOT NULL CHECK (status IN ('充电中', '待结算', '已完成', '已取消')),
    start_time  TEXT    NOT NULL,
    end_time    TEXT,
    energy_kwh  REAL    NOT NULL DEFAULT 0,
    amount      REAL    NOT NULL DEFAULT 0,
    created_at  TEXT    NOT NULL
);

CREATE TABLE IF NOT EXISTS recharge_log (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER NOT NULL REFERENCES user(id),
    amount     REAL    NOT NULL,
    result     TEXT    NOT NULL,
    created_at TEXT    NOT NULL
);

CREATE TABLE IF NOT EXISTS audit_log (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    actor      TEXT    NOT NULL,
    action     TEXT    NOT NULL,
    target     TEXT    NOT NULL,
    result     TEXT    NOT NULL,
    created_at TEXT    NOT NULL
);

CREATE TABLE IF NOT EXISTS load_forecast (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id    INTEGER NOT NULL REFERENCES station(id),
    horizon_hours INTEGER NOT NULL,
    pred_kwh      REAL    NOT NULL,
    pred_idle     INTEGER NOT NULL,
    peak_hour     TEXT    NOT NULL,
    created_at    TEXT    NOT NULL
);

CREATE TABLE IF NOT EXISTS reservation (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER NOT NULL REFERENCES user(id),
    pile_id    INTEGER NOT NULL REFERENCES pile(id),
    status     TEXT    NOT NULL DEFAULT '有效' CHECK (status IN ('有效', '已取消', '已履约')),
    expire_at  TEXT    NOT NULL,
    created_at TEXT    NOT NULL
);

CREATE TABLE IF NOT EXISTS station_review (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER NOT NULL REFERENCES user(id),
    station_id INTEGER NOT NULL REFERENCES station(id),
    pile_id    INTEGER NOT NULL DEFAULT 0,
    score      INTEGER NOT NULL CHECK (score BETWEEN 1 AND 5),
    comment    TEXT    NOT NULL DEFAULT '',
    created_at TEXT    NOT NULL
);

CREATE TABLE IF NOT EXISTS review_doc (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    pile_id    INTEGER NOT NULL,
    user_id    INTEGER NOT NULL,
    doc        TEXT    NOT NULL,
    created_at TEXT    NOT NULL
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_review_doc_user_pile ON review_doc(user_id, pile_id);

CREATE TABLE IF NOT EXISTS hourly_load (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id INTEGER NOT NULL,
    hour       INTEGER NOT NULL,
    pred_kwh   REAL    NOT NULL,
    created_at TEXT    NOT NULL
);

CREATE TABLE IF NOT EXISTS fault_risk (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    pile_no    TEXT    NOT NULL,
    station    TEXT    NOT NULL,
    score      REAL    NOT NULL,
    level      TEXT    NOT NULL,
    reason     TEXT    NOT NULL,
    created_at TEXT    NOT NULL
);

CREATE TABLE IF NOT EXISTS analysis_alert (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    level      TEXT    NOT NULL,
    title      TEXT    NOT NULL,
    detail     TEXT    NOT NULL,
    created_at TEXT    NOT NULL
);

CREATE TABLE IF NOT EXISTS dispatch_plan (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    station    TEXT    NOT NULL,
    recommend  REAL    NOT NULL,
    priority   INTEGER NOT NULL,
    reason     TEXT    NOT NULL,
    created_at TEXT    NOT NULL
);

CREATE TABLE IF NOT EXISTS analysis_report (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    model_version TEXT    NOT NULL,
    mae           REAL    NOT NULL,
    rmse          REAL    NOT NULL,
    sample_n      INTEGER NOT NULL,
    weather       TEXT    NOT NULL,
    created_at    TEXT    NOT NULL
);

DROP INDEX IF EXISTS idx_order_created_at;
DROP INDEX IF EXISTS idx_recharge_user_created;
CREATE INDEX IF NOT EXISTS idx_order_user_status ON charge_order(user_id, status);
CREATE INDEX IF NOT EXISTS idx_order_user_id ON charge_order(user_id, id DESC);
CREATE INDEX IF NOT EXISTS idx_order_status ON charge_order(status);
CREATE INDEX IF NOT EXISTS idx_order_start ON charge_order(start_time);
CREATE INDEX IF NOT EXISTS idx_pile_station ON pile(station_id);
CREATE INDEX IF NOT EXISTS idx_pile_status ON pile(status);
CREATE INDEX IF NOT EXISTS idx_reservation_user_status ON reservation(user_id, status);
CREATE INDEX IF NOT EXISTS idx_reservation_pile_status ON reservation(pile_id, status);
CREATE INDEX IF NOT EXISTS idx_reservation_status_expire ON reservation(status, expire_at);
CREATE INDEX IF NOT EXISTS idx_station_review_station ON station_review(station_id, id DESC);
CREATE INDEX IF NOT EXISTS idx_station_review_user_pile ON station_review(user_id, pile_id);
CREATE INDEX IF NOT EXISTS idx_station_review_pile_id ON station_review(pile_id, id DESC);
CREATE INDEX IF NOT EXISTS idx_review_doc_pile_id ON review_doc(pile_id, id DESC);
CREATE INDEX IF NOT EXISTS idx_recharge_user_id ON recharge_log(user_id, id DESC);
