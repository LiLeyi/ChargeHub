<script setup>
defineProps({
  items: { type: Array, default: () => [] },
});
</script>

<template>
  <ol class="rank">
    <li v-for="(row, i) in items" :key="row.name">
      <em>No.{{ i + 1 }}</em>
      <span>{{ row.name }}</span>
      <b>{{ row.value }}元</b>
      <i :style="{ width: row.pct + '%' }"></i>
    </li>
  </ol>
</template>

<style scoped>
.rank {
  list-style: none;
  margin: 0;
  padding: 4px 2px;
  height: 100%;
  overflow: auto;
}
li {
  position: relative;
  display: grid;
  grid-template-columns: 42px 1fr auto;
  gap: 6px;
  align-items: center;
  padding: 8px 4px 10px;
  font-size: 12px;
}
em {
  font-style: normal;
  color: #3ee0c3;
}
span {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  color: #d9fff6;
}
b { color: #9fe8d8; font-weight: 600; }
i {
  position: absolute;
  left: 42px;
  right: 8px;
  bottom: 4px;
  height: 3px;
  border-radius: 99px;
  background: linear-gradient(90deg, #3ee0c3, #f4c36a);
  max-width: calc(100% - 50px);
}
</style>
