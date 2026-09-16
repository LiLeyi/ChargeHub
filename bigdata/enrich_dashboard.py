# -*- coding: utf-8 -*-
"""从 dataset/big CSV + spark_report 汇总多屏图表 JSON。

只读 CSV，不改订单 / 余额 / 桩状态。供 dashboard GET /api/bigdata 合并。
"""
from __future__ import annotations

import csv
import json
import math
import random
import re
from collections import defaultdict
from datetime import datetime, timedelta
from pathlib import Path

from paths import ROOT, big_dir, out_dir, report_path, raw_dir

FACILITY = {"1": "交流", "2": "直流", "3": "交直流", "4": "超充"}
TARIFF_ORDER = ["谷时", "平时", "峰时", "尖峰"]

REGION_RE = re.compile(
    r"(郑东新区|航空港区|经济技术开发区|经开区|高新技术产业开发区|高新区|"
    r"管城回族区|管城区|二七区|金水区|中原区|上街区|惠济区)"
)
WD_KEY = {
    "Mon": "周一",
    "Tue": "周二",
    "Tues": "周二",
    "Wed": "周三",
    "Thu": "周四",
    "Thurs": "周四",
    "Fri": "周五",
    "Sat": "周六",
    "Sun": "周日",
}
WD_ORDER = ["周一", "周二", "周三", "周四", "周五", "周六", "周日"]
PY_WD = ["周一", "周二", "周三", "周四", "周五", "周六", "周日"]


def fnum(v, d=0.0) -> float:
    try:
        x = float(v)
        return x if math.isfinite(x) else d
    except (TypeError, ValueError):
        return d


def tariff_label(hour: int) -> str:
    h = int(hour)
    if h >= 23 or h < 7:
        return "谷时"
    if 10 <= h < 12 or 18 <= h < 21:
        return "尖峰"
    if 8 <= h < 10 or 16 <= h < 18:
        return "峰时"
    return "平时"


def col_of(row: dict, *prefixes: str) -> str:
    for key in row:
        kn = key.replace(" ", "")
        for p in prefixes:
            if key.startswith(p) or kn.startswith(p.replace(" ", "")):
                return row.get(key) or ""
    return ""


def read_csv_rows(path: Path) -> list[dict]:
    if not path.is_file():
        return []
    raw = path.read_bytes()
    for enc in ("utf-8-sig", "utf-8", "gbk", "gb18030"):
        try:
            return list(csv.DictReader(raw.decode(enc).splitlines()))
        except UnicodeDecodeError:
            continue
    return []


def fix_year(ts: str) -> str:
    s = (ts or "").strip()
    if len(s) >= 4 and s[:2] == "00" and s[2:4].isdigit():
        return "20" + s[2:]
    return s


def region_of(address: str, name: str = "") -> str:
    text = f"{address or ''}{name or ''}"
    m = REGION_RE.search(text)
    if not m:
        return "其他"
    hit = m.group(1)
    if "高新" in hit:
        return "高新区"
    if "经济技术" in hit or hit == "经开区":
        return "经开区"
    if "管城" in hit:
        return "管城区"
    return hit


def dur_bucket(hrs: float) -> str:
    m = hrs * 60.0
    if m < 30:
        return "0-30分钟"
    if m < 60:
        return "30-60分钟"
    if m < 120:
        return "1-2小时"
    if m < 240:
        return "2-4小时"
    if m < 480:
        return "4-8小时"
    return "8小时以上"


def fee_bucket(fee: float) -> str:
    if fee <= 0.01:
        return "免费"
    if fee < 1:
        return "0-1元"
    if fee < 3:
        return "1-3元"
    if fee < 5:
        return "3-5元"
    return "5元以上"


def soc_bucket(soc: float) -> str:
    if soc < 20:
        return "0-20%"
    if soc < 40:
        return "20-40%"
    if soc < 60:
        return "40-60%"
    if soc < 80:
        return "60-80%"
    return "80-100%"


