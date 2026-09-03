/** Pure data helpers shared by the dashboard controller and its tests. */

export function formatMoneyFromCents(cents) {
  if (!Number.isSafeInteger(cents) || cents < 0) {
    return "--";
  }
  const yuan = Math.floor(cents / 100);
  const remainder = String(cents % 100).padStart(2, "0");
  return `${yuan}.${remainder}`;
}

export function formatNumber(value, fractionDigits = 0) {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    return "--";
  }
  return value.toFixed(fractionDigits);
}

export function selectRevenueRange(points, days) {
  if (days !== 7 && days !== 30) {
    throw new RangeError(`unsupported revenue range: ${days}`);
  }
  if (!Array.isArray(points)) {
    return [];
  }
  return points.slice(-days);
}

export function getPileStatusItems(pileStatus = {}) {
  const definitions = [
    ["idle", "空闲"],
    ["inUse", "在用"],
    ["fault", "故障"],
  ];
  const values = definitions.map(([key]) => {
    const value = pileStatus[key];
    return Number.isSafeInteger(value) && value >= 0 ? value : 0;
  });
  const total = values.reduce((sum, value) => sum + value, 0);
  return definitions.map(([key, name], index) => ({
    key,
    name,
    value: values[index],
    percent: total === 0 ? 0 : Number(((values[index] / total) * 100).toFixed(1)),
  }));
}

export function selectForecast(loadForecast, horizonHours) {
  if (![1, 6, 24].includes(horizonHours)) {
    throw new RangeError(`unsupported forecast horizon: ${horizonHours}`);
  }
  if (loadForecast?.status !== "available" || !Array.isArray(loadForecast.series)) {
    return null;
  }
  return loadForecast.series.find((item) => item.horizonHours === horizonHours) ?? null;
}

function requireObject(value, path) {
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    throw new TypeError(`invalid dashboard field: ${path}`);
  }
  return value;
}

function requireFiniteNumber(value, path, maximum = Number.POSITIVE_INFINITY) {
  if (typeof value !== "number" || !Number.isFinite(value) || value < 0 || value > maximum) {
    throw new TypeError(`invalid dashboard field: ${path}`);
  }
}

function requireNonnegativeInteger(value, path) {
  if (!Number.isSafeInteger(value) || value < 0) {
    throw new TypeError(`invalid dashboard field: ${path}`);
  }
}

function requireZonedDateTime(value, path) {
  if (typeof value !== "string" || !/T.*(?:Z|[+-]\d{2}:\d{2})$/.test(value) || Number.isNaN(Date.parse(value))) {
    throw new TypeError(`invalid dashboard field: ${path}`);
  }
}

export function validateDashboardSnapshot(snapshot) {
  if (!snapshot || typeof snapshot !== "object" || Array.isArray(snapshot)) {
    throw new TypeError("invalid dashboard snapshot");
  }
  if (snapshot.schemaVersion !== "1.0") {
    throw new Error(`incompatible dashboard schema: ${snapshot.schemaVersion ?? "missing"}`);
  }
  requireZonedDateTime(snapshot.generatedAt, "generatedAt");
  if (snapshot.timeZone !== "Asia/Shanghai") throw new TypeError("invalid dashboard field: timeZone");
  if (!["live", "mock"].includes(snapshot.source)) throw new TypeError("invalid dashboard field: source");

  const metrics = requireObject(snapshot.metrics, "metrics");
  requireFiniteNumber(metrics.totalChargeKwh, "metrics.totalChargeKwh");
  requireNonnegativeInteger(metrics.totalRevenueCents, "metrics.totalRevenueCents");
  requireNonnegativeInteger(metrics.activePileCount, "metrics.activePileCount");

  const trend = requireObject(snapshot.revenueTrend, "revenueTrend");
  if (trend.rangeDays !== 30 || !Array.isArray(trend.points) || trend.points.length !== 30) {
    throw new TypeError("invalid dashboard field: revenueTrend");
  }
  let previousDate = "";
  trend.points.forEach((point, index) => {
    requireObject(point, `revenueTrend.points.${index}`);
    if (typeof point.date !== "string" || !/^\d{4}-\d{2}-\d{2}$/.test(point.date) || point.date <= previousDate) {
      throw new TypeError(`invalid dashboard field: revenueTrend.points.${index}.date`);
    }
    previousDate = point.date;
    requireNonnegativeInteger(point.revenueCents, `revenueTrend.points.${index}.revenueCents`);
  });

  const pileStatus = requireObject(snapshot.pileStatus, "pileStatus");
  ["idle", "inUse", "fault"].forEach((key) => requireNonnegativeInteger(pileStatus[key], `pileStatus.${key}`));

  if (!Array.isArray(snapshot.hourlyCharge) || snapshot.hourlyCharge.length !== 24) {
    throw new TypeError("invalid dashboard field: hourlyCharge");
  }
  snapshot.hourlyCharge.forEach((point, hour) => {
    requireObject(point, `hourlyCharge.${hour}`);
    if (point.hour !== hour) throw new TypeError(`invalid dashboard field: hourlyCharge.${hour}.hour`);
    requireFiniteNumber(point.chargeKwh, `hourlyCharge.${hour}.chargeKwh`);
  });

  if (!Array.isArray(snapshot.stationRanking)) throw new TypeError("invalid dashboard field: stationRanking");
  snapshot.stationRanking.forEach((station, index) => {
    requireObject(station, `stationRanking.${index}`);
    requireNonnegativeInteger(station.stationId, `stationRanking.${index}.stationId`);
    if (typeof station.stationName !== "string") throw new TypeError(`invalid dashboard field: stationRanking.${index}.stationName`);
    requireNonnegativeInteger(station.revenueCents, `stationRanking.${index}.revenueCents`);
    requireNonnegativeInteger(station.orderCount, `stationRanking.${index}.orderCount`);
    requireFiniteNumber(station.utilizationPercent, `stationRanking.${index}.utilizationPercent`, 100);
  });

  const forecast = requireObject(snapshot.loadForecast, "loadForecast");
  if (!["available", "unavailable"].includes(forecast.status) || !Array.isArray(forecast.series)) {
    throw new TypeError("invalid dashboard field: loadForecast");
  }
  if (forecast.status === "available") {
    const horizons = forecast.series.map((series) => series.horizonHours).sort((left, right) => left - right);
    if (horizons.join(",") !== "1,6,24") throw new TypeError("invalid dashboard field: loadForecast.series");
    forecast.series.forEach((series, seriesIndex) => {
      if (!Array.isArray(series.points)) throw new TypeError(`invalid dashboard field: loadForecast.series.${seriesIndex}.points`);
      series.points.forEach((point, pointIndex) => {
        requireObject(point, `loadForecast.series.${seriesIndex}.points.${pointIndex}`);
        requireZonedDateTime(point.time, `loadForecast.series.${seriesIndex}.points.${pointIndex}.time`);
        requireFiniteNumber(point.loadKw, `loadForecast.series.${seriesIndex}.points.${pointIndex}.loadKw`);
        requireNonnegativeInteger(point.availablePileCount, `loadForecast.series.${seriesIndex}.points.${pointIndex}.availablePileCount`);
        if (typeof point.isPeak !== "boolean") throw new TypeError(`invalid dashboard field: loadForecast.series.${seriesIndex}.points.${pointIndex}.isPeak`);
      });
    });
  } else if (forecast.series.length !== 0) {
    throw new TypeError("invalid dashboard field: loadForecast.series");
  }
  return snapshot;
}
