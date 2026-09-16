<script setup>
/**
 * 风险调度：两行五列。上：营收排行 | 24h 负荷（跨三列） | 告警。
 * 下：闲置/负荷、热力、电站 PCA 画像、RFM 条、桩状态/特征。
 * 不要用三行四列：中间行高度不够会把热力/散点压扁。
 */
import PanelCard from "../components/PanelCard.vue";
import ChartBox from "../components/ChartBox.vue";
import RankList from "../components/RankList.vue";
import AlertList from "../components/AlertList.vue";

defineProps({
  ranks: { type: Array, default: () => [] },
  alerts: { type: Array, default: () => [] },
  loadOpt: { type: Object, default: () => ({}) },
  heatOpt: { type: Object, default: () => ({}) },
  clusterOpt: { type: Object, default: () => ({}) },
  rfmOpt: { type: Object, default: () => ({}) },
  pileOpt: { type: Object, default: () => ({}) },
  featOpt: { type: Object, default: () => ({}) },
  idleOpt: { type: Object, default: () => ({}) },
  occOpt: { type: Object, default: () => ({}) },
});
</script>

<template>
  <div class="grid">
    <PanelCard class="cell-rank" title="电站营收排行">
      <RankList :items="ranks" />
    </PanelCard>
    <PanelCard class="cell-load" title="24h 负荷预测 / 历史">
      <ChartBox :option="loadOpt" />
    </PanelCard>
    <PanelCard class="cell-alert" title="实时告警">
      <AlertList :items="alerts" />
    </PanelCard>
    <PanelCard class="cell-idle" title="闲置 / 负荷">
      <div class="split-v">
        <ChartBox :option="idleOpt" />
        <ChartBox :option="occOpt" />
      </div>
    </PanelCard>
    <PanelCard class="cell-heat" title="时段热力">
      <ChartBox :option="heatOpt" />
    </PanelCard>
    <PanelCard class="cell-cluster" title="电站画像">
      <ChartBox :option="clusterOpt" />
    </PanelCard>
    <PanelCard class="cell-rfm" title="用户 RFM">
      <ChartBox :option="rfmOpt" />
    </PanelCard>
    <PanelCard class="cell-pile" title="桩状态 / 特征">
      <div class="split-v">
        <ChartBox :option="pileOpt" />
        <ChartBox :option="featOpt" />
      </div>
    </PanelCard>
  </div>
</template>

<style scoped>
.grid {
  height: 100%;
  min-height: 0;
  display: grid;
  grid-template-columns: minmax(170px, 0.88fr) minmax(0, 1.12fr) minmax(0, 1.28fr) minmax(0, 1.08fr) minmax(170px, 0.92fr);
  grid-template-rows: minmax(0, 1.05fr) minmax(0, 1fr);
  gap: 8px;
}
.cell-rank { grid-column: 1; grid-row: 1; }
.cell-load { grid-column: 2 / 5; grid-row: 1; }
.cell-alert { grid-column: 5; grid-row: 1; }
.cell-idle { grid-column: 1; grid-row: 2; }
.cell-heat { grid-column: 2; grid-row: 2; }
.cell-cluster { grid-column: 3; grid-row: 2; }
.cell-rfm { grid-column: 4; grid-row: 2; }
.cell-pile { grid-column: 5; grid-row: 2; }
.split-v {
  display: grid;
  grid-template-rows: 1fr 1fr;
  gap: 4px;
  height: 100%;
  min-height: 0;
}
</style>
