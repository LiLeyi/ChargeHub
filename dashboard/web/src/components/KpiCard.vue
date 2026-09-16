<script setup>
/** 顶栏 KPI：label / 大数字 / 灰色 hint，左侧色条用 CSS 变量 --c。 */
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
