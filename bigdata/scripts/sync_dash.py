# -*- coding: utf-8 -*-
from pathlib import Path
import shutil

src = Path("/mnt/d/大三小学期/计算机软件实训/ChargeHub")
for dest in (
    Path("/home/bit/ChargeHub-Linux"),
    Path("/home/bit/projects/ChargeHub"),
):
    (dest / "dashboard").mkdir(parents=True, exist_ok=True)
    (dest / "bigdata" / "output").mkdir(parents=True, exist_ok=True)
    shutil.copy2(src / "dashboard" / "index.html", dest / "dashboard" / "index.html")
    shutil.copy2(src / "dashboard" / "app.py", dest / "dashboard" / "app.py")
    out = src / "bigdata" / "output"
    if out.exists():
        for p in out.glob("*.json"):
            shutil.copy2(p, dest / "bigdata" / "output" / p.name)
print("SYNC_OK")
