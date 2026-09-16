<script setup>
/**
 * 充电行为页栅格。
 *
 * 【布局】2 行 × 3 列。第一行 24h 分布跨两列；第二行 SOC / 时长 / 费用；
 * 右下是电站效率散点。
 *
 * 【数据】option 由 apply() 从 charts.hours / soc / duration / fees / efficiency 填好。
 * 效率图：横轴 log(订单量)、纵轴均电量、颜色=Spark 四类画像。
 * 不能改成均时长×均电量——扩样后那两维几乎常数，点会挤成一团。
 *
 * 【不要改】散点坐标类型（必须是 log）和 series 分组逻辑。
 */
import PanelCard from "../components/PanelCard.vue";
import ChartBox from "../components/ChartBox.vue";

defineProps({
  /** 24 小时订单柱 + 电量线 */
  hourOpt: { type: Object, default: () => ({}) },
  /** 电池 SOC 分箱柱 */
  socOpt: { type: Object, default: () => ({}) },
  /** 充电时长分箱柱 */
  durOpt: { type: Object, default: () => ({}) },
  /** 充电费用分箱柱 */
  feeOpt: { type: Object, default: () => ({}) },
  /** 电站效率散点（log 订单量 × 均电量，颜色=画像） */
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
