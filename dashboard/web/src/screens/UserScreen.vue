<script setup>
/**
 * 用户分析页栅格。
 *
 * 【布局】2 行 × 3 列。上：价值饼图 | K 轮廓曲线 | 雷达；
 * 下：RFM 散点跨两列 + 右侧画像文字卡。
 *
 * 【数据】charts.rfm_extra（enrich_dashboard.rfm_pack）。
 * 高价值 / 流失预警人数可以不相等：那是评分切，不是按 50% 切人。
 * K 曲线解释「为什么 k=4」。雷达五维已归一 0–100。
 *
 * 画像卡片只展示 portraits[] 文案，不在这里算 RFM。
 */
import PanelCard from "../components/PanelCard.vue";
import ChartBox from "../components/ChartBox.vue";

defineProps({
  /** 用户价值分群环图（高价值 / 流失预警 / 常规） */
  rfmPieOpt: { type: Object, default: () => ({}) },
  /** k=2..6 轮廓系数折线 */
  kCurveOpt: { type: Object, default: () => ({}) },
  /** 群体特征雷达 */
  radarOpt: { type: Object, default: () => ({}) },
  /** RFM 空间散点：x=R 天，y=F 次，点大小=M */
  rfmScatterOpt: { type: Object, default: () => ({}) },
  /**
   * 画像卡片。
   * @type {{ name: string, users: number, share: number, avg_recency: number, avg_monetary: number }[]}
   */
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
/* 画像卡上下各一块，人数文案来自 rfm_pack，本层不算分。 */
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