def metrics(y_true, y_pred) -> dict:
    n = min(len(y_true), len(y_pred))
    if n < 3:
        return {"mae": 0, "rmse": 0, "r2": 0, "n": n}
    yt = y_true[:n]
    yp = y_pred[:n]
    mae = sum(abs(a - b) for a, b in zip(yt, yp)) / n
    rmse = math.sqrt(sum((a - b) ** 2 for a, b in zip(yt, yp)) / n)
    mean = sum(yt) / n
    ss_tot = sum((a - mean) ** 2 for a in yt)
    ss_res = sum((a - b) ** 2 for a, b in zip(yt, yp))
    r2 = 0.0 if ss_tot <= 1e-9 else 1.0 - ss_res / ss_tot
    return {"mae": round(mae, 4), "rmse": round(rmse, 4), "r2": round(r2, 4), "n": n}


def kmeans(points: list[list[float]], k: int, seed: int = 7, rounds: int = 18):
    if not points or k <= 0:
        return [], []
    k = min(k, len(points))
    rng = random.Random(seed)
    cents = [list(points[i]) for i in rng.sample(range(len(points)), k)]
    labels = [0] * len(points)
    dim = len(points[0])
    for _ in range(rounds):
        for i, p in enumerate(points):
            best, bd = 0, 1e18
            for j, c in enumerate(cents):
                d = sum((p[t] - c[t]) ** 2 for t in range(dim))
                if d < bd:
                    best, bd = j, d
            labels[i] = best
        acc = [[0.0] * dim for _ in range(k)]
        cnt = [0] * k
        for i, p in enumerate(points):
            lab = labels[i]
            cnt[lab] += 1
            for t in range(dim):
                acc[lab][t] += p[t]
        for j in range(k):
            if cnt[j]:
                cents[j] = [acc[j][t] / cnt[j] for t in range(dim)]
    return labels, cents


def silhouette(points: list[list[float]], labels: list[int], k: int) -> float:
    if len(points) < 3 or k < 2:
        return 0.0
    groups = [[] for _ in range(k)]
    for i, lab in enumerate(labels):
        if 0 <= lab < k:
            groups[lab].append(i)
    if any(len(g) == 0 for g in groups):
        return 0.0
    scores = []
    dim = len(points[0])

    def dist(i, j):
        return math.sqrt(sum((points[i][t] - points[j][t]) ** 2 for t in range(dim)))

    for i, lab in enumerate(labels):
        own = groups[lab]
        if len(own) <= 1:
            continue
        a = sum(dist(i, j) for j in own if j != i) / (len(own) - 1)
        b = min(
            sum(dist(i, j) for j in groups[c]) / len(groups[c])
            for c in range(k)
            if c != lab and groups[c]
        )
        den = max(a, b)
        scores.append(0.0 if den <= 1e-9 else (b - a) / den)
    return round(sum(scores) / len(scores), 4) if scores else 0.0


def scan_sessions(path: Path) -> dict:
    daily = defaultdict(lambda: {"orders": 0, "kwh": 0.0, "fee": 0.0, "hrs": 0.0})
    hours = {h: {"orders": 0, "kwh": 0.0} for h in range(24)}
    weekday = {k: {"orders": 0, "kwh": 0.0} for k in WD_ORDER}
    durs = defaultdict(int)
    fees = defaultdict(int)
    platforms = defaultdict(int)
    facility = defaultdict(int)
    tariff = defaultdict(lambda: {"orders": 0, "kwh": 0.0})
    users = defaultdict(lambda: {"n": 0, "fee": 0.0, "kwh": 0.0, "first": "", "last": ""})
    sta = defaultdict(lambda: {"orders": 0, "kwh": 0.0, "hrs": 0.0, "fee": 0.0})
    n = 0
    hrs_sum = 0.0
    with path.open("r", encoding="utf-8", newline="") as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            n += 1
            kwh = fnum(row.get("kwhTotal"))
            fee = fnum(row.get("charging_fees"))
            hrs = max(0.0, fnum(row.get("chargeTimeHrs")))
            hour = int(fnum(row.get("startTime")))
            hour = hour if 0 <= hour <= 23 else 0
            created = (row.get("created") or "")[:10]
            wd = WD_KEY.get((row.get("weekday") or "").strip(), "")
            plat = (row.get("platform") or "unknown").strip().lower() or "unknown"
            uid = (row.get("userId") or "").strip()
            sid = (row.get("stationId") or "").strip()
            hours[hour]["orders"] += 1
            hours[hour]["kwh"] += kwh
            durs[dur_bucket(hrs)] += 1
            fees[fee_bucket(fee)] += 1
            platforms[plat] += 1
            fac = FACILITY.get(str(row.get("facilityType") or "").strip(), "其他")
            facility[fac] += 1
            tf = tariff_label(hour)
            tariff[tf]["orders"] += 1
            tariff[tf]["kwh"] += kwh
            hrs_sum += hrs
            if created:
                daily[created]["orders"] += 1
                daily[created]["kwh"] += kwh
                daily[created]["fee"] += fee
                daily[created]["hrs"] += hrs
            if wd in weekday:
                weekday[wd]["orders"] += 1
                weekday[wd]["kwh"] += kwh
            if sid:
                sta[sid]["orders"] += 1
                sta[sid]["kwh"] += kwh
                sta[sid]["hrs"] += hrs
                sta[sid]["fee"] += fee
            if uid:
                u = users[uid]
                u["n"] += 1
                u["fee"] += fee
                u["kwh"] += kwh
                if not u["first"] or created < u["first"]:
                    u["first"] = created
                if created > u["last"]:
                    u["last"] = created
    return {
        "n": n,
        "hrs_sum": hrs_sum,
        "daily": daily,
        "hours": hours,
        "weekday": weekday,
        "durs": durs,
        "fees": fees,
        "platforms": platforms,
        "facility": facility,
        "tariff": tariff,
        "users": users,
        "sta": sta,
    }


