from __future__ import annotations

import sqlite3
import sys
import tempfile
import threading
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from database.initdb import connect as init_connect
from database.initdb import initDb


NOW = "2026-01-01 12:00:00"
EXPIRE = "2030-01-01 12:15:00"


def connect(path: Path, busy_timeout: int = 2000) -> sqlite3.Connection:
    connection = sqlite3.connect(path, timeout=busy_timeout / 1000)
    connection.execute("PRAGMA foreign_keys=ON")
    connection.execute(f"PRAGMA busy_timeout={busy_timeout}")
    return connection


class DatabaseReliabilityTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory()
        self.path = initDb(Path(self.tempdir.name) / "chargehub.db")
        self.connection = connect(self.path)

    def tearDown(self) -> None:
        self.connection.close()
        self.tempdir.cleanup()

    def assert_constraint(self, sql: str, parameters: tuple[object, ...]) -> None:
        with self.assertRaises(sqlite3.IntegrityError):
            self.connection.execute(sql, parameters)
        self.connection.rollback()

    def test_duplicate_phone_is_rejected(self) -> None:
        before = self.connection.execute(
            "SELECT COUNT(*) FROM user WHERE phone='13800138000'"
        ).fetchone()[0]
        self.assert_constraint(
            "INSERT INTO user(phone,nickname,password_hash,status,created_at) VALUES(?,?,?,?,?)",
            ("13800138000", "重复用户", "hash", "正常", NOW),
        )
        after = self.connection.execute(
            "SELECT COUNT(*) FROM user WHERE phone='13800138000'"
        ).fetchone()[0]
        self.assertEqual((before, after), (1, 1))

    def test_check_constraints_reject_invalid_states_and_scores(self) -> None:
        self.assert_constraint("UPDATE user SET status=? WHERE id=1", ("未知",))
        self.assert_constraint("UPDATE pile SET status=? WHERE id=1", ("离线",))
        self.assert_constraint(
            "INSERT INTO charge_order(order_no,user_id,pile_id,status,start_time,created_at) "
            "VALUES(?,?,?,?,?,?)",
            ("BAD-STATUS", 4, 4, "未知", NOW, NOW),
        )
        self.assert_constraint(
            "INSERT INTO reservation(user_id,pile_id,status,expire_at,created_at) VALUES(?,?,?,?,?)",
            (4, 4, "未知", EXPIRE, NOW),
        )
        self.assert_constraint(
            "INSERT INTO station_review(user_id,station_id,pile_id,score,comment,created_at) "
            "VALUES(?,?,?,?,?,?)",
            (4, 1, 4, 0, "非法评分", NOW),
        )
        self.assert_constraint(
            "INSERT INTO station_review(user_id,station_id,pile_id,score,comment,created_at) "
            "VALUES(?,?,?,?,?,?)",
            (4, 1, 4, 6, "非法评分", NOW),
        )

    def test_foreign_keys_are_enabled_and_reject_invalid_references(self) -> None:
        self.assertEqual(self.connection.execute("PRAGMA foreign_keys").fetchone()[0], 1)
        self.assert_constraint(
            "INSERT INTO reservation(user_id,pile_id,status,expire_at,created_at) VALUES(?,?,?,?,?)",
            (9999, 1, "有效", EXPIRE, NOW),
        )
        self.assert_constraint(
            "INSERT INTO charge_order(order_no,user_id,pile_id,status,start_time,created_at) "
            "VALUES(?,?,?,?,?,?)",
            ("BAD-FK", 1, 9999, "充电中", NOW, NOW),
        )

    def test_initializer_connection_enables_foreign_keys_and_busy_timeout(self) -> None:
        connection = init_connect(self.path)
        try:
            self.assertEqual(connection.execute("PRAGMA foreign_keys").fetchone()[0], 1)
            self.assertEqual(connection.execute("PRAGMA busy_timeout").fetchone()[0], 5000)
        finally:
            connection.close()

    def test_partial_unique_indexes_enforce_active_business_rules(self) -> None:
        self.connection.execute(
            "INSERT INTO reservation(user_id,pile_id,status,expire_at,created_at) VALUES(?,?,?,?,?)",
            (4, 4, "有效", EXPIRE, NOW),
        )
        self.connection.commit()
        self.assert_constraint(
            "INSERT INTO reservation(user_id,pile_id,status,expire_at,created_at) VALUES(?,?,?,?,?)",
            (3, 4, "有效", EXPIRE, NOW),
        )
        self.assert_constraint(
            "INSERT INTO reservation(user_id,pile_id,status,expire_at,created_at) VALUES(?,?,?,?,?)",
            (4, 8, "有效", EXPIRE, NOW),
        )
        self.connection.execute(
            "INSERT INTO reservation(user_id,pile_id,status,expire_at,created_at) VALUES(?,?,?,?,?)",
            (4, 4, "已取消", EXPIRE, NOW),
        )
        self.connection.execute(
            "INSERT INTO charge_order(order_no,user_id,pile_id,status,start_time,created_at) "
            "VALUES(?,?,?,?,?,?)",
            ("OPEN-1", 4, 4, "充电中", NOW, NOW),
        )
        self.connection.commit()
        self.assert_constraint(
            "INSERT INTO charge_order(order_no,user_id,pile_id,status,start_time,created_at) "
            "VALUES(?,?,?,?,?,?)",
            ("OPEN-2", 4, 5, "待结算", NOW, NOW),
        )
        self.assert_constraint(
            "INSERT INTO charge_order(order_no,user_id,pile_id,status,start_time,created_at) "
            "VALUES(?,?,?,?,?,?)",
            ("OPEN-3", 3, 4, "充电中", NOW, NOW),
        )

    def test_transaction_failure_rolls_back_all_recharge_changes(self) -> None:
        before_balance = self.connection.execute("SELECT balance FROM user WHERE id=1").fetchone()[0]
        before_logs = self.connection.execute("SELECT COUNT(*) FROM recharge_log").fetchone()[0]
        self.connection.execute(
            "CREATE TRIGGER fail_recharge_log BEFORE INSERT ON recharge_log "
            "BEGIN SELECT RAISE(ABORT, 'forced failure'); END"
        )
        self.connection.commit()
        try:
            self.connection.execute("BEGIN IMMEDIATE")
            self.connection.execute("UPDATE user SET balance=ROUND(balance + ?, 2) WHERE id=?", (25, 1))
            self.connection.execute(
                "INSERT INTO recharge_log(user_id,amount,result,created_at) VALUES(?,?,?,?)",
                (1, 25, "成功", NOW),
            )
            self.connection.commit()
        except sqlite3.IntegrityError:
            self.connection.rollback()
        else:
            self.fail("forced transaction failure did not occur")
        self.assertEqual(
            self.connection.execute("SELECT balance FROM user WHERE id=1").fetchone()[0],
            before_balance,
        )
        self.assertEqual(
            self.connection.execute("SELECT COUNT(*) FROM recharge_log").fetchone()[0],
            before_logs,
        )

    def test_database_lock_prevents_partial_write_and_allows_retry(self) -> None:
        locked = connect(self.path)
        contender = connect(self.path, busy_timeout=50)
        before = contender.execute("SELECT balance FROM user WHERE id=1").fetchone()[0]
        try:
            locked.execute("BEGIN IMMEDIATE")
            locked.execute("UPDATE user SET balance=ROUND(balance + 1, 2) WHERE id=1")
            with self.assertRaisesRegex(sqlite3.OperationalError, "locked"):
                contender.execute("BEGIN IMMEDIATE")
            contender.rollback()
            self.assertEqual(contender.execute("SELECT balance FROM user WHERE id=1").fetchone()[0], before)
            locked.rollback()
            contender.execute("BEGIN IMMEDIATE")
            contender.execute("UPDATE user SET balance=ROUND(balance + 1, 2) WHERE id=1")
            contender.commit()
            self.assertEqual(contender.execute("SELECT balance FROM user WHERE id=1").fetchone()[0], before + 1)
        finally:
            if locked.in_transaction:
                locked.rollback()
            if contender.in_transaction:
                contender.rollback()
            locked.close()
            contender.close()

    def test_two_connections_cannot_reserve_the_same_pile(self) -> None:
        self.connection.execute("DELETE FROM reservation")
        self.connection.commit()
        barrier = threading.Barrier(2)

        def reserve(user_id: int) -> str:
            connection = connect(self.path)
            try:
                barrier.wait()
                connection.execute("BEGIN IMMEDIATE")
                connection.execute(
                    "INSERT INTO reservation(user_id,pile_id,status,expire_at,created_at) "
                    "VALUES(?,?,?,?,?)",
                    (user_id, 1, "有效", EXPIRE, NOW),
                )
                connection.commit()
                return "committed"
            except sqlite3.IntegrityError:
                connection.rollback()
                return "constraint"
            finally:
                connection.close()

        with ThreadPoolExecutor(max_workers=2) as executor:
            outcomes = list(executor.map(reserve, (1, 2)))
        self.assertCountEqual(outcomes, ("committed", "constraint"))
        active = self.connection.execute(
            "SELECT COUNT(*) FROM reservation WHERE pile_id=1 AND status='有效'"
        ).fetchone()[0]
        self.assertEqual(active, 1)

    def test_two_connections_cannot_create_duplicate_charging_orders(self) -> None:
        self.connection.execute("DELETE FROM charge_order")
        self.connection.commit()
        barrier = threading.Barrier(2)

        def start(order_no: str, user_id: int, pile_id: int) -> str:
            connection = connect(self.path)
            try:
                barrier.wait()
                connection.execute("BEGIN IMMEDIATE")
                connection.execute(
                    "INSERT INTO charge_order(order_no,user_id,pile_id,status,start_time,created_at) "
                    "VALUES(?,?,?,?,?,?)",
                    (order_no, user_id, pile_id, "充电中", NOW, NOW),
                )
                connection.commit()
                return "committed"
            except sqlite3.IntegrityError:
                connection.rollback()
                return "constraint"
            finally:
                connection.close()

        with ThreadPoolExecutor(max_workers=2) as executor:
            futures = (
                executor.submit(start, "RACE-PILE-1", 1, 1),
                executor.submit(start, "RACE-PILE-2", 2, 1),
            )
            outcomes = [future.result() for future in futures]
        self.assertCountEqual(outcomes, ("committed", "constraint"))
        active = self.connection.execute(
            "SELECT COUNT(*) FROM charge_order WHERE pile_id=1 AND status='充电中'"
        ).fetchone()[0]
        self.assertEqual(active, 1)

    def test_conflicting_legacy_data_is_preserved_when_unique_index_creation_fails(self) -> None:
        self.connection.execute("DROP INDEX uq_reservation_active_pile")
        self.connection.execute("DROP INDEX uq_reservation_active_user")
        self.connection.execute(
            "INSERT INTO reservation(user_id,pile_id,status,expire_at,created_at) "
            "VALUES(?,?,?,?,?)",
            (1, 1, "有效", EXPIRE, NOW),
        )
        self.connection.execute(
            "INSERT INTO reservation(user_id,pile_id,status,expire_at,created_at) "
            "VALUES(?,?,?,?,?)",
            (2, 1, "有效", EXPIRE, NOW),
        )
        self.connection.commit()

        with self.assertRaises(sqlite3.IntegrityError):
            self.connection.execute(
                "CREATE UNIQUE INDEX uq_reservation_active_pile "
                "ON reservation(pile_id) WHERE status='有效'"
            )

        rows = self.connection.execute(
            "SELECT user_id, pile_id, status FROM reservation ORDER BY id"
        ).fetchall()
        self.assertEqual(
            [tuple(row) for row in rows],
            [(1, 1, "有效"), (2, 1, "有效")],
        )


if __name__ == "__main__":
    unittest.main()
