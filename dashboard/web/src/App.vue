<script setup>
/**
 * 大屏壳（大屏前端主文件）：顶栏品牌 + 六页签 + KPI 条 + 舞台。
 *
 * 【职责】只负责「谁上台、格子怎么切」。图的 series 在 useDashboard.apply，
 * 本文件禁止 fetch、禁止改 option。
 *
 * 【布局】.page 三行 Grid：64px 顶栏 / 78px KPI / 剩下全给舞台。
 * minmax(0,1fr) 和 .stage { min-height:0 } 必须成对出现，否则子图高度算成内容高度，ECharts 被压扁。
 *
 * 【页签】dash.tab 驱动 v-if（不是 v-show）：切走即卸载 Screen，ChartBox 才能 dispose canvas。
 * 质量页顶栏换成 dash.qualityKpis（原始会话/通过率/免费单），其余页用 Spark/业务 KPI。
 *
 * 【刷新】按钮调用 dash.refresh → 并行三个 GET。另有 12 秒定时器在 useDashboard 里。
 *
 * 【协作】engine / dateRange 来自 Spark KPI（HDFS 或 LOCAL）。打开方式见 dashboard/app.py。
 */
import { reactive } from "vue";
import KpiCard from "./components/KpiCard.vue";
import OpsScreen from "./screens/OpsScreen.vue";
import BehaviorScreen from "./screens/BehaviorScreen.vue";
import ForecastScreen from "./screens/ForecastScreen.vue";
import UserScreen from "./screens/UserScreen.vue";
import QualityScreen from "./screens/QualityScreen.vue";
import RiskScreen from "./screens/RiskScreen.vue";
import { useDashboard } from "./composables/useDashboard.js";

/** 全页唯一数据源。reactive 包一层是为了模板里直接写 dash.tab = ... */
const dash = reactive(useDashboard());
</script>

<template>
  <div class="page">
    <header class="head">
      <!-- 左：品牌；中：六页签；右：HDFS/LOCAL、样本日期、时钟、手动刷新 -->
      <div class="brand">
        <i></i>
        <div>
          <h1>ChargeHub 智慧充电运营大屏</h1>
          <p>HADOOP · SPARK · MACHINE LEARNING · VUE 3</p>
        </div>
      </div>
      <nav>
        <button
          v-for="item in dash.tabs"
          :key="item.id"
          :class="{ on: dash.tab === item.id }"
          type="button"
          @click="dash.tab = item.id"
        >
          {{ item.label }}
        </button>
      </nav>
      <div class="meta">
        <span>{{ dash.engine }}</span>
        <small>{{ dash.dateRange }}</small>
        <b>{{ dash.clock }}</b>
        <button class="refresh" type="button" @click="dash.refresh">刷新数据</button>
      </div>
    </header>

    <!-- 质量页换清洗口径 KPI，其余页用 Spark/业务 KPI。 -->
    <div class="kpis">
      <KpiCard
        v-for="item in (dash.tab === 'quality' ? dash.qualityKpis : dash.kpis)"
        :key="item.key"
        :label="item.label"
        :value="item.value"
        :hint="item.hint"
        :color="item.color"
      />
    </div>

    <!-- v-if 切页：卸载旧 Screen，让 ChartBox dispose，避免 canvas 泄漏。 -->
    <div class="stage">
      <OpsScreen
        v-if="dash.tab === 'ops'"
        :daily-opt="dash.dailyOpt"
        :platform-opt="dash.platformOpt"
        :region-opt="dash.regionOpt"
        :top-opt="dash.topOpt"
        :weekday-opt="dash.weekdayOpt"
      />
      <BehaviorScreen
        v-if="dash.tab === 'behavior'"
        :hour-opt="dash.hourOpt"
        :soc-opt="dash.socOpt"
        :dur-opt="dash.durOpt"
        :fee-opt="dash.feeOpt"
        :eff-opt="dash.effOpt"
      />
      <ForecastScreen
        v-if="dash.tab === 'forecast'"
        :forecast-opt="dash.forecastOpt"
        :test-opt="dash.testOpt"
        :model-opt="dash.modelOpt"
        :best-models="dash.bestModels"
      />
      <UserScreen
        v-if="dash.tab === 'users'"
        :rfm-pie-opt="dash.rfmPieOpt"
        :k-curve-opt="dash.kCurveOpt"
        :radar-opt="dash.radarOpt"
        :rfm-scatter-opt="dash.rfmScatterOpt"
        :portraits="dash.portraits"
      />
      <QualityScreen
        v-if="dash.tab === 'quality'"
        :issue-opt="dash.issueOpt"
        :bat-status-opt="dash.batStatusOpt"
        :funnel-opt="dash.funnelOpt"
        :q-tariff-opt="dash.qTariffOpt"
        :q-facility-opt="dash.qFacilityOpt"
        :volt-soc-opt="dash.voltSocOpt"
        :soc-daily-opt="dash.socDailyOpt"
        :temp-opt="dash.tempOpt"
      />
      <RiskScreen
        v-if="dash.tab === 'risk'"
        :ranks="dash.ranks"
        :alerts="dash.alerts"
        :load-opt="dash.loadOpt"
        :heat-opt="dash.heatOpt"
        :cluster-opt="dash.clusterOpt"
        :rfm-opt="dash.rfmOpt"
        :pile-opt="dash.pileOpt"
        :feat-opt="dash.featOpt"
        :idle-opt="dash.idleOpt"
        :occ-opt="dash.occOpt"
      />
    </div>
  </div>
