#!/usr/bin/env bash
# x86 开发构建（Qt6/QML 原型宿主 + 控制台宿主可选）。
#   ./scripts/build-qt-x86.sh            # 默认 CLUSTER_LOGIC_UI=qt
#   CLUSTER_LOGIC_UI=none ./scripts/build-qt-x86.sh   # 控制台/MVP
#   CLUSTER_LOGIC_UI=kanzi ...            # 交叉编译时用 cross-aarch64.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UI="${CLUSTER_LOGIC_UI:-qt}"
BUILD_DIR="${CLUSTER_LOGIC_BUILD_DIR:-build-x86-$UI}"

echo "[x86] root=$ROOT"
echo "[x86] ui=$UI out=$BUILD_DIR"

cmake -S "$ROOT" -B "$ROOT/$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCLUSTER_LOGIC_UI="$UI"

cmake --build "$ROOT/$BUILD_DIR" -j"$(nproc)"

BIN=$(find "$ROOT/$BUILD_DIR" -maxdepth 1 -type f -perm -u+x \( -name 'cluster-logic*' \) | head -1)
echo "[x86] OK: $BIN"
echo "[x86] run: cd $ROOT/$BUILD_DIR && $BIN --inject-demo"
