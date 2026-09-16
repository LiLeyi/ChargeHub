# -*- coding: utf-8 -*-
"""WSL 入口：去 CRLF 后执行 Hadoop/Spark 安装或流水线。"""
from __future__ import annotations

import os
import stat
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
os.environ.setdefault("HOME", "/home/bit")
os.environ.setdefault("USER", "bit")
os.environ.setdefault("CHARGEHUB_SRC", "/mnt/d/大三小学期/计算机软件实训/ChargeHub")


def strip_scripts() -> None:
    root = HERE.parent
    for p in list(HERE.glob("*.sh")) + list(root.glob("*.py")):
        data = p.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
        p.write_bytes(data)
        if p.suffix == ".sh":
            p.chmod(p.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def main() -> None:
    strip_scripts()
    action = sys.argv[1] if len(sys.argv) > 1 else "setup"
    if action == "setup":
        script = HERE / "setup_hadoop_spark.sh"
    elif action == "pipeline":
        script = HERE / "run_pipeline.sh"
    elif action == "hdfs":
        script = HERE / "start_hdfs.sh"
    elif action == "spark":
        script = HERE / "run_spark_only.sh"
    else:
        raise SystemExit("usage: bootstrap_wsl.py [setup|pipeline|hdfs|spark]")
    raise SystemExit(subprocess.call(["/bin/bash", str(script)]))


if __name__ == "__main__":
    main()
