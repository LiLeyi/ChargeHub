# -*- coding: utf-8 -*-
"""Build the versioned read-only snapshot consumed by the Web dashboard."""
from __future__ import annotations

import sqlite3
from collections.abc import Callable
from datetime import datetime, timedelta
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path
from urllib.parse import quote
from zoneinfo import ZoneInfo


timeZone = ZoneInfo("Asia/Shanghai")
supportedHorizons = (1, 6, 24)
validOrderCondition = (
    "status='已完成' AND energy_kwh >= 0 AND amount >= 0 "
    "AND datetime(start_time) IS NOT NULL "
    "AND datetime(end_time) >= datetime(start_time)"
)


class DashboardDataError(RuntimeError):
    """A safe, categorized failure while reading the dashboard database."""

    def __init__(self, category: str) -> None:
        super().__init__(category)
        self.category = category


class DashboardRepository:
    """Load one normalized snapshot through one read-only SQLite connection."""

    def __init__(
        self,
        databasePath: Path,
        nowProvider: Callable[[], datetime] | None = None,
    ) -> None:
        self.databasePath = databasePath
        self.nowProvider = nowProvider or (lambda: datetime.now(timeZone))

    def loadSnapshot(self) -> dict:
        now = self.nowProvider()
        if now.tzinfo is None:
            now = now.replace(tzinfo=timeZone)
        else:
            now = now.astimezone(timeZone)

        connection = None
        try:
            connection = self.openReadonlyConnection()
            connection.execute("BEGIN")
            pileStatus = self.loadPileStatus(connection)
            try:
                loadForecast = self.loadForecast(connection, now)
            except (sqlite3.Error, TypeError, ValueError, OverflowError):
                loadForecast = self.unavailableForecast()
            snapshot = {
                "schemaVersion": "1.0",
                "generatedAt": now.isoformat(timespec="seconds"),
                "timeZone": "Asia/Shanghai",
                "source": "live",
                "metrics": self.loadMetrics(connection, pileStatus),
                "revenueTrend": self.loadRevenueTrend(connection, now),
                "pileStatus": pileStatus,
                "hourlyCharge": self.loadHourlyCharge(connection, now),
                "stationRanking": self.loadStationRanking(connection, now),
                "loadForecast": loadForecast,
            }
            connection.rollback()
            return snapshot
        except (OSError, sqlite3.Error, TypeError, ValueError) as error:
            raise DashboardDataError(type(error).__name__) from error
        finally:
            if connection is not None:
                connection.close()

    def openReadonlyConnection(self) -> sqlite3.Connection:
        path = self.databasePath.resolve()
        uri = f"file:{quote(str(path))}?mode=ro"
        connection = sqlite3.connect(uri, uri=True, timeout=5)
        connection.row_factory = sqlite3.Row
        connection.execute("PRAGMA query_only = ON")
        connection.execute("PRAGMA busy_timeout = 5000")
        return connection

    @staticmethod
    def loadMetrics(connection: sqlite3.Connection, pileStatus: dict) -> dict:
        row = connection.execute(
            "SELECT COALESCE(SUM(energy_kwh), 0) AS charge_kwh, "
            "COALESCE(SUM(amount), 0) AS revenue_yuan "
            f"FROM charge_order WHERE {validOrderCondition}"
        ).fetchone()
        return {
            "totalChargeKwh": round(float(row["charge_kwh"]), 3),
            "totalRevenueCents": DashboardRepository.toCents(row["revenue_yuan"]),
            "activePileCount": pileStatus["idle"] + pileStatus["inUse"],
        }

    @staticmethod
    def loadPileStatus(connection: sqlite3.Connection) -> dict:
        result = {"idle": 0, "inUse": 0, "fault": 0}
        names = {"闲置": "idle", "在用": "inUse", "故障": "fault"}
        rows = connection.execute(
            "SELECT status, COUNT(*) AS count FROM pile "
            "WHERE status IN ('闲置', '在用', '故障') GROUP BY status"
        ).fetchall()
        for row in rows:
            result[names[row["status"]]] = int(row["count"])
        return result

    @staticmethod
    def loadRevenueTrend(connection: sqlite3.Connection, now: datetime) -> dict:
        firstDay = (now - timedelta(days=29)).date()
        lastDay = now.date()
        rows = connection.execute(
            "SELECT date(start_time) AS day, COALESCE(SUM(amount), 0) AS revenue_yuan "
            f"FROM charge_order WHERE {validOrderCondition} "
            "AND date(start_time) BETWEEN ? AND ? GROUP BY date(start_time)",
            (firstDay.isoformat(), lastDay.isoformat()),
        ).fetchall()
        byDay = {row["day"]: DashboardRepository.toCents(row["revenue_yuan"]) for row in rows}
        points = []
        for offset in range(30):
            day = firstDay + timedelta(days=offset)
            text = day.isoformat()
            points.append({"date": text, "revenueCents": byDay.get(text, 0)})
        return {"rangeDays": 30, "points": points}

    @staticmethod
    def loadHourlyCharge(connection: sqlite3.Connection, now: datetime) -> list[dict]:
        rows = connection.execute(
            "SELECT CAST(strftime('%H', start_time) AS INTEGER) AS hour, "
            "COALESCE(SUM(energy_kwh), 0) AS charge_kwh "
            f"FROM charge_order WHERE {validOrderCondition} "
            "AND date(start_time)=? GROUP BY strftime('%H', start_time)",
            (now.date().isoformat(),),
        ).fetchall()
        byHour = {int(row["hour"]): round(float(row["charge_kwh"]), 3) for row in rows}
        return [{"hour": hour, "chargeKwh": byHour.get(hour, 0.0)} for hour in range(24)]

    @staticmethod
    def loadStationRanking(connection: sqlite3.Connection, now: datetime) -> list[dict]:
        utilizationStart = now - timedelta(days=30)
        windowStart = utilizationStart.strftime("%Y-%m-%d %H:%M:%S")
        windowEnd = now.strftime("%Y-%m-%d %H:%M:%S")
        rows = connection.execute(
            f"WITH valid_order AS (SELECT * FROM charge_order WHERE {validOrderCondition}) "
            "SELECT s.id AS station_id, s.name AS station_name, "
            "COALESCE(SUM(o.amount), 0) AS revenue_yuan, "
            "COUNT(DISTINCT o.id) AS order_count, "
            "COUNT(DISTINCT p.id) AS pile_count, "
            "COALESCE(SUM(CASE WHEN datetime(o.start_time) < datetime(?) "
            "AND datetime(o.end_time) > datetime(?) "
            "THEN (julianday(MIN(datetime(o.end_time), datetime(?))) "
            "- julianday(MAX(datetime(o.start_time), datetime(?))))*1440 ELSE 0 END), 0) "
            "AS used_minutes FROM station s LEFT JOIN pile p ON p.station_id=s.id "
            "LEFT JOIN valid_order o ON o.pile_id=p.id GROUP BY s.id, s.name",
            (windowEnd, windowStart, windowEnd, windowStart),
        ).fetchall()
        result = []
        for row in rows:
            availableMinutes = int(row["pile_count"]) * 30 * 24 * 60
            utilization = 0.0
            if availableMinutes > 0:
                utilization = min(100.0, max(0.0, float(row["used_minutes"]) * 100 / availableMinutes))
            result.append(
                {
                    "stationId": int(row["station_id"]),
                    "stationName": str(row["station_name"]),
                    "revenueCents": DashboardRepository.toCents(row["revenue_yuan"]),
                    "orderCount": int(row["order_count"]),
                    "utilizationPercent": round(utilization, 1),
                }
            )
        result.sort(key=lambda item: (-item["revenueCents"], item["stationId"]))
        return result

    @staticmethod
    def loadForecast(connection: sqlite3.Connection, now: datetime) -> dict:
        if not DashboardRepository.tableExists(connection, "load_forecast"):
            return DashboardRepository.unavailableForecast()
        rows = connection.execute(
            "SELECT horizon_hours, pred_kwh, pred_idle, peak_hour FROM load_forecast "
            "WHERE horizon_hours IN (1, 6, 24) AND pred_kwh >= 0 AND pred_idle >= 0"
        ).fetchall()
        grouped = {horizon: [] for horizon in supportedHorizons}
        for row in rows:
            grouped[int(row["horizon_hours"])].append(row)
        if any(not grouped[horizon] for horizon in supportedHorizons):
            return DashboardRepository.unavailableForecast()

        modelVersion = "baseline-1"
        if DashboardRepository.tableExists(connection, "analysis_report"):
            report = connection.execute(
                "SELECT model_version FROM analysis_report ORDER BY id DESC LIMIT 1"
            ).fetchone()
            if report and report["model_version"]:
                modelVersion = str(report["model_version"])

        series = []
        for horizon in supportedHorizons:
            target = now + timedelta(hours=horizon)
            values = grouped[horizon]
            # The existing model stores energy for the whole horizon. Dividing
            # kWh by hours yields the average kW required by the Web contract.
            energyKwh = sum(float(row["pred_kwh"]) for row in values)
            peakText = target.strftime("%H:00")
            series.append(
                {
                    "horizonHours": horizon,
                    "points": [
                        {
                            "time": target.isoformat(timespec="seconds"),
                            "loadKw": round(energyKwh / horizon, 3),
                            "availablePileCount": sum(int(row["pred_idle"]) for row in values),
                            "isPeak": any(row["peak_hour"] == peakText for row in values),
                        }
                    ],
                }
            )
        return {"status": "available", "modelVersion": modelVersion, "series": series}

    @staticmethod
    def unavailableForecast() -> dict:
        return {"status": "unavailable", "modelVersion": None, "series": []}

    @staticmethod
    def tableExists(connection: sqlite3.Connection, table: str) -> bool:
        row = connection.execute(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", (table,)
        ).fetchone()
        return row is not None

    @staticmethod
    def toCents(yuan: object) -> int:
        value = Decimal(str(yuan or 0)) * 100
        return int(value.quantize(Decimal("1"), rounding=ROUND_HALF_UP))
