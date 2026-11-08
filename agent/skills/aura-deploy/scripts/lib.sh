#!/usr/bin/env bash
# 部署脚本公共函数

log()  { printf "\033[1;34m[aura]\033[0m %s\n" "$*"; }
ok()   { printf "\033[1;32m[ ok ]\033[0m %s\n" "$*"; }
warn() { printf "\033[1;33m[warn]\033[0m %s\n" "$*"; }
die()  { printf "\033[1;31m[fail]\033[0m %s\n" "$*" >&2; exit 1; }

require_root() {
    [ "$(id -u)" -eq 0 ] || die "需要 root 权限，请用 sudo bash $0 执行"
}

have_cmd() {
    command -v "$1" >/dev/null 2>&1
}

# 从脚本位置向上找到包含 algorithm/Makefile 的仓库根目录
repo_root() {
    local dir
    dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    local i
    for i in 1 2 3 4 5; do
        if [ -f "$dir/algorithm/Makefile" ] && [ -f "$dir/service/CMakeLists.txt" ]; then
            printf '%s\n' "$dir"
            return 0
        fi
        dir="$(dirname "$dir")"
    done
    return 1
}

# 检查某个内核模块是否已加载
module_loaded() {
    lsmod 2>/dev/null | awk '{print $1}' | grep -qx "$1"
}
