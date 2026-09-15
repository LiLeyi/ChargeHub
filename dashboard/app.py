# -*- coding: utf-8 -*-
"""ChargeHub 运营大屏 HTTP：只读 SQLite + Spark JSON + 静态页。

【职责】
    给浏览器提供只读 API 和 Vue 3 + ECharts 大屏。不改订单、余额、桩状态，
    也没有 POST/PUT/DELETE。用户端业务仍走 TCP :8888，见 protocol/messages.md。

【传输】
    Flask 默认绑定 0.0.0.0:5000。本机浏览器 http://127.0.0.1:5000 ；
    组员用管理端底栏同网段 IP:5000。与 Socket 端口 8888 互不替代。

【数据】
    业务 KPI 读管理端同一份 chargehub.db（WAL）。
    智能分析图表优先读 bigdata/output/spark_report.json（Hadoop + PySpark）。
    没有 Spark 输出时回退 analysis_* 表。表缺失时 q() 返回 []，页面不崩。

【协作】
    管理端 MainWindow.openDash 拉起本进程。Spark 作业见 bigdata/README.md。
    分时电价的启用/改价只在运营 GUI。

【接口】docs/接口约定.md 、 docs/大数据与智能分析.md
"""
from __future__ import annotations

import json
import os
import sqlite3
import sys
from datetime import datetime, timedelta
from pathlib import Path

from flask import Flask, jsonify, send_from_directory

ROOT = Path(__file__).resolve().parents[1]
STATIC = Path(__file__).resolve().parent
DIST = STATIC / "dist"
PAGE_ROOT = DIST if (DIST / "index.html").is_file() else STATIC

app = Flask(__name__, static_folder=str(PAGE_ROOT), static_url_path="")


def findDb() -> Path:
    """按环境变量和管理端常见路径找只读库，找不到则返回默认路径（调用方再决定是否 init）。

    探测顺序：
      1. CHARGEHUB_DB（作业/脚本显式指定）
      2. 与本文件相对的 admin/data、adminserver/data
      3. ~/ChargeHub-Linux/admin/data（WSL 成品）
      4. ~/projects/ChargeHub/... 与 /home/bit/...（开发树）
      5. 工程 database/chargehub.db

    不创建文件、不写库。
    """
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


def findSparkReport() -> Path | None:
    """定位 PySpark 写出的 spark_report.json，找不到返回 None。"""
    home = Path.home()
    env = Path(os.environ["CHARGEHUB_SPARK_OUT"]) if os.environ.get("CHARGEHUB_SPARK_OUT") else None
    cands = [
        env / "spark_report.json" if env else None,
        ROOT / "bigdata" / "output" / "spark_report.json",
        home / "ChargeHub-Linux" / "bigdata" / "output" / "spark_report.json",
        home / "projects" / "ChargeHub" / "bigdata" / "output" / "spark_report.json",
        Path("/mnt/d/大三小学期/计算机软件实训/ChargeHub/bigdata/output/spark_report.json"),
    ]
    for p in cands:
        if p is not None and p.exists():
            return p
    return None


def loadSpark() -> dict:
    """读 Spark 报告；文件坏了或没有则 {}。"""
    p = findSparkReport()
    if p is None:
        return {}
    try:
        return json.loads(p.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}


def q(sql: str, args=()):
    """只读查询；表/列不存在时返回空列表，避免大屏因分析表未刷新而 500。

    每次调用独立 connect，用完关闭。row_factory=Row 转 dict，键为 SQL 列名（下划线）。
    对外 JSON 再在视图函数里改成小驼峰。
    """
    conn = sqlite3.connect(str(DB))
    conn.row_factory = sqlite3.Row
    try:
        rows = [dict(r) for r in conn.execute(sql, args).fetchall()]
    except sqlite3.OperationalError:
        rows = []
    conn.close()
    return rows


def one(sql: str, args=()):
    """只取第一行；没有则空 dict。"""
    rows = q(sql, args)
    return rows[0] if rows else {}


@app.get("/")
def index():
    """GET / → Vue 3 + ECharts 大屏（优先 dashboard/dist）。不是 API。"""
    return send_from_directory(PAGE_ROOT, "index.html")