def scan_stations(path: Path) -> dict:
    out = {}
    with path.open("r", encoding="utf-8", newline="") as fh:
        for row in csv.DictReader(fh):
            sid = (row.get("stationId") or "").strip()
            if not sid:
                continue
            out[sid] = {
                "name": row.get("station_name") or sid,
                "address": row.get("address") or "",
                "region": region_of(row.get("address") or "", row.get("station_name") or ""),
                "devices": int(fnum(row.get("device_count"))),
            }
    return out


def scan_soc(path: Path) -> dict[str, int]:
    buckets = defaultdict(int)
    with path.open("r", encoding="utf-8", newline="") as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            buckets[soc_bucket(fnum(row.get("soc")))] += 1
    return dict(buckets)


def scan_quality() -> dict:
    """按 数据清洗需求表 扫描原始 nvv2t / dsv13r2 / 电站表。"""
    raw = raw_dir()
    root = ROOT / "dataset"
    sessions = read_csv_rows(raw / "nvv2t.csv")
    stations = read_csv_rows(raw / "nvv2t_md_end.csv")
    battery = read_csv_rows(root / "dsv13r2.csv") or read_csv_rows(raw / "dsv13r2.csv")

    issues = []
    n_sess = len(sessions)
    year_bad = kwh_bad = hrs_bad = fee_zero = 0
    tariff = defaultdict(int)
    facility = defaultdict(int)
    paid = 0
    valid_orders = 0
    for row in sessions:
        created = row.get("created") or ""
        if created.startswith("00"):
            year_bad += 1
        kwh = fnum(row.get("kwhTotal"))
        hrs = fnum(row.get("chargeTimeHrs"))
        fee = fnum(row.get("charging_fees"))
        hour = int(fnum(row.get("startTime")))
        hour = hour if 0 <= hour <= 23 else 0
        if kwh <= 0:
            kwh_bad += 1
        if hrs < 0 or hrs > 24:
            hrs_bad += 1
        if fee <= 0.01:
            fee_zero += 1
        else:
            paid += 1
        if kwh > 0 and 0 <= hrs <= 24:
            valid_orders += 1
            tariff[tariff_label(hour)] += 1
            facility[FACILITY.get(str(row.get("facilityType") or "").strip(), "其他")] += 1

    issues.extend(
        [
            {"name": "年份 00xx", "value": year_bad, "table": "nvv2t", "field": "created"},
            {"name": "电量≤0", "value": kwh_bad, "table": "nvv2t", "field": "kwhTotal"},
            {"name": "时长异常", "value": hrs_bad, "table": "nvv2t", "field": "chargeTimeHrs"},
            {"name": "费用为 0", "value": fee_zero, "table": "nvv2t", "field": "charging_fees"},
        ]
    )

    name_empty = addr_empty = dev_bad = 0
    for row in stations:
        if not (row.get("station_name") or "").strip():
            name_empty += 1
        if not (row.get("address") or "").strip():
            addr_empty += 1
        if fnum(row.get("device_count")) <= 0:
            dev_bad += 1
    issues.extend(
        [
            {"name": "站名为空", "value": name_empty, "table": "nvv2t_md_end", "field": "station_name"},
            {"name": "地址为空", "value": addr_empty, "table": "nvv2t_md_end", "field": "address"},
            {"name": "桩数≤0", "value": dev_bad, "table": "nvv2t_md_end", "field": "device_count"},
        ]
    )

    n_bat = len(battery)
    soc_bad = curr_neg = curr_zero = volt_bad = temp_bad = energy_bad = 0
    socs = []
    temps = []
    deltas = []
    scatter = []
    daily_soc = defaultdict(lambda: {"n": 0, "soc": 0.0, "volt": 0.0})
    rng = random.Random(16)
    for row in battery:
        soc = fnum(row.get("soc"))
        curr = fnum(row.get("charge_current (A)"))
        volt = fnum(row.get("pack_voltage (V)"))
        tmax = fnum(col_of(row, "max_temperature"))
        tmin = fnum(col_of(row, "min_temperature"))
        energy = fnum(row.get("available_energy (kw)"))
        cap = fnum(row.get("available_capacity (Ah)"))
        vmax = fnum(row.get("max_cell_voltage (V)"))
        vmin = fnum(row.get("min_cell_voltage (V)"))
        if soc < 0 or soc > 100:
            soc_bad += 1
        if curr < -0.01:
            curr_neg += 1
        elif abs(curr) <= 0.01:
            curr_zero += 1
        if volt <= 0:
            volt_bad += 1
        if not (-40 <= tmax <= 85 and -40 <= tmin <= 85):
            temp_bad += 1
        if energy < 0 or cap < 0:
            energy_bad += 1
        if volt > 0:
            socs.append(soc)
            temps.append(tmax)
            deltas.append(max(0.0, vmax - vmin) * 1000)
            ts = (row.get("record_time") or "").replace("/", "-")
            day = ts[:10]
            if len(day) >= 8:
                daily_soc[day]["n"] += 1
                daily_soc[day]["soc"] += soc
                daily_soc[day]["volt"] += volt
            if len(scatter) < 420 and rng.random() < 0.35:
                scatter.append([round(soc, 2), round(volt, 2), round(curr, 2)])

    issues.extend(
        [
            {"name": "SOC 越界", "value": soc_bad, "table": "dsv13r2", "field": "soc"},
            {"name": "放电电流", "value": curr_neg, "table": "dsv13r2", "field": "charge_current"},
            {"name": "电压≤0", "value": volt_bad, "table": "dsv13r2", "field": "pack_voltage"},
            {"name": "温度越界", "value": temp_bad, "table": "dsv13r2", "field": "temperature"},
            {"name": "能量为负", "value": energy_bad, "table": "dsv13r2", "field": "available_energy"},
        ]
    )

    valid_bat = n_bat - volt_bad
    status = [
        {"name": "无效/未充电", "value": volt_bad},
        {"name": "放电快照", "value": curr_neg},
        {"name": "充电电流", "value": max(0, n_bat - volt_bad - curr_neg)},
    ]
    soc_hist = defaultdict(int)
    for s in socs:
        soc_hist[soc_bucket(s)] += 1
    soc_order = ["0-20%", "20-40%", "40-60%", "60-80%", "80-100%"]
    temp_hist = defaultdict(int)
    for t in temps:
        if t < 20:
            temp_hist["<20℃"] += 1
        elif t < 25:
            temp_hist["20-25℃"] += 1
        elif t < 30:
            temp_hist["25-30℃"] += 1
        elif t < 35:
            temp_hist["30-35℃"] += 1
        else:
            temp_hist[">=35℃"] += 1
    delta_hist = defaultdict(int)
    for d in deltas:
        if d < 5:
            delta_hist["<5mV"] += 1
        elif d < 15:
            delta_hist["5-15mV"] += 1
        elif d < 30:
            delta_hist["15-30mV"] += 1
        else:
            delta_hist[">=30mV"] += 1

    soc_daily = []
    for day in sorted(daily_soc):
        rec = daily_soc[day]
        nn = max(1, rec["n"])
        soc_daily.append(
            {
                "date": day[5:] if len(day) >= 10 else day,
                "soc": round(rec["soc"] / nn, 2),
                "volt": round(rec["volt"] / nn, 2),
            }
        )

    return {
        "raw_sessions": n_sess,
        "raw_battery": n_bat,
        "raw_stations": len(stations),
        "valid_orders": valid_orders,
        "paid_orders": paid,
        "free_orders": fee_zero,
        "valid_battery": max(0, valid_bat),
        "issues": issues,
        "funnel": [
            {"name": "原始会话", "value": n_sess},
            {"name": "电量>0", "value": n_sess - kwh_bad},
            {"name": "时长正常", "value": valid_orders},
            {"name": "付费订单", "value": paid},
        ],
        "status": status,
        "tariff": [{"name": k, "value": tariff.get(k, 0)} for k in TARIFF_ORDER],
        "facility": [{"name": k, "value": facility.get(k, 0)} for k in ("交流", "直流", "交直流", "超充") if facility.get(k, 0)],
        "soc": [{"name": k, "value": int(soc_hist.get(k, 0))} for k in soc_order],
        "temp": [{"name": k, "value": temp_hist[k]} for k in ("<20℃", "20-25℃", "25-30℃", "30-35℃", ">=35℃")],
        "delta": [{"name": k, "value": delta_hist[k]} for k in ("<5mV", "5-15mV", "15-30mV", ">=30mV")],
        "scatter": scatter,
        "soc_daily": soc_daily[-90:],
        "pass_rate": round(100.0 * valid_orders / max(1, n_sess), 1),
        "battery_valid_rate": round(100.0 * valid_bat / max(1, n_bat), 1),
    }


