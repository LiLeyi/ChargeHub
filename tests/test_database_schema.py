from __future__ import annotations

import re
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from database.initdb import initDb


EXPECTED_INDEXES = {
    "uq_reservation_active_pile": ("pile_id",),
    "uq_reservation_active_user": ("user_id",),
    "uq_order_open_user": ("user_id",),
    "uq_order_charging_pile": ("pile_id",),
    "idx_order_user_status": ("user_id", "status"),
    "idx_order_user_id": ("user_id", "id"),
    "idx_order_status_start": ("status", "start_time"),
    "idx_order_start": ("start_time",),
    "idx_pile_station": ("station_id",),
    "idx_pile_status": ("status",),
    "idx_reservation_user_status_expire_id": ("user_id", "status", "expire_at", "id"),
    "idx_reservation_pile_status": ("pile_id", "status"),
    "idx_reservation_status_expire": ("status", "expire_at"),
    "idx_station_review_station": ("station_id", "id"),
    "idx_station_review_user_pile": ("user_id", "pile_id"),
    "idx_station_review_pile_id": ("pile_id", "id"),
    "idx_review_doc_pile_id": ("pile_id", "id"),
    "idx_recharge_user_id": ("user_id", "id"),
}

LEGACY_INDEXES = (
    "idx_order_created_at",
    "idx_recharge_user_created",
    "idx_order_status",
    "idx_reservation_user_status",
)

QUERY_PLANS = {
    "idx_order_user_id": (
        "SELECT o.*, p.pile_no, p.type, p.power_kw, s.name, s.price_per_kwh "
        "FROM charge_order o JOIN pile p ON p.id=o.pile_id "
        "JOIN station s ON s.id=p.station_id "
        "WHERE o.user_id=? ORDER BY o.id DESC LIMIT 40",
        (1,),
    ),
    "idx_station_review_user_pile": (
        "SELECT id FROM station_review WHERE user_id=? AND pile_id=?",
        (1, 1),
    ),
    "idx_station_review_pile_id": (
        "SELECT r.id, r.score, r.comment, r.created_at, r.user_id, u.nickname "
        "FROM station_review r JOIN user u ON u.id=r.user_id "
        "WHERE r.pile_id=? ORDER BY r.id DESC",
        (1,),
    ),
    "idx_review_doc_pile_id": (
        "SELECT d.id, d.doc, u.nickname FROM review_doc d "
        "JOIN user u ON u.id=d.user_id WHERE d.pile_id=? ORDER BY d.id DESC",
        (1,),
    ),
    "idx_recharge_user_id": (
        "SELECT * FROM recharge_log WHERE user_id=? ORDER BY id DESC LIMIT 30",
        (1,),
    ),
    "idx_order_status_start": (
        "SELECT IFNULL(SUM(amount),0) FROM charge_order "
        "WHERE status='已完成' AND start_time>=? AND start_time<?",
        ("2020-01-01 00:00:00", "2030-01-01 00:00:00"),
    ),
    "idx_reservation_user_status_expire_id": (
        "SELECT * FROM reservation WHERE user_id=? AND status=? "
        "ORDER BY expire_at, id DESC",
        (1, "有效"),
    ),
}


def schema_objects(sql: str) -> dict[tuple[str, str], str]:
    connection = sqlite3.connect(":memory:")
    try:
        connection.executescript(sql)
        rows = connection.execute(
            "SELECT type, name, sql FROM sqlite_master "
            "WHERE type IN ('table', 'index') AND sql IS NOT NULL"
        )
        return {
            (kind, name): re.sub(r"\s+", "", definition).lower()
            for kind, name, definition in rows
        }
    finally:
        connection.close()


class DatabaseSchemaTest(unittest.TestCase):
    def test_cpp_and_sql_schema_objects_match(self) -> None:
        sql_schema = (ROOT / "database" / "schema.sql").read_text(encoding="utf-8")
        cpp = (ROOT / "adminserver" / "src" / "database.cpp").read_text(encoding="utf-8")
        match = re.search(r'R"SQL\((.*?)\)SQL"', cpp, re.DOTALL)
        self.assertIsNotNone(match, "database.cpp embedded schema was not found")
        self.assertEqual(schema_objects(sql_schema), schema_objects(match.group(1)))

    def test_initdb_creates_query_indexes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = initDb(Path(directory) / "chargehub.db")
            connection = sqlite3.connect(path)
            try:
                self.assertEqual(connection.execute("PRAGMA integrity_check").fetchone()[0], "ok")
                self.assertEqual(connection.execute("PRAGMA foreign_key_check").fetchall(), [])
                for index_name, expected_columns in EXPECTED_INDEXES.items():
                    columns = tuple(
                        row[2] for row in connection.execute(f"PRAGMA index_info({index_name})")
                    )
                    self.assertEqual(columns, expected_columns, index_name)
                existing_indexes = {
                    row[0] for row in connection.execute("SELECT name FROM sqlite_master WHERE type='index'")
                }
                for legacy_index in LEGACY_INDEXES:
                    self.assertNotIn(legacy_index, existing_indexes)
                for index_name, (sql, parameters) in QUERY_PLANS.items():
                    plan = "\n".join(
                        row[3] for row in connection.execute("EXPLAIN QUERY PLAN " + sql, parameters)
                    )
                    self.assertIn(index_name, plan, plan)
            finally:
                connection.close()

    def test_schema_removes_legacy_indexes(self) -> None:
        schema = (ROOT / "database" / "schema.sql").read_text(encoding="utf-8")
        connection = sqlite3.connect(":memory:")
        try:
            connection.executescript(schema)
            connection.execute("CREATE INDEX idx_order_created_at ON charge_order(created_at)")
            connection.execute(
                "CREATE INDEX idx_recharge_user_created ON recharge_log(user_id, created_at DESC)"
            )
            connection.execute("CREATE INDEX idx_order_status ON charge_order(status)")
            connection.execute(
                "CREATE INDEX idx_reservation_user_status ON reservation(user_id, status)"
            )
            connection.executescript(schema)
            indexes = {
                row[0] for row in connection.execute("SELECT name FROM sqlite_master WHERE type='index'")
            }
            for legacy_index in LEGACY_INDEXES:
                self.assertNotIn(legacy_index, indexes)
        finally:
            connection.close()


if __name__ == "__main__":
    unittest.main()
