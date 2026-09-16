/**
 * Vite：Vue 插件、产物写到 ../dist、开发时 /api 代理到 Flask :5000。
 * 组员只看大屏不必跑 Vite，直接 python3 dashboard/app.py 即可。
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