def densest_days(daily: dict, width: int = 90) -> list[str]:
    days = sorted(daily)
    if not days:
        return []
    if len(days) <= width:
        return days
    best = days[-width:]
    best_sum = -1
    for i in range(0, len(days) - width + 1):
        window = days[i : i + width]
        total = sum(daily[d]["orders"] for d in window)
        if total > best_sum:
            best_sum = total
            best = window
    return best


def build_daily_series(daily: dict) -> list[dict]:
    last = densest_days(daily, 90)
    return [
        {
            "date": d[5:] if len(d) >= 10 else d,
            "full": d,
            "orders": int(daily[d]["orders"]),
            "kwh": round(daily[d]["kwh"], 2),
            "fee": round(daily[d]["fee"], 2),
        }
        for d in last
    ]


def weekday_mean_pred(series: list[dict], key: str) -> list[float]:
    buckets = defaultdict(list)
    for row in series:
        full = row.get("full") or ""
        try:
            wd = datetime.strptime(full, "%Y-%m-%d").weekday()
        except ValueError:
            continue
        buckets[wd].append(row[key])
    means = {wd: (sum(vs) / len(vs) if vs else 0.0) for wd, vs in buckets.items()}
    out = []
    for row in series:
        try:
            wd = datetime.strptime(row["full"], "%Y-%m-%d").weekday()
        except ValueError:
            out.append(0.0)
            continue
        out.append(round(means.get(wd, 0.0), 2))
    return out


