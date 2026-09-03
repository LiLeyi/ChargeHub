#!/bin/bash
# 打包 C++ Qt 用户端 / 管理端（须先 qmake && make）
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/dist/linux}"
ADMIN="$ROOT/adminserver/adminserver"
USER="$ROOT/userclient/userclient"
test -x "$ADMIN" || { echo "missing $ADMIN  (先 qmake && make)"; exit 1; }
test -x "$USER" || { echo "missing $USER"; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/admin/lib" "$OUT/admin/plugins/platforms" "$OUT/admin/plugins/sqldrivers" \
         "$OUT/user/lib" "$OUT/user/plugins/platforms" "$OUT/dashboard"

copyDeps() {
  local bin="$1" dest="$2"
  ldd "$bin" | awk '/=>/ {print $3} /^\// {print $1}' | while read -r lib; do
    [ -n "$lib" ] && [ -f "$lib" ] || continue
    case "$lib" in
      */ld-linux*|*/libc.so*|*/libm.so*|*/libpthread.so*|*/libdl.so*|*/librt.so*|*/libresolv.so*|*/libgcc_s.so*) continue ;;
    esac
    cp -L --remove-destination "$lib" "$dest/" 2>/dev/null || true
  done
}

cp -a "$ADMIN" "$OUT/admin/adminserver"
cp -a "$USER" "$OUT/user/userclient"
copyDeps "$ADMIN" "$OUT/admin/lib"
copyDeps "$USER" "$OUT/user/lib"

QTPLUG=/usr/lib/x86_64-linux-gnu/qt5/plugins
if [ -d "$QTPLUG" ]; then
  cp -L "$QTPLUG/platforms/libqxcb.so" "$OUT/admin/plugins/platforms/" 2>/dev/null || true
  cp -L "$QTPLUG/platforms/libqxcb.so" "$OUT/user/plugins/platforms/" 2>/dev/null || true
  cp -L "$QTPLUG/sqldrivers/libqsqlite.so" "$OUT/admin/plugins/sqldrivers/" 2>/dev/null || true
  copyDeps "$QTPLUG/platforms/libqxcb.so" "$OUT/admin/lib"
  copyDeps "$QTPLUG/platforms/libqxcb.so" "$OUT/user/lib"
  copyDeps "$QTPLUG/sqldrivers/libqsqlite.so" "$OUT/admin/lib"
fi

cp -a "$ROOT/dashboard/app.py" "$ROOT/dashboard/index.html" "$OUT/dashboard/"
if [ -f "$ROOT/dashboard/echarts.min.js" ]; then
  cp -a "$ROOT/dashboard/echarts.min.js" "$OUT/dashboard/"
fi

cat > "$OUT/admin/run.sh" <<'EOF'
#!/bin/bash
DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$DIR/lib:${LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$DIR/plugins"
export QT_QPA_PLATFORM=xcb
cd "$DIR"
exec "$DIR/adminserver"
EOF
cat > "$OUT/user/run.sh" <<'EOF'
#!/bin/bash
DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$DIR/lib:${LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$DIR/plugins"
export QT_QPA_PLATFORM=xcb
exec "$DIR/userclient"
EOF
cat > "$OUT/dashboard/run.sh" <<'EOF'
#!/bin/bash
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"
python3 app.py
EOF
chmod +x "$OUT/admin/run.sh" "$OUT/user/run.sh" "$OUT/dashboard/run.sh" \
         "$OUT/admin/adminserver" "$OUT/user/userclient"

cat > "$OUT/使用说明.txt" <<'EOF'
ChargeHub（Ubuntu 22.04）
========================

一、服务器电脑
1. 进入 admin 文件夹执行 ./run.sh
2. 登录 admin / 123456
3. 看窗口底部状态栏里的局域网 IP，例如 192.168.1.8:8888
4. 大屏：cd dashboard && ./run.sh ，浏览器 http://127.0.0.1:5000

二、发给同学整个 user 文件夹
执行 ./run.sh
登录页填管理端底部的 IP:8888
演示账号 13800138000 / 123456，也可自行注册。

注意：同一局域网；管理端一直开着；不要在 Windows 上运行这些文件。
EOF

echo "OK $OUT"
