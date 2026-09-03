# -*- coding: utf-8 -*-
"""从 SQLite 导出订单 CSV，供课程数据集提交。"""
from __future__ import annotations

import csv
import sqlite3
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DB = ROOT / "adminserver" / "data" / "chargehub.db"
if not DB.exists():
    DB = ROOT / "database" / "chargehub.db"
OUT = ROOT / "docs" / "数据集-第X组.csv"


def main() -> None:
    if not DB.exists():
        sys.path.insert(0, str(ROOT))
        from database.initdb import initDb

        initDb(DB)
    conn = sqlite3.connect(str(DB))
    rows = conn.execute(
        "SELECT o.order_no, o.status, o.start_time, o.end_time, o.energy_kwh, o.amount, "
        "u.phone, p.pile_no, s.name "
        "FROM charge_order o "
        "JOIN user u ON u.id=o.user_id "
        "JOIN pile p ON p.id=o.pile_id "
        "JOIN station s ON s.id=p.station_id "
        "ORDER BY o.start_time"
    ).fetchall()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f)
        w.writerow(["订单号", "状态", "开始时间", "结束时间", "电量kWh", "金额", "手机号", "电桩编号", "电站"])
        w.writerows(rows)
    print("wrote", OUT, "rows", len(rows))


if __name__ == "__main__":
    main()
