import json
import urllib.request

with urllib.request.urlopen("http://127.0.0.1:5000/api/bigdata", timeout=5) as r:
    d = json.loads(r.read().decode("utf-8"))
print(
    "ready",
    d.get("ready"),
    "rfm",
    len(d.get("rfm") or []),
    "imp",
    len(d.get("importances") or []),
    "ano",
    (d.get("anomalies") or {}).get("count"),
    "models",
    len(d.get("models") or []),
    "engine",
    (d.get("kpis") or {}).get("engine"),
)
with urllib.request.urlopen("http://127.0.0.1:5000/", timeout=5) as r:
    html = r.read().decode("utf-8")
print("html", "CHARGEHUB" in html, "用户分层" in html, "特征贡献" in html, "Hadoop" not in html)
