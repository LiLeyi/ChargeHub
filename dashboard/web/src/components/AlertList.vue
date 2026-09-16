<script setup>
/**
 * 风险页告警列表。文案来自 Spark `build_alerts`，本组件只做样式。
 *
 * 【level】「严重」「错误」红点，「一般」金点，其它青点。
 * 接口失败时 useDashboard.fail() 会塞一条 level=「错误」的项，页面不白屏。
 *
 * @prop {{ level: string, title: string }[]} items
 */
defineProps({
  items: { type: Array, default: () => [] },
});
</script>

<template>
  <ul class="alerts">
    <li v-for="(a, i) in items" :key="i" :class="a.level">
      <i></i>
      <span>{{ a.level }}</span>
      <b>{{ a.title }}</b>
    </li>
  </ul>
</template>

<style scoped>
.alerts {
  list-style: none;
  margin: 0;
  padding: 2px;
  height: 100%;
  overflow: auto;
}
li {
  display: grid;
  grid-template-columns: 8px 48px 1fr;
  gap: 8px;
  align-items: center;
  padding: 8px 6px;
  font-size: 12px;
  border-radius: 8px;
  margin-bottom: 6px;
  background: rgba(255, 255, 255, 0.03);
}
i {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: #5ad0ff;
}
li.严重 i { background: #ff7b7b; box-shadow: 0 0 8px #ff7b7b; }
li.一般 i { background: #f4c36a; }
li.错误 i { background: #ff7b7b; }
span { color: #7aa8a0; }
b { font-weight: 500; color: #e7fbf4; }
</style>
