import { validateDashboardSnapshot } from "./dashboardmodel.js";

function errorMessage(error) {
  if (error?.name === "AbortError") {
    return "刷新已取消";
  }
  return error instanceof Error && error.message ? error.message : "运营数据暂不可用";
}

export class HttpDashboardSource {
  constructor({ fetchFn = globalThis.fetch, url = "/api/dashboard" } = {}) {
    this.fetchFn = fetchFn;
    this.url = url;
  }

  async load({ signal } = {}) {
    const response = await this.fetchFn.call(globalThis, this.url, {
      cache: "no-store",
      headers: { Accept: "application/json" },
      signal,
    });
    if (!response.ok) {
      let message = `运营数据请求失败（HTTP ${response.status}）`;
      try {
        const body = await response.json();
        if (body?.error?.message) message = body.error.message;
      } catch {
        // Keep the safe status-based message for invalid error responses.
      }
      throw new Error(message);
    }
    return response.json();
  }
}

export class MockDashboardSource extends HttpDashboardSource {
  constructor(options = {}) {
    super({ ...options, url: options.url ?? "/data/dashboard.json" });
  }
}

export class DashboardController {
  constructor({
    source,
    onStateChange = () => {},
    now = () => new Date(),
    setIntervalFn = globalThis.setInterval,
    clearIntervalFn = globalThis.clearInterval,
    refreshMilliseconds = 5000,
  }) {
    if (!source || typeof source.load !== "function") {
      throw new TypeError("dashboard source must provide load()");
    }
    this.source = source;
    this.onStateChange = onStateChange;
    this.now = now;
    this.setIntervalFn = setIntervalFn;
    this.clearIntervalFn = clearIntervalFn;
    this.refreshMilliseconds = refreshMilliseconds;
    this.state = {
      status: "loading",
      snapshot: null,
      lastSuccessAt: null,
      error: null,
    };
    this.intervalId = null;
    this.inFlight = null;
    this.abortController = null;
    this.running = false;
  }

  getState() {
    return { ...this.state };
  }

  start() {
    if (this.running) return;
    this.running = true;
    void this.refresh();
    this.intervalId = this.setIntervalFn.call(
      globalThis,
      () => { void this.refresh(); },
      this.refreshMilliseconds,
    );
  }

  refresh() {
    if (this.inFlight) return this.inFlight;
    this.abortController = new AbortController();
    const currentAbortController = this.abortController;
    this.inFlight = this.source.load({ signal: currentAbortController.signal })
      .then((snapshot) => {
        if (currentAbortController.signal.aborted) return;
        validateDashboardSnapshot(snapshot);
        this.updateState({
          status: "ready",
          snapshot,
          lastSuccessAt: this.now().toISOString(),
          error: null,
        });
      })
      .catch((error) => {
        if (currentAbortController.signal.aborted) return;
        this.updateState({
          status: this.state.snapshot ? "stale" : "error",
          error: errorMessage(error),
        });
      })
      .finally(() => {
        if (this.abortController === currentAbortController) {
          this.abortController = null;
          this.inFlight = null;
        }
      });
    return this.inFlight;
  }

  stop() {
    this.running = false;
    if (this.intervalId !== null) {
      this.clearIntervalFn.call(globalThis, this.intervalId);
      this.intervalId = null;
    }
    this.abortController?.abort();
    this.abortController = null;
    this.inFlight = null;
  }

  updateState(patch) {
    this.state = { ...this.state, ...patch };
    this.onStateChange(this.getState());
  }
}
