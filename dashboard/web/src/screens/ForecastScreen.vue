<script setup>
import PanelCard from "../components/PanelCard.vue";
import ChartBox from "../components/ChartBox.vue";

defineProps({
  forecastOpt: { type: Object, default: () => ({}) },
  testOpt: { type: Object, default: () => ({}) },
  modelOpt: { type: Object, default: () => ({}) },
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
