import { nextTick, onBeforeUnmount, onMounted, watch } from "vue";
import * as echarts from "echarts";

/**
 * 把 ECharts 绑到某个 DOM ref。option 一变就 setOption(true 替换)。
 * ResizeObserver + window.resize 防止 CSS Grid 改高度后图还是扁的。
 * 卸载时 dispose，避免切 tab 泄漏 canvas。
 *
 * @param {import('vue').Ref<HTMLElement|null>} elRef
 * @param {import('vue').Ref<object>} optionRef
 */
export function useEcharts(elRef, optionRef) {
  let chart = null;
  let ro = null;

  function option() {
    return optionRef && optionRef.value ? optionRef.value : {};
  }

  function render() {
    const el = elRef.value;
    if (!el) return;
    if (!chart) chart = echarts.init(el, null, { renderer: "canvas" });
    const w = el.clientWidth;
    const h = el.clientHeight;
    if (w > 8 && h > 8) chart.resize({ width: w, height: h });
    chart.setOption(option(), true);
  }

  function onResize() {
    if (chart) chart.resize();
  }

  onMounted(async () => {
    await nextTick();
    render();
    window.addEventListener("resize", onResize);
    if (typeof ResizeObserver !== "undefined" && elRef.value) {
      ro = new ResizeObserver(() => onResize());
      ro.observe(elRef.value);
    }
  });

  onBeforeUnmount(() => {
    window.removeEventListener("resize", onResize);
    if (ro) ro.disconnect();
    if (chart) {
      chart.dispose();
      chart = null;
    }
  });

  watch(optionRef, () => render(), { deep: true });
}
