# -*- coding: utf-8 -*-
"""Shared isolated fixtures for ChargeHub automated tests."""
from __future__ import annotations

import sqlite3
import tempfile
from pathlib import Path


class TemporaryDatabase:
    """Create and dispose an initialized SQLite database outside the project tree."""

    def __init__(self, *, withHistory: bool = False) -> None:
        self.withHistory = withHistory
        self.tempdir: tempfile.TemporaryDirectory[str] | None = None
        self.path: Path | None = None

    def __enter__(self) -> Path:
        from database.initdb import initDb

        self.tempdir = tempfile.TemporaryDirectory(prefix="chargehubtest-")
        self.path = Path(self.tempdir.name) / "chargehub.db"
        initDb(self.path, withHistory=self.withHistory)
        return self.path

    def __exit__(self, excType, excValue, traceback) -> None:
        if self.tempdir is not None:
            self.tempdir.cleanup()


def rowCount(path: Path, table: str) -> int:
    """Return a table count for fixture assertions using a fixed table allowlist."""
    if table not in {"user", "station", "pile", "charge_order", "recharge_log"}:
        raise ValueError("unsupported fixture table")
    with sqlite3.connect(str(path)) as connection:
        row = connection.execute(f"SELECT COUNT(*) FROM {table}").fetchone()
    return int(row[0])
