/**
 * ChargeHub 运营大屏：把 Flask 三个只读接口转成 ECharts option。
 *
 * 数据流（答辩画这张）：
 *   GET /api/overview  →  SQLite 营收/桩/闲置（业务库，元）
 *   GET /api/analysis  →  分析表或 Spark 小时负荷/告警
 *   GET /api/bigdata   →  spark_report + charts（多屏主数据）
 *
 * apply() 一次性灌进各 *Opt ref，屏幕组件只收 props，不自己 fetch。
 * 顶栏六页：ops 运营总览 / behavior 行为 / forecast 预测 /
 * users 用户 / quality 质量 / risk 风险调度。
 *
 * 硬约束：没有任何 POST；刷新只重新 GET。
 */
import { computed, onBeforeUnmount, onMounted, ref } from "vue";

const C = {
  cyan: "#3ee0c3",
  gold: "#f4c36a",
  rose: "#ff7b7b",
  blue: "#5ad0ff",
  mint: "#86efac",
  purple: "#8b9cff",
};

/** 坐标轴皮肤：青绿标签 + 淡网格，所有笛卡尔图共用。 */
function axis() {
  return {
    axisLabel: { color: "#7aa8a0", fontSize: 10 },
    axisLine: { lineStyle: { color: "rgba(78,214,196,.18)" } },
    splitLine: { lineStyle: { color: "rgba(78,214,196,.08)" } },
  };
}

/** 安全转数字，NaN/Infinity 用默认值，避免 ECharts 整图空白。 */
function n(v, d = 0) {
  const x = Number(v);
  return Number.isFinite(x) ? x : d;
}

function fmtInt(v) {
  return Math.round(n(v)).toLocaleString("en-US");
}

function fmtKwh(v) {
  const x = n(v);
  return x.toLocaleString("en-US", { maximumFractionDigits: x >= 100 ? 2 : 2 });
}

