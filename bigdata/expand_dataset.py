# -*- coding: utf-8 -*-
"""把 dataset/04.数据集最终版 扩成模拟生产规模的 CSV，供 Hadoop/Spark 读取。

原理：保留全部真实会话与电池遥测，再按真实站/时段/电量分布加噪声复制。
不写 charge_order / user.balance / pile.status。
"""
from __future__ import annotations

import argparse
import csv
import math
import random
from collections import defaultdict
from datetime import datetime, timedelta
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from paths import RAW_DIR_CANDIDATES, big_dir, raw_dir

TARGET_SESSIONS = 220000
TARGET_TELEMETRY = 80000
SEED = 20260912


def _open(path: Path):
    """UTF-8-SIG 打开原始 CSV（Excel 导出常见 BOM）。"""
    return path.open("r", encoding="utf-8-sig", newline="")


def load_sessions(raw: Path) -> list[dict]:
    """读真实会话 nvv2t.csv。"""
    with _open(raw / "nvv2t.csv") as f:
        return list(csv.DictReader(f))


def load_stations(raw: Path) -> list[dict]:
    """读电站维表 nvv2t_md_end.csv。"""
    with _open(raw / "nvv2t_md_end.csv") as f:
        return list(csv.DictReader(f))


def load_battery(raw: Path) -> list[dict]:
    """读电池遥测 dsv13r2.csv（列名含空格和单位）。"""
    with _open(raw / "dsv13r2.csv") as f:
        return list(csv.DictReader(f))


def fix_year(ts: str, year_shift: int) -> str:
    """修正两位年并整体平移 year_shift 年，让扩样落在 2025 演示窗口。"""
    ts = (ts or "").strip()
    if len(ts) < 10:
        return ts
    try:
        y = int(ts[:4])
        if y < 100:
            y += 2000
        y += year_shift
        return f"{y:04d}{ts[4:]}"
    except ValueError:
        return ts


def jitter(val: float, scale: float, lo: float, hi: float) -> float:
    """相对噪声后再夹紧到 [lo, hi]，保持真实分布形状。"""
    x = val * (1.0 + random.uniform(-scale, scale))
    return max(lo, min(hi, x))


def weekday_name(dt: datetime) -> str:
    """与原始表一致的 Mon/Tues/... 拼写。"""
    return ["Mon", "Tues", "Wed", "Thurs", "Fri", "Sat", "Sun"][dt.weekday()]


def weekday_flags(dt: datetime) -> dict:
    """one-hot 星期列，兼容原始宽表。"""
    names = ["Mon", "Tues", "Wed", "Thurs", "Fri", "Sat", "Sun"]
    return {n: 1 if i == dt.weekday() else 0 for i, n in enumerate(names)}


def parse_created(raw: str) -> datetime | None:
    """解析 created 时间，失败返回 None（该行不参与扩样模板）。"""
    raw = fix_year(raw, 0)
    for fmt in ("%Y-%m-%d %H:%M:%S", "%Y/%m/%d %H:%M:%S"):
        try:
            return datetime.strptime(raw[:19], fmt)
        except ValueError:
            continue
    return None


def expand_sessions(rows: list[dict], target: int) -> list[dict]:
    """保留全部真实会话，再按站/时段分布加噪声复制到 target 行。

    新 sessionId 从 1 亿起，电量/时长/费用做 jitter，时间平移到 2025。
    SEED=20260912 可复现。不写业务库。
    """
    random.seed(SEED)
    out = []
    for i, r in enumerate(rows):
        item = dict(r)
        item["sessionId"] = str(100000000 + i)
        item["created"] = fix_year(r.get("created", ""), 10)
        item["ended"] = fix_year(r.get("ended", ""), 10)
        item["source"] = "real"
        out.append(item)
    if len(out) >= target:
        return out[:target]

    stations = [r.get("stationId") for r in rows if r.get("stationId")]
    users = [r.get("userId") for r in rows if r.get("userId")]
    platforms = [r.get("platform") or "android" for r in rows]
    templates = rows[:]
    nxt = 200000000
    while len(out) < target:
        src = random.choice(templates)
        created = parse_created(src.get("created", ""))
        if created is None:
            created = datetime(2024, 1, 1, 8, 0, 0)
        created = created.replace(year=2023 + (len(out) % 3)) + timedelta(
            days=random.randint(-40, 40),
            hours=random.randint(-3, 3),
            minutes=random.randint(0, 50),
        )
        kwh = jitter(float(src.get("kwhTotal") or 8.0), 0.35, 0.4, 80.0)
        hours = jitter(float(src.get("chargeTimeHrs") or 1.6), 0.30, 0.15, 10.0)
        fee = float(src.get("charging_fees") or 0.0)
        if fee <= 0:
            fee = round(kwh * random.uniform(0.6, 1.4), 2)
        else:
            fee = round(jitter(fee, 0.25, 0.0, 200.0), 2)
        ended = created + timedelta(hours=hours)
        flags = weekday_flags(created)
        item = {
            "sessionId": str(nxt),
            "kwhTotal": f"{kwh:.4f}",
            "charging_fees": f"{fee:.2f}",
            "created": created.strftime("%Y-%m-%d %H:%M:%S"),
            "ended": ended.strftime("%Y-%m-%d %H:%M:%S"),
            "startTime": str(created.hour),
            "endTime": str(ended.hour),
            "chargeTimeHrs": f"{hours:.6f}",
            "weekday": weekday_name(created),
            "platform": random.choice(platforms),
            "userId": random.choice(users) if users else str(90000000 + (nxt % 5000)),
            "stationId": random.choice(stations),
            "locationId": src.get("locationId") or "0",
            "managerVehicle": src.get("managerVehicle") or "0",
            "facilityType": src.get("facilityType") or "3",
            "source": "synth",
        }
        item.update(flags)
        out.append(item)
        nxt += 1
    return out


