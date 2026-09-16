<script setup>
/**
 * 数据质量页栅格。
 *
 * 【布局】3 行 × 3 列。日均 SOC 跨两列（.wide）。格子归前端；
 * 每个数字对应哪条清洗规则问数据清洗同学。
 *
 * 【数据来源】charts.quality，由 enrich_dashboard.scan_quality 扫原始
 * nvv2t + dsv13r2 得到，不是扩样 dataset/big。
 *
 * 【口径提示】漏斗：原始会话 → 电量>0 → 时长 0–24h → 付费。
 * 免费单单独计数，不是直接当脏数据删。电池有效 = pack_voltage>0。
 */
import PanelCard from "../components/PanelCard.vue";
import ChartBox from "../components/ChartBox.vue";

defineProps({
  /** 清洗规则命中条形（年份异常、电量≤0、时长越界等） */
  issueOpt: { type: Object, default: () => ({}) },
  /** 电池记录有效/无效饼 */
  batStatusOpt: { type: Object, default: () => ({}) },
  /** 会话清洗漏斗柱 */
  funnelOpt: { type: Object, default: () => ({}) },
  /** 清洗后分时电价时段分布 */
  qTariffOpt: { type: Object, default: () => ({}) },
  /** 设施类型：交流/直流/交直流/超充 */
  qFacilityOpt: { type: Object, default: () => ({}) },
  /** 有效电池 SOC × 包电压散点 */
  voltSocOpt: { type: Object, default: () => ({}) },
  /** 有效电池日均 SOC / 电压双折线 */
  socDailyOpt: { type: Object, default: () => ({}) },
  /** 有效记录温度分箱 */
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
