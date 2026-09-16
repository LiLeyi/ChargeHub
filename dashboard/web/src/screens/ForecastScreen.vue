<script setup>
/**
 * 智能预测页栅格。
 *
 * 【布局】2 行 × 2 列。左列是图（.wide 只占第 1 列），右上是「最佳模型」文字卡。
 * 本页不跑训练：数字来自 charts.forecast7、charts.daily_pred_orders、Spark models。
 *
 * 【数据】
 *   forecastOpt  未来 7 天：历史日实柱订单 + 预测电量线
 *   testOpt      测试集实际值实线 vs 预测值虚线
 *   modelOpt     MAE / RMSE 柱 + R² 线（右轴 0–1）
 *   bestModels[] { task, name, r2, rmse, mae } 给右侧卡片
 *
 * 老师问「浏览器训练了吗」：没有，只读 JSON。
 */
import PanelCard from "../components/PanelCard.vue";
import ChartBox from "../components/ChartBox.vue";

defineProps({
  /** 未来 7 天需求（柱=历史订单，线=预测电量） */
  forecastOpt: { type: Object, default: () => ({}) },
  /** 测试集实际 vs 预测 */
  testOpt: { type: Object, default: () => ({}) },
  /** 多模型 MAE/RMSE/R² 对照 */
  modelOpt: { type: Object, default: () => ({}) },
  /**
   * 最佳模型卡片。
   * @type {{ task: string, name: string, r2: number, rmse: number, mae: number }[]}
   */
  bestModels: { type: Array, default: () => [] },
});
</script>

<template>
  <div class="grid">
    <PanelCard class="wide" title="未来7天充电需求预测">
      <ChartBox :option="forecastOpt" />
    </PanelCard>
    <PanelCard title="最佳预测模型">
      <div class="models">
        <article v-for="m in bestModels" :key="m.task" class="card">
          <span>{{ m.task }}</span>
          <b>{{ m.name }}</b>
          <p>R² {{ Number(m.r2 || 0).toFixed(4) }} · RMSE {{ Number(m.rmse || 0).toFixed(2) }} · MAE {{ Number(m.mae || 0).toFixed(2) }}</p>
        </article>
        <p v-if="!bestModels.length" class="empty">等待模型指标</p>
      </div>
    </PanelCard>
    <PanelCard class="wide" title="测试集实际值与预测值">
      <ChartBox :option="testOpt" />
    </PanelCard>
    <PanelCard title="模型评估对比">
      <ChartBox :option="modelOpt" />
    </PanelCard>
  </div>
</template>

<style scoped>
.grid {
  height: 100%;
  min-height: 0;
  display: grid;
  grid-template-columns: 1.4fr 0.9fr;
  grid-template-rows: 1.1fr 1fr;
  gap: 8px;
}
/* 只占左列，把右列留给模型卡片 / 评估柱图。 */
.wide { grid-column: 1 / 2; }
.models {
  height: 100%;
  display: flex;
  flex-direction: column;
  gap: 10px;
  padding: 8px 4px;
}
.card {
  flex: 1;
  padding: 12px 14px;
  border-radius: 10px;
  background: rgba(255, 255, 255, 0.03);
  border: 1px solid rgba(78, 214, 196, 0.14);
}
span { color: #7aa8a0; font-size: 12px; }
b { display: block; margin: 8px 0 6px; font-size: 22px; color: #5ad0ff; }
p { margin: 0; color: #9fe8d8; font-size: 12px; line-height: 1.6; }
.empty { color: #7aa8a0; }
</style>