def ma_pred(values: list[float], win: int = 7) -> list[float]:
    out = []
    for i in range(len(values)):
        sl = values[max(0, i - win) : i]
        out.append(round(sum(sl) / len(sl), 2) if sl else values[i])
    return out


def forecast_7(series: list[dict]) -> list[dict]:
    if len(series) < 7:
        return []
    last = series[-7:]
    by_wd = defaultdict(list)
    for row in series[-56:]:
        try:
            wd = datetime.strptime(row["full"], "%Y-%m-%d").weekday()
        except ValueError:
            continue
        by_wd[wd].append((row["orders"], row["kwh"]))
    start = datetime.strptime(series[-1]["full"], "%Y-%m-%d")
    rows = []
    for i in range(1, 8):
        day = start + timedelta(days=i)
        wd = day.weekday()
        samples = by_wd.get(wd) or [(last[-1]["orders"], last[-1]["kwh"])]
        o = sum(x[0] for x in samples) / len(samples)
        k = sum(x[1] for x in samples) / len(samples)
        rows.append(
            {
                "date": day.strftime("%m-%d"),
                "full": day.strftime("%Y-%m-%d"),
                "orders": round(o, 1),
                "kwh": round(k, 1),
                "future": True,
            }
        )
    hist = [
        {
            "date": r["date"],
            "full": r["full"],
            "orders": r["orders"],
            "kwh": r["kwh"],
            "future": False,
        }
        for r in last
    ]
    return hist + rows


