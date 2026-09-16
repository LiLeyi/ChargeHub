<script setup>
/**
 * 用户分析：价值分群饼图、K 轮廓曲线、雷达、RFM 散点、画像卡片。
 * 高价值/流失预警人数可以不相等，逻辑在 enrich_dashboard.rfm_pack。
 */
import PanelCard from "../components/PanelCard.vue";
import ChartBox from "../components/ChartBox.vue";

defineProps({
  rfmPieOpt: { type: Object, default: () => ({}) },
  kCurveOpt: { type: Object, default: () => ({}) },
  radarOpt: { type: Object, default: () => ({}) },
  rfmScatterOpt: { type: Object, default: () => ({}) },
  portraits: { type: Array, default: () => [] },
});
</script>

<template>
  <div class="grid">
    <PanelCard title="用户价值分群">
      <ChartBox :option="rfmPieOpt" />
    </PanelCard>
    <PanelCard title="K值与轮廓系数">
      <ChartBox :option="kCurveOpt" />
    </PanelCard>
    <PanelCard title="群体特征雷达图">
      <ChartBox :option="radarOpt" />
    </PanelCard>
    <PanelCard class="wide" title="用户 RFM 空间分布">
      <ChartBox :option="rfmScatterOpt" />
    </PanelCard>
    <PanelCard title="用户群体画像">
      <div class="faces">
        <article v-for="p in portraits" :key="p.name">
          <h4>{{ p.name }}</h4>
          <b>{{ p.users }} 人</b>
          <p>占比 {{ p.share }}%</p>
          <p>平均间隔 {{ p.avg_recency }} 天</p>
          <p>平均消费 {{ p.avg_monetary }} 元</p>
        </article>
        <p v-if="!portraits.length" class="empty">等待分群结果</p>
      </div>
    </PanelCard>
  </div>
</template>

<style scoped>
.grid {
  height: 100%;
  min-height: 0;
  display: grid;
  grid-template-columns: 1fr 1fr 1fr;
  grid-template-rows: 1.1fr 1fr;
  gap: 8px;
}
.wide { grid-column: 1 / 3; }
.faces {
  height: 100%;
  display: grid;
  grid-template-rows: 1fr 1fr;
  gap: 8px;
  padding: 4px 0;
}
article {
  padding: 10px 12px;
  border-radius: 10px;
  background: rgba(255, 255, 255, 0.03);
  border: 1px solid rgba(78, 214, 196, 0.14);
}
h4 { margin: 0; color: #9fe8d8; font-size: 13px; font-weight: 600; }
b { display: block; margin: 6px 0; font-size: 22px; color: #3ee0c3; }
p { margin: 2px 0; color: #7aa8a0; font-size: 12px; }
.empty { color: #7aa8a0; }
</style>
