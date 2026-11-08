#!/usr/bin/env bash
# 步骤 2：安装编译依赖
set -euo pipefail
. "$(dirname "$0")/lib.sh"

require_root
log "安装系统依赖"

export DEBIAN_FRONTEND=noninteractive
apt-get update

packages=(
    build-essential cmake pkg-config git
    "linux-headers-$(uname -r)"
    libssl-dev libsqlite3-dev libjsoncpp-dev
    zlib1g-dev libbrotli-dev libc-ares-dev uuid-dev
)

apt-get install -y --no-install-recommends "${packages[@]}"

ok "依赖安装完成"
ok "下一步执行 02-ch347-module.sh"
