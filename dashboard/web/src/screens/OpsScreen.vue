<script setup>
/**
 * 运营总览页栅格（大屏前端排版 / 可视化认 option）。
 *
 * 【布局】2 行 × 3 列。第一行日趋势跨两列（.wide），右边平台饼图。
 * 第二行：地区对比 | 热门站 TOP8 | 星期分布。
 *
 * 【数据】全部来自 useDashboard.apply，本文件不 fetch。
 *   dailyOpt     charts.daily        双 y 轴：订单折线 + 电量折线
 *   platformOpt  charts.platform     IOS/ANDROID/WEB 环图
 *   regionOpt    charts.regions      柱=订单，线=电量
 *   topOpt       bigdata.topStations 横向 kWh TOP 8
 *   weekdayOpt   charts.weekday      一周订单柱
 *
 * 【不要改】series 颜色/坐标；缺数时 option 为空对象，ChartBox 画空白即可。
 */
import PanelCard from "../components/PanelCard.vue";
import ChartBox from "../components/ChartBox.vue";

defineProps({
  /** 每日订单与充电量趋势 */
  dailyOpt: { type: Object, default: () => ({}) },
  /** 用户平台环图 */
  platformOpt: { type: Object, default: () => ({}) },
  /** 地区订单/电量对照 */
  regionOpt: { type: Object, default: () => ({}) },
  /** 热门充电站横向条 */
  topOpt: { type: Object, default: () => ({}) },
  /** 星期订单柱 */
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
/* 日趋势占第 1–2 列，把主趋势图做大。 */
.wide { grid-column: 1 / 3; }
</style>