def expand_battery(rows: list[dict], session_ids: list[str], target: int) -> list[dict]:
    """扩电池遥测，esd 尽量挂到已有 sessionId，便于 battery_risks 关联电站。"""
    random.seed(SEED + 1)
    out = []
    for i, r in enumerate(rows):
        item = dict(r)
        if i < len(session_ids):
            item["esd"] = session_ids[i]
        item["source"] = "real"
        out.append(item)
    templates = rows[:]
    i = 0
    while len(out) < target:
        src = dict(random.choice(templates))
        src["esd"] = session_ids[i % len(session_ids)]
        try:
            src["soc"] = f"{jitter(float(src.get('soc') or 40), 0.25, 2.0, 100.0):.2f}"
        except ValueError:
            src["soc"] = "40.00"
        try:
            src["max_temperature (℃)"] = str(int(jitter(float(src.get("max_temperature (℃)") or 32), 0.2, 15, 62)))
            src["min_temperature (℃)"] = str(int(jitter(float(src.get("min_temperature (℃)") or 28), 0.2, 10, 55)))
        except ValueError:
            pass
        src["source"] = "synth"
        out.append(src)
        i += 1
    return out


def write_csv(path: Path, rows: list[dict], fieldnames: list[str]) -> None:
    """UTF-8 无 BOM 写出，Spark 用 encoding=UTF-8 读。"""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)


def main() -> None:
    """写出 dataset/big/sessions.csv、stations.csv、telemetry.csv。"""
    parser = argparse.ArgumentParser(description="扩样 ChargeHub 充电大数据集")
    parser.add_argument("--sessions", type=int, default=TARGET_SESSIONS)
    parser.add_argument("--telemetry", type=int, default=TARGET_TELEMETRY)
    args = parser.parse_args()

    raw = raw_dir()
    if not (raw / "nvv2t.csv").exists():
        for c in RAW_DIR_CANDIDATES:
            if (c / "nvv2t.csv").exists():
                raw = c
                break
    if not (raw / "nvv2t.csv").exists():
        raise SystemExit(f"找不到 nvv2t.csv，已试: {raw}")

    dest = big_dir()
    dest.mkdir(parents=True, exist_ok=True)

    sessions = expand_sessions(load_sessions(raw), args.sessions)
    stations = load_stations(raw)
    battery = expand_battery(load_battery(raw), [s["sessionId"] for s in sessions], args.telemetry)

    sess_fields = [
        "sessionId", "kwhTotal", "charging_fees", "created", "ended", "startTime", "endTime",
        "chargeTimeHrs", "weekday", "platform", "userId", "stationId", "locationId",
        "managerVehicle", "facilityType", "Mon", "Tues", "Wed", "Thurs", "Fri", "Sat", "Sun", "source",
    ]
    bat_fields = list(load_battery(raw)[0].keys()) + ["source"]
    # 去重且保序
    seen = set()
    bat_fields = [c for c in bat_fields if not (c in seen or seen.add(c))]

    write_csv(dest / "sessions.csv", sessions, sess_fields)
    write_csv(dest / "stations.csv", stations, list(stations[0].keys()) if stations else ["stationId"])
    write_csv(dest / "telemetry.csv", battery, bat_fields)

    by_src = defaultdict(int)
    for s in sessions:
        by_src[s.get("source", "?")] += 1
    print("EXPAND_OK")
    print("raw", raw)
    print("out", dest)
    print("sessions", len(sessions), dict(by_src))
    print("stations", len(stations), "telemetry", len(battery))


if __name__ == "__main__":
    main()
