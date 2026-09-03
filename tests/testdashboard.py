# -*- coding: utf-8 -*-
"""Dashboard HTTP tests backed by an isolated temporary SQLite database."""
from __future__ import annotations

import json
import sqlite3
import sys
import unittest
from datetime import datetime, timedelta
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch
from zoneinfo import ZoneInfo


projectRoot = Path(__file__).resolve().parents[1]
if str(projectRoot) not in sys.path:
    sys.path.insert(0, str(projectRoot))

from dashboard import app as dashboardapp
from dashboard.dashboardrepository import DashboardRepository

from testsupport import TemporaryDatabase, rowCount


class DashboardFixtureTest(unittest.TestCase):
    def setUp(self) -> None:
        self.originalDatabasePath = dashboardapp.DB
        self.originalRepositoryFactory = dashboardapp.dashboardRepositoryFactory

    def tearDown(self) -> None:
        dashboardapp.DB = self.originalDatabasePath
        dashboardapp.dashboardRepositoryFactory = self.originalRepositoryFactory

    def useFixedTime(self, fixedNow: datetime) -> None:
        dashboardapp.dashboardRepositoryFactory = lambda databasePath: DashboardRepository(
            databasePath, nowProvider=lambda: fixedNow
        )

    def testOverviewReadsTheTemporaryDatabaseWithoutMutatingIt(self) -> None:
        with TemporaryDatabase(withHistory=False) as databasePath:
            dashboardapp.DB = databasePath
            before = rowCount(databasePath, "charge_order")

            response = dashboardapp.app.test_client().get("/api/overview")

            self.assertEqual(response.status_code, 200)
            overview = response.get_json()
            self.assertEqual(overview["users"], 4)
            self.assertNotIn("db", overview)
            self.assertNotIn(str(databasePath), response.get_data(as_text=True))
            self.assertEqual(rowCount(databasePath, "charge_order"), before)

    def testLegacyDashboardQueriesCannotWriteTheDatabase(self) -> None:
        with TemporaryDatabase(withHistory=False) as databasePath:
            dashboardapp.DB = databasePath
            before = rowCount(databasePath, "user")

            result = dashboardapp.q("DELETE FROM user")

            self.assertEqual(result, [])
            self.assertEqual(rowCount(databasePath, "user"), before)

    def testDashboardReturnsOneVersionedNormalizedSnapshot(self) -> None:
        with TemporaryDatabase(withHistory=True) as databasePath:
            dashboardapp.DB = databasePath

            response = dashboardapp.app.test_client().get("/api/dashboard")

            self.assertEqual(response.status_code, 200)
            snapshot = response.get_json()
            self.assertEqual(snapshot["schemaVersion"], "1.0")
            self.assertEqual(snapshot["timeZone"], "Asia/Shanghai")
            self.assertEqual(snapshot["source"], "live")
            self.assertEqual(len(snapshot["revenueTrend"]["points"]), 30)
            self.assertEqual(len(snapshot["hourlyCharge"]), 24)
            self.assertIsInstance(snapshot["metrics"]["totalRevenueCents"], int)
            self.assertEqual(response.headers["Cache-Control"], "no-store")
            serialized = json.dumps(snapshot, ensure_ascii=False).lower()
            for forbidden in [str(databasePath).lower(), "password", "token", "phone"]:
                self.assertNotIn(forbidden, serialized)

    def testDashboardNormalizesDatesHoursAndRejectsInvalidOrders(self) -> None:
        with TemporaryDatabase(withHistory=False) as databasePath:
            now = datetime(2026, 9, 2, 12, 0, tzinfo=ZoneInfo("Asia/Shanghai"))
            self.useFixedTime(now)
            todayAtOne = now.replace(hour=1, minute=15, second=0, microsecond=0)
            firstDay = todayAtOne - timedelta(days=29)
            with sqlite3.connect(str(databasePath)) as connection:
                orders = [
                    ("VALID-TODAY", todayAtOne, 4.5, 1.23),
                    ("VALID-FIRST", firstDay, 2.0, 2.00),
                    ("INVALID-NEGATIVE", todayAtOne, -9.0, 99.00),
                ]
                for orderNo, startedAt, energyKwh, amount in orders:
                    timeText = startedAt.strftime("%Y-%m-%d %H:%M:%S")
                    connection.execute(
                        "INSERT INTO charge_order(order_no,user_id,pile_id,status,start_time,end_time,"
                        "energy_kwh,amount,created_at) VALUES(?,1,1,'已完成',?,?,?,?,?)",
                        (orderNo, timeText, timeText, energyKwh, amount, timeText),
                    )
                with self.assertRaises(sqlite3.IntegrityError):
                    connection.execute(
                        "INSERT INTO charge_order(order_no,user_id,pile_id,status,start_time,end_time,"
                        "energy_kwh,amount,created_at) VALUES(?,1,1,'已完成',?,?,?,?,?)",
                        (
                            "VALID-TODAY",
                            timeText,
                            timeText,
                            4.5,
                            1.23,
                            timeText,
                        ),
                    )
                connection.execute(
                    "INSERT INTO charge_order(order_no,user_id,pile_id,status,start_time,end_time,"
                    "energy_kwh,amount,created_at) VALUES('INVALID-TIME',1,1,'已完成',"
                    "'not-a-time','not-a-time',5,50,'not-a-time')"
                )
                connection.execute(
                    "INSERT INTO charge_order(order_no,user_id,pile_id,status,start_time,end_time,"
                    "energy_kwh,amount,created_at) VALUES('INVALID-END',1,1,'已完成',"
                    "'2026-09-02 02:00:00',NULL,5,50,'2026-09-02 02:00:00')"
                )
            dashboardapp.DB = databasePath

            snapshot = dashboardapp.app.test_client().get("/api/dashboard").get_json()

            points = snapshot["revenueTrend"]["points"]
            expectedDates = [
                (firstDay + timedelta(days=offset)).date().isoformat() for offset in range(30)
            ]
            self.assertEqual([point["date"] for point in points], expectedDates)
            self.assertEqual(points[0], {"date": firstDay.date().isoformat(), "revenueCents": 200})
            self.assertEqual(points[-1], {"date": now.date().isoformat(), "revenueCents": 123})
            self.assertTrue(all(point["revenueCents"] == 0 for point in points[1:-1]))
            self.assertEqual([item["hour"] for item in snapshot["hourlyCharge"]], list(range(24)))
            self.assertEqual(snapshot["hourlyCharge"][1]["chargeKwh"], 4.5)
            self.assertEqual(snapshot["metrics"]["totalChargeKwh"], 6.5)
            self.assertEqual(snapshot["metrics"]["totalRevenueCents"], 323)
            self.assertEqual(snapshot["stationRanking"][0]["revenueCents"], 323)

    def testPileStatusAndStationRankingUseStableBusinessRules(self) -> None:
        with TemporaryDatabase(withHistory=False) as databasePath:
            now = datetime(2026, 9, 2, 12, 0, tzinfo=ZoneInfo("Asia/Shanghai"))
            self.useFixedTime(now)
            timeText = now.strftime("%Y-%m-%d %H:%M:%S")
            with sqlite3.connect(str(databasePath)) as connection:
                connection.execute("UPDATE pile SET status='在用' WHERE id=1")
                for orderNo, pileId in [("TIE-STATION-1", 1), ("TIE-STATION-2", 5)]:
                    connection.execute(
                        "INSERT INTO charge_order(order_no,user_id,pile_id,status,start_time,end_time,"
                        "energy_kwh,amount,created_at) VALUES(?,1,?,'已完成',?,?,?,?,?)",
                        (orderNo, pileId, timeText, timeText, 3.0, 10.00, timeText),
                    )
            dashboardapp.DB = databasePath

            snapshot = dashboardapp.app.test_client().get("/api/dashboard").get_json()

            self.assertEqual(snapshot["pileStatus"], {"idle": 10, "inUse": 1, "fault": 1})
            self.assertEqual(snapshot["metrics"]["activePileCount"], 11)
            ranking = snapshot["stationRanking"]
            self.assertEqual([item["stationId"] for item in ranking], [1, 2, 3])
            self.assertEqual(ranking[0]["revenueCents"], 1000)
            self.assertEqual(ranking[0]["orderCount"], 1)
            self.assertIn("utilizationPercent", ranking[0])

    def testUnreadableCoreDatabaseReturnsSafeNoStoreResponse(self) -> None:
        with TemporaryDirectory(prefix="chargehubmissing-") as tempdir:
            databasePath = Path(tempdir) / "missing.db"
            dashboardapp.DB = databasePath

            response = dashboardapp.app.test_client().get("/api/dashboard")

            self.assertEqual(response.status_code, 503)
            self.assertEqual(response.headers["Cache-Control"], "no-store")
            body = response.get_data(as_text=True)
            self.assertNotIn(str(databasePath), body)
            self.assertNotIn("SELECT", body)
            self.assertEqual(response.get_json()["error"]["code"], "DASHBOARD_UNAVAILABLE")

    def testMissingPredictionTablesOnlyDegradeForecast(self) -> None:
        with TemporaryDatabase(withHistory=False) as databasePath:
            with sqlite3.connect(str(databasePath)) as connection:
                connection.execute("DROP TABLE load_forecast")
                connection.execute("DROP TABLE analysis_report")
            dashboardapp.DB = databasePath

            response = dashboardapp.app.test_client().get("/api/dashboard")

            self.assertEqual(response.status_code, 200)
            self.assertEqual(
                response.get_json()["loadForecast"],
                {"status": "unavailable", "modelVersion": None, "series": []},
            )

    def testIncompatiblePredictionTableOnlyDegradesForecast(self) -> None:
        with TemporaryDatabase(withHistory=False) as databasePath:
            with sqlite3.connect(str(databasePath)) as connection:
                connection.execute("DROP TABLE load_forecast")
                connection.execute("CREATE TABLE load_forecast(legacy_value TEXT)")
            dashboardapp.DB = databasePath

            response = dashboardapp.app.test_client().get("/api/dashboard")

            self.assertEqual(response.status_code, 200)
            self.assertEqual(response.get_json()["loadForecast"]["status"], "unavailable")

    def testForecastProvidesTheSupportedHorizonsWhenDataExists(self) -> None:
        with TemporaryDatabase(withHistory=False) as databasePath:
            now = datetime(2026, 9, 2, 12, 0, tzinfo=ZoneInfo("Asia/Shanghai"))
            self.useFixedTime(now)
            createdAt = now.strftime("%Y-%m-%d %H:%M:%S")
            with sqlite3.connect(str(databasePath)) as connection:
                for horizon, energyKwh in [(1, 12.0), (6, 60.0), (24, 240.0)]:
                    connection.execute(
                        "INSERT INTO load_forecast(station_id,horizon_hours,pred_kwh,pred_idle,"
                        "peak_hour,created_at) VALUES(1,?,?,3,'00:00',?)",
                        (horizon, energyKwh, createdAt),
                    )
            dashboardapp.DB = databasePath

            forecast = dashboardapp.app.test_client().get("/api/dashboard").get_json()["loadForecast"]

            self.assertEqual(forecast["status"], "available")
            self.assertEqual([item["horizonHours"] for item in forecast["series"]], [1, 6, 24])
            self.assertEqual([item["points"][0]["loadKw"] for item in forecast["series"]], [12.0, 10.0, 10.0])

    def testSnapshotUsesOneReadOnlyDatabaseConnection(self) -> None:
        with TemporaryDatabase(withHistory=False) as databasePath:
            dashboardapp.DB = databasePath
            with patch(
                "dashboard.dashboardrepository.sqlite3.connect",
                wraps=sqlite3.connect,
            ) as connect:
                response = dashboardapp.app.test_client().get("/api/dashboard")

            self.assertEqual(response.status_code, 200)
            self.assertEqual(connect.call_count, 1)
            self.assertIn("mode=ro", connect.call_args.args[0])

    def testEmptyPileTableUsesSafeZeroCounts(self) -> None:
        with TemporaryDatabase(withHistory=False) as databasePath:
            with sqlite3.connect(str(databasePath)) as connection:
                connection.execute("DELETE FROM pile")
            dashboardapp.DB = databasePath

            snapshot = dashboardapp.app.test_client().get("/api/dashboard").get_json()

            self.assertEqual(snapshot["pileStatus"], {"idle": 0, "inUse": 0, "fault": 0})
            self.assertEqual(snapshot["metrics"]["activePileCount"], 0)
            self.assertTrue(
                all(item["utilizationPercent"] == 0 for item in snapshot["stationRanking"])
            )

    def testFixedDemoDataUsesTheLiveSnapshotContract(self) -> None:
        demoPath = projectRoot / "dashboard" / "data" / "dashboard.json"

        snapshot = json.loads(demoPath.read_text(encoding="utf-8"))

        self.assertEqual(snapshot["schemaVersion"], "1.0")
        self.assertEqual(snapshot["source"], "mock")
        self.assertEqual(len(snapshot["revenueTrend"]["points"]), 30)
        self.assertEqual([item["hour"] for item in snapshot["hourlyCharge"]], list(range(24)))
        self.assertEqual(
            [item["horizonHours"] for item in snapshot["loadForecast"]["series"]],
            [1, 6, 24],
        )
        self.assertIsInstance(snapshot["metrics"]["totalRevenueCents"], int)
        self.assertEqual(
            snapshot["metrics"]["totalRevenueCents"],
            sum(item["revenueCents"] for item in snapshot["stationRanking"]),
        )

    def testUtilizationClipsOrdersToTheThirtyDayWindow(self) -> None:
        with TemporaryDatabase(withHistory=False) as databasePath:
            now = datetime(2026, 9, 2, 12, 0, tzinfo=ZoneInfo("Asia/Shanghai"))
            self.useFixedTime(now)
            windowStart = now - timedelta(days=30)
            with sqlite3.connect(str(databasePath)) as connection:
                connection.execute("DELETE FROM pile WHERE id != 1")
                orders = [
                    ("OVERLAP", windowStart - timedelta(days=1), windowStart + timedelta(days=1)),
                    ("FUTURE", now + timedelta(days=1), now + timedelta(days=2)),
                ]
                for orderNo, startedAt, endedAt in orders:
                    connection.execute(
                        "INSERT INTO charge_order(order_no,user_id,pile_id,status,start_time,end_time,"
                        "energy_kwh,amount,created_at) VALUES(?,1,1,'已完成',?,?,?,?,?)",
                        (
                            orderNo,
                            startedAt.strftime("%Y-%m-%d %H:%M:%S"),
                            endedAt.strftime("%Y-%m-%d %H:%M:%S"),
                            3.0,
                            10.0,
                            startedAt.strftime("%Y-%m-%d %H:%M:%S"),
                        ),
                    )
            dashboardapp.DB = databasePath

            ranking = dashboardapp.app.test_client().get("/api/dashboard").get_json()["stationRanking"]

            stationOne = next(item for item in ranking if item["stationId"] == 1)
            self.assertEqual(stationOne["utilizationPercent"], 3.3)

    def testDashboardStartupNeverCreatesAMissingBusinessDatabase(self) -> None:
        with TemporaryDirectory(prefix="chargehubstartup-") as tempdir:
            missingPath = Path(tempdir) / "missing.db"
            with patch.object(dashboardapp, "findDb", return_value=missingPath), patch.object(
                dashboardapp.app, "run"
            ) as run:
                dashboardapp.main()

            self.assertFalse(missingPath.exists())
            run.assert_called_once()


if __name__ == "__main__":
    unittest.main()
