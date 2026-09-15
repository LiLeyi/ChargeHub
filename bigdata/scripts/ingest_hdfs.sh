#!/bin/bash
# 把 dataset/big 三个 CSV 放到 HDFS /chargehub/dataset。
set -euo pipefail
export HOME="${HOME:-/home/bit}"
. "$HOME/opt/chargehub-spark.env"
SRC="${CHARGEHUB_SRC:-/mnt/d/大三小学期/计算机软件实训/ChargeHub}"
BIG="$SRC/dataset/big"
if [ ! -f "$BIG/sessions.csv" ]; then
  echo "missing $BIG/sessions.csv, run expand_dataset.py first"
  exit 1
fi
"$HADOOP_HOME/bin/hdfs" dfs -mkdir -p /chargehub/dataset
"$HADOOP_HOME/bin/hdfs" dfs -put -f "$BIG/sessions.csv" "$BIG/stations.csv" "$BIG/telemetry.csv" /chargehub/dataset/
"$HADOOP_HOME/bin/hdfs" dfs -ls /chargehub/dataset
echo "INGEST_OK"
