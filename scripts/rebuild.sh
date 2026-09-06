#!/bin/bash
# 同步源码 → 编译 → 安装到 ~/ChargeHub-Linux → 打开三个窗口
# 不要在共享盘上直接 make。请在 Ubuntu 里执行本脚本。
set -e

if [ "$(id -u)" -eq 0 ]; then
  TARGET_USER="${SUDO_USER:-bit}"
  TARGET_HOME="$(getent passwd "$TARGET_USER" | cut -d: -f6)"
  [ -n "$TARGET_HOME" ] || TARGET_HOME="/home/bit"
else
  TARGET_USER="$(id -un)"
  TARGET_HOME="$HOME"
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"
if [ -f /mnt/hgfs/ChargeHub/ChargeHub.pro ]; then
  SRC=/mnt/hgfs/ChargeHub
elif [ -f "$REPO/ChargeHub.pro" ]; then
  SRC="$REPO"
else
  echo "找不到 ChargeHub 工程。请把整个文件夹拷进 Ubuntu，或挂成共享文件夹 ChargeHub。"
  exit 1
fi

DST="$TARGET_HOME/projects/ChargeHub"
APP="$TARGET_HOME/ChargeHub-Linux"
if [ -d /mnt/hgfs/ChargeHub/docs ]; then
  LOG=/mnt/hgfs/ChargeHub/docs/rebuild.log
else
  mkdir -p "$APP"
  LOG="$APP/rebuild.log"
fi

{
  echo "==== rebuild $(date) user=$TARGET_USER src=$SRC ===="
  pkill -u "$TARGET_USER" -f '/adminserver' || true
  pkill -u "$TARGET_USER" -f '/userclient' || true
  pkill -u "$TARGET_USER" -f '/admin_server' || true
  pkill -u "$TARGET_USER" -f '/user_client' || true
  pkill -u "$TARGET_USER" -f 'dashboard/app.py' || true
  sleep 1
  export PATH=/usr/lib/qt5/bin:$PATH

  if [ -e "$DST/ChargeHub.pro" ] && [ "$SRC" -ef "$DST" ]; then
    echo "源码已在 $DST，跳过拷贝"
  else
    rm -rf "$DST"
    mkdir -p "$DST"
    cp -a "$SRC/ChargeHub.pro" "$DST/"
    cp -a "$SRC/adminserver" "$DST/"
    cp -a "$SRC/userclient" "$DST/"
    cp -a "$SRC/common" "$DST/"
    cp -a "$SRC/database" "$DST/"
    cp -a "$SRC/dashboard" "$DST/"
  fi
  python3 - <<PY
from pathlib import Path
root = Path("$DST")
for p in root.rglob("*"):
    if p.suffix.lower() in {".cpp", ".h", ".pro", ".sh"} and p.is_file():
        p.write_bytes(p.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n"))
PY
  cd "$DST"
  qmake ChargeHub.pro
  make -j"$(nproc)"
  echo BUILD_OK
  mkdir -p "$APP/admin" "$APP/user" "$APP/dashboard"
  install -m 755 "$DST/adminserver/adminserver" "$APP/admin/adminserver"
  install -m 755 "$DST/userclient/userclient" "$APP/user/userclient"
  cat > "$APP/admin/run.sh" <<'EOF'
#!/bin/bash
DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$DIR/lib:${LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$DIR/plugins"
export QT_QPA_PLATFORM=xcb
mkdir -p "$HOME/.xdg-runtime"
chmod 700 "$HOME/.xdg-runtime"
export XDG_RUNTIME_DIR="$HOME/.xdg-runtime"
cd "$DIR"
exec "$DIR/adminserver"
EOF
  cat > "$APP/user/run.sh" <<'EOF'
#!/bin/bash
DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$DIR/lib:${LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$DIR/plugins"
export QT_QPA_PLATFORM=xcb
mkdir -p "$HOME/.xdg-runtime"
chmod 700 "$HOME/.xdg-runtime"
export XDG_RUNTIME_DIR="$HOME/.xdg-runtime"
exec "$DIR/userclient"
EOF
  chmod +x "$APP/admin/run.sh" "$APP/user/run.sh"
  cp -a "$DST/dashboard/." "$APP/dashboard/"
  chown -R "$TARGET_USER:$TARGET_USER" "$APP" "$DST" 2>/dev/null || true
  echo "编好了。Windows 上双击 scripts/打开运营后台.bat 和 scripts/打开用户端.bat"
  echo UI_OK
} > "$LOG" 2>&1
