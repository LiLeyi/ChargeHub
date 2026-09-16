#!/bin/bash
# 扩样 →（可选）HDFS → Spark 分析 → 写分析表。
# HDFS 不通时自动走 file://，作业仍算成功。
set -euo pipefail
export HOME="${HOME:-/home/bit}"
if [ -f "$HOME/opt/chargehub-spark.env" ]; then
  . "$HOME/opt/chargehub-spark.env"
fi
SRC="${CHARGEHUB_SRC:-/mnt/d/大三小学期/计算机软件实训/ChargeHub}"
BD="$SRC/bigdata"
python3 - <<'PY'
from pathlib import Path
for rel in [
    "bigdata/expand_dataset.py",
    "bigdata/spark_analyze.py",
    "bigdata/apply_results.py",
    "bigdata/paths.py",
    "bigdata/scripts/run_pipeline.sh",
]:
    p = Path("/mnt/d/大三小学期/计算机软件实训/ChargeHub") / rel
    if p.is_file():
        p.write_bytes(p.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n"))
PY

cd "$BD"
echo "==== expand dataset ===="
python3 expand_dataset.py

if [ -x "${HADOOP_HOME:-}/bin/hdfs" ]; then
  if "$HADOOP_HOME/bin/hdfs" dfs -ls / >/dev/null 2>&1; then
    echo "==== ingest HDFS ===="
    bash "$BD/scripts/ingest_hdfs.sh" || echo "HDFS ingest skipped"
  else
    echo "HDFS not up, Spark will use file://"
  fi
fi

echo "==== spark-submit ===="
if [ -x "${SPARK_HOME:-}/bin/spark-submit" ]; then
  "$SPARK_HOME/bin/spark-submit" --master local[*] --driver-memory 2g "$BD/spark_analyze.py"
else
  python3 "$BD/spark_analyze.py"
fi

echo "==== apply to sqlite analysis tables ===="
python3 "$BD/apply_results.py" || echo "APPLY skipped (no db yet)"
echo "PIPELINE_OK"
