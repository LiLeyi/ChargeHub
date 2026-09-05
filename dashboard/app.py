# -*- coding: utf-8 -*-
"""ChargeHub 运营大屏：只读 SQLite + ECharts，不改订单/余额。

HTTP（默认 0.0.0.0:5000，组员浏览器可填服务器 IP）：
  GET /              index.html
  GET /api/overview  今日/本月营收、桩状态、地图点
  GET /api/analysis  负荷预测与告警（读分析表）
  GET /api/tariffs   分时电价（只读）

库路径优先环境变量 CHARGEHUB_DB，否则找管理端 data/chargehub.db。
"""
from __future__ import annotations

import os
import sqlite3
import sys
from datetime import datetime, timedelta
from pathlib import Path

from flask import Flask, jsonify, send_from_directory

ROOT = Path(__file__).resolve().parents[1]
STATIC = Path(__file__).resolve().parent

app = Flask(__name__, static_folder=str(STATIC), static_url_path="")


def findDb() -> Path:
    home = Path.home()
    env = Path(os.environ["CHARGEHUB_DB"]) if os.environ.get("CHARGEHUB_DB") else None
    candidates = [
        env,
        ROOT / "admin" / "data" / "chargehub.db",
        ROOT / "adminserver" / "data" / "chargehub.db",
        home / "ChargeHub-Linux" / "admin" / "data" / "chargehub.db",
        home / "projects" / "ChargeHub" / "adminserver" / "data" / "chargehub.db",
        Path("/home/bit/projects/ChargeHub/adminserver/data/chargehub.db"),
        ROOT / "database" / "chargehub.db",
    ]
    for p in candidates:
        if p is not None and p.exists():
            return p
    return ROOT / "adminserver" / "data" / "chargehub.db"


DB = findDb()


def q(sql: str, args=()):
    conn = sqlite3.connect(str(DB))
    conn.row_factory = sqlite3.Row
    try:
        rows = [dict(r) for r in conn.execute(sql, args).fetchall()]
    except sqlite3.OperationalError:
        rows = []
    conn.close()
    return rows


def one(sql: str, args=()):
    rows = q(sql, args)
    return rows[0] if rows else {}


@app.get("/")
def index():
    return send_from_directory(STATIC, "index.html")


def summed(where, args=()):
    row = one(
        f"SELECT IFNULL(SUM(amount),0) AS s, IFNULL(SUM(energy_kwh),0) AS e, COUNT(*) AS n "
        f"FROM charge_order WHERE status='已完成' AND {where}",
        args,
    )
    return {"amount": round(row.get("s") or 0, 2), "energy": round(row.get("e") or 0, 2), "orders": row.get("n") or 0}


