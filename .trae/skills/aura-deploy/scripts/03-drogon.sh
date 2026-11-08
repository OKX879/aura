#!/usr/bin/env bash
# 步骤 4：源码编译安装 Drogon 1.9.11
set -euo pipefail
. "$(dirname "$0")/lib.sh"

require_root

if [ -f /usr/local/lib/cmake/Drogon/DrogonConfig.cmake ]; then
    ok "Drogon 已安装，跳过"
    ok "下一步执行 04-build-aura.sh"
    exit 0
fi

log "编译 Drogon（首次约 10–20 分钟）"
mkdir -p /opt/aura/src
if [ -d /opt/aura/src/drogon/.git ]; then
    git -C /opt/aura/src/drogon pull --ff-only
    git -C /opt/aura/src/drogon submodule update --init --recursive
else
    git clone --recursive https://github.com/drogonframework/drogon.git /opt/aura/src/drogon
fi

log "编译 trantor"
cmake -S /opt/aura/src/drogon/trantor -B /opt/aura/src/drogon/trantor/build \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_TESTING=OFF
cmake --build /opt/aura/src/drogon/trantor/build -j"$(nproc)"
cmake --install /opt/aura/src/drogon/trantor/build

log "编译 drogon"
cmake -S /opt/aura/src/drogon -B /opt/aura/src/drogon/build \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DBUILD_EXAMPLES=OFF -DBUILD_CTL=OFF -DBUILD_TESTING=OFF \
    -DBUILD_ORM=OFF -DBUILD_REDIS=OFF -DBUILD_SQLITE=OFF \
    -DBUILD_MYSQL=OFF -DBUILD_POSTGRESQL=OFF
cmake --build /opt/aura/src/drogon/build -j"$(nproc)"
cmake --install /opt/aura/src/drogon/build

[ -f /usr/local/lib/cmake/Drogon/DrogonConfig.cmake ] || die "安装后仍找不到 DrogonConfig.cmake"
ok "Drogon 安装完成"
ok "下一步执行 04-build-aura.sh"
