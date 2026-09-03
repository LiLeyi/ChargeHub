import assert from "node:assert/strict";
import test from "node:test";

import { DashboardController, HttpDashboardSource } from "../dashboarddata.js";

const snapshot = (value = 1) => ({
  schemaVersion: "1.0",
  generatedAt: "2026-09-03T12:00:00+08:00",
  timeZone: "Asia/Shanghai",
  source: "mock",
  metrics: { totalChargeKwh: value, totalRevenueCents: 100, activePileCount: 1 },
  revenueTrend: {
    rangeDays: 30,
    points: Array.from({ length: 30 }, (_, index) => ({
      date: `2026-08-${String(index + 1).padStart(2, "0")}`,
      revenueCents: index,
    })),
  },
  pileStatus: { idle: 1, inUse: 0, fault: 0 },
  hourlyCharge: Array.from({ length: 24 }, (_, hour) => ({ hour, chargeKwh: hour })),
  stationRanking: [],
  loadForecast: { status: "unavailable", modelVersion: null, series: [] },
});

function deferred() {
  let resolve;
  let reject;
  const promise = new Promise((resolvePromise, rejectPromise) => {
    resolve = resolvePromise;
    reject = rejectPromise;
  });
  return { promise, resolve, reject };
}

test("calls a browser-style fetch function with its global receiver", async () => {
  const browserGlobal = globalThis;
  function fetchFn() {
    assert.equal(this, browserGlobal);
    return Promise.resolve({ ok: true, json: async () => snapshot() });
  }
  const source = new HttpDashboardSource({ fetchFn });

  assert.equal((await source.load()).schemaVersion, "1.0");
});

test("allows only one refresh request at a time", async () => {
  const pending = deferred();
  let loadCount = 0;
  const controller = new DashboardController({
    source: { load: () => { loadCount += 1; return pending.promise; } },
  });

  const first = controller.refresh();
  const second = controller.refresh();
  assert.equal(loadCount, 1);
  pending.resolve(snapshot());
  await Promise.all([first, second]);
  assert.equal(controller.getState().status, "ready");
});

test("keeps the last trusted snapshot when a later refresh fails", async () => {
  let attempt = 0;
  const controller = new DashboardController({
    source: {
      load: async () => {
        attempt += 1;
        if (attempt === 1) return snapshot(8);
        throw new Error("网络连接失败");
      },
    },
    now: () => new Date("2026-09-03T04:00:00.000Z"),
  });

  await controller.refresh();
  await controller.refresh();
  const state = controller.getState();
  assert.equal(state.status, "stale");
  assert.equal(state.snapshot.metrics.totalChargeKwh, 8);
  assert.equal(state.lastSuccessAt, "2026-09-03T04:00:00.000Z");
  assert.match(state.error, /网络连接失败/);
});

test("shows an error state when the first refresh fails", async () => {
  const controller = new DashboardController({
    source: { load: async () => { throw new Error("服务不可用"); } },
  });

  await controller.refresh();
  assert.deepEqual(controller.getState().status, "error");
  assert.equal(controller.getState().snapshot, null);
});

test("start and stop own exactly one timer and abort the active request", () => {
  const pending = deferred();
  const timers = [];
  const cleared = [];
  let signal;
  const controller = new DashboardController({
    source: { load: ({ signal: receivedSignal }) => { signal = receivedSignal; return pending.promise; } },
    setIntervalFn: (callback, milliseconds) => {
      timers.push({ callback, milliseconds });
      return 77;
    },
    clearIntervalFn: (timerId) => cleared.push(timerId),
  });

  controller.start();
  controller.start();
  assert.equal(timers.length, 1);
  assert.equal(timers[0].milliseconds, 5000);
  controller.stop();
  assert.deepEqual(cleared, [77]);
  assert.equal(signal.aborted, true);
  pending.resolve(snapshot());
});

test("calls browser-style timer functions with their global receiver", () => {
  const browserGlobal = globalThis;
  let cleared = false;
  function setIntervalFn() {
    assert.equal(this, browserGlobal);
    return 88;
  }
  function clearIntervalFn(timerId) {
    assert.equal(this, browserGlobal);
    assert.equal(timerId, 88);
    cleared = true;
  }
  const controller = new DashboardController({
    source: { load: async () => snapshot() },
    setIntervalFn,
    clearIntervalFn,
  });

  controller.start();
  controller.stop();
  assert.equal(cleared, true);
});

test("an incompatible response cannot replace the trusted snapshot", async () => {
  let next = snapshot(5);
  const controller = new DashboardController({
    source: { load: async () => next },
  });

  await controller.refresh();
  next = { ...snapshot(99), schemaVersion: "2.0" };
  await controller.refresh();
  assert.equal(controller.getState().status, "stale");
  assert.equal(controller.getState().snapshot.metrics.totalChargeKwh, 5);
});

test("a malformed compatible response cannot replace the trusted snapshot", async () => {
  let next = snapshot(5);
  const controller = new DashboardController({ source: { load: async () => next } });

  await controller.refresh();
  next = { ...snapshot(99), hourlyCharge: [{ hour: 0, chargeKwh: 4 }] };
  await controller.refresh();
  assert.equal(controller.getState().status, "stale");
  assert.equal(controller.getState().snapshot.metrics.totalChargeKwh, 5);
  assert.match(controller.getState().error, /hourlyCharge/);
});

test("stop followed by immediate start is not blocked by an old request", async () => {
  const first = deferred();
  const second = deferred();
  let loadCount = 0;
  const controller = new DashboardController({
    source: { load: () => { loadCount += 1; return loadCount === 1 ? first.promise : second.promise; } },
    setIntervalFn: () => 1,
    clearIntervalFn: () => {},
  });

  controller.start();
  controller.stop();
  controller.start();
  assert.equal(loadCount, 2);
  second.resolve(snapshot(2));
  await controller.refresh();
  first.resolve(snapshot(1));
  await first.promise;
  assert.equal(controller.getState().snapshot.metrics.totalChargeKwh, 2);
  controller.stop();
});
