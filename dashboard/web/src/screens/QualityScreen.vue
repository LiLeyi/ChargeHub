<script setup>
/**
 * 数据质量：清洗问题、电池有效率、漏斗、峰谷、设施、电压-SOC、日 SOC、温度。
 * 数据来自 enrich_dashboard.scan_quality（原始 nvv2t + dsv13r2），不是扩样表。
 */
import PanelCard from "../components/PanelCard.vue";
import ChartBox from "../components/ChartBox.vue";

defineProps({
  issueOpt: { type: Object, default: () => ({}) },
  batStatusOpt: { type: Object, default: () => ({}) },
  funnelOpt: { type: Object, default: () => ({}) },
  qTariffOpt: { type: Object, default: () => ({}) },
  qFacilityOpt: { type: Object, default: () => ({}) },
  voltSocOpt: { type: Object, default: () => ({}) },
  socDailyOpt: { type: Object, default: () => ({}) },
  tempOpt: { type: Object, default: () => ({}) },
});
</script>

<template>
  <div class="grid">
    <PanelCard title="清洗规则命中（数据清洗需求表）">
      <ChartBox :option="issueOpt" />
    </PanelCard>
    <PanelCard title="电池记录状态">
      <ChartBox :option="batStatusOpt" />
    </PanelCard>
    <PanelCard title="会话清洗漏斗">
      <ChartBox :option="funnelOpt" />
    </PanelCard>
    <PanelCard title="分时电价时段（清洗后）">
      <ChartBox :option="qTariffOpt" />
    </PanelCard>
    <PanelCard title="设施类型（交流/直流/交直流/超充）">
      <ChartBox :option="qFacilityOpt" />
    </PanelCard>
    <PanelCard title="有效电池 SOC 与包电压">
      <ChartBox :option="voltSocOpt" />
    </PanelCard>
    <PanelCard class="wide" title="有效电池日均 SOC / 电压">
      <ChartBox :option="socDailyOpt" />
    </PanelCard>
    <PanelCard title="有效记录温度分布">
      <ChartBox :option="tempOpt" />
    </PanelCard>
  </div>
</template>

<style scoped>
.grid {
  height: 100%;
  min-height: 0;
  display: grid;
  grid-template-columns: 1.15fr 0.9fr 0.95fr;
  grid-template-rows: 1fr 1fr 0.95fr;
  gap: 8px;
}
.wide { grid-column: 1 / 3; }
</style>
