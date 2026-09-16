<script setup>
/**
 * 风险调度页栅格。必须两行五列，不要改成三行。
 *
 * 【为什么两行】三行时中间行高度不够，热力 / PCA 散点会被压成一条。
 * 上：营收排行 | 24h 负荷（跨三列） | 告警。
 * 下：闲置/负荷仪表 | 热力 | 电站 PCA 画像 | RFM 条 | 桩状态+特征（上下对切）。
 *
 * 【定位】每个 .cell-* 写死 grid-column / grid-row，避免 auto-placement 把负荷图挤走。
 *
 * 【数据】load/heat/cluster/rfm/feat 来自 Spark；ranks/pile/idle 来自业务库 overview。
 * 告警文案 Spark build_alerts。本文件不改 series。
 */
import PanelCard from "../components/PanelCard.vue";
import ChartBox from "../components/ChartBox.vue";
import RankList from "../components/RankList.vue";
import AlertList from "../components/AlertList.vue";

defineProps({
  /** 电站营收排行：{ name, value, pct }，pct 已在 apply 按最大值归一 */
  ranks: { type: Array, default: () => [] },
  /** 告警：{ level, title }，level 含「严重」时红点 */
  alerts: { type: Array, default: () => [] },
  /** 24h 负荷：金线预测 + 青柱历史 */
  loadOpt: { type: Object, default: () => ({}) },
  /** 星期×小时热力 */
  heatOpt: { type: Object, default: () => ({}) },
  /** 电站 PCA 画像散点，颜色=四类相对命名 */
  clusterOpt: { type: Object, default: () => ({}) },
  /** Spark 侧 RFM 粗分群条形 */
  rfmOpt: { type: Object, default: () => ({}) },
  /** 业务库桩状态饼 */
  pileOpt: { type: Object, default: () => ({}) },
  /** GBT 特征贡献横向条 */
  featOpt: { type: Object, default: () => ({}) },
  /** 闲置率半环仪表 */
  idleOpt: { type: Object, default: () => ({}) },
  /** 负荷率半环仪表 */
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
/* 五列两行。列宽用 minmax 防止窄屏把热力列挤没。 */
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
/* 同一格上下叠两个图（闲置/负荷，桩状态/特征）。 */
.split-v {
  display: grid;
  grid-template-rows: 1fr 1fr;
  gap: 4px;
  height: 100%;
  min-height: 0;
}
</style>
