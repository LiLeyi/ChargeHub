#!/bin/bash
# 在 WSL Ubuntu-22.04 为 ChargeHub 安装 OpenJDK11 + Hadoop 3.3.6 + Spark 3.5.1。
# 幂等：已存在发行包则跳过下载。HDFS 格式化只在 namenode 目录为空时执行。
set -euo pipefail
export HOME="${HOME:-/home/bit}"
export USER="${USER:-bit}"
if [ ! -d "$HOME" ] || [ "$HOME" = "/" ]; then
  export HOME=/home/bit
fi

OPT="$HOME/opt"
HADOOP_VER=3.3.6
SPARK_VER="${SPARK_VER:-3.5.5}"
HADOOP_DIR="$OPT/hadoop-$HADOOP_VER"
SPARK_DIR="$OPT/spark-$SPARK_VER-bin-hadoop3"
ENV_FILE="$OPT/chargehub-spark.env"
SRC="${CHARGEHUB_SRC:-/mnt/d/大三小学期/计算机软件实训/ChargeHub}"
CONF_SRC="$SRC/bigdata/conf"

mkdir -p "$OPT" "$OPT/hadoop-data/tmp" "$HOME/.ssh"

echo "==== install packages ===="
sudo -n apt-get update -qq || sudo apt-get update -qq
sudo -n DEBIAN_FRONTEND=noninteractive apt-get install -y \
  openjdk-11-jdk-headless wget curl openssh-client openssh-server python3-pip python3-numpy \
  || sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
  openjdk-11-jdk-headless wget curl openssh-client openssh-server python3-pip python3-numpy

JAVA_HOME="$(dirname "$(dirname "$(readlink -f "$(command -v java)")")")"
if [ ! -x "$JAVA_HOME/bin/java" ]; then
  if [ -x /usr/lib/jvm/java-11-openjdk-amd64/bin/java ]; then
    JAVA_HOME=/usr/lib/jvm/java-11-openjdk-amd64
  fi
fi
echo "JAVA_HOME=$JAVA_HOME"
"$JAVA_HOME/bin/java" -version

download() {
  local url="$1" dest="$2"
  if [ -s "$dest" ]; then
    echo "keep $dest ($(du -h "$dest" | awk '{print $1}'))"
    return 0
  fi
  rm -f "$dest"
  echo "GET $url"
  if wget -c --progress=dot:giga -O "$dest.part" "$url"; then
    if [ -s "$dest.part" ]; then
      mv "$dest.part" "$dest"
      return 0
    fi
  fi
  rm -f "$dest.part"
  return 1
}

echo "==== download Hadoop / Spark (清华镜像，失败再走 archive) ===="
HADOOP_TGZ="$OPT/hadoop-$HADOOP_VER.tar.gz"
set +e
download "https://mirrors.tuna.tsinghua.edu.cn/apache/hadoop/common/hadoop-$HADOOP_VER/hadoop-$HADOOP_VER.tar.gz" "$HADOOP_TGZ"
if [ ! -s "$HADOOP_TGZ" ]; then
  download "https://archive.apache.org/dist/hadoop/common/hadoop-$HADOOP_VER/hadoop-$HADOOP_VER.tar.gz" "$HADOOP_TGZ"
fi
rm -f "$OPT/spark-3.5.1-bin-hadoop3.tgz"
SPARK_TGZ=""
for SPARK_VER in 3.5.5 3.5.3 3.5.1; do
  SPARK_DIR="$OPT/spark-$SPARK_VER-bin-hadoop3"
  cand="$OPT/spark-$SPARK_VER-bin-hadoop3.tgz"
  if download "https://mirrors.tuna.tsinghua.edu.cn/apache/spark/spark-$SPARK_VER/spark-$SPARK_VER-bin-hadoop3.tgz" "$cand" \
    || download "https://mirrors.huaweicloud.com/apache/spark/spark-$SPARK_VER/spark-$SPARK_VER-bin-hadoop3.tgz" "$cand" \
    || download "https://archive.apache.org/dist/spark/spark-$SPARK_VER/spark-$SPARK_VER-bin-hadoop3.tgz" "$cand"; then
    SPARK_TGZ="$cand"
    echo "using Spark $SPARK_VER"
    break
  fi
done
set -e
test -s "$HADOOP_TGZ"
test -n "$SPARK_TGZ" && test -s "$SPARK_TGZ"

if [ ! -x "$HADOOP_DIR/bin/hadoop" ]; then
  echo "==== extract Hadoop ===="
  tar -xzf "$HADOOP_TGZ" -C "$OPT"
fi
if [ ! -x "$SPARK_DIR/bin/spark-submit" ]; then
  echo "==== extract Spark ===="
  tar -xzf "$SPARK_TGZ" -C "$OPT"
