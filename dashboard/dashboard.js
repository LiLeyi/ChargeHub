import { formatMoneyFromCents, formatNumber, getPileStatusItems, selectForecast, selectRevenueRange } from "./dashboardmodel.js";
import { DashboardController, HttpDashboardSource, MockDashboardSource } from "./dashboarddata.js";

const byId = (id) => document.getElementById(id);
const colors = { blue: "#4ca9ff", cyan: "#42e0db", green: "#49e29f", orange: "#ffb55e", red: "#ff6b73", muted: "#8da9c3", grid: "rgba(118, 170, 219, 0.12)" };
const chartIds = ["revenuechart", "pilechart", "hourlychart", "forecastchart"];
const charts = new Map(chartIds.map((id) => [id, globalThis.echarts.init(byId(id), null, { renderer: "canvas" })]));
let revenueDays = 7;
let forecastHours = 1;
let currentSnapshot = null;

function commonAxis() {
  return { axisLine: { lineStyle: { color: colors.grid } }, axisTick: { show: false }, axisLabel: { color: colors.muted, fontSize: 10 }, splitLine: { lineStyle: { color: colors.grid } } };
}

function setText(id, value) { byId(id).textContent = value; }

function formatDateTime(value) {
  if (!value) return "--";
  const parsed = new Date(value);
  if (Number.isNaN(parsed.valueOf())) return "--";
  return new Intl.DateTimeFormat("zh-CN", { timeZone: "Asia/Shanghai", month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit", second: "2-digit", hour12: false }).format(parsed);
}

function renderRevenue(snapshot) {
  const points = selectRevenueRange(snapshot.revenueTrend?.points, revenueDays);
  charts.get("revenuechart").setOption({
    animationDuration: 350,
    grid: { left: 18, right: 22, top: 20, bottom: 18, containLabel: true },
    tooltip: { trigger: "axis", renderMode: "richText", valueFormatter: (value) => `¥ ${formatMoneyFromCents(value)}` },
    xAxis: { ...commonAxis(), type: "category", boundaryGap: false, data: points.map((point) => point.date.slice(5)) },
    yAxis: { ...commonAxis(), type: "value", name: "元", nameTextStyle: { color: colors.muted, fontSize: 10 }, axisLabel: { color: colors.muted, formatter: (value) => Math.round(value / 100) } },
    series: [{ name: "营收", type: "line", smooth: true, symbol: "circle", symbolSize: revenueDays === 7 ? 7 : 3, data: points.map((point) => point.revenueCents), lineStyle: { color: colors.blue, width: 2.5 }, itemStyle: { color: colors.cyan, borderColor: "#102b49", borderWidth: 2 }, areaStyle: { color: new globalThis.echarts.graphic.LinearGradient(0, 0, 0, 1, [{ offset: 0, color: "rgba(76, 169, 255, 0.35)" }, { offset: 1, color: "rgba(76, 169, 255, 0.01)" }]) } }],
  }, true);
}

function renderPileStatus(snapshot) {
  const items = getPileStatusItems(snapshot.pileStatus);
  charts.get("pilechart").setOption({
    tooltip: { trigger: "item", renderMode: "richText", formatter: "{b}: {c}台 ({d}%)" },
    color: [colors.green, colors.blue, colors.red],
    series: [{ type: "pie", radius: ["54%", "75%"], center: ["50%", "50%"], label: { show: false }, emphasis: { scaleSize: 5 }, data: items.map((item) => ({ name: item.name, value: item.value })) }],
  }, true);
  const colorValues = [colors.green, colors.blue, colors.red];
  byId("pilelegend").replaceChildren(...items.map((item, index) => {
    const wrapper = document.createElement("div");
    wrapper.className = "pileitem";
    const label = document.createElement("span");
    const dot = document.createElement("i");
    dot.style.backgroundColor = colorValues[index];
    label.append(dot, document.createTextNode(item.name));
    const value = document.createElement("strong");
    value.textContent = `${item.value} 台 · ${item.percent}%`;
    wrapper.append(label, value);
    return wrapper;
  }));
}

function renderHourly(snapshot) {
  const points = Array.isArray(snapshot.hourlyCharge) ? snapshot.hourlyCharge : [];
  charts.get("hourlychart").setOption({
    grid: { left: 18, right: 18, top: 18, bottom: 18, containLabel: true },
    tooltip: { trigger: "axis", renderMode: "richText", valueFormatter: (value) => `${value} kWh` },
    xAxis: { ...commonAxis(), type: "category", data: points.map((point) => `${String(point.hour).padStart(2, "0")}:00`), axisLabel: { color: colors.muted, interval: 2, fontSize: 9 } },
    yAxis: { ...commonAxis(), type: "value", name: "kWh", nameTextStyle: { color: colors.muted, fontSize: 10 } },
    series: [{ name: "充电量", type: "bar", barMaxWidth: 20, data: points.map((point) => point.chargeKwh), itemStyle: { borderRadius: [3, 3, 0, 0], color: new globalThis.echarts.graphic.LinearGradient(0, 0, 0, 1, [{ offset: 0, color: colors.cyan }, { offset: 1, color: "rgba(66, 224, 219, 0.18)" }]) } }],
  }, true);
}

function renderRanking(snapshot) {
  const ranking = Array.isArray(snapshot.stationRanking) ? snapshot.stationRanking : [];
  const rows = ranking.map((station, index) => {
    const row = document.createElement("tr");
    const values = [String(index + 1), station.stationName ?? "--", formatMoneyFromCents(station.revenueCents), Number.isSafeInteger(station.orderCount) ? String(station.orderCount) : "--", formatNumber(station.utilizationPercent, 1) === "--" ? "--" : `${formatNumber(station.utilizationPercent, 1)}%`];
    values.forEach((value) => { const cell = document.createElement("td"); cell.textContent = value; row.append(cell); });
    return row;
  });
  byId("rankingbody").replaceChildren(...rows);
  byId("rankingempty").hidden = rows.length !== 0;
}

function renderForecast(snapshot) {
  const forecast = selectForecast(snapshot.loadForecast, forecastHours);
  const chart = charts.get("forecastchart");
  setText("forecastmodel", `模型版本 ${snapshot.loadForecast?.modelVersion ?? "--"}`);
  byId("forecastempty").hidden = Boolean(forecast);
  if (!forecast) { chart.clear(); byId("forecastfacts").replaceChildren(); return; }
  const points = Array.isArray(forecast.points) ? forecast.points : [];
  chart.setOption({
    grid: { left: 18, right: 18, top: 18, bottom: 20, containLabel: true },
    tooltip: { trigger: "axis", renderMode: "richText", valueFormatter: (value) => `${value} kW` },
    xAxis: { ...commonAxis(), type: "category", data: points.map((point) => formatDateTime(point.time)) },
    yAxis: { ...commonAxis(), type: "value", name: "kW", nameTextStyle: { color: colors.muted } },
    series: [{ name: "预测负荷", type: "line", smooth: true, data: points.map((point) => point.loadKw), lineStyle: { color: colors.orange, width: 2.5 }, itemStyle: { color: colors.orange }, areaStyle: { color: "rgba(255, 181, 94, 0.14)" } }],
  }, true);
  const lastPoint = points.at(-1);
  const facts = [["预测窗口", `${forecastHours} 小时`], ["预计负荷", lastPoint ? `${formatNumber(lastPoint.loadKw, 1)} kW` : "--"], ["预计空闲", lastPoint ? `${lastPoint.availablePileCount ?? "--"} 台` : "--"], ["高峰提示", lastPoint?.isPeak ? "预计高峰" : "普通时段"]].map(([labelText, valueText]) => {
    const item = document.createElement("span");
    item.append(document.createTextNode(labelText));
    const value = document.createElement("strong");
    value.textContent = valueText;
    item.append(value);
    return item;
  });
  byId("forecastfacts").replaceChildren(...facts);
}

function renderSnapshot(snapshot) {
  currentSnapshot = snapshot;
  setText("totalcharge", formatNumber(snapshot.metrics?.totalChargeKwh, 1));
  setText("totalrevenue", formatMoneyFromCents(snapshot.metrics?.totalRevenueCents));
  setText("activepiles", Number.isSafeInteger(snapshot.metrics?.activePileCount) ? snapshot.metrics.activePileCount : "--");
  setText("generatedat", formatDateTime(snapshot.generatedAt));
  setText("sourcebadge", snapshot.source === "mock" ? "演示数据" : "实时数据");
  renderRevenue(snapshot);
  renderPileStatus(snapshot);
  renderHourly(snapshot);
  renderRanking(snapshot);
  renderForecast(snapshot);
}

function renderState(state) {
  const statusLabels = { loading: "正在加载", ready: "数据正常", stale: "数据已过期", error: "加载失败" };
  const statusBadge = byId("statusbadge");
  statusBadge.className = `status ${state.status}`;
  statusBadge.textContent = statusLabels[state.status];
  byId("refreshbutton").disabled = state.status === "loading";
  setText("lastsuccess", formatDateTime(state.lastSuccessAt));
  byId("errorbanner").hidden = !state.error;
  if (state.error) { setText("errortitle", state.status === "stale" ? "刷新失败，正在显示上次数据" : "运营数据暂不可用"); setText("errormessage", state.error); }
  byId("emptystate").hidden = state.status !== "error";
  byId("contentgrid").hidden = state.status === "error";
  if (state.snapshot && state.snapshot !== currentSnapshot) renderSnapshot(state.snapshot);
}

const demoMode = new URLSearchParams(globalThis.location.search).get("demo") === "1";
const source = demoMode ? new MockDashboardSource() : new HttpDashboardSource();
const controller = new DashboardController({ source, onStateChange: renderState });

document.querySelectorAll("[data-revenuedays]").forEach((button) => button.addEventListener("click", () => {
  revenueDays = Number(button.dataset.revenuedays);
  document.querySelectorAll("[data-revenuedays]").forEach((item) => item.classList.toggle("active", item === button));
  if (currentSnapshot) renderRevenue(currentSnapshot);
}));
document.querySelectorAll("[data-forecasthours]").forEach((button) => button.addEventListener("click", () => {
  forecastHours = Number(button.dataset.forecasthours);
  document.querySelectorAll("[data-forecasthours]").forEach((item) => item.classList.toggle("active", item === button));
  if (currentSnapshot) renderForecast(currentSnapshot);
}));
byId("refreshbutton").addEventListener("click", () => { void controller.refresh(); });
byId("retrybutton").addEventListener("click", () => { void controller.refresh(); });
globalThis.addEventListener("resize", () => charts.forEach((chart) => chart.resize()));
globalThis.addEventListener("pagehide", () => controller.stop());
globalThis.addEventListener("pageshow", () => { if (!document.hidden) controller.start(); });
document.addEventListener("visibilitychange", () => { if (document.hidden) controller.stop(); else controller.start(); });
renderState(controller.getState());
if (!document.hidden) controller.start();
