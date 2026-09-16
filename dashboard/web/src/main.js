/**
 * Vue 3 入口。只挂 App，样式在 style.css。
 * 生产构建产物在 dashboard/dist，由 Flask send_from_directory 提供。
 */
import { createApp } from "vue";
import App from "./App.vue";
import "./style.css";

createApp(App).mount("#app");
