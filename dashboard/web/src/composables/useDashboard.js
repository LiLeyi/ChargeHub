import { computed, onBeforeUnmount, onMounted, ref } from "vue";

const C = {
  cyan: "#3ee0c3",
  gold: "#f4c36a",
  rose: "#ff7b7b",
  blue: "#5ad0ff",
  mint: "#86efac",
};

function axis() {
  return {
    axisLabel: { color: "#7aa8a0", fontSize: 10 },
    axisLine: { lineStyle: { color: "rgba(78,214,196,.18)" } },
    splitLine: { lineStyle: { color: "rgba(78,214,196,.08)" } },
  };
}

function n(v, d = 0) {
  const x = Number(v);
  return Number.isFinite(x) ? x : d;
}

async function jget(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url} ${r.status}`);
  return r.json();
}

export function useDashboard() {
  const clock = ref("--:--:--");
  const engine = ref("HDFS");
  const kpis = ref([
    { key: "today", label: "今日营收", value: "¥ 0", color: C.cyan },
    { key: "kwh", label: "累计电量 kWh", value: "0", color: C.cyan },
    { key: "sessions", label: "会话", value: "0", color: C.gold },
    { key: "ano", label: "异常", value: "0", color: C.rose },
    { key: "peak", label: "晚高峰", value: "--:00", color: C.blue },
    { key: "mae", label: "MAE", value: "--", color: C.cyan },
  ]);
  const ranks = ref([]);
  const idlePct = ref(0);
  const loadPct = ref(0);
  const alerts = ref([]);
  const errorText = ref("");
  const loadOpt = ref({});
  const heatOpt = ref({});
  const clusterOpt = ref({});
  const rfmOpt = ref({});
  const pileOpt = ref({});
  const featOpt = ref({});
  const idleOpt = ref({});
  const occOpt = ref({});

  let timer = 0;

  function fail(err) {
    errorText.value = err?.message || String(err || "数据失败");
    alerts.value = [{ level: "错误", title: errorText.value }];
  }

  function apply(ov, an, bd) {
    const sparkKpi = bd?.kpis || {};
    const rp = bd?.report?.mae != null ? bd.report : an?.report || {};
    const ano = bd?.anomalies || {};
    clock.value = ov.updated || clock.value;
    engine.value = sparkKpi.engine || an?.engine || "LOCAL";
    kpis.value = [
      { key: "today", label: "今日营收", value: `¥ ${Math.round(n(ov.today?.amount))}`, color: C.cyan },
      {
        key: "kwh",
        label: "累计电量 kWh",
        value: String(Math.round(sparkKpi.kwh_total != null ? sparkKpi.kwh_total : n(ov.month?.energy))),
        color: C.cyan,
      },
      {
        key: "sessions",
        label: "会话",
        value: String(Math.round(sparkKpi.sessions != null ? sparkKpi.sessions : n(ov.users))),
        color: C.gold,
      },
      {
        key: "ano",
        label: "异常",
        value: String(Math.round(ano.count != null ? ano.count : n(sparkKpi.anomalies))),
        color: C.rose,
      },
      {
        key: "peak",
        label: "晚高峰",
        value: `${String(sparkKpi.peak_hour ?? 0).padStart(2, "0")}:00`,
        color: C.blue,
      },
      { key: "mae", label: "MAE", value: n(rp.mae).toFixed(2), color: C.cyan },
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
    idlePct.value = total ? Math.round((idle / total) * 100) : 0;
    loadPct.value = Math.max(8, Math.min(96, 100 - idlePct.value));
    idleOpt.value = gaugeOption("闲置", idlePct.value, C.cyan);
    occOpt.value = gaugeOption("负荷", loadPct.value, C.gold);

    const rawAlerts = bd?.alerts?.length ? bd.alerts : an?.alerts || [];
    alerts.value = rawAlerts.length
      ? rawAlerts.map((a) => ({ level: a.level || "提示", title: a.title || "" }))
      : [{ level: "提示", title: "运行平稳" }];
    errorText.value = "";

    const hourly = bd?.hourly?.length ? bd.hourly : an?.hourly || [];
    const hs = [...Array(24).keys()].map((h) => {
      const row = hourly.find((x) => Number(x.hour) === h) || {};
      return {
        x: String(h).padStart(2, "0"),
        pred: n(row.pred_kwh),
        hist: n(row.hist_kwh),
      };
    });
    loadOpt.value = {
      textStyle: { color: "#c9ece4", fontSize: 11 },
      grid: { left: 10, right: 10, top: 28, bottom: 4, containLabel: true },
      tooltip: { trigger: "axis", confine: true, backgroundColor: "rgba(6,16,20,.92)" },
      legend: { data: ["预测", "历史"], textStyle: { color: "#8fbfb6", fontSize: 10 }, top: 2, right: 8 },
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
      grid: { left: 36, right: 12, top: 8, bottom: 16 },
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
      textStyle: { color: "#c9ece4", fontSize: 11 },
      legend: {
        type: "scroll",
        top: 2,
        right: 8,
        textStyle: { color: "#8fbfb6", fontSize: 10 },
        data: Object.keys(grouped),
      },
      tooltip: {
        trigger: "item",
        confine: true,
        formatter: (p) => {
          const v = p.value || [];
          return `${v[2] || ""}<br/>${v[3] || ""}${v[4] != null ? ` · ${v[4]} 次` : ""}`;
        },
      },
      grid: { left: 8, right: 12, top: 28, bottom: 8, containLabel: true },
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
      textStyle: { color: "#c9ece4", fontSize: 11 },
      grid: { left: 8, right: 24, top: 8, bottom: 4, containLabel: true },
      tooltip: { trigger: "axis", confine: true },
      xAxis: { type: "value", ...axis() },
      yAxis: { type: "category", data: rfm.map((r) => r.name).reverse(), ...axis() },
      series: [
        {
          type: "bar",
          data: rfm.map((r) => r.value).reverse(),
          itemStyle: { borderRadius: [0, 8, 8, 0], color: C.gold },
        },
      ],
    };

    const piles = (ov.piles || []).map((p) => ({ name: p.status, value: n(p.n) }));
    pileOpt.value = {
      textStyle: { color: "#c9ece4", fontSize: 11 },
      tooltip: { trigger: "item" },
      series: [
        {
          type: "pie",
          radius: ["42%", "68%"],
          center: ["50%", "55%"],
          label: { color: "#c9ece4", fontSize: 10 },
          data: piles.map((p, i) => ({
            ...p,
            itemStyle: { color: [C.cyan, C.gold, C.rose, C.blue][i % 4] },
          })),
        },
      ],
    };

    const imps = (bd.importances || []).slice(0, 6).map((i) => ({
      name: String(i.name || "").slice(0, 8),
      value: Math.round(n(i.value) * 1000) / 10,
    }));
    featOpt.value = {
      textStyle: { color: "#c9ece4", fontSize: 11 },
      grid: { left: 8, right: 10, top: 8, bottom: 4, containLabel: true },
      xAxis: { type: "value", ...axis() },
      yAxis: { type: "category", data: imps.map((i) => i.name).reverse(), ...axis() },
      series: [
        {
          type: "bar",
          data: imps.map((i) => i.value).reverse(),
          itemStyle: { color: C.blue, borderRadius: [0, 4, 4, 0] },
        },
      ],
    };
  }

  function gaugeOption(name, value, color) {
    return {
      series: [
        {
          type: "gauge",
          startAngle: 210,
          endAngle: -30,
          min: 0,
          max: 100,
          radius: "95%",
          progress: { show: true, width: 10, itemStyle: { color } },
          axisLine: { lineStyle: { width: 10, color: [[1, "rgba(78,214,196,.15)"]] } },
          axisTick: { show: false },
          splitLine: { show: false },
          axisLabel: { show: false },
          pointer: { show: false },
          title: { offsetCenter: [0, "28%"], color: "#7aa8a0", fontSize: 11 },
          detail: { valueAnimation: true, fontSize: 18, color, offsetCenter: [0, "-8%"], formatter: "{value}%" },
          data: [{ value, name }],
        },
      ],
    };
  }

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
    timer = window.setInterval(refresh, 8000);
  });
  onBeforeUnmount(() => clearInterval(timer));

  return {
    clock,
    engine,
    kpis,
    ranks,
    alerts,
    loadOpt,
    heatOpt,
    clusterOpt,
    rfmOpt,
    pileOpt,
    featOpt,
    idleOpt,
    occOpt,
    ready: computed(() => ranks.value.length > 0 || alerts.value.length > 0),
  };
}
