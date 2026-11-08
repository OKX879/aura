#!/usr/bin/env bash
# 构建并安装 AuraService，可用 INSTALL_DIR 指定安装目录
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
install_dir="${INSTALL_DIR:-/opt/aura/app}"

# 先编译算法静态库
make -C "$here/algorithm"

cmake -S "$here/service" -B "$here/service/build" \
      -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "$here/service/build" -j"$(nproc)"

install -Dm755 "$here/service/build/AuraService" "$install_dir/AuraService"
echo "AuraService -> $install_dir/AuraService"
