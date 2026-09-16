#!/bin/bash
# 启动单节点 HDFS。失败不阻断 Spark file:// 回退。
set -euo pipefail
export HOME="${HOME:-/home/bit}"
. "$HOME/opt/chargehub-spark.env"
sudo -n service ssh start 2>/dev/null || true
"$HADOOP_HOME/sbin/start-dfs.sh"
sleep 2
jps || true
"$HADOOP_HOME/bin/hdfs" dfsadmin -report | head -20 || true
echo "HDFS_START_OK"
