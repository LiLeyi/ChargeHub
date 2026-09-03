import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const dashboardRoot = new URL("../", import.meta.url);

async function read(relativePath) {
  return readFile(new URL(relativePath, dashboardRoot), "utf8");
}

test("page loads split local assets without inline implementation", async () => {
  const html = await read("index.html");
  assert.match(html, /href="dashboard\.css"/);
  assert.match(html, /src="echarts\.min\.js"/);
  assert.match(html, /type="module" src="dashboard\.js"/);
  assert.doesNotMatch(html, /<style[>\s]/i);
  assert.doesNotMatch(html, /<script>[^<]/i);
});

test("dashboard never renders database text through innerHTML", async () => {
  const source = await read("dashboard.js");
  assert.doesNotMatch(source, /innerHTML/);
  assert.match(source, /textContent/);
  assert.match(source, /replaceChildren/);
});

test("hidden error and degradation regions cannot be forced visible by component styles", async () => {
  const css = await read("dashboard.css");
  assert.match(css, /\[hidden\]\s*\{\s*display:\s*none\s*!important;/);
});

test("page lifecycle stops background work and resumes from page cache", async () => {
  const source = await read("dashboard.js");
  assert.match(source, /addEventListener\("pagehide"/);
  assert.match(source, /addEventListener\("pageshow"/);
  assert.match(source, /document\.hidden/);
});
