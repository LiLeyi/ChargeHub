# -*- coding: utf-8 -*-
"""启动作业大屏并检查 /api/bigdata。不改业务表。"""
from __future__ import annotations

import json
import os
import subprocess
import time
import urllib.request
from pathlib import Path

HOME = Path(os.environ.get("HOME") or "/home/bit")
DASH = HOME / "ChargeHub-Linux" / "dashboard"
os.environ["CHARGEHUB_DB"] = str(HOME / "ChargeHub-Linux" / "admin" / "data" / "chargehub.db")
os.environ["HOME"] = str(HOME)

subprocess.call(["fuser", "-k", "5000/tcp"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(1)
log = Path("/tmp/chargehub_dash.log")
proc = subprocess.Popen(
    ["python3", "app.py"],
    cwd=str(DASH),
    stdout=log.open("w"),
    stderr=subprocess.STDOUT,
    start_new_session=True,
)
ok = False
body = {}
for _ in range(20):
    time.sleep(0.5)
    try:
        with urllib.request.urlopen("http://127.0.0.1:5000/api/bigdata", timeout=2) as r:
            body = json.loads(r.read().decode("utf-8"))
            ok = True
            break
    except Exception:
        continue
print("DASH_PID", proc.pid)
print("READY", ok, "sessions", (body.get("kpis") or {}).get("sessions"), "mae", (body.get("report") or {}).get("mae"))
print("CLUSTERS", len(body.get("clusters") or []), "PATH", body.get("path"))
if not ok:
    print(log.read_text(encoding="utf-8", errors="replace")[-800:])
    raise SystemExit(1)
with urllib.request.urlopen("http://127.0.0.1:5000/api/analysis", timeout=2) as r:
    an = json.loads(r.read().decode("utf-8"))
print("ANALYSIS_MODEL", (an.get("report") or {}).get("model_version"), "engine", an.get("engine"))
print("VERIFY_OK")
