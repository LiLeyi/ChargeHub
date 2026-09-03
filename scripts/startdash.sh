#!/bin/bash
# 启动运营大屏 Flask（不杀 Qt 进程）
LOG=/mnt/hgfs/ChargeHub/docs/startdash.log
{
  set -e
  echo "==== startdash $(date) ===="
  if [ ! -d /mnt/hgfs/ChargeHub ]; then
    mkdir -p /mnt/hgfs
    vmhgfs-fuse .host:/ /mnt/hgfs -o allow_other || true
  fi
  python3 -c "import flask" 2>/dev/null || apt-get install -y python3-flask
  mkdir -p /home/bit/ChargeHub-Linux/dashboard /home/bit/projects/ChargeHub/dashboard
  if [ -d /mnt/hgfs/ChargeHub/dashboard ]; then
    cp -a /mnt/hgfs/ChargeHub/dashboard/. /home/bit/ChargeHub-Linux/dashboard/
    cp -a /mnt/hgfs/ChargeHub/dashboard/. /home/bit/projects/ChargeHub/dashboard/
  fi
  pkill -u bit -f 'dashboard/app.py' || true
  pkill -f 'python3 .*app.py' || true
  sleep 1
  DB=/home/bit/ChargeHub-Linux/admin/data/chargehub.db
  if [ ! -f "$DB" ]; then
    DB=/home/bit/projects/ChargeHub/adminserver/data/chargehub.db
  fi
  chown -R bit:bit /home/bit/ChargeHub-Linux/dashboard /home/bit/projects/ChargeHub/dashboard
  cd /home/bit/ChargeHub-Linux/dashboard
  sudo -u bit env CHARGEHUB_DB="$DB" HOME=/home/bit LANG=zh_CN.UTF-8 \
    nohup python3 app.py >/tmp/chargehub_dash.log 2>&1 &
  sleep 2
  echo "--- ss ---"
  ss -lntp | grep 5000 || true
  echo "--- curl ---"
  curl -s -o /dev/null -w "HTTP %{http_code}\n" http://127.0.0.1:5000/ || true
  echo "--- log ---"
  cat /tmp/chargehub_dash.log || true
  echo "--- db ---"
  echo "$DB"
  ls -l "$DB" 2>/dev/null || true
  echo DASH_OK
} > "$LOG" 2>&1
