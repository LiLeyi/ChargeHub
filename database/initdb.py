# -*- coding: utf-8 -*-
"""Initialize ChargeHub SQLite database with schema and demo seed data.

可选工具：平时管理端第一次启动会自己建表。本脚本用于单独灌演示库。
不要在用户端调用。不要在已有联调库上覆盖，除非明确要重置。
"""
from __future__ import annotations

import hashlib
import json
import random
import sqlite3
from datetime import datetime, timedelta
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def schemaFile() -> Path:
    """权威表结构：database/schema.sql。"""
    return ROOT / "schema.sql"


SCHEMA = schemaFile()
DB_PATH = ROOT / "chargehub.db"
ADMIN_HASH = hashlib.sha256(b"123456").hexdigest()

STATIONS = [
    (1, "北京理工大学充电站", "北京市海淀区中关村南大街5号", 116.3473, 39.9644, 1.28),
    (2, "中关村软件园充电站", "北京市海淀区东北旺西路8号", 116.3105, 39.9832, 1.35),
    (3, "五道口地铁充电站", "北京市海淀区成府路五道口", 116.3382, 39.9928, 1.20),
]


def connect(path: Path = DB_PATH) -> sqlite3.Connection:
    conn = sqlite3.connect(str(path))
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    return conn


def initDb(path: Path = DB_PATH, *, withHistory: bool = True) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        path.unlink()
    conn = connect(path)
    conn.executescript(schemaFile().read_text(encoding="utf-8"))
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    conn.execute(
        "INSERT INTO admin(username, password_hash, created_at) VALUES (?,?,?)",
        ("admin", ADMIN_HASH, now),
    )

    users = [
        ("13800138000", "用户8000", 80.00, "正常"),
        ("13912345678", "用户5678", 36.50, "正常"),
        ("18611112222", "用户2222", 12.00, "正常"),
        ("17700009999", "用户9999", 5.00, "冻结"),
    ]
    for phone, nick, bal, status in users:
        conn.execute(
            "INSERT INTO user(phone, nickname, avatar_path, password_hash, balance, status, created_at) "
            "VALUES (?,?,?,?,?,?,?)",
            (phone, nick, "", ADMIN_HASH, bal, status, now),
        )

    for s in STATIONS:
        conn.execute(
            "INSERT INTO station(id, name, address, lng, lat, price_per_kwh) "
            "VALUES (?,?,?,?,?,?)",
            s,
        )

    piles = []
    pid = 1
    for sid in (1, 2, 3):
        for i in range(1, 5):
            kind = "快充" if i <= 2 else "慢充"
            power = 60.0 if kind == "快充" else 7.0
            status = "故障" if (sid == 3 and i == 4) else "闲置"
            no = f"ST{sid:02d}-P{i:02d}"
            piles.append((pid, no, sid, kind, power, status))
            conn.execute(
                "INSERT INTO pile(id, pile_no, station_id, type, power_kw, status, "
                "total_charge_count, total_charge_minutes) VALUES (?,?,?,?,?,?,0,0)",
                (pid, no, sid, kind, power, status),
            )
            pid += 1

    if withHistory:
        rng = random.Random(42)
        order_id = 1
        healthy = [p for p in piles if p[5] != "故障"]
        for day in range(30, 0, -1):
            n = rng.randint(6, 14)
            for _ in range(n):
                pile = rng.choice(healthy)
                user_id = rng.choice([1, 2, 3])
                start = datetime.now() - timedelta(
                    days=day, hours=rng.randint(6, 22), minutes=rng.randint(0, 59)
                )
                minutes = rng.choice([20, 30, 40, 50, 60, 75, 90])
                end = start + timedelta(minutes=minutes)
                power = pile[4]
                price = {1: 1.28, 2: 1.35, 3: 1.20}[pile[2]]
                energy = round(power * (minutes / 60.0), 3)
                amount = round(energy * price, 2)
                order_no = f"CH{start.strftime('%Y%m%d')}{order_id:05d}"
                conn.execute(
                    "INSERT INTO charge_order(order_no, user_id, pile_id, status, start_time, "
                    "end_time, energy_kwh, amount, created_at) VALUES (?,?,?,?,?,?,?,?,?)",
                    (
                        order_no,
                        user_id,
                        pile[0],
                        "已完成",
                        start.strftime("%Y-%m-%d %H:%M:%S"),
                        end.strftime("%Y-%m-%d %H:%M:%S"),
                        energy,
                        amount,
                        start.strftime("%Y-%m-%d %H:%M:%S"),
                    ),
                )
                conn.execute(
                    "UPDATE pile SET total_charge_count = total_charge_count + 1, "
                    "total_charge_minutes = total_charge_minutes + ? WHERE id = ?",
                    (minutes, pile[0]),
                )
                order_id += 1

        conn.execute(
            "INSERT INTO recharge_log(user_id, amount, result, created_at) VALUES (?,?,?,?)",
            (1, 80.00, "成功", now),
        )
        conn.execute(
            "INSERT INTO audit_log(actor, action, target, result, created_at) VALUES (?,?,?,?,?)",
            ("system", "INIT", "database", "成功", now),
        )
        reviews = [
            (1, 1, 1, 5, "P01 快充功率稳，下课过来很快就满"),
            (2, 1, 2, 4, "P02 高峰要排队，充满后记得挪车"),
            (1, 2, 5, 5, "软件园这根快充车位宽，办公充电方便"),
            (3, 2, 7, 5, "慢充一夜充满，早上取车正好"),
            (2, 3, 10, 5, "快充稳定，从五道口过来很合适"),
        ]
        for uid, sid, pid, score, comment in reviews:
            conn.execute(
                "INSERT INTO station_review(user_id,station_id,pile_id,score,comment,created_at) VALUES (?,?,?,?,?,?)",
                (uid, sid, pid, score, comment, now),
            )
            doc = json.dumps(
                {
                    "schema": "chargehub.review.v1",
                    "userId": uid,
                    "pileId": pid,
                    "stationId": sid,
                    "score": score,
                    "comment": comment,
                    "createdAt": now,
                    "updatedAt": now,
                },
                ensure_ascii=False,
            )
            conn.execute(
                "INSERT INTO review_doc(pile_id,user_id,doc,created_at) VALUES (?,?,?,?)",
                (pid, uid, doc, now),
            )

    conn.commit()
    conn.close()
    return path


if __name__ == "__main__":
    p = initDb()
    print("database ready:", p)