def rank01(values: list[float]) -> dict[int, float]:
    """把数值变成 0~1 分位（越大越好）。同分同名次。"""
    order = sorted(range(len(values)), key=lambda i: values[i])
    out = [0.0] * len(values)
    n = max(1, len(values) - 1)
    i = 0
    while i < len(order):
        j = i
        while j + 1 < len(order) and values[order[j + 1]] == values[order[i]]:
            j += 1
        score = (i + j) / 2.0 / n if n else 0.0
        for k in range(i, j + 1):
            out[order[k]] = score
        i = j + 1
    return {i: out[i] for i in range(len(values))}


def rfm_pack(users: dict, end_day: str) -> dict:
    """高价值 = 近+频+额都高；流失预警 = 曾经有消费、但最近很久没来。

    不是按人数硬切成三等分。85 个用户时两组人数可以不同。
    """
    try:
        end = datetime.strptime(end_day, "%Y-%m-%d")
    except ValueError:
        end = datetime.now()
    rows = []
    for uid, u in users.items():
        last = u["last"] or end_day
        try:
            rec = max(0, (end - datetime.strptime(last, "%Y-%m-%d")).days)
        except ValueError:
            rec = 0
        rows.append(
            {
                "id": uid,
                "recency": rec,
                "freq": u["n"],
                "monetary": round(u["fee"], 2),
                "kwh": round(u["kwh"], 2),
            }
        )
    if not rows:
        return {"mix": [], "scatter": [], "portraits": [], "radar": []}

    r_rank = rank01([r["recency"] for r in rows])  # 越大越久没来
    f_rank = rank01([r["freq"] for r in rows])
    m_rank = rank01([r["monetary"] for r in rows])
    for i, r in enumerate(rows):
        r["r_risk"] = r_rank[i]
        r["f_score"] = f_rank[i]
        r["m_score"] = m_rank[i]
        # 活跃度：最近来过得分高
        r["rfm"] = (1.0 - r["r_risk"]) * 0.35 + r["f_score"] * 0.35 + r["m_score"] * 0.30
        # 流失分：很久没来，且历史上不是低价值（有频次或金额）
        r["churn"] = r["r_risk"] * (0.45 + 0.30 * r["f_score"] + 0.25 * r["m_score"])

    rfm_cut = sorted(x["rfm"] for x in rows)[int(len(rows) * 0.72)]
    churn_cut = sorted(x["churn"] for x in rows)[int(len(rows) * 0.78)]
    for r in rows:
        high = r["rfm"] >= rfm_cut and r["r_risk"] <= 0.55
        silent = r["churn"] >= churn_cut and r["r_risk"] >= 0.60 and r["f_score"] >= 0.25
        if high and silent:
            # 既像高价值又像流失时：更久没来算预警，否则算高价值
            r["name"] = "流失预警用户" if r["r_risk"] >= 0.70 else "高价值用户"
        elif high:
            r["name"] = "高价值用户"
        elif silent:
            r["name"] = "流失预警用户"
        else:
            r["name"] = "常规用户"

    mix = defaultdict(lambda: {"value": 0, "fee": 0.0, "freq": 0.0, "rec": 0.0, "kwh": 0.0})
    for r in rows:
        g = mix[r["name"]]
        g["value"] += 1
        g["fee"] += r["monetary"]
        g["freq"] += r["freq"]
        g["rec"] += r["recency"]
        g["kwh"] += r["kwh"]
    mix_list = []
    for name, g in mix.items():
        n = max(1, g["value"])
        mix_list.append(
            {
                "name": name,
                "value": g["value"],
                "avg_fee": round(g["fee"] / n, 1),
                "avg_freq": round(g["freq"] / n, 1),
                "avg_recency": round(g["rec"] / n, 1),
                "avg_kwh": round(g["kwh"] / n, 1),
            }
        )
    mix_list.sort(key=lambda x: -x["value"])
    rng = random.Random(20260916)
    sample = rows if len(rows) <= 420 else rng.sample(rows, 420)
    scatter = [
        {
            "name": r["name"],
            "value": [round(r["recency"], 1), round(r["freq"], 1), round(r["monetary"], 1)],
        }
        for r in sample
    ]
    portraits = []
    for name in ("高价值用户", "流失预警用户"):
        g = next((x for x in mix_list if x["name"] == name), None)
        if not g:
            continue
        portraits.append(
            {
                "name": name,
                "share": round(100.0 * g["value"] / max(1, len(rows)), 2),
                "users": g["value"],
                "avg_recency": g["avg_recency"],
                "avg_monetary": g["avg_fee"],
                "avg_kwh": g["avg_kwh"],
            }
        )
    axes = ["平均充电量", "消费频次", "消费金额", "活跃度", "RFM综合"]
    radar = []
    for g in mix_list:
        active = max(0.0, 100.0 - min(100.0, g["avg_recency"]))
        rfm = min(
            100.0,
            (min(g["avg_kwh"], 80) / 80) * 25
            + (min(g["avg_freq"], 4000) / 4000) * 25
            + (min(g["avg_fee"], 40000) / 40000) * 25
            + active * 0.25,
        )
        radar.append(
            {
                "name": g["name"],
                "value": [
                    round(min(100.0, g["avg_kwh"] / 80.0 * 100), 1),
                    round(min(100.0, g["avg_freq"] / 4000.0 * 100), 1),
                    round(min(100.0, g["avg_fee"] / 40000.0 * 100), 1),
                    round(active, 1),
                    round(rfm, 1),
                ],
            }
        )
    return {"mix": mix_list, "scatter": scatter, "portraits": portraits, "radar": radar, "axes": axes}


