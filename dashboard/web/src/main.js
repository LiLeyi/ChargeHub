/**
 * Vue 3 入口。只做三件事：引入全局皮肤、创建根组件、挂到 #app。
 *
 * 【谁讲】大屏前端。本文件没有业务逻辑、没有 fetch。
 *
 * 【两种打开方式】
 *   开发：`npm run dev`（Vite），index.html 里的 /src/main.js 由 Vite 编译。
 *   答辩/组员：`python3 dashboard/app.py` 托管已经 build 好的 dashboard/dist，
 *   不必再装 Node。Flask 找不到 dist 才会回退到源码目录（一般用不到）。
 *
 * 【挂载点】dashboard/web/index.html 的 <div id="app">。
 */
import { createApp } from "vue";
import App from "./App.vue";
import "./style.css";

createApp(App).mount("#app");
