<script setup>
/**
 * 大屏壳：顶栏品牌 + 六页签 + KPI 条 + 舞台。
 * 质量页换用 qualityKpis，其余页用 Spark/业务 KPI。
 * 不在这里画图，只按 dash.tab 挂载对应 Screen。
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

const dash = reactive(useDashboard());
</script>

<template>
  <div class="page">
    <header class="head">
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
.page {
  width: 100%;
  height: 100%;
  padding: 8px 10px 10px;
  display: grid;
  grid-template-rows: 64px 78px minmax(0, 1fr);
  gap: 8px;
}
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
.stage {
  min-height: 0;
  height: 100%;
}
.stage > * {
  height: 100%;
}
</style>
