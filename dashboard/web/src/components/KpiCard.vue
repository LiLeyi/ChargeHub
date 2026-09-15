<script setup>
defineProps({
  label: { type: String, required: true },
  value: { type: String, required: true },
  color: { type: String, default: "#3ee0c3" },
});
</script>

<template>
  <article class="kpi">
    <span>{{ label }}</span>
    <b :style="{ color }">{{ value }}</b>
  </article>
</template>

<style scoped>
.kpi {
  height: 100%;
  min-width: 0;
  overflow: hidden;
  padding: 8px 12px;
  background: rgba(10, 28, 34, 0.72);
  border: 1px solid rgba(78, 214, 196, 0.18);
  border-radius: 12px;
  position: relative;
}
.kpi::after {
  content: "";
  position: absolute;
  left: 0;
  top: 0;
  bottom: 0;
  width: 2px;
  background: linear-gradient(180deg, var(--c, #3ee0c3), transparent);
}
span {
  display: block;
  color: #7aa8a0;
  font-size: 11px;
  letter-spacing: 2px;
}
b {
  display: block;
  margin-top: 6px;
  font-size: 22px;
  font-weight: 650;
  letter-spacing: 0.4px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
</style>
