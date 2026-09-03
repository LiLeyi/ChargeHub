# -*- coding: utf-8 -*-
"""Validate the integer-cent wallet schema and deterministic demo fixture."""
from __future__ import annotations

import sqlite3
import unittest
from pathlib import Path

from testsupport import TemporaryDatabase


projectRoot = Path(__file__).resolve().parents[1]


class WalletSchemaTest(unittest.TestCase):
    def testDemoWalletUsesIntegerCentsAndUniqueRequestIds(self) -> None:
        with TemporaryDatabase(withHistory=True) as path:
            with sqlite3.connect(str(path)) as connection:
                user = connection.execute(
                    "SELECT balance, balance_cents, typeof(balance_cents) FROM user WHERE id=1"
                ).fetchone()
                ledger = connection.execute(
                    "SELECT amount_cents,balance_after_cents,request_id,trade_no,status "
                    "FROM recharge_log WHERE user_id=1"
                ).fetchone()

        self.assertEqual(user, (80.0, 8000, "integer"))
        self.assertEqual(
            ledger,
            (8000, 8000, "legacy-seed-1", "RCSEED00000001", "succeeded"),
        )

    def testSchemaAndCppMigrationDeclareTheSameWalletFields(self) -> None:
        schema = (projectRoot / "database" / "schema.sql").read_text(encoding="utf-8")
        databaseSource = (projectRoot / "adminserver" / "src" / "database.cpp").read_text(
            encoding="utf-8"
        )
        for field in [
            "balance_cents",
            "amount_cents",
            "balance_after_cents",
            "request_id",
            "trade_no",
        ]:
            self.assertIn(field, schema)
            self.assertIn(field, databaseSource)
        self.assertIn("idx_recharge_request_id", schema)
        self.assertIn("idx_recharge_request_id", databaseSource)


if __name__ == "__main__":
    unittest.main()
