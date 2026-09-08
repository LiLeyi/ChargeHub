#!/bin/sh
# 在独立临时目录构建；测试自己创建临时 SQLite，不访问演示/联调库。
set -eu
testSourceDirectory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
testBuildDirectory=$(mktemp -d /tmp/chargehub-admin-tests.XXXXXX)
cd "$testBuildDirectory"
qmake "$testSourceDirectory/adminoperations.pro"
if ! make -j2 > build.log 2>&1; then
    tail -60 build.log
    exit 1
fi
QT_QPA_PLATFORM=offscreen ./testadminoperations "$@"
