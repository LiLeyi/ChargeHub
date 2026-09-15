<script setup>
import KpiCard from "./components/KpiCard.vue";
import PanelCard from "./components/PanelCard.vue";
import ChartBox from "./components/ChartBox.vue";
import RankList from "./components/RankList.vue";
import AlertList from "./components/AlertList.vue";
import { useDashboard } from "./composables/useDashboard.js";

const {
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
} = useDashboard();
</script>

<template>
  <div class="page">
    <header class="head">
      <div class="brand-line"></div>
      <h1>CHARGEHUB</h1>
      <div class="meta">
        <span>{{ engine }}</span>
        <b>{{ clock }}</b>
      </div>
    </header>

    <div class="kpis">
      <KpiCard
        v-for="item in kpis"
        :key="item.key"
        :label="item.label"
        :value="item.value"
        :color="item.color"
      />
    </div>

    <div class="stage">
      <div class="col left">
        <PanelCard title="电站营收排行">
          <RankList :items="ranks" />
        </PanelCard>
        <PanelCard title="闲置 / 负荷">
          <div class="split">
            <ChartBox :option="idleOpt" />
            <ChartBox :option="occOpt" />
          </div>
        </PanelCard>
      </div>

      <div class="col mid">
        <PanelCard class="span-load" title="24h 负荷预测 / 历史">
          <ChartBox :option="loadOpt" />
        </PanelCard>
        <PanelCard title="时段热力">
          <ChartBox :option="heatOpt" />
        </PanelCard>
        <PanelCard title="电站画像">
          <ChartBox :option="clusterOpt" />
        </PanelCard>
      </div>

      <div class="col right">
        <PanelCard title="实时告警">
          <AlertList :items="alerts" />
        </PanelCard>
        <PanelCard title="用户 RFM">
          <ChartBox :option="rfmOpt" />
        </PanelCard>
        <PanelCard title="桩状态 / 特征">
          <div class="split">
            <ChartBox :option="pileOpt" />
            <ChartBox :option="featOpt" />
          </div>
        </PanelCard>
      </div>
    </div>
  </div>
</template>

<style scoped>
.page {
  width: 100%;
  height: 100%;
  padding: 8px 10px 10px;
  display: grid;
  grid-template-rows: 50px 76px minmax(0, 1fr);
  gap: 8px;
}
.head {
  display: grid;
  grid-template-columns: 160px 1fr 240px;
  align-items: center;
  gap: 8px;
  min-height: 0;
}
.brand-line {
  height: 4px;
  border-radius: 99px;
  background: linear-gradient(90deg, transparent, #3ee0c3, transparent);
}
h1 {
  margin: 0;
  text-align: center;
  letter-spacing: 10px;
  font-size: 22px;
  font-weight: 800;
  color: #d9fff6;
  text-shadow: 0 0 18px rgba(62, 224, 195, 0.35);
}
.meta {
  display: flex;
  justify-content: flex-end;
  align-items: center;
  gap: 12px;
  color: #7aa8a0;
  font-size: 13px;
}
.meta b {
  color: #3ee0c3;
  font-size: 16px;
  letter-spacing: 1px;
}
.kpis {
  display: grid;
  grid-template-columns: repeat(6, minmax(0, 1fr));
  gap: 8px;
  min-height: 0;
}
.stage {
  display: grid;
  grid-template-columns: 22% minmax(0, 1fr) 25%;
  gap: 8px;
  min-height: 0;
  height: 100%;
}
.col {
  display: grid;
  gap: 8px;
  height: 100%;
  min-height: 0;
  min-width: 0;
}
.left { grid-template-rows: minmax(0, 1fr) 150px; }
.mid {
  grid-template-columns: minmax(0, 1fr) minmax(0, 1fr);
  grid-template-rows: 44% minmax(0, 1fr);
}
.span-load { grid-column: 1 / -1; }
.right { grid-template-rows: minmax(0, 1.2fr) minmax(0, 0.9fr) minmax(0, 1fr); }
.split {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 6px;
  height: 100%;
  min-height: 0;
}
</style>
