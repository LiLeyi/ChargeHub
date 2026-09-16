# -*- coding: utf-8 -*-
"""ChargeHub 大数据路径：作业树、WSL 成品、HDFS、Spark 输出。

只解析路径，不连库、不改订单/余额/桩状态。
"""
from __future__ import annotations

import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HOME = Path(os.environ.get("HOME") or Path.home())

RAW_DIR_CANDIDATES = [
    ROOT / "dataset" / "04.数据集最终版",
    Path("/mnt/d/大三小学期/计算机软件实训/ChargeHub/dataset/04.数据集最终版"),
    HOME / "ChargeHub-Linux" / "dataset" / "04.数据集最终版",
]

BIG_DIR_CANDIDATES = [
    ROOT / "dataset" / "big",
    Path("/mnt/d/大三小学期/计算机软件实训/ChargeHub/dataset/big"),
    HOME / "ChargeHub-Linux" / "dataset" / "big",
]

OUT_DIR_CANDIDATES = [
    Path(os.environ["CHARGEHUB_SPARK_OUT"]) if os.environ.get("CHARGEHUB_SPARK_OUT") else None,
    ROOT / "bigdata" / "output",
    HOME / "ChargeHub-Linux" / "bigdata" / "output",
    HOME / "projects" / "ChargeHub" / "bigdata" / "output",
    Path("/mnt/d/大三小学期/计算机软件实训/ChargeHub/bigdata/output"),
]


def first_existing(cands, file_name: str | None = None) -> Path | None:
    for p in cands:
        if p is None:
            continue
        target = p / file_name if file_name else p
        if target.exists():
            return p if file_name else target
    return None


def raw_dir() -> Path:
    found = first_existing(RAW_DIR_CANDIDATES, "nvv2t.csv")
    return found or RAW_DIR_CANDIDATES[0]


def big_dir() -> Path:
    found = first_existing(BIG_DIR_CANDIDATES, "sessions.csv")
    return found or BIG_DIR_CANDIDATES[0]


def out_dir() -> Path:
    found = first_existing(OUT_DIR_CANDIDATES, "spark_report.json")
    if found:
        return found
    for p in OUT_DIR_CANDIDATES:
        if p is not None:
            return p
    return ROOT / "bigdata" / "output"


def report_path() -> Path:
    return out_dir() / "spark_report.json"


def hdfs_uri() -> str:
    return os.environ.get("CHARGEHUB_HDFS", "hdfs://localhost:9000").rstrip("/")


def input_uri() -> str:
    """HDFS 优先；NameNode 不通时由 spark_analyze 回退 file://。"""
    if os.environ.get("CHARGEHUB_DATA_URI"):
        return os.environ["CHARGEHUB_DATA_URI"].rstrip("/")
    return hdfs_uri() + "/chargehub/dataset"
