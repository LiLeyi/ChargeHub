# -*- coding: utf-8 -*-
"""ChargeHub 大数据路径：作业树、WSL 成品、HDFS、Spark 输出。

所有 Spark / enrich / apply / Flask 找文件都走这里，避免各写一套绝对路径。
只解析路径，不连库、不改订单/余额/桩状态。

环境变量:
    CHARGEHUB_SPARK_OUT  覆盖输出目录（deploydash.sh 会设到 ChargeHub-Linux/bigdata/output）
    CHARGEHUB_HDFS       NameNode，默认 hdfs://localhost:9000
    CHARGEHUB_DATA_URI   直接指定输入根（跳过 HDFS 拼接）
    HOME                 WSL 用户家目录
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
    """返回候选里第一个存在的目录（给了 file_name）或文件（没给 file_name）。"""
    for p in cands:
        if p is None:
            continue
        target = p / file_name if file_name else p
        if target.exists():
            return p if file_name else target
    return None


def raw_dir() -> Path:
    """原始表目录（含 nvv2t.csv）。找不到时仍返回作业树默认路径，让报错信息可读。"""
    found = first_existing(RAW_DIR_CANDIDATES, "nvv2t.csv")
    return found or RAW_DIR_CANDIDATES[0]


def big_dir() -> Path:
    """扩样后的生产 CSV 目录（sessions.csv）。"""
    found = first_existing(BIG_DIR_CANDIDATES, "sessions.csv")
    return found or BIG_DIR_CANDIDATES[0]


def out_dir() -> Path:
    """spark_report.json / dash_charts.json 所在目录。优先已有报告的位置。"""
    found = first_existing(OUT_DIR_CANDIDATES, "spark_report.json")
    if found:
        return found
    for p in OUT_DIR_CANDIDATES:
        if p is not None:
            return p
    return ROOT / "bigdata" / "output"


def report_path() -> Path:
    """spark_report.json 的完整路径。"""
    return out_dir() / "spark_report.json"


def hdfs_uri() -> str:
    """NameNode 根 URI，末尾无斜杠。"""
    return os.environ.get("CHARGEHUB_HDFS", "hdfs://localhost:9000").rstrip("/")


def input_uri() -> str:
    """Spark 输入根。HDFS 优先；NameNode 不通时由 spark_analyze.resolve_input 改 file://。"""
    if os.environ.get("CHARGEHUB_DATA_URI"):
        return os.environ["CHARGEHUB_DATA_URI"].rstrip("/")
    return hdfs_uri() + "/chargehub/dataset"
