<script setup>
import PanelCard from "../components/PanelCard.vue";
import ChartBox from "../components/ChartBox.vue";

defineProps({
  hourOpt: { type: Object, default: () => ({}) },
  socOpt: { type: Object, default: () => ({}) },
  durOpt: { type: Object, default: () => ({}) },
  feeOpt: { type: Object, default: () => ({}) },
  effOpt: { type: Object, default: () => ({}) },
});
</script>

<template>
  <div class="grid">
    <PanelCard class="wide" title="24小时充电行为分布">
      <ChartBox :option="hourOpt" />
    </PanelCard>
    <PanelCard title="电池 SOC 分布">
      <ChartBox :option="socOpt" />
    </PanelCard>
    <PanelCard title="充电时长分布">
      <ChartBox :option="durOpt" />
    </PanelCard>
    <PanelCard title="充电费用分布">
      <ChartBox :option="feeOpt" />
    </PanelCard>
    <PanelCard title="充电站运营效率（按画像）">
      <ChartBox :option="effOpt" />
    </PanelCard>
  </div>
</template>

<style scoped>
.grid {
  height: 100%;
  min-height: 0;
  display: grid;
  grid-template-columns: 1.2fr 0.9fr 1fr;
  grid-template-rows: 1.15fr 1fr;
  gap: 8px;
}
.wide { grid-column: 1 / 3; }
</style>
