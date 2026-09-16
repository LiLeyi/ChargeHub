/**
 * ChargeHub 运营大屏的数据层（组合式函数）。
 *
 * 【职责】可视化同学主讲本文件。把 Flask 三个只读 GET 转成 ECharts option
 * 和 KPI 文案。屏幕 `.vue` 只收 props，禁止自己 fetch、禁止改 series。
 *
 * 【数据流】答辩画这张：
 *   GET /api/overview  →  SQLite 营收 / 桩状态 / 闲置（业务库，金额单位元）
 *   GET /api/analysis  →  分析表或 Spark 小时负荷 / 告警（无 Spark JSON 时的回退）
 *   GET /api/bigdata   →  spark_report + charts（六页主数据）
 *   apply(ov, an, bd)  →  一次写入全部 *Opt / kpis / ranks / alerts
 *
 * 【页签】ops 运营总览 / behavior 行为 / forecast 预测 /
 * users 用户 / quality 质量 / risk 风险调度。切页不重新请求；
 * 点「刷新数据」或 12 秒定时器才再 GET。质量页顶栏换成 qualityKpis。
 *
 * 【硬约束】没有任何 POST / PUT。失败只改告警文案，不写库。
 *
 * 【接口字段】见 docs/接口约定.md。键变了只改 apply()，不要改 Screen。
 */
import { computed, onBeforeUnmount, onMounted, ref } from "vue";

/** 全图共用色板：青=主指标，金=预测/排行，玫瑰=告警/流失，蓝=对照柱。 */
const C = {
  cyan: "#3ee0c3",
  gold: "#f4c36a",
  rose: "#ff7b7b",
  blue: "#5ad0ff",
  mint: "#86efac",
  purple: "#8b9cff",
};

/**
 * 笛卡尔坐标系皮肤。所有柱/线/散点共用，保证六页配色一致。
 * @returns {{axisLabel: object, axisLine: object, splitLine: object}}
 */
function axis() {
  return {
    axisLabel: { color: "#7aa8a0", fontSize: 10 },
    axisLine: { lineStyle: { color: "rgba(78,214,196,.18)" } },
    splitLine: { lineStyle: { color: "rgba(78,214,196,.08)" } },
  };
}

/**
 * 安全转数字。JSON 缺字段、字符串、NaN 时用默认值，避免 ECharts 整图空白。
 * @param {*} v 原始值
 * @param {number} [d=0] 回退
 * @returns {number}
 */
function n(v, d = 0) {
  const x = Number(v);
  return Number.isFinite(x) ? x : d;
}

/** 整数千分位，给 KPI 大数字用。 */
function fmtInt(v) {
  return Math.round(n(v)).toLocaleString("en-US");
}

/** 电量保留两位小数 + 千分位。 */
function fmtKwh(v) {
  const x = n(v);
  return x.toLocaleString("en-US", { maximumFractionDigits: x >= 100 ? 2 : 2 });
}

/**
 * 只读 GET JSON。非 2xx 抛错，由 refresh() 的 catch → fail() 写成告警，不写库。
 * @param {string} url 相对路径，如 `/api/overview`（Vite 开发时代理到 :5000）
 */
