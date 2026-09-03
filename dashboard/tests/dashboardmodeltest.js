import assert from "node:assert/strict";
import test from "node:test";

import {
  formatMoneyFromCents,
  formatNumber,
  getPileStatusItems,
  selectForecast,
  selectRevenueRange,
  validateDashboardSnapshot,
} from "../dashboardmodel.js";

function completeSnapshot() {
  return {
    schemaVersion: "1.0",
    generatedAt: "2026-09-03T12:00:00+08:00",
    timeZone: "Asia/Shanghai",
    source: "mock",
    metrics: { totalChargeKwh: 0, totalRevenueCents: 0, activePileCount: 0 },
    revenueTrend: {
      rangeDays: 30,
      points: Array.from({ length: 30 }, (_, index) => ({
        date: `2026-08-${String(index + 1).padStart(2, "0")}`,
        revenueCents: 0,
      })),
    },
    pileStatus: { idle: 0, inUse: 0, fault: 0 },
    hourlyCharge: Array.from({ length: 24 }, (_, hour) => ({ hour, chargeKwh: 0 })),
    stationRanking: [],
    loadForecast: { status: "unavailable", modelVersion: null, series: [] },
  };
}

test("formats integer cents without floating-point arithmetic", () => {
  assert.equal(formatMoneyFromCents(0), "0.00");
  assert.equal(formatMoneyFromCents(1), "0.01");
  assert.equal(formatMoneyFromCents(1000000), "10000.00");
  assert.equal(formatMoneyFromCents(null), "--");
});

test("selects only supported revenue windows", () => {
  const points = Array.from({ length: 30 }, (_, index) => ({
    date: `2026-08-${String(index + 1).padStart(2, "0")}`,
    revenueCents: index * 100,
  }));

  assert.deepEqual(selectRevenueRange(points, 7), points.slice(-7));
  assert.deepEqual(selectRevenueRange(points, 30), points);
  assert.throws(() => selectRevenueRange(points, 14), /unsupported revenue range/);
});

test("formats valid zero values and rejects missing or invalid numbers", () => {
  assert.equal(formatNumber(0, 1), "0.0");
  assert.equal(formatNumber(12.345, 2), "12.35");
  assert.equal(formatNumber(undefined, 1), "--");
  assert.equal(formatNumber(Number.NaN, 1), "--");
});

test("calculates all pile percentages safely", () => {
  assert.deepEqual(getPileStatusItems({ idle: 0, inUse: 0, fault: 0 }), [
    { key: "idle", name: "空闲", value: 0, percent: 0 },
    { key: "inUse", name: "在用", value: 0, percent: 0 },
    { key: "fault", name: "故障", value: 0, percent: 0 },
  ]);
  assert.deepEqual(
    getPileStatusItems({ idle: 1, inUse: 2, fault: 1 }).map((item) => item.percent),
    [25, 50, 25],
  );
});

test("selects only supported forecast horizons", () => {
  const forecast = {
    status: "available",
    series: [
      { horizonHours: 1, points: [{ loadKw: 10 }] },
      { horizonHours: 6, points: [{ loadKw: 20 }] },
      { horizonHours: 24, points: [{ loadKw: 30 }] },
    ],
  };

  assert.equal(selectForecast(forecast, 6).points[0].loadKw, 20);
  assert.equal(selectForecast({ status: "unavailable", series: [] }, 1), null);
  assert.throws(() => selectForecast(forecast, 2), /unsupported forecast horizon/);
});

test("rejects incompatible dashboard snapshots", () => {
  assert.doesNotThrow(() => validateDashboardSnapshot(completeSnapshot()));
  assert.throws(
    () => validateDashboardSnapshot({ schemaVersion: "2.0" }),
    /incompatible dashboard schema/,
  );
  assert.throws(() => validateDashboardSnapshot(null), /invalid dashboard snapshot/);
});

test("rejects malformed fields even when the schema version is compatible", () => {
  assert.throws(
    () => validateDashboardSnapshot({ ...completeSnapshot(), source: "fallback" }),
    /source/,
  );
  assert.throws(
    () => validateDashboardSnapshot({ ...completeSnapshot(), hourlyCharge: [] }),
    /hourlyCharge/,
  );
  const snapshot = completeSnapshot();
  snapshot.revenueTrend.points[3].date = snapshot.revenueTrend.points[2].date;
  assert.throws(() => validateDashboardSnapshot(snapshot), /date/);
});
