#!/usr/bin/env bash
# 步骤 5：编译 libaura 与 AuraService
set -euo pipefail
. "$(dirname "$0")/lib.sh"

require_root
root="$(repo_root)" || die "找不到仓库根目录，请在 AURA 仓库内执行"
log "仓库根目录 $root"

log "编译算法库"
make -C "$root/algorithm"
[ -f "$root/algorithm/libaura.a" ] || die "libaura.a 未生成"

log "配置服务"
cmake -S "$root/service" -B "$root/service/build" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DAURA_LIVE_CORE_DIR="$root/algorithm" \
    -DAURA_RADAR_DIR="$root/radar"

log "编译服务"
cmake --build "$root/service/build" -j"$(nproc)"

[ -x "$root/service/build/AuraService" ] || die "AuraService 未生成"
ok "编译完成：$root/service/build/AuraService"
ok "下一步执行 05-install-service.sh"
