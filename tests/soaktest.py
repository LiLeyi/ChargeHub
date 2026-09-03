# -*- coding: utf-8 -*-
"""Run a repeatable headless dashboard soak against an isolated database."""
from __future__ import annotations

import argparse
import json
import resource
import sys
import time
from pathlib import Path


projectRoot = Path(__file__).resolve().parents[1]
if str(projectRoot) not in sys.path:
    sys.path.insert(0, str(projectRoot))

from dashboard import app as dashboardapp
from testsupport import TemporaryDatabase


def currentRssKb() -> int:
    """Return maximum resident memory in KiB on Linux and macOS."""
    value = int(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss)
    return value // 1024 if sys.platform == "darwin" else value


def validateSnapshot(snapshot: object) -> None:
    """Raise ValueError when a response cannot be safely rendered by the page."""
    if not isinstance(snapshot, dict) or snapshot.get("schemaVersion") != "1.0":
        raise ValueError("incompatible dashboard schema")
    if snapshot.get("source") not in {"live", "mock"}:
        raise ValueError("invalid dashboard source")
    if len(snapshot.get("revenueTrend", {}).get("points", [])) != 30:
        raise ValueError("revenue trend is incomplete")
    if len(snapshot.get("hourlyCharge", [])) != 24:
        raise ValueError("hourly charge series is incomplete")


def runSoak(
    databasePath: Path,
    *,
    durationSeconds: float = 1800,
    intervalSeconds: float = 5,
    maxRssGrowthKb: int = 50 * 1024,
) -> dict:
    """Poll the real Flask route and report latency, failures and memory growth."""
    if durationSeconds <= 0 or intervalSeconds <= 0:
        raise ValueError("durations must be positive")
    originalDatabasePath = dashboardapp.DB
    startedAt = time.monotonic()
    deadline = startedAt + durationSeconds
    initialRssKb = currentRssKb()
    attempts = 0
    failures = 0
    slowResponses = 0
    maxResponseMs = 0.0
    firstError = None
    dashboardapp.DB = databasePath
    client = dashboardapp.app.test_client()
    try:
        while attempts == 0 or time.monotonic() < deadline:
            requestStartedAt = time.monotonic()
            try:
                response = client.get("/api/dashboard")
                responseMs = (time.monotonic() - requestStartedAt) * 1000
                maxResponseMs = max(maxResponseMs, responseMs)
                if responseMs >= 1000:
                    slowResponses += 1
                if response.status_code != 200:
                    raise ValueError(f"unexpected HTTP {response.status_code}")
                validateSnapshot(response.get_json())
            except Exception as error:  # capture evidence and finish the planned soak
                failures += 1
                if firstError is None:
                    firstError = f"{type(error).__name__}: {error}"
            attempts += 1
            remaining = deadline - time.monotonic()
            if remaining > 0:
                time.sleep(min(intervalSeconds, remaining))
    finally:
        dashboardapp.DB = originalDatabasePath
    finalRssKb = currentRssKb()
    rssGrowthKb = max(0, finalRssKb - initialRssKb)
    return {
        "passed": failures == 0 and slowResponses == 0 and rssGrowthKb <= maxRssGrowthKb,
        "durationSeconds": round(time.monotonic() - startedAt, 3),
        "attempts": attempts,
        "failures": failures,
        "slowResponses": slowResponses,
        "maxResponseMs": round(maxResponseMs, 3),
        "rssGrowthKb": rssGrowthKb,
        "maxRssGrowthKb": maxRssGrowthKb,
        "firstError": firstError,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--duration-seconds", type=float, default=1800)
    parser.add_argument("--interval-seconds", type=float, default=5)
    args = parser.parse_args()
    with TemporaryDatabase(withHistory=True) as databasePath:
        result = runSoak(
            databasePath,
            durationSeconds=args.duration_seconds,
            intervalSeconds=args.interval_seconds,
        )
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