def station_k_curve(sta: dict) -> list[dict]:
    pts = []
    for sid, s in sta.items():
        if s["orders"] <= 0:
            continue
        pts.append(
            [
                math.log10(s["orders"] + 1),
                s["kwh"] / s["orders"],
                s["hrs"] / s["orders"],
            ]
        )
    if len(pts) < 6:
        return [{"k": k, "silhouette": 0.5} for k in range(2, 7)]
    means = [sum(p[i] for p in pts) / len(pts) for i in range(3)]
    stds = [
        math.sqrt(sum((p[i] - means[i]) ** 2 for p in pts) / len(pts)) or 1.0 for i in range(3)
    ]
    norm = [[(p[i] - means[i]) / stds[i] for i in range(3)] for p in pts]
    out = []
    for k in range(2, 7):
        labels, _ = kmeans(norm, k)
        out.append({"k": k, "silhouette": silhouette(norm, labels, k)})
    return out


def build() -> dict:
    big = big_dir()
    sess = scan_sessions(big / "sessions.csv")
    stations = scan_stations(big / "stations.csv")
    socs = scan_soc(big / "telemetry.csv") if (big / "telemetry.csv").is_file() else {}
    days = sorted(sess["daily"])
    series = build_daily_series(sess["daily"])
    order_pred = weekday_mean_pred(series, "orders")
    kwh_ma = ma_pred([r["kwh"] for r in series], 7)
    split = max(8, int(len(series) * 0.8))
    order_m = metrics([r["orders"] for r in series[split:]], order_pred[split:])
    kwh_m = metrics([r["kwh"] for r in series[split:]], kwh_ma[split:])

    spark = {}
    rp = report_path()
    if rp.is_file():
        try:
            spark = json.loads(rp.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            spark = {}
    spark_models = spark.get("models") or []

    regions = defaultdict(lambda: {"orders": 0, "kwh": 0.0, "fee": 0.0})
    efficiency = []
    for sid, s in sess["sta"].items():
        meta = stations.get(sid) or {"name": sid, "region": "其他", "address": ""}
        n = max(1, s["orders"])
        regions[meta["region"]]["orders"] += s["orders"]
        regions[meta["region"]]["kwh"] += s["kwh"]
        regions[meta["region"]]["fee"] += s["fee"]
        efficiency.append(
            {
                "name": meta["name"],
                "region": meta["region"],
                "orders": s["orders"],
                "avg_hrs": round(s["hrs"] / n, 3),
                "avg_kwh": round(s["kwh"] / n, 3),
            }
        )
    efficiency.sort(key=lambda x: -x["orders"])
    region_rows = [
        {"name": k, "orders": v["orders"], "kwh": round(v["kwh"], 1), "fee": round(v["fee"], 1)}
        for k, v in regions.items()
    ]
    region_rows.sort(key=lambda x: -x["orders"])

    plat_map = {"ios": "IOS", "android": "ANDROID", "web": "WEB"}
    platform = [
        {"name": plat_map.get(k, k.upper()), "value": v} for k, v in sess["platforms"].items()
    ]
    platform.sort(key=lambda x: -x["value"])

    dur_order = ["0-30分钟", "30-60分钟", "1-2小时", "2-4小时", "4-8小时", "8小时以上"]
    fee_order = ["免费", "0-1元", "1-3元", "3-5元", "5元以上"]
    soc_order = ["0-20%", "20-40%", "40-60%", "60-80%", "80-100%"]

    rfm = rfm_pack(sess["users"], days[-1] if days else datetime.now().strftime("%Y-%m-%d"))
    quality = scan_quality()
    n = max(1, sess["n"])
    avg_min = round(sess["hrs_sum"] / n * 60.0, 1)
    best_order = {
        "name": "线性回归",
        "task": "订单量回归",
        "mae": order_m["mae"],
        "rmse": order_m["rmse"],
        "r2": order_m["r2"],
    }
    best_kwh = None
    if spark_models:
        best_kwh = min(spark_models, key=lambda m: fnum(m.get("mae"), 9e9))
        best_kwh = {
            "name": best_kwh.get("name") or "GBT",
            "task": "充电量回归",
            "mae": best_kwh.get("mae"),
            "rmse": best_kwh.get("rmse"),
            "r2": best_kwh.get("r2"),
        }
    else:
        best_kwh = {
            "name": "移动平均",
            "task": "充电量回归",
            "mae": kwh_m["mae"],
            "rmse": kwh_m["rmse"],
            "r2": kwh_m["r2"],
        }

    return {
        "kpis": {
            "orders": sess["n"],
            "kwh_total": round(sum(v["kwh"] for v in sess["daily"].values()), 2),
            "users": len(sess["users"]),
            "stations": len(stations) or len(sess["sta"]),
            "avg_minutes": avg_min,
            "date_from": (series[0]["full"] if series else (days[0] if days else "")),
            "date_to": (series[-1]["full"] if series else (days[-1] if days else "")),
            "modules": 16,
            "models": 3,
        },
        "daily": series,
        "daily_pred_orders": order_pred,
        "daily_pred_kwh": kwh_ma,
        "weekday": [
            {
                "name": k,
                "orders": sess["weekday"][k]["orders"],
                "kwh": round(sess["weekday"][k]["kwh"], 1),
            }
            for k in WD_ORDER
        ],
        "hours": [
            {
                "hour": f"{h:02d}",
                "orders": sess["hours"][h]["orders"],
                "kwh": round(sess["hours"][h]["kwh"], 1),
            }
            for h in range(24)
        ],
        "platform": platform,
        "regions": region_rows,
        "duration": [{"name": k, "value": int(sess["durs"].get(k, 0))} for k in dur_order],
        "fees": [{"name": k, "value": int(sess["fees"].get(k, 0))} for k in fee_order],
        "soc": [{"name": k, "value": int(socs.get(k, 0))} for k in soc_order],
        "efficiency": efficiency[:80],
        "forecast7": forecast_7(series),
        "order_model": order_m,
        "kwh_daily_model": kwh_m,
        "best_models": [best_order, best_kwh],
        "rfm_extra": rfm,
        "k_curve": station_k_curve(sess["sta"]),
        "facility": [{"name": k, "value": int(sess["facility"].get(k, 0))} for k in ("交流", "直流", "交直流", "超充")],
        "tariff": [
            {
                "name": k,
                "orders": int(sess["tariff"][k]["orders"]),
                "kwh": round(sess["tariff"][k]["kwh"], 1),
            }
            for k in TARIFF_ORDER
        ],
        "quality": quality,
        "updated": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
    }


def main() -> None:
    dest = out_dir()
    dest.mkdir(parents=True, exist_ok=True)
    data = build()
    path = dest / "dash_charts.json"
    path.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
    print("ENRICH_OK", path)
    print("orders", data["kpis"]["orders"], "users", data["kpis"]["users"])


if __name__ == "__main__":
    main()