async function jget(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url} ${r.status}`);
  return r.json();
}

/**
 * ECharts 公共 grid / tooltip / legend。
 * containLabel:true 让轴标签占在 grid 内，防止中文站名把图画没。
 * @param {string[]|undefined} legend 图例名；不传则不要 legend 条，给 grid 多留高度
 */
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

/**
 * 半环仪表（风险页闲置率 / 负荷率）。
 * 半径按容器短边百分比，格子够高才显得圆；指针关掉，只看进度弧。
 * @param {string} name 中心下方标题
 * @param {number} value 0–100
 * @param {string} color 进度弧颜色
 */
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
 * 大屏组合式入口。App.vue 里 `reactive(useDashboard())` 只调一次，六页共享同一份状态。
 *
 * 返回值字段名必须与各 Screen 的 props 对齐（如 dailyOpt → OpsScreen）。
 * 切 tab 只改 `tab`，不重新 apply；刷新走 refresh()。
 *
 * @returns {object} tab / tabs / kpis / qualityKpis / *Opt / refresh
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
  /* 以下 *Opt 全部是 ECharts option。命名必须与对应 Screen 的 props 一致。 */
  /* 风险页 */
  const loadOpt = ref({});
  const heatOpt = ref({});
  const clusterOpt = ref({});
  const rfmOpt = ref({});
  const pileOpt = ref({});
  const featOpt = ref({});
  const idleOpt = ref({});
  const occOpt = ref({});
  /* 运营总览 */
  const dailyOpt = ref({});
  const platformOpt = ref({});
  const regionOpt = ref({});
  const topOpt = ref({});
  const weekdayOpt = ref({});
  /* 充电行为 */
  const hourOpt = ref({});
  const socOpt = ref({});
  const durOpt = ref({});
  const feeOpt = ref({});
  const effOpt = ref({});
  /* 智能预测 */
  const forecastOpt = ref({});
  const testOpt = ref({});
  const modelOpt = ref({});
  /* 用户分析 */
  const rfmPieOpt = ref({});
  const kCurveOpt = ref({});
  const radarOpt = ref({});
  const rfmScatterOpt = ref({});
  /* 数据质量（口径问清洗，本层只画） */
  const qualityKpis = ref([]);
  const issueOpt = ref({});
  const batStatusOpt = ref({});
  const funnelOpt = ref({});
  const qTariffOpt = ref({});
  const qFacilityOpt = ref({});
  const voltSocOpt = ref({});
  const socDailyOpt = ref({});
  const tempOpt = ref({});

  /** 12 秒轮询句柄；卸载时 clear，避免切走大屏后仍打接口。 */
  let timer = 0;

  /** 顶栏六页签。id 就是 App.vue 里 v-if 的 dash.tab。 */
  const tabs = [
    { id: "ops", label: "运营总览" },
    { id: "behavior", label: "行为分析" },
    { id: "forecast", label: "智能预测" },
    { id: "users", label: "用户分析" },
    { id: "quality", label: "数据质量" },
    { id: "risk", label: "风险调度" },
  ];

  /** 接口失败：只改告警列表，不改已画出的 option，页面不白屏。 */
  function fail(err) {
    errorText.value = err?.message || String(err || "数据失败");
    alerts.value = [{ level: "错误", title: errorText.value }];
  }

  /**
   * 把 overview + analysis + bigdata 三份 JSON 映射成 KPI 和各图 option。
   *
   * 优先级：运营数字优先 charts.kpis / spark kpis；告警优先 Spark alerts；
   * 小时负荷优先 bd.hourly，没有则 an.hourly。
   * 电站画像：有 pca_x/pca_y 用 PCA 两轴，否则回退「会话 × 均电量」。
   * 效率散点：横轴 log(订单量)、纵轴均电量、颜色=画像。不能用均时长×均电量
   * （扩样后那两维几乎常数，点会挤成一团）。
   *
   * @param {object} ov GET /api/overview
   * @param {object} an GET /api/analysis
   * @param {object} bd GET /api/bigdata（含 report 键与 charts 扁平表）
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
    // 顶栏 6 张 KPI（质量页会被 qualityKpis 替换）。订单/电量来自 Spark，桩数来自业务库。
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

    // 风险页营收排行：pct 按最大值归一成 0–100，RankList 用它画底条宽度。
    const mix = (ov.mix || []).map((s) => ({ name: s.name, value: Math.round(n(s.value)) }));
    const maxMix = Math.max(1, ...mix.map((s) => s.value));
    ranks.value = mix.map((s) => ({ ...s, pct: Math.round((s.value / maxMix) * 100) }));

    // 风险页半环：闲置率 = 空闲桩 / 总桩；负荷率 = 100 - 闲置（夹在 8–96，避免弧看不见）。
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

    // 告警文案来自 Spark build_alerts；没有则写「运行平稳」，接口挂了走 fail()。
    const rawAlerts = bd?.alerts?.length ? bd.alerts : an?.alerts || [];
    alerts.value = rawAlerts.length
      ? rawAlerts.map((a) => ({ level: a.level || "提示", title: a.title || "" }))
      : [{ level: "提示", title: "运行平稳" }];
    errorText.value = "";

    // 风险页 24h 负荷：金线=预测 kWh，青柱=历史 kWh。缺小时补 0，保证横轴永远 00–23。
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

    // 风险页热力：ECharts heatmap 数据是 [小时, 星期下标, kWh]。visualMap 关掉，颜色映射藏在格子里。
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

    // 风险页电站画像：优先 PCA(pc1, pc2)；点大小随会话数对数放大。四类颜色与相对命名一致。
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

    // 风险页 RFM 条形：Spark 粗分群人数。用户页更细的评分饼图用 charts.rfm_extra。
    const rfm = (bd.rfm || []).map((r) => ({ name: r.name, value: n(r.value) }));
    rfmOpt.value = {
      ...base(),
      tooltip: { trigger: "axis", confine: true },
      xAxis: { type: "value", ...axis() },
      yAxis: { type: "category", data: rfm.map((r) => r.name).reverse(), ...axis() },
      series: [{ type: "bar", data: rfm.map((r) => r.value).reverse(), itemStyle: { borderRadius: [0, 8, 8, 0], color: C.gold } }],
    };

    // 风险页桩状态饼：读业务库 pile.status，不是 Spark。
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

    // 风险页特征贡献：GBT 前 6 个特征，value 已 ×100 成百分数展示。
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

    // —— 运营总览 —— 日趋势双 y 轴：左订单折线，右充电量折线。
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

    // 用户平台环图：IOS / ANDROID / WEB，颜色写死便于答辩指认。
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

    // 地区对比：柱=订单，线=电量。站名可能较长，x 轴文字旋转 20°。
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

    // 热门站横向条：Spark station_rank 的 kWh TOP 8，站名超长截断。
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

    // 星期柱：一周订单量，不是热力图（热力在风险页）。
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

    // —— 充电行为 —— 24h 柱+线：柱订单、线电量。
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

    // 分类柱的共用工厂：SOC / 时长 / 费用 / 温度分箱都走这里。
    const barCat = (rows, color) => ({
      ...base(),
      xAxis: { type: "category", data: rows.map((r) => r.name), axisLabel: { ...axis().axisLabel, rotate: 18 }, ...axis() },
      yAxis: { type: "value", ...axis() },
      series: [{ type: "bar", data: rows.map((r) => r.value), itemStyle: { color, borderRadius: [4, 4, 0, 0] } }],
    });
    socOpt.value = barCat(ch.soc || [], C.mint);
    durOpt.value = barCat(ch.duration || [], C.purple);
    feeOpt.value = barCat(ch.fees || [], C.gold);

    // 效率散点：横轴 log(订单量) 拉开长尾站，纵轴均电量，颜色=KMeans 画像。
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

    // —— 智能预测 —— 7 日：历史日画实柱订单，全序列画预测电量线；未来日订单为 null。
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

    // 测试集对照：实线=真实日订单，虚线=季节回退预测。不在浏览器里训练。
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

    // 三模型柱+线：MAE/RMSE 左轴，R² 右轴 0–1。可附带「订单季节」第四组。
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

    // —— 用户分析 —— 画像卡片文案 + RFM 评分饼。人数可以不等（评分切，不是 50% 切人）。
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

    // K 曲线：k=2..6 轮廓系数，给「为什么 k=4」用。
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

    // 群体雷达：五维已归一到 0–100，indicator.max 固定 100。
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

    // RFM 散点：x=Recency 天，y=Frequency 次，点大小随 Monetary。
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

    // —— 数据质量 —— 口径归清洗 scan_quality；这里只负责把数字画成柱/漏斗/散点。
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

  /**
   * 并行拉三个只读 GET，再 apply。
   * 失败走 fail()：风险页告警变红字，已有图保持上一帧。
   */
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
    /* 12 秒轮询：会议室电视自动换数；切页不走这里，只改 tab。 */
    timer = window.setInterval(refresh, 12000);
  });
  onBeforeUnmount(() => clearInterval(timer));

  // 字段名 = Screen props 名。新增图时先在这里加 ref，再在对应 Screen defineProps。
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