@app.get("/api/overview")
def overview():
    today = datetime.now().strftime("%Y-%m-%d")
    month = datetime.now().strftime("%Y-%m")
    piles = q("SELECT status, COUNT(*) AS n FROM pile GROUP BY status")
    stations = q("SELECT id, name, lng, lat FROM station")
    idle_rank = []
    geo = []
    for s in stations:
        piles_s = q("SELECT status, type FROM pile WHERE station_id=?", (s["id"],))
        idle = sum(1 for p in piles_s if p["status"] == "闲置")
        idle_rank.append({"name": s["name"], "idle": idle, "total": len(piles_s)})
        rev = one(
            "SELECT IFNULL(SUM(o.amount),0) AS a, COUNT(*) AS n FROM charge_order o "
            "JOIN pile p ON p.id=o.pile_id WHERE o.status='已完成' AND p.station_id=?",
            (s["id"],),
        )
        geo.append(
            {
                "name": s["name"],
                "lng": s["lng"],
                "lat": s["lat"],
                "idle": idle,
                "total": len(piles_s),
                "amount": round(rev.get("a") or 0, 2),
                "orders": rev.get("n") or 0,
            }
        )
    idle_rank.sort(key=lambda x: -x["idle"])

    trend7, trend30 = [], []
    for i in range(29, -1, -1):
        d = (datetime.now() - timedelta(days=i)).strftime("%Y-%m-%d")
        s = summed("substr(start_time,1,10)=?", (d,))
        item = {"date": d[5:], "amount": s["amount"], "energy": s["energy"], "orders": s["orders"]}
        trend30.append(item)
        if i <= 6:
            trend7.append(item)

    mix = q(
        "SELECT s.name, IFNULL(SUM(o.amount),0) AS amount FROM charge_order o "
        "JOIN pile p ON p.id=o.pile_id JOIN station s ON s.id=p.station_id "
        "WHERE o.status='已完成' GROUP BY s.id ORDER BY amount DESC"
    )
    hours = []
    for h in range(24):
        n = one(
            "SELECT COUNT(*) AS n FROM charge_order WHERE substr(start_time,12,2)=?",
            (f"{h:02d}",),
        ).get("n", 0)
        hours.append({"hour": f"{h:02d}", "orders": n})

    users = one("SELECT COUNT(*) AS n FROM user")
    active = one(
        "SELECT COUNT(DISTINCT user_id) AS n FROM charge_order WHERE substr(start_time,1,10)=?",
        (today,),
    )
    return jsonify(
        {
            "today": summed("substr(start_time,1,10)=?", (today,)),
            "month": summed("substr(start_time,1,7)=?", (month,)),
            "total": summed("1=1"),
            "users": users.get("n") or 0,
            "activeToday": active.get("n") or 0,
            "piles": piles,
            "idleRank": idle_rank,
            "geo": geo,
            "trend": trend7,
            "trend30": trend30,
            "mix": [{"name": r["name"], "value": round(r["amount"] or 0, 2)} for r in mix],
            "hours": hours,
            "updated": datetime.now().strftime("%H:%M:%S"),
            "db": str(DB),
        }
    )


@app.get("/api/analysis")
def analysis():
    report = one("SELECT * FROM analysis_report ORDER BY id DESC LIMIT 1")
    hourly = q(
        "SELECT h.hour, h.pred_kwh, s.name FROM hourly_load h "
        "JOIN station s ON s.id=h.station_id ORDER BY s.id, h.hour"
    )
    forecasts = q(
        "SELECT f.*, s.name FROM load_forecast f JOIN station s ON s.id=f.station_id "
        "ORDER BY f.station_id, f.horizon_hours"
    )
    risks = q("SELECT * FROM fault_risk ORDER BY score DESC LIMIT 12")
    alerts = q("SELECT * FROM analysis_alert ORDER BY id DESC LIMIT 16")
    plan = q("SELECT * FROM dispatch_plan ORDER BY priority")
    return jsonify(
        {
            "report": report,
            "hourly": hourly,
            "forecasts": forecasts,
            "risks": risks,
            "alerts": alerts,
            "plan": plan,
            "updated": datetime.now().strftime("%H:%M:%S"),
        }
    )


@app.get("/api/tariffs")
def tariffs():
    rows = q(
        "SELECT t.station_id, s.name, t.start_hour, t.end_hour, t.price_per_kwh, t.label "
        "FROM tariff_rule t JOIN station s ON s.id=t.station_id "
        "ORDER BY t.station_id, t.start_hour"
    )
    return jsonify(
        {
            "items": [
                {
                    "stationId": r.get("station_id"),
                    "station": r.get("name"),
                    "startHour": r.get("start_hour"),
                    "endHour": r.get("end_hour"),
                    "pricePerKwh": r.get("price_per_kwh"),
                    "label": r.get("label"),
                }
                for r in rows
            ],
            "updated": datetime.now().strftime("%H:%M:%S"),
        }
    )


def main() -> None:
    global DB
    DB = findDb()
    if not DB.exists():
        sys.path.insert(0, str(ROOT))
        from database.initdb import initDb

        DB = ROOT / "database" / "chargehub.db"
        initDb(DB)
    print("database:", DB)
    print("dashboard http://127.0.0.1:5000")
    print("dashboard http://0.0.0.0:5000  (局域网可用虚拟机 IP 访问)")
    app.run(host="0.0.0.0", port=5000, debug=False)


if __name__ == "__main__":
    main()
