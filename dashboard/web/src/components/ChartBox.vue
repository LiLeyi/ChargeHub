<script setup>
/** ECharts 容器：宽高 100%，option 经 useEcharts 绑定。 */
import { computed, ref } from "vue";
import { useEcharts } from "../composables/useEcharts.js";

const props = defineProps({
  option: { type: Object, default: () => ({}) },
});

const el = ref(null);
const optionRef = computed(() => props.option);
useEcharts(el, optionRef);
</script>

<template>
  <div ref="el" class="chart"></div>
</template>

<style scoped>
.chart {
  width: 100%;
  height: 100%;
  min-height: 0;
  min-width: 0;
}
</style>
