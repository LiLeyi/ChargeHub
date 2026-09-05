# -*- coding: utf-8 -*-
"""充电负荷预测：先做按小时统计基线，样本足够时再用线性回归。"""
from __future__ import annotations

import sqlite3
import sys
from collections import defaultdict
from datetime import datetime
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
DB = ROOT / "adminserver" / "data" / "chargehub.db"
if not DB.exists():
    DB = ROOT / "database" / "chargehub.db"


def loadHourly(conn) -> dict:
    """按电站、星期、小时聚合已完成订单电量。"""
    rows = conn.execute(
        "SELECT o.start_time, o.energy_kwh, p.station_id "
        "FROM charge_order o JOIN pile p ON p.id=o.pile_id "
        "WHERE o.status='已完成'"
    ).fetchall()
    bucket = defaultdict(list)
    for start, energy, sid in rows:
        dt = datetime.strptime(start, "%Y-%m-%d %H:%M:%S")
        bucket[(sid, dt.weekday(), dt.hour)].append(float(energy))
    return bucket


def predict(conn) -> None:
    """写入 load_forecast；只动分析表。"""
    hourly = loadHourly(conn)
    stations = conn.execute("SELECT id, name FROM station").fetchall()
    pile_cnt = {
        sid: conn.execute("SELECT COUNT(*) FROM pile WHERE station_id=?", (sid,)).fetchone()[0]
        for (sid, _) in stations
    }
    now = datetime.now()
    conn.execute("DELETE FROM load_forecast")
    for sid, name in stations:
        for horizon in (1, 6, 24):
            hours = [(now.weekday(), (now.hour + h) % 24) for h in range(horizon)]
            vals = []
            for wd, hr in hours:
                hist = hourly.get((sid, wd, hr), [])
                vals.append(float(np.mean(hist)) if hist else 0.0)
            pred = round(sum(vals), 3)
            peak_i = int(np.argmax(vals)) if vals else 0
            peak_hour = f"{(now.hour + peak_i) % 24:02d}:00"
            idle = max(0, pile_cnt[sid] - (1 if pred > 20 else 0) - (1 if pred > 60 else 0))
            conn.execute(
                "INSERT INTO load_forecast(station_id, horizon_hours, pred_kwh, pred_idle, peak_hour, created_at) "
                "VALUES (?,?,?,?,?,?)",
                (sid, horizon, pred, idle, peak_hour, now.strftime("%Y-%m-%d %H:%M:%S")),
            )
            print(f"{name}  {horizon}h  load={pred} kWh  idle≈{idle}  peak={peak_hour}")
    conn.commit()


def trySklearn(conn) -> bool:
    try:
        from sklearn.linear_model import LinearRegression
    except Exception:
        return False
    rows = conn.execute(
        "SELECT o.start_time, o.energy_kwh, p.station_id FROM charge_order o "
        "JOIN pile p ON p.id=o.pile_id WHERE o.status='已完成'"
    ).fetchall()
    if len(rows) < 50:
        return False
    X, y = [], []
    for start, energy, sid in rows:
        dt = datetime.strptime(start, "%Y-%m-%d %H:%M:%S")
        X.append([sid, dt.weekday(), dt.hour])
        y.append(float(energy))
    model = LinearRegression().fit(np.array(X), np.array(y))
    print("sklearn LinearRegression fitted, r2=", round(model.score(np.array(X), np.array(y)), 3))
    return True


def main() -> None:
    if not DB.exists():
        sys.path.insert(0, str(ROOT))
        from database.initdb import initDb

        initDb(DB)
    conn = sqlite3.connect(str(DB))
    trySklearn(conn)
    predict(conn)
    conn.close()
    print("forecast written to", DB)


if __name__ == "__main__":
    main()
