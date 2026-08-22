#!/usr/bin/env bash
# Cross-build aarch64 binary (buildroot SDK). UI 后端由 CLUSTER_LOGIC_UI 选择：
#   CLUSTER_LOGIC_UI=none  → 控制台/MVP（数据源验证）
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# 通过环境变量指定 aarch64 buildroot SDK 根目录
SDK="${CLUSTER_LOGIC_AARCH64_SDK:-}"
UI="${CLUSTER_LOGIC_UI:-kanzi}"

case "$UI" in
  kanzi) BUILD_DIR="${CLUSTER_LOGIC_BUILD_DIR:-build-aarch64-kanzi}" ;;
  qt)    BUILD_DIR="${CLUSTER_LOGIC_BUILD_DIR:-build-aarch64-qt}" ;;
  none)  BUILD_DIR="${CLUSTER_LOGIC_BUILD_DIR:-build-aarch64}" ;;
  *) echo "unknown CLUSTER_LOGIC_UI=$UI (kanzi|qt|none)" >&2; exit 1 ;;
esac

TOOLCHAIN="$SDK/share/buildroot/toolchainfile.cmake"
if [[ ! -f "$TOOLCHAIN" ]]; then
  echo "missing toolchain: $TOOLCHAIN" >&2
  exit 1
fi

# yaml-cpp：优先系统库；跨编译/离线时 CMake 会自动 FetchContent 拉取。
# 如需离线预置，可手动 vendor 到 third_party/yaml-cpp（目录已被 gitignore）。

echo "[cross] root=$ROOT"
echo "[cross] sdk=$SDK"
echo "[cross] out=$BUILD_DIR"
echo "[cross] ui=$UI"

cmake -S "$ROOT" -B "$ROOT/$BUILD_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCLUSTER_LOGIC_UI="$UI" \
  -DYAML_BUILD_SHARED_LIBS=OFF

cmake --build "$ROOT/$BUILD_DIR" -j"$(nproc)"

BIN=$(find "$ROOT/$BUILD_DIR" -name 'cluster-logic*' -type f -perm -u+x | head -1)
file "$BIN"
echo "[cross] OK: $BIN"
