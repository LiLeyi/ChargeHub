<script setup>
defineProps({
  title: { type: String, default: "" },
});
</script>

<template>
  <section class="panel">
    <h3 v-if="title">{{ title }}</h3>
    <div class="body">
      <slot />
    </div>
  </section>
</template>

<style scoped>
.panel {
  min-width: 0;
  min-height: 0;
  height: 100%;
  overflow: hidden;
  display: flex;
  flex-direction: column;
  background: rgba(10, 28, 34, 0.72);
  border: 1px solid rgba(78, 214, 196, 0.18);
  border-radius: 12px;
  box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.03);
}
h3 {
  margin: 0;
  padding: 8px 10px 0;
  color: #9fe8d8;
  font-size: 12px;
  letter-spacing: 2px;
  font-weight: 600;
  flex: 0 0 auto;
}
.body {
  flex: 1 1 0;
  min-height: 0;
  min-width: 0;
  padding: 4px 8px 8px;
  overflow: hidden;
}
</style>
