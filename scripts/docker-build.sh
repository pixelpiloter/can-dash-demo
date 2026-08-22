#!/usr/bin/env bash
# Build cluster-logic-kanzi inside Docker (Linux). No Windows host build.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${CLUSTER_LOGIC_DOCKER_IMAGE:-ubuntu:22.04}"
BUILD_DIR="${CLUSTER_LOGIC_BUILD_DIR:-build-linux}"

echo "[docker-build] root=$ROOT image=$IMAGE out=$BUILD_DIR"

docker run --rm \
  --entrypoint bash \
  -v "$ROOT:/src:rw" \
  -w /src \
  "$IMAGE" \
  -lc "
    set -euo pipefail
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq cmake g++ libyaml-cpp-dev pkg-config
    cmake --version | head -1
    g++ --version | head -1
    # Prefer system yaml-cpp; ignore broken/partial vendored tree if present
    cmake -S . -B '${BUILD_DIR}' -DCMAKE_BUILD_TYPE=Release
    cmake --build '${BUILD_DIR}' -j\"\$(nproc)\"
    echo '[docker-build] OK'
    ls -la '${BUILD_DIR}/cluster-logic-kanzi' '${BUILD_DIR}/Release/cluster-logic-kanzi' 2>/dev/null || find '${BUILD_DIR}' -name cluster-logic-kanzi -type f
  "