/** GET JSON；非 2xx 抛错，由 refresh().catch(fail) 变成顶栏告警。 */
async function jget(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url} ${r.status}`);
  return r.json();
}

/** ECharts 公共 grid/tooltip/legend。containLabel 防止轴标签把图挤扁。 */
function base(legend) {
  return {
    textStyle: { color: "#c9ece4", fontSize: 11 },
    grid: { left: 12, right: 16, top: legend ? 28 : 12, bottom: 8, containLabel: true },
    tooltip: { trigger: "axis", confine: true, backgroundColor: "rgba(6,16,20,.92)", borderColor: "rgba(78,214,196,.2)" },
    legend: legend
      ? { data: legend, textStyle: { color: "#8fbfb6", fontSize: 10 }, top: 2, right: 8 }
      : undefined,
  };
}

/** 半环仪表（闲置/负荷）。半径按短边百分比，格子变高时才显得圆。 */
function gaugeOption(name, value, color) {
  return {
    series: [
      {
        type: "gauge",
        startAngle: 210,
        endAngle: -30,
        min: 0,
        max: 100,
        radius: "82%",
        center: ["50%", "54%"],
        progress: { show: true, width: 12, itemStyle: { color } },
        axisLine: { lineStyle: { width: 12, color: [[1, "rgba(78,214,196,.15)"]] } },
        axisTick: { show: false },
        splitLine: { show: false },
        axisLabel: { show: false },
        pointer: { show: false },
        title: { offsetCenter: [0, "32%"], color: "#7aa8a0", fontSize: 11 },
        detail: { valueAnimation: true, fontSize: 20, color, offsetCenter: [0, "-6%"], formatter: "{value}%" },
        data: [{ value, name }],
      },
    ],
  };
}

/**
 * 大屏组合式入口。App.vue 里 reactive(useDashboard()) 一次，所有屏共享。
 *
 * @returns {object} tab/kpis/*Opt/refresh 等，字段名与各 Screen props 对齐
 */
export function useDashboard() {
  const tab = ref("ops");
  const clock = ref("--:--:--");
  const engine = ref("HDFS");
  const dateRange = ref("");
  const kpis = ref([]);
  const ranks = ref([]);
  const alerts = ref([]);
  const errorText = ref("");
  const bestModels = ref([]);
  const portraits = ref([]);
  const radarAxes = ref([]);
  const loadOpt = ref({});
  const heatOpt = ref({});
  const clusterOpt = ref({});
  const rfmOpt = ref({});
  const pileOpt = ref({});
  const featOpt = ref({});
  const idleOpt = ref({});
  const occOpt = ref({});
  const dailyOpt = ref({});
  const platformOpt = ref({});
  const regionOpt = ref({});
  const topOpt = ref({});
  const weekdayOpt = ref({});
  const hourOpt = ref({});
  const socOpt = ref({});
  const durOpt = ref({});
  const feeOpt = ref({});
  const effOpt = ref({});
  const forecastOpt = ref({});
  const testOpt = ref({});
  const modelOpt = ref({});
  const rfmPieOpt = ref({});
  const kCurveOpt = ref({});
  const radarOpt = ref({});
  const rfmScatterOpt = ref({});
  const qualityKpis = ref([]);
  const issueOpt = ref({});
  const batStatusOpt = ref({});
  const funnelOpt = ref({});
  const qTariffOpt = ref({});
  const qFacilityOpt = ref({});
  const voltSocOpt = ref({});
  const socDailyOpt = ref({});
  const tempOpt = ref({});

  let timer = 0;

  const tabs = [
    { id: "ops", label: "运营总览" },
    { id: "behavior", label: "行为分析" },
    { id: "forecast", label: "智能预测" },
    { id: "users", label: "用户分析" },
    { id: "quality", label: "数据质量" },
    { id: "risk", label: "风险调度" },
  ];

  function fail(err) {
    errorText.value = err?.message || String(err || "数据失败");
    alerts.value = [{ level: "错误", title: errorText.value }];
  }

  /**
   * 把 overview + analysis + bigdata 三份 JSON 映射成 KPI 和各图 option。
   * 运营数字优先 charts.kpis / spark kpis；告警优先 Spark alerts。
   * 电站画像用 PCA；效率散点用订单量对数轴 × 均电量，颜色=画像，避免点挤成一团。
   */
  function apply(ov, an, bd) {
    const ch = bd?.charts || {};
    const sparkKpi = { ...(bd?.kpis || {}), ...(ch.kpis || {}) };
    const rp = bd?.report?.mae != null ? bd.report : an?.report || {};
    const ano = bd?.anomalies || {};
    clock.value = ov.updated || clock.value;
    engine.value = sparkKpi.engine || an?.engine || "HDFS";
    const from = sparkKpi.date_from || "";
    const to = sparkKpi.date_to || "";
    dateRange.value = from && to ? `${from} 至 ${to}` : "";
    const hint = dateRange.value;
    kpis.value = [
      { key: "orders", label: "有效充电订单", value: fmtInt(sparkKpi.orders || sparkKpi.sessions), hint, color: C.cyan },
      { key: "kwh", label: "累计充电量", value: `${fmtKwh(sparkKpi.kwh_total)} kWh`, hint, color: C.cyan },
      { key: "users", label: "服务用户", value: fmtInt(sparkKpi.users || ov.users), hint: `KMeans 最优 K=${sparkKpi.clusters || 4}`, color: C.blue },
      { key: "piles", label: "充电站数", value: fmtInt(sparkKpi.stations), hint: `会话 ${(ov.piles || []).reduce((s, p) => s + n(p.n), 0)} 桩`, color: C.gold },
      {
        key: "mins",
        label: "平均充电时长",
        value: `${n(sparkKpi.avg_minutes).toFixed(1)} 分钟`,
        hint: `晚高峰 SOC ${n(sparkKpi.peak_hour)}:00`,
        color: C.mint,
      },
      {
        key: "mod",
        label: "分析与模型",
        value: `${sparkKpi.modules || 12} + ${sparkKpi.models || 3} 模块`,
        hint: `异常 ${Math.round(n(ano.count != null ? ano.count : sparkKpi.anomalies))} · MAE ${n(rp.mae).toFixed(2)}`,
        color: C.gold,
      },
    ];

    const mix = (ov.mix || []).map((s) => ({ name: s.name, value: Math.round(n(s.value)) }));
    const maxMix = Math.max(1, ...mix.map((s) => s.value));
    ranks.value = mix.map((s) => ({ ...s, pct: Math.round((s.value / maxMix) * 100) }));

    let idle = 0;
    let total = 0;
    (ov.idleRank || []).forEach((s) => {
      idle += n(s.idle);
      total += n(s.total);
    });
    const idlePct = total ? Math.round((idle / total) * 100) : 0;
    const loadPct = Math.max(8, Math.min(96, 100 - idlePct));
    idleOpt.value = gaugeOption("闲置", idlePct, C.cyan);
    occOpt.value = gaugeOption("负荷", loadPct, C.gold);

    const rawAlerts = bd?.alerts?.length ? bd.alerts : an?.alerts || [];
    alerts.value = rawAlerts.length
      ? rawAlerts.map((a) => ({ level: a.level || "提示", title: a.title || "" }))
      : [{ level: "提示", title: "运行平稳" }];
    errorText.value = "";

    const hourly = bd?.hourly?.length ? bd.hourly : an?.hourly || [];
    const hs = [...Array(24).keys()].map((h) => {
      const row = hourly.find((x) => Number(x.hour) === h) || {};
      return { x: String(h).padStart(2, "0"), pred: n(row.pred_kwh), hist: n(row.hist_kwh) };
    });
    loadOpt.value = {
      ...base(["预测", "历史"]),
      xAxis: { type: "category", data: hs.map((i) => i.x), ...axis() },
      yAxis: { type: "value", ...axis() },
      series: [
        {
          name: "预测",
          type: "line",
          smooth: true,
          symbol: "none",
          data: hs.map((i) => i.pred),
          lineStyle: { color: C.gold, width: 2 },
          areaStyle: { color: "rgba(244,195,106,.16)" },
        },
        {
          name: "历史",
          type: "bar",
          data: hs.map((i) => i.hist),
          itemStyle: { color: "rgba(62,224,195,.35)", borderRadius: [2, 2, 0, 0] },
        },
      ],
    };

    const wds = ["Mon", "Tues", "Wed", "Thurs", "Fri", "Sat", "Sun"];
    const heatData = (bd.heatmap || [])
      .map((h) => [Number(h.hour), wds.indexOf(h.weekday), n(h.kwh)])
      .filter((d) => d[1] >= 0);
    const maxH = heatData.reduce((m, d) => Math.max(m, d[2]), 1);
    heatOpt.value = {
      textStyle: { color: "#c9ece4", fontSize: 11 },
      tooltip: { formatter: (p) => `${wds[p.data[1]]} ${p.data[0]}:00  ${p.data[2]}` },
      grid: { left: 8, right: 8, top: 6, bottom: 4, containLabel: true },
      xAxis: {
        type: "category",
        data: [...Array(24).keys()].map((i) => String(i).padStart(2, "0")),
        ...axis(),
      },
      yAxis: { type: "category", data: ["一", "二", "三", "四", "五", "六", "日"], ...axis() },
      visualMap: { min: 0, max: maxH, show: false, inRange: { color: ["#0a1c22", "#1a6b62", C.cyan, C.gold] } },
      series: [{ type: "heatmap", data: heatData, itemStyle: { borderWidth: 1, borderColor: "#061014" } }],
    };

    const palette = { 晚高峰型: C.gold, 均衡高负荷型: C.cyan, 慢充短时型: C.blue, 低频型: C.mint };
    const clusters = bd.clusters || [];
    const usePca = clusters.some((c) => c.pca_x != null && c.pca_y != null);
    const xs = [];
    const ys = [];
    const grouped = {};
    clusters.forEach((c) => {
      const x = usePca ? n(c.pca_x) : n(c.sessions);
      const y = usePca ? n(c.pca_y) : n(c.avg_kwh);
      xs.push(x);
      ys.push(y);
      const lab = c.label || "其他";
      if (!grouped[lab]) grouped[lab] = [];
      grouped[lab].push({ name: c.name, value: [x, y, c.name, lab, c.sessions] });
    });
    const pad = (arr) => {
      if (!arr.length) return { min: -1, max: 1 };
      const mn = Math.min(...arr);
      const mx = Math.max(...arr);
      const span = Math.max(0.2, mx - mn);
      return { min: mn - span * 0.18, max: mx + span * 0.18 };
    };
    const xr = pad(xs);
    const yr = pad(ys);
    clusterOpt.value = {
      ...base(Object.keys(grouped)),
      tooltip: {
        trigger: "item",
        confine: true,
        formatter: (p) => {
          const v = p.value || [];
          return `${v[2] || ""}<br/>${v[3] || ""}${v[4] != null ? ` · ${v[4]} 次` : ""}`;
        },
      },
      xAxis: {
        type: "value",
        min: xr.min,
        max: xr.max,
        scale: true,
        name: usePca ? "PC1" : "会话",
        nameTextStyle: { color: "#7aa8a0", fontSize: 10 },
        ...axis(),
      },
      yAxis: {
        type: "value",
        min: yr.min,
        max: yr.max,
        scale: true,
        name: usePca ? "PC2" : "kWh",
        nameTextStyle: { color: "#7aa8a0", fontSize: 10 },
        ...axis(),
      },
      series: Object.keys(grouped).map((lab) => ({
        name: lab,
        type: "scatter",
        clip: true,
        symbolSize: (v) => 8 + Math.min(12, Math.log10((v[4] || 1) + 1) * 3.2),
        itemStyle: { color: palette[lab] || C.cyan },
        data: grouped[lab],
      })),
    };

    const rfm = (bd.rfm || []).map((r) => ({ name: r.name, value: n(r.value) }));
    rfmOpt.value = {
      ...base(),
      tooltip: { trigger: "axis", confine: true },
      xAxis: { type: "value", ...axis() },
      yAxis: { type: "category", data: rfm.map((r) => r.name).reverse(), ...axis() },
      series: [{ type: "bar", data: rfm.map((r) => r.value).reverse(), itemStyle: { borderRadius: [0, 8, 8, 0], color: C.gold } }],
    };

    const piles = (ov.piles || []).map((p) => ({ name: p.status, value: n(p.n) }));
    pileOpt.value = {
      textStyle: { color: "#c9ece4", fontSize: 11 },
      tooltip: { trigger: "item" },
      series: [
        {
          type: "pie",
          radius: ["38%", "62%"],
          center: ["50%", "50%"],
          label: { color: "#c9ece4", fontSize: 10 },
          data: piles.map((p, i) => ({ ...p, itemStyle: { color: [C.cyan, C.gold, C.rose, C.blue][i % 4] } })),
        },
      ],
    };

    const imps = (bd.importances || []).slice(0, 6).map((i) => ({
      name: String(i.name || "").slice(0, 8),
      value: Math.round(n(i.value) * 1000) / 10,
    }));
    featOpt.value = {
      ...base(),
      xAxis: { type: "value", ...axis() },
      yAxis: { type: "category", data: imps.map((i) => i.name).reverse(), ...axis() },
      series: [{ type: "bar", data: imps.map((i) => i.value).reverse(), itemStyle: { color: C.blue, borderRadius: [0, 4, 4, 0] } }],
    };

    const daily = ch.daily || [];
    dailyOpt.value = {
      ...base(["订单数", "充电量"]),
      xAxis: { type: "category", data: daily.map((d) => d.date), boundaryGap: true, ...axis() },
      yAxis: [
        { type: "value", name: "单", nameTextStyle: { color: "#7aa8a0" }, ...axis() },
        { type: "value", name: "kWh", nameTextStyle: { color: "#7aa8a0" }, ...axis() },
      ],
      series: [
        {
          name: "订单数",
          type: "line",
          smooth: true,
          symbol: "none",
          data: daily.map((d) => d.orders),
          lineStyle: { color: C.cyan, width: 2 },
          areaStyle: { color: "rgba(62,224,195,.12)" },
        },
        {
          name: "充电量",
          type: "line",
          yAxisIndex: 1,
          smooth: true,
          symbol: "none",
          data: daily.map((d) => d.kwh),
          lineStyle: { color: C.blue, width: 2 },
        },
      ],
    };

    const plats = ch.platform || bd.platform || [];
    const platColor = { IOS: C.cyan, ANDROID: C.purple, WEB: "#6ec6ff" };
    platformOpt.value = {
      textStyle: { color: "#c9ece4", fontSize: 11 },
      tooltip: { trigger: "item" },
      legend: { bottom: 2, textStyle: { color: "#8fbfb6", fontSize: 10 } },
      series: [
        {
          type: "pie",
          radius: ["48%", "72%"],
          center: ["50%", "46%"],
          label: { color: "#c9ece4", formatter: "{b} {d}%" },
          data: plats.map((p) => ({
            name: p.name,
            value: p.value,
            itemStyle: { color: platColor[String(p.name).toUpperCase()] || C.gold },
          })),
        },
      ],
    };

    const regions = (ch.regions || []).slice(0, 8);
    regionOpt.value = {
      ...base(["订单数", "充电量"]),
      xAxis: { type: "category", data: regions.map((r) => r.name), axisLabel: { ...axis().axisLabel, rotate: 20 }, ...axis() },
      yAxis: [
        { type: "value", ...axis() },
        { type: "value", ...axis() },
      ],
      series: [
        {
          name: "订单数",
          type: "bar",
          data: regions.map((r) => r.orders),
          itemStyle: { color: C.blue, borderRadius: [3, 3, 0, 0] },
        },
        {
          name: "充电量",
          type: "line",
          yAxisIndex: 1,
          smooth: true,
          data: regions.map((r) => r.kwh),
          lineStyle: { color: C.gold, width: 2 },
          itemStyle: { color: C.gold },
        },
      ],
    };

    const topRows = (bd.topStations || []).slice(0, 8);
    topOpt.value = {
      ...base(),
      tooltip: { trigger: "axis", confine: true },
      grid: { left: 8, right: 28, top: 8, bottom: 4, containLabel: true },
      xAxis: { type: "value", name: "kWh", ...axis() },
      yAxis: { type: "category", data: topRows.map((t) => t.name).reverse(), axisLabel: { width: 120, overflow: "truncate", ...axis().axisLabel }, ...axis() },
      series: [
        {
          type: "bar",
          data: topRows.map((t) => n(t.kwh)).reverse(),
          itemStyle: { color: C.blue, borderRadius: [0, 6, 6, 0] },
        },
      ],
    };

    const wday = ch.weekday || [];
    weekdayOpt.value = {
      ...base(["订单数"]),
      xAxis: { type: "category", data: wday.map((w) => w.name), ...axis() },
      yAxis: { type: "value", ...axis() },
      series: [
        {
          name: "订单数",
          type: "bar",
          data: wday.map((w) => w.orders),
          itemStyle: { color: C.gold, borderRadius: [4, 4, 0, 0] },
        },
      ],
    };

    const hours = ch.hours || [];
    hourOpt.value = {
      ...base(["订单数", "充电量"]),
      xAxis: { type: "category", data: hours.map((h) => h.hour), ...axis() },
      yAxis: [
        { type: "value", ...axis() },
        { type: "value", ...axis() },
      ],
      series: [
        {
          name: "订单数",
          type: "bar",
          data: hours.map((h) => h.orders),
          itemStyle: { color: C.blue, borderRadius: [3, 3, 0, 0] },
        },
        {
          name: "充电量",
          type: "line",
          yAxisIndex: 1,
          smooth: true,
          symbol: "circle",
          symbolSize: 6,
          data: hours.map((h) => h.kwh),
          lineStyle: { color: C.gold, width: 2 },
          itemStyle: { color: C.gold },
        },
      ],
    };

    const barCat = (rows, color) => ({
      ...base(),
      xAxis: { type: "category", data: rows.map((r) => r.name), axisLabel: { ...axis().axisLabel, rotate: 18 }, ...axis() },
      yAxis: { type: "value", ...axis() },
      series: [{ type: "bar", data: rows.map((r) => r.value), itemStyle: { color, borderRadius: [4, 4, 0, 0] } }],
    });
    socOpt.value = barCat(ch.soc || [], C.mint);
    durOpt.value = barCat(ch.duration || [], C.purple);
    feeOpt.value = barCat(ch.fees || [], C.gold);

    const clusterByName = {};
    (bd.clusters || []).forEach((c) => {
      if (c?.name) clusterByName[c.name] = c;
    });
    const effPalette = { 晚高峰型: C.gold, 均衡高负荷型: C.cyan, 慢充短时型: C.blue, 低频型: C.mint };
    const effGroups = {};
    const effXs = [];
    const effYs = [];
    (ch.efficiency || []).forEach((e) => {
      const lab = clusterByName[e.name]?.label || e.region || "其他";
      if (!effGroups[lab]) effGroups[lab] = [];
      const x = Math.max(1, n(e.orders));
      const y = n(e.avg_kwh);
      effXs.push(x);
      effYs.push(y);
      effGroups[lab].push({
        name: e.name,
        value: [x, y, e.name, lab, n(e.avg_hrs), n(e.orders)],
      });
    });
    const yrEff = pad(effYs);
    const xmin = effXs.length ? Math.max(1, Math.min(...effXs) * 0.72) : 1;
    const xmax = effXs.length ? Math.max(...effXs) * 1.15 : 10;
    effOpt.value = {
      ...base(Object.keys(effGroups)),
      tooltip: {
        trigger: "item",
        confine: true,
        formatter: (p) => {
          const v = p.value || [];
          return `${v[2] || ""}<br/>${v[3] || ""} · ${v[5] || 0} 单<br/>均电量 ${v[1]} kWh · 均时长 ${v[4]} h`;
        },
      },
      xAxis: {
        type: "log",
        name: "订单量",
        nameLocation: "middle",
        nameGap: 22,
        min: xmin,
        max: xmax,
        nameTextStyle: { color: "#7aa8a0", fontSize: 10 },
        ...axis(),
      },
      yAxis: {
        type: "value",
        name: "平均电量 kWh",
        scale: true,
        min: yrEff.min,
        max: yrEff.max,
        nameTextStyle: { color: "#7aa8a0", fontSize: 10 },
        ...axis(),
      },
      series: Object.keys(effGroups).map((lab) => ({
        name: lab,
        type: "scatter",
        symbolSize: (v) => 8 + Math.min(18, Math.log10((v[5] || 1) + 1) * 5),
        itemStyle: { color: effPalette[lab] || C.purple, opacity: 0.82 },
        data: effGroups[lab],
      })),
    };

    const f7 = ch.forecast7 || [];
    forecastOpt.value = {
      ...base(["实际订单", "预测充电量"]),
      xAxis: { type: "category", data: f7.map((d) => d.date), ...axis() },
      yAxis: [
        { type: "value", ...axis() },
        { type: "value", ...axis() },
      ],
      series: [
        {
          name: "实际订单",
          type: "bar",
          data: f7.map((d) => (d.future ? null : d.orders)),
          itemStyle: { color: C.blue, borderRadius: [3, 3, 0, 0] },
        },
        {
          name: "预测充电量",
          type: "line",
          yAxisIndex: 1,
          smooth: true,
          data: f7.map((d) => d.kwh),
          lineStyle: { color: C.gold, width: 2 },
          itemStyle: { color: C.gold },
        },
      ],
    };

    const predO = ch.daily_pred_orders || [];
    testOpt.value = {
      ...base(["实际值", "预测值"]),
      xAxis: { type: "category", data: daily.map((d) => d.date), ...axis() },
      yAxis: { type: "value", ...axis() },
      series: [
        {
          name: "实际值",
          type: "line",
          smooth: true,
          symbol: "none",
          data: daily.map((d) => d.orders),
          lineStyle: { color: C.cyan, width: 2 },
        },
        {
          name: "预测值",
          type: "line",
          smooth: true,
          symbol: "none",
          data: predO,
          lineStyle: { color: C.rose, width: 2, type: "dashed" },
        },
      ],
    };

    const models = (bd.models || []).map((m) => ({
      name: m.name,
      mae: n(m.mae),
      rmse: n(m.rmse),
      r2: n(m.r2),
    }));
    if (ch.order_model) {
      models.push({ name: "订单季节", mae: n(ch.order_model.mae), rmse: n(ch.order_model.rmse), r2: n(ch.order_model.r2) });
    }
    modelOpt.value = {
      ...base(["MAE", "RMSE", "R²"]),
      xAxis: { type: "category", data: models.map((m) => m.name), ...axis() },
      yAxis: [
        { type: "value", name: "误差", ...axis() },
        { type: "value", name: "R²", min: 0, max: 1, ...axis() },
      ],
      series: [
        { name: "MAE", type: "bar", data: models.map((m) => m.mae), itemStyle: { color: C.purple } },
        { name: "RMSE", type: "bar", data: models.map((m) => m.rmse), itemStyle: { color: C.gold } },
        { name: "R²", type: "line", yAxisIndex: 1, data: models.map((m) => m.r2), itemStyle: { color: C.mint }, lineStyle: { color: C.mint } },
      ],
    };

    bestModels.value = ch.best_models || [];
    const extra = ch.rfm_extra || {};
    portraits.value = extra.portraits || [];
    radarAxes.value = extra.axes || ["平均充电量", "消费频次", "消费金额", "活跃度", "RFM综合"];
    const rmix = extra.mix || rfm;
    const pieColor = { 高价值用户: C.mint, 流失预警用户: C.rose, 常规用户: C.cyan, 高价值: C.mint, 沉默: C.rose, 常规: C.cyan };
    rfmPieOpt.value = {
      textStyle: { color: "#c9ece4", fontSize: 11 },
      tooltip: { trigger: "item" },
      legend: { bottom: 0, textStyle: { color: "#8fbfb6", fontSize: 10 } },
      series: [
        {
          type: "pie",
          radius: ["42%", "68%"],
          center: ["50%", "44%"],
          label: { color: "#c9ece4", formatter: "{b}\n{d}%" },
          data: rmix.map((p) => ({ name: p.name, value: p.value, itemStyle: { color: pieColor[p.name] || C.blue } })),
        },
      ],
    };

    const kc = ch.k_curve || [];
    kCurveOpt.value = {
      ...base(),
      xAxis: { type: "category", data: kc.map((k) => `K=${k.k}`), ...axis() },
      yAxis: { type: "value", min: 0, max: 1, ...axis() },
      series: [
        {
          type: "line",
          smooth: true,
          symbol: "circle",
          symbolSize: 8,
          data: kc.map((k) => k.silhouette),
          lineStyle: { color: C.gold, width: 2 },
          itemStyle: { color: C.gold },
          areaStyle: { color: "rgba(244,195,106,.12)" },
        },
      ],
    };

    const radarRows = extra.radar || [];
    radarOpt.value = {
      textStyle: { color: "#c9ece4", fontSize: 11 },
      legend: { bottom: 0, textStyle: { color: "#8fbfb6", fontSize: 10 } },
      radar: {
        indicator: radarAxes.value.map((name) => ({ name, max: 100 })),
        axisName: { color: "#8fbfb6", fontSize: 10 },
        splitLine: { lineStyle: { color: "rgba(78,214,196,.18)" } },
        splitArea: { areaStyle: { color: ["rgba(8,24,32,.4)", "rgba(8,24,32,.15)"] } },
      },
      series: [
        {
          type: "radar",
          data: radarRows.map((r, i) => ({
            name: r.name,
            value: r.value,
            lineStyle: { color: [C.cyan, C.rose, C.gold][i % 3] },
            itemStyle: { color: [C.cyan, C.rose, C.gold][i % 3] },
            areaStyle: { opacity: 0.18 },
          })),
        },
      ],
    };

    const sc = extra.scatter || [];
    const byName = {};
    sc.forEach((p) => {
      if (!byName[p.name]) byName[p.name] = [];
      byName[p.name].push(p.value);
    });
    rfmScatterOpt.value = {
      ...base(Object.keys(byName)),
      tooltip: { trigger: "item", formatter: (p) => `${p.seriesName}<br/>R ${p.value[0]} · F ${p.value[1]} · M ${p.value[2]}` },
      xAxis: { type: "value", name: "Recency / 天", ...axis() },
      yAxis: { type: "value", name: "Frequency / 次", ...axis() },
      series: Object.keys(byName).map((name, i) => ({
        name,
        type: "scatter",
        symbolSize: (v) => 7 + Math.min(16, Math.log10((v[2] || 1) + 1) * 3),
        itemStyle: { color: [C.cyan, C.rose, C.gold][i % 3], opacity: 0.8 },
        data: byName[name],
      })),
    };

    const q = ch.quality || {};
    qualityKpis.value = [
      { key: "raw", label: "原始会话", value: fmtInt(q.raw_sessions), hint: "nvv2t 真实表", color: C.cyan },
      { key: "ok", label: "清洗后有效", value: fmtInt(q.valid_orders), hint: "剔除电量0 / 时长异常", color: C.mint },
      { key: "pass", label: "会话通过率", value: `${n(q.pass_rate).toFixed(1)}%`, hint: "按清洗需求表", color: C.gold },
      { key: "bat", label: "有效电池记录", value: `${n(q.battery_valid_rate).toFixed(1)}%`, hint: "电压>0 的 dsv13r2", color: C.blue },
      { key: "free", label: "免费订单", value: fmtInt(q.free_orders), hint: "费用为 0，需单独统计", color: C.rose },
      { key: "paid", label: "付费订单", value: fmtInt(q.paid_orders), hint: "真实样本付费单", color: C.gold },
    ];
    const issues = (q.issues || []).filter((i) => n(i.value) > 0);
    issueOpt.value = {
      ...base(),
      grid: { left: 8, right: 16, top: 8, bottom: 8, containLabel: true },
      xAxis: { type: "value", ...axis() },
      yAxis: { type: "category", data: issues.map((i) => i.name).reverse(), ...axis() },
      series: [
        {
          type: "bar",
          data: issues.map((i) => i.value).reverse(),
          itemStyle: { color: C.rose, borderRadius: [0, 6, 6, 0] },
        },
      ],
    };
    batStatusOpt.value = {
      textStyle: { color: "#c9ece4", fontSize: 11 },
      tooltip: { trigger: "item" },
      legend: { bottom: 2, textStyle: { color: "#8fbfb6", fontSize: 10 } },
      series: [
        {
          type: "pie",
          radius: ["42%", "68%"],
          center: ["50%", "46%"],
          label: { color: "#c9ece4", formatter: "{b}\n{d}%" },
          data: (q.status || []).map((p, i) => ({
            ...p,
            itemStyle: { color: [C.rose, C.gold, C.cyan][i % 3] },
          })),
        },
      ],
    };
    funnelOpt.value = {
      ...base(),
      xAxis: { type: "category", data: (q.funnel || []).map((f) => f.name), ...axis() },
      yAxis: { type: "value", ...axis() },
      series: [
        {
          type: "bar",
          data: (q.funnel || []).map((f) => f.value),
          itemStyle: { color: C.blue, borderRadius: [4, 4, 0, 0] },
        },
      ],
    };
    qTariffOpt.value = {
      ...base(),
      xAxis: { type: "category", data: (q.tariff || []).map((t) => t.name), ...axis() },
      yAxis: { type: "value", ...axis() },
      series: [
        {
          type: "bar",
          data: (q.tariff || []).map((t) => t.value),
          itemStyle: {
            color: (p) => [C.mint, C.cyan, C.gold, C.rose][p.dataIndex % 4],
            borderRadius: [4, 4, 0, 0],
          },
        },
      ],
    };
    const facColor = { 交流: C.mint, 直流: C.blue, 交直流: C.cyan, 超充: C.gold };
    qFacilityOpt.value = {
      textStyle: { color: "#c9ece4", fontSize: 11 },
      tooltip: { trigger: "item" },
      legend: { bottom: 2, textStyle: { color: "#8fbfb6", fontSize: 10 } },
      series: [
        {
          type: "pie",
          radius: ["42%", "68%"],
          center: ["50%", "46%"],
          label: { color: "#c9ece4", formatter: "{b} {d}%" },
          data: (q.facility || []).map((p) => ({
            ...p,
            itemStyle: { color: facColor[p.name] || C.purple },
          })),
        },
      ],
    };
    voltSocOpt.value = {
      ...base(),
      tooltip: { trigger: "item", formatter: (p) => `SOC ${p.value[0]}%<br/>电压 ${p.value[1]} V<br/>电流 ${p.value[2]} A` },
      xAxis: { type: "value", name: "SOC %", ...axis() },
      yAxis: { type: "value", name: "包电压 V", ...axis() },
      series: [
        {
          type: "scatter",
          symbolSize: 8,
          itemStyle: { color: C.cyan, opacity: 0.7 },
          data: q.scatter || [],
        },
      ],
    };
    const sd = q.soc_daily || [];
    socDailyOpt.value = {
      ...base(["平均 SOC", "平均电压"]),
      xAxis: { type: "category", data: sd.map((d) => d.date), ...axis() },
      yAxis: [
        { type: "value", name: "SOC", ...axis() },
        { type: "value", name: "V", ...axis() },
      ],
      series: [
        {
          name: "平均 SOC",
          type: "line",
          smooth: true,
          symbol: "none",
          data: sd.map((d) => d.soc),
          lineStyle: { color: C.mint, width: 2 },
        },
        {
          name: "平均电压",
          type: "line",
          yAxisIndex: 1,
          smooth: true,
          symbol: "none",
          data: sd.map((d) => d.volt),
          lineStyle: { color: C.gold, width: 2 },
        },
      ],
    };
    tempOpt.value = barCat(q.temp || [], C.gold);
  }

  /** 并行拉三个只读接口；失败只改告警文案，不写库。 */
  async function refresh() {
    try {
      const [ov, an, bd] = await Promise.all([jget("/api/overview"), jget("/api/analysis"), jget("/api/bigdata")]);
      apply(ov || {}, an || {}, bd || {});
    } catch (err) {
      fail(err);
    }
  }

  onMounted(() => {
    refresh();
    timer = window.setInterval(refresh, 12000);
  });
  onBeforeUnmount(() => clearInterval(timer));

  return {
    tab,
    tabs,
    clock,
    engine,
    dateRange,
    kpis,
    qualityKpis,
    ranks,
    alerts,
    bestModels,
    portraits,
    errorText,
    refresh,
    loadOpt,
    heatOpt,
    clusterOpt,
    rfmOpt,
    pileOpt,
    featOpt,
    idleOpt,
    occOpt,
    dailyOpt,
    platformOpt,
    regionOpt,
    topOpt,
    weekdayOpt,
    hourOpt,
    socOpt,
    durOpt,
    feeOpt,
    effOpt,
    forecastOpt,
    testOpt,
    modelOpt,
    rfmPieOpt,
    kCurveOpt,
    radarOpt,
    rfmScatterOpt,
    issueOpt,
    batStatusOpt,
    funnelOpt,
    qTariffOpt,
    qFacilityOpt,
    voltSocOpt,
    socDailyOpt,
    tempOpt,
    ready: computed(() => kpis.value.length > 0),
  };
}
