import { nextTick, onBeforeUnmount, onMounted, watch } from "vue";
import * as echarts from "echarts";

/**
 * 把一份 ECharts option 绑到某个 DOM 节点。
 *
 * 【职责】可视化同学讲绑定/刷新/销毁；前端同学保证父格有明确宽高
 * （PanelCard `.body` 必须 `min-height:0`，否则 Grid 子项高度为 auto，图画扁）。
 *
 * 【生命周期】
 *   onMounted  → nextTick 后 init + 第一次 setOption
 *   watch(option, deep) → 数据刷新时整图替换（notMerge=true，避免旧 series 残留）
 *   ResizeObserver + window.resize → CSS Grid 改高度后立刻 resize，否则热力/散点仍是旧尺寸
 *   onBeforeUnmount → dispose。App.vue 用 v-if 切页，不 dispose 会泄漏 canvas
 *
 * 【渲染】canvas 而不是 SVG：点多（效率散点、RFM 散点）时更稳。
 * 宽或高小于 8px 时跳过 resize，避免隐藏节点上报 0 把图毁掉。
 *
 * @param {import('vue').Ref<HTMLElement|null>} elRef ChartBox 根节点
 * @param {import('vue').Ref<object>} optionRef 来自 useDashboard 的 *Opt
 */
export function useEcharts(elRef, optionRef) {
  /** @type {echarts.ECharts|null} */
  let chart = null;
  /** @type {ResizeObserver|null} */
  let ro = null;

  /** 取当前 option；未就绪时给空对象，避免 setOption(undefined) 抛错。 */
  function option() {
    return optionRef && optionRef.value ? optionRef.value : {};
  }

  /**
   * 初始化（仅一次）并按容器像素 resize 后再 setOption。
   * 必须先 resize 再画：Grid 布局完成前 clientHeight 可能是 0。
   */
  function render() {
    const el = elRef.value;
    if (!el) return;
    if (!chart) chart = echarts.init(el, null, { renderer: "canvas" });
    const w = el.clientWidth;
    const h = el.clientHeight;
    if (w > 8 && h > 8) chart.resize({ width: w, height: h });
    chart.setOption(option(), true);
  }

  /** 只改尺寸不改数据。窗口拉满、切页签回来时走这里。 */
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
