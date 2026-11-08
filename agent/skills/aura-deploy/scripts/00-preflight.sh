#!/usr/bin/env bash
# 步骤 1：部署前环境检查
set -euo pipefail
. "$(dirname "$0")/lib.sh"

require_root
log "环境检查"

arch="$(uname -m)"
[ "$arch" = "aarch64" ] && ok "架构 $arch" || warn "架构是 $arch，本项目只在 aarch64 上验证过"

if [ -r /etc/os-release ]; then
    . /etc/os-release
    case "${VERSION_ID:-}" in
        22.04) ok "系统 $PRETTY_NAME" ;;
        *)     warn "系统是 ${PRETTY_NAME:-未知}，验证过的版本是 Ubuntu 22.04" ;;
    esac
fi

kernel="$(uname -r)"
if [ -d "/lib/modules/$kernel/build" ]; then
    ok "内核头文件 $kernel"
else
    die "缺少 /lib/modules/$kernel/build，先安装 linux-headers-$kernel"
fi

for cmd in gcc make cmake git; do
    if have_cmd "$cmd"; then
        ok "$cmd $($cmd --version 2>/dev/null | head -1)"
    else
        warn "$cmd 未安装，下一步会安装"
    fi
done

avail_kb="$(df -Pk /opt 2>/dev/null | awk 'NR==2 {print $4}')"
if [ -n "${avail_kb:-}" ] && [ "$avail_kb" -gt 5242880 ]; then
    ok "/opt 剩余 $((avail_kb / 1024 / 1024)) GB"
else
    warn "/opt 剩余不足 5 GB，Drogon 编译可能失败"
fi

for dev in /dev/spidev8.0 /dev/ttyACM0; do
    if [ -e "$dev" ]; then
        ok "设备节点 $dev"
    else
        warn "缺少 $dev，将在第 3 步安装 CH347 模块后检查"
    fi
done

if have_cmd modinfo && modinfo ch34x_mphsi_master >/dev/null 2>&1; then
    ok "CH347 内核模块已安装"
else
    warn "CH347 内核模块未安装，第 3 步会编译"
fi

if [ -f /usr/local/lib/cmake/Drogon/DrogonConfig.cmake ]; then
    ok "Drogon 已安装"
else
    warn "Drogon 未安装，第 4 步会编译"
fi

ok "环境检查完成，可以执行 01-install-deps.sh"