</template>

<style scoped>
/* 整页三行：顶栏、KPI、舞台。第三行 minmax(0,1fr) 把剩余视口分给图。 */
.page {
  width: 100%;
  height: 100%;
  padding: 8px 10px 10px;
  display: grid;
  grid-template-rows: 64px 78px minmax(0, 1fr);
  gap: 8px;
}
/* 顶栏三列：品牌 | 页签居中 | 引擎/时间/刷新 */
.head {
  display: grid;
  grid-template-columns: minmax(240px, 1.1fr) minmax(0, 1.4fr) minmax(240px, 1fr);
  align-items: center;
  gap: 8px;
  padding: 0 6px;
}
.brand {
  display: flex;
  align-items: center;
  gap: 10px;
}
.brand i {
  width: 36px;
  height: 36px;
  border-radius: 10px;
  background: linear-gradient(135deg, #3ee0c3, #5ad0ff);
  box-shadow: 0 0 16px rgba(62, 224, 195, 0.35);
}
h1 {
  margin: 0;
  font-size: 20px;
  letter-spacing: 1px;
  color: #e8fffa;
}
.brand p {
  margin: 2px 0 0;
  font-size: 10px;
  letter-spacing: 1.4px;
  color: #6fa39a;
}
nav {
  display: flex;
  justify-content: center;
  gap: 8px;
}
nav button {
  border: 1px solid transparent;
  background: rgba(255, 255, 255, 0.04);
  color: #8fbfb6;
  padding: 7px 12px;
  border-radius: 99px;
  cursor: pointer;
  font-size: 12px;
}
nav button.on {
  color: #062018;
  background: linear-gradient(90deg, #3ee0c3, #5ad0ff);
  font-weight: 700;
}
.meta {
  display: flex;
  justify-content: flex-end;
  align-items: center;
  gap: 10px;
  color: #7aa8a0;
  font-size: 12px;
}
.meta b {
  color: #3ee0c3;
  font-size: 16px;
  letter-spacing: 1px;
}
.meta small { color: #5e8c84; }
.refresh {
  border: 1px solid rgba(62, 224, 195, 0.4);
  background: rgba(62, 224, 195, 0.12);
  color: #d9fff6;
  border-radius: 8px;
  padding: 6px 10px;
  cursor: pointer;
}
.kpis {
  display: grid;
  grid-template-columns: repeat(6, minmax(0, 1fr));
  gap: 8px;
  min-height: 0;
}
/* 舞台必须把高度传给当前 Screen；子选择器 height:100% 接住 v-if 出来的根节点。 */
.stage {
  min-height: 0;
  height: 100%;
}
.stage > * {
  height: 100%;
}
</style>
