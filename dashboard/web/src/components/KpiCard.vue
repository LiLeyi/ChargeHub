<script setup>
/**
 * 顶栏 KPI 卡片。App.vue 里循环 dash.kpis 或 dash.qualityKpis。
 *
 * 【字段】label 小标题、value 已经格式化好的大数字字符串、hint 灰色说明、color 色条。
 * 数字怎么算在 useDashboard.apply，本组件只排版。超长数字用 ellipsis 避免撑破六列。
 *
 * 【样式】左侧 3px 色条用 CSS 变量 --c，由 color prop 注入。
 *
 * @prop {string} label 例如「累计充电量」
 * @prop {string} value 例如「12,345.00 kWh」
 * @prop {string} [hint] 例如日期区间或「nvv2t 真实表」
 * @prop {string} [color="#3ee0c3"] 大数字和左侧色条
 */
defineProps({
  label: { type: String, required: true },
  value: { type: String, required: true },
  hint: { type: String, default: "" },
  color: { type: String, default: "#3ee0c3" },
});
</script>

<template>
  <article class="kpi" :style="{ '--c': color }">
    <span>{{ label }}</span>
    <b :style="{ color }">{{ value }}</b>
    <em v-if="hint">{{ hint }}</em>
  </article>
</template>

<style scoped>
.kpi {
  height: 100%;
  min-width: 0;
  overflow: hidden;
  padding: 8px 12px 6px;
  background: linear-gradient(180deg, rgba(12, 36, 52, 0.92), rgba(8, 22, 32, 0.88));
  border: 1px solid rgba(78, 214, 196, 0.2);
  border-radius: 10px;
  position: relative;
}
.kpi::after {
  content: "";
  position: absolute;
  left: 0;
  top: 10px;
  bottom: 10px;
  width: 3px;
  border-radius: 99px;
  background: linear-gradient(180deg, var(--c, #3ee0c3), transparent);
}
span {
  display: block;
  color: #7aa8a0;
  font-size: 11px;
  letter-spacing: 1px;
}
b {
  display: block;
  margin-top: 4px;
  font-size: 22px;
  font-weight: 700;
  letter-spacing: 0.4px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
em {
  display: block;
  margin-top: 2px;
  font-style: normal;
  color: #5e8c84;
  font-size: 10px;
}
</style>
