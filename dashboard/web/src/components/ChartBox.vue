<script setup>
/**
 * ECharts 画布容器。
 *
 * 【职责】可视化：把 option 交给 useEcharts；前端：保证本节点在 Grid 里宽高都是 100%。
 * 父级 PanelCard.body 若没有 min-height:0，这里 height:100% 会算成 0，图画不出来。
 *
 * 【不要】在本组件 fetch 或改 series。option 来自 Screen 的 props，Screen 来自 useDashboard。
 *
 * @prop {object} option ECharts setOption 的完整配置；空对象时画空白轴
 */
import { computed, ref } from "vue";
import { useEcharts } from "../composables/useEcharts.js";

const props = defineProps({
  /** 完整 ECharts option；apply() 写入后 deep watch 会整图替换 */
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