def summed(where, args=()):
    """已完成订单按条件汇总金额（元）、电量、笔数。只读 charge_order。"""
    row = one(
        f"SELECT IFNULL(SUM(amount),0) AS s, IFNULL(SUM(energy_kwh),0) AS e, COUNT(*) AS n "
        f"FROM charge_order WHERE status='已完成' AND {where}",
        args,
    )
    return {"amount": round(row.get("s") or 0, 2), "energy": round(row.get("e") or 0, 2), "orders": row.get("n") or 0}


@app.get("/api/overview")
def overview():
    """GET /api/overview：今日/本月/累计营收、桩状态、闲置排行、地图点、7/30 日趋势、站营收、24h 分布。

    响应 JSON 主要键：today/month/total（amount,energy,orders）、users、activeToday、
    piles[{status,n}]、idleRank、geo[{name,lng,lat,idle,total,amount,orders}]、
    trend、trend30、mix、hours、updated、db。
    全部 SELECT，无写。金额单位元。
    """
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
            "spark": loadSpark().get("kpis") or {},
        }
    )


@app.get("/api/analysis")
def analysis():
    """GET /api/analysis：负荷预测、分时、故障风险、告警、调度建议。

    优先合并 Spark 报告（模型指标、告警、调度）；没有则只读 analysis_* 表。
    不在这里跑 ML。作业入口是 bigdata/scripts/run_pipeline.sh。
    """
    spark = loadSpark()
    report = spark.get("report") or one("SELECT * FROM analysis_report ORDER BY id DESC LIMIT 1")
    hourly = spark.get("hourly") or q(
        "SELECT h.hour, h.pred_kwh, s.name FROM hourly_load h "
        "JOIN station s ON s.id=h.station_id ORDER BY s.id, h.hour"
    )
    forecasts = spark.get("forecasts") or q(
        "SELECT f.*, s.name FROM load_forecast f JOIN station s ON s.id=f.station_id "
        "ORDER BY f.station_id, f.horizon_hours"
    )
    risks = spark.get("battery") or q("SELECT * FROM fault_risk ORDER BY score DESC LIMIT 12")
    alerts = spark.get("alerts") or q("SELECT * FROM analysis_alert ORDER BY id DESC LIMIT 16")
    plan = spark.get("plan") or q("SELECT * FROM dispatch_plan ORDER BY priority")
    return jsonify(
        {
            "report": report,
            "hourly": hourly,
            "forecasts": forecasts,
            "risks": risks,
            "alerts": alerts,
            "plan": plan,
            "engine": (spark.get("kpis") or {}).get("engine") or "sqlite",
            "updated": datetime.now().strftime("%H:%M:%S"),
        }
    )


@app.get("/api/bigdata")
def bigdata():
    """GET /api/bigdata：Hadoop/PySpark 全量结果（聚类、热力、电池、平台）。

    没有 spark_report.json 时返回 ready=false，大屏仍显示业务 KPI。
    """
    spark = loadSpark()
    p = findSparkReport()
    return jsonify(
        {
            "ready": bool(spark),
            "path": str(p) if p else "",
            "kpis": spark.get("kpis") or {},
            "report": spark.get("report") or {},
            "hourly": spark.get("hourly") or [],
            "forecasts": spark.get("forecasts") or [],
            "clusters": spark.get("clusters") or [],
            "battery": spark.get("battery") or [],
            "alerts": spark.get("alerts") or [],
            "plan": spark.get("plan") or [],
            "heatmap": spark.get("heatmap") or [],
            "platform": spark.get("platform") or [],
            "topStations": spark.get("top_stations") or [],
            "models": spark.get("models") or [],
            "importances": spark.get("importances") or [],
            "anomalies": spark.get("anomalies") or {},
            "rfm": spark.get("rfm") or [],
            "rules": spark.get("rules") or [],
            "peakClf": spark.get("peak_clf") or {},
            "updated": spark.get("updated") or datetime.now().strftime("%H:%M:%S"),
        }
    )


@app.get("/api/tariffs")
def tariffs():
    """GET /api/tariffs：分时电价只读列表。无规则的站不会出现。

    每行：stationId, station, startHour, endHour, pricePerKwh, label（峰/平/谷）。
    启用/改价只在管理端 GUI，本接口不提供 POST。
    """
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
    """找库；文件不存在才 init 演示库。然后 0.0.0.0:5000 只读服务（debug=False）。"""
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
