#!/bin/bash
# 不编译，只打开管理端、用户端、运营大屏。请在 Ubuntu 桌面里打开终端再执行。
set -e
APP="$HOME/ChargeHub-Linux"
if [ ! -x "$APP/admin/run.sh" ] || [ ! -x "$APP/user/run.sh" ]; then
  echo "还没有编过成品，请先在工程目录执行："
  echo "  bash scripts/rebuild.sh"
  exit 1
fi
pkill -f '/ChargeHub-Linux/admin/adminserver' 2>/dev/null || true
pkill -f '/ChargeHub-Linux/user/userclient' 2>/dev/null || true
pkill -f 'ChargeHub-Linux/dashboard/app.py' 2>/dev/null || true
sleep 1
cd "$APP/admin"
nohup bash ./run.sh >/tmp/chargehub_admin.log 2>&1 &
sleep 1
cd "$APP/user"
nohup bash ./run.sh >/tmp/chargehub_user.log 2>&1 &
if [ -f "$APP/dashboard/app.py" ]; then
  cd "$APP/dashboard"
  CHARGEHUB_DB="$APP/admin/data/chargehub.db" nohup python3 app.py >/tmp/chargehub_dash.log 2>&1 &
fi
echo
echo "已启动三个程序："
echo "  管理端窗口：账号 admin    密码 123456"
echo "  用户端窗口：手机 13800138000  密码 123456"
echo "             服务器地址请填  127.0.0.1:8888"
echo "  大屏：用浏览器打开  http://127.0.0.1:5000"
echo
