<script setup>
import PanelCard from "../components/PanelCard.vue";
import ChartBox from "../components/ChartBox.vue";

defineProps({
  dailyOpt: { type: Object, default: () => ({}) },
  platformOpt: { type: Object, default: () => ({}) },
  regionOpt: { type: Object, default: () => ({}) },
  topOpt: { type: Object, default: () => ({}) },
  weekdayOpt: { type: Object, default: () => ({}) },
});
</script>

<template>
  <div class="grid">
    <PanelCard class="wide" title="每日订单与充电量趋势">
      <ChartBox :option="dailyOpt" />
    </PanelCard>
    <PanelCard title="用户平台分布">
      <ChartBox :option="platformOpt" />
    </PanelCard>
    <PanelCard title="地区业务综合对比">
      <ChartBox :option="regionOpt" />
    </PanelCard>
    <PanelCard title="热门充电站 TOP 8">
      <ChartBox :option="topOpt" />
    </PanelCard>
    <PanelCard title="星期订单分布">
      <ChartBox :option="weekdayOpt" />
    </PanelCard>
  </div>
</template>

<style scoped>
.grid {
  height: 100%;
  min-height: 0;
  display: grid;
  grid-template-columns: 1.35fr 0.85fr 1fr;
  grid-template-rows: 1.15fr 1fr;
  gap: 8px;
}
.wide { grid-column: 1 / 3; }
</style>
