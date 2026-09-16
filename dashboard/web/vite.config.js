/**
 * Vite 构建配置（大屏前端同学讲）。
 *
 * plugins.vue     编译 .vue 单文件组件（script setup + scoped CSS）
 * base: "/"       Flask 从站点根路径托管 dist，不要写成相对路径
 * resolve.alias @ 指向 src/，源码里可用 "@/composables/..."（当前文件多用相对路径）
 * build.outDir    写到 dashboard/dist，Flask app.py 优先 send_from_directory 这里
 * emptyOutDir     每次 build 清空旧 hash 文件，避免 dist/assets 堆积
 * server.proxy    `npm run dev` 时浏览器打 /api/* 转发到 Flask :5000，绕开 CORS
 *
 * 组员只看大屏：不必跑 Vite，仓库已有 dist，直接 python3 dashboard/app.py。
 */
import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";
import { fileURLToPath, URL } from "node:url";

export default defineConfig({
  plugins: [vue()],
  base: "/",
  resolve: {
    alias: { "@": fileURLToPath(new URL("./src", import.meta.url)) },
  },
  build: {
    outDir: fileURLToPath(new URL("../dist", import.meta.url)),
    emptyOutDir: true,
    assetsDir: "assets",
  },
  server: {
    proxy: { "/api": "http://127.0.0.1:5000" },
  },
});
