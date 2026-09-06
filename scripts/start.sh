#!/bin/bash
# 终端备用。日常请双击 scripts/打开运营后台.bat 和 打开用户端.bat。
set -e
APP="$HOME/ChargeHub-Linux"
if [ ! -x "$APP/admin/run.sh" ] || [ ! -x "$APP/user/run.sh" ]; then
  echo "还没有编过成品，请先执行： bash scripts/rebuild.sh"
  exit 1
fi
export DISPLAY="${DISPLAY:-:0}"
export QT_QPA_PLATFORM=xcb
mkdir -p "$HOME/.xdg-runtime"
chmod 700 "$HOME/.xdg-runtime"
export XDG_RUNTIME_DIR="$HOME/.xdg-runtime"
export LANG="${LANG:-zh_CN.UTF-8}"
cd "$APP/admin"
nohup bash ./run.sh >/tmp/chargehub_admin.log 2>&1 &
sleep 1
cd "$APP/user"
nohup bash ./run.sh >/tmp/chargehub_user.log 2>&1 &
echo "管理端 admin / 123456"
echo "用户端 13800138000 / 123456  服务器 127.0.0.1:8888"
