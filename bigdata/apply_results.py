# -*- coding: utf-8 -*-
"""把 Spark JSON 写入分析派生表，供 Qt 智能分析和大屏 /api/analysis 使用。

【只允许写的表】hourly_load / load_forecast / fault_risk / analysis_alert /
dispatch_plan / analysis_report。先 DELETE 再 INSERT。
【禁止写】charge_order、user.balance、pile.status。

管理端 AnalyticsService::refreshForecast 优先读同一份 JSON 走等价逻辑；
本脚本给命令行 / ml/forecast.py 调用。小时负荷会对每个业务库电站复制一份
全网曲线（示意调度，不是逐站独立模型）。
"""
from __future__ import annotations

import json
import os
import sqlite3
import sys
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from paths import report_path  # noqa: E402


def find_db() -> Path:
    """找管理端 SQLite，顺序与 dashboard.app.findDb 对齐。"""
    home = Path(os.environ.get("HOME") or Path.home())
    env = Path(os.environ["CHARGEHUB_DB"]) if os.environ.get("CHARGEHUB_DB") else None
    cands = [
        env,
        ROOT / "adminserver" / "data" / "chargehub.db",
        ROOT / "admin" / "data" / "chargehub.db",
        home / "ChargeHub-Linux" / "admin" / "data" / "chargehub.db",
        home / "projects" / "ChargeHub" / "adminserver" / "data" / "chargehub.db",
        ROOT / "database" / "chargehub.db",
    ]
    for p in cands:
        if p is not None and p.exists():
            return p
    return ROOT / "adminserver" / "data" / "chargehub.db"


def load_bundle() -> dict:
    """读 spark_report.json；没有文件则退出，提示先跑 spark_analyze.py。"""
    p = report_path()
    if not p.exists():
        raise SystemExit(f"找不到 {p}，请先跑 spark_analyze.py")
    return json.loads(p.read_text(encoding="utf-8"))


def main() -> None:
    """清空六张分析表后写入 Spark 结果，commit 一次。"""
    bundle = load_bundle()
    db = find_db()
    if not db.exists():
        raise SystemExit(f"找不到库 {db}")
    conn = sqlite3.connect(str(db))
    stations = conn.execute("SELECT id, name FROM station").fetchall()
    if not stations:
        raise SystemExit("station 表为空，无法映射预测")
    t = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    hourly = bundle.get("hourly") or []
    forecasts = bundle.get("forecasts") or []
    battery = bundle.get("battery") or []
    alerts = bundle.get("alerts") or []
    plan = bundle.get("plan") or []
    report = bundle.get("report") or {}

    cur = conn.cursor()
    for table in (
        "hourly_load",
        "load_forecast",
        "fault_risk",
        "analysis_alert",
        "dispatch_plan",
        "analysis_report",
    ):
        cur.execute(f"DELETE FROM {table}")

    for sid, _name in stations:
        for h in hourly:
            cur.execute(
                "INSERT INTO hourly_load(station_id,hour,pred_kwh,created_at) VALUES(?,?,?,?)",
                (sid, int(h.get("hour") or 0), float(h.get("pred_kwh") or 0), t),
            )
        for f in forecasts:
            cur.execute(
                "INSERT INTO load_forecast(station_id,horizon_hours,pred_kwh,pred_idle,peak_hour,created_at) "
                "VALUES(?,?,?,?,?,?)",
                (
                    sid,
                    int(f.get("horizon_hours") or 0),
                    float(f.get("pred_kwh") or 0),
                    int(f.get("pred_idle") or 0),
                    str(f.get("peak_hour") or ""),
                    t,
                ),
            )

    for b in battery[:20]:
        cur.execute(
            "INSERT INTO fault_risk(pile_no,station,score,level,reason,created_at) VALUES(?,?,?,?,?,?)",
            (
                str(b.get("sessionId") or "spark"),
                str(b.get("station") or "-"),
                float(b.get("score") or 0),
                str(b.get("level") or "需关注"),
                str(b.get("reason") or ""),
                t,
            ),
        )
    for a in alerts:
        cur.execute(
            "INSERT INTO analysis_alert(level,title,detail,created_at) VALUES(?,?,?,?)",
            (a.get("level") or "提示", a.get("title") or "", a.get("detail") or "", a.get("created_at") or t),
        )
    for p in plan:
        cur.execute(
            "INSERT INTO dispatch_plan(station,recommend,priority,reason,created_at) VALUES(?,?,?,?,?)",
            (
                p.get("station") or "",
                float(p.get("recommend") or 0),
                int(p.get("priority") or 0),
                p.get("reason") or "",
                p.get("created_at") or t,
            ),
        )
    cur.execute(
        "INSERT INTO analysis_report(model_version,mae,rmse,sample_n,weather,created_at) VALUES(?,?,?,?,?,?)",
        (
            report.get("model_version") or "spark-gbt-kmeans-v1",
            float(report.get("mae") or 0),
            float(report.get("rmse") or 0),
            int(report.get("sample_n") or 0),
            str(report.get("weather") or "spark"),
            report.get("created_at") or t,
        ),
    )
    conn.commit()
    conn.close()
    print("APPLY_OK", db)


if __name__ == "__main__":
    main()