fi
ln -sfn "$HADOOP_DIR" "$OPT/hadoop"
ln -sfn "$SPARK_DIR" "$OPT/spark"

echo "==== write Hadoop / Spark conf ===="
if [ -f "$CONF_SRC/core-site.xml" ]; then
  cp -f "$CONF_SRC/core-site.xml" "$HADOOP_DIR/etc/hadoop/core-site.xml"
  cp -f "$CONF_SRC/hdfs-site.xml" "$HADOOP_DIR/etc/hadoop/hdfs-site.xml"
fi
if [ -f "$CONF_SRC/spark-defaults.conf" ]; then
  cp -f "$CONF_SRC/spark-defaults.conf" "$SPARK_DIR/conf/spark-defaults.conf"
fi
# 覆盖 tmp 目录为当前用户
sed -i "s|/home/bit/opt/hadoop-data|$OPT/hadoop-data|g" "$HADOOP_DIR/etc/hadoop/core-site.xml" || true
sed -i "s|/home/bit/opt/hadoop-data|$OPT/hadoop-data|g" "$HADOOP_DIR/etc/hadoop/hdfs-site.xml" || true

HADOOP_ENV="$HADOOP_DIR/etc/hadoop/hadoop-env.sh"
if ! grep -q "CHARGEHUB_JAVA" "$HADOOP_ENV" 2>/dev/null; then
  cat >> "$HADOOP_ENV" <<EOF

# CHARGEHUB_JAVA
export JAVA_HOME=$JAVA_HOME
export HDFS_NAMENODE_USER=$USER
export HDFS_DATANODE_USER=$USER
export HDFS_SECONDARYNAMENODE_USER=$USER
export YARN_RESOURCEMANAGER_USER=$USER
export YARN_NODEMANAGER_USER=$USER
EOF
fi

cat > "$ENV_FILE" <<EOF
export JAVA_HOME=$JAVA_HOME
export HADOOP_HOME=$OPT/hadoop
export SPARK_HOME=$OPT/spark
export HADOOP_CONF_DIR=\$HADOOP_HOME/etc/hadoop
export HADOOP_USER_NAME=$USER
export HDFS_NAMENODE_USER=$USER
export HDFS_DATANODE_USER=$USER
export HDFS_SECONDARYNAMENODE_USER=$USER
export YARN_RESOURCEMANAGER_USER=$USER
export YARN_NODEMANAGER_USER=$USER
export PATH=\$SPARK_HOME/bin:\$HADOOP_HOME/bin:\$HADOOP_HOME/sbin:\$JAVA_HOME/bin:\$PATH
export PYSPARK_PYTHON=python3
export PYSPARK_DRIVER_PYTHON=python3
export CHARGEHUB_SRC="$SRC"
EOF

if ! grep -q chargehub-spark.env "$HOME/.bashrc" 2>/dev/null; then
  echo '[ -f "$HOME/opt/chargehub-spark.env" ] && . "$HOME/opt/chargehub-spark.env"' >> "$HOME/.bashrc"
fi

echo "==== ssh localhost for HDFS scripts ===="
sudo -n service ssh start 2>/dev/null || sudo service ssh start 2>/dev/null || true
if [ ! -f "$HOME/.ssh/id_ed25519" ] && [ ! -f "$HOME/.ssh/id_rsa" ]; then
  ssh-keygen -t ed25519 -N "" -f "$HOME/.ssh/id_ed25519"
fi
AUTH="$HOME/.ssh/authorized_keys"
touch "$AUTH"
for pub in "$HOME/.ssh/id_ed25519.pub" "$HOME/.ssh/id_rsa.pub"; do
  if [ -f "$pub" ] && ! grep -qF "$(cat "$pub")" "$AUTH" 2>/dev/null; then
    cat "$pub" >> "$AUTH"
  fi
done
chmod 700 "$HOME/.ssh"
chmod 600 "$AUTH"
grep -q "StrictHostKeyChecking no" "$HOME/.ssh/config" 2>/dev/null || cat >> "$HOME/.ssh/config" <<EOF
Host localhost
  StrictHostKeyChecking no
  UserKnownHostsFile /dev/null
EOF
chmod 600 "$HOME/.ssh/config"
ssh -o BatchMode=yes -o ConnectTimeout=5 localhost true 2>/dev/null || true

echo "==== format namenode if empty ===="
if [ ! -d "$OPT/hadoop-data/namenode/current" ]; then
  . "$ENV_FILE"
  "$HADOOP_DIR/bin/hdfs" namenode -format -force -nonInteractive
fi

echo "SETUP_HADOOP_OK"
echo "HADOOP_HOME=$OPT/hadoop"
echo "SPARK_HOME=$OPT/spark"
echo "source $ENV_FILE"
