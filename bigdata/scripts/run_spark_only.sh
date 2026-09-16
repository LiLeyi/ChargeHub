#!/bin/bash
set -euo pipefail
export HOME="${HOME:-/home/bit}"
. "$HOME/opt/chargehub-spark.env"
SRC="${CHARGEHUB_SRC:-/mnt/d/大三小学期/计算机软件实训/ChargeHub}"
"$SPARK_HOME/bin/spark-submit" --master local[*] --driver-memory 2g "$SRC/bigdata/spark_analyze.py"
python3 "$SRC/bigdata/apply_results.py" || echo "APPLY skipped"
echo "SPARK_ONLY_OK"
