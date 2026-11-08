#!/usr/bin/env bash
# 步骤 3：编译安装 CH347 内核模块，固定 SPI 总线号
set -euo pipefail
. "$(dirname "$0")/lib.sh"

require_root
log "检查 CH347 内核模块"

if modinfo -n ch34x_mphsi_master >/dev/null 2>&1; then
    ok "模块已安装：$(modinfo -n ch34x_mphsi_master)"
else
    warn "模块未安装，开始编译 WCH 官方驱动"
    mkdir -p /opt/aura/src
    if [ -d /opt/aura/src/ch34x_mphsi_master_linux/.git ]; then
        git -C /opt/aura/src/ch34x_mphsi_master_linux pull --ff-only
    else
        git clone https://github.com/WCHSoftGroup/ch34x_mphsi_master_linux.git \
            /opt/aura/src/ch34x_mphsi_master_linux
    fi
    make -C /opt/aura/src/ch34x_mphsi_master_linux/driver
    make -C /opt/aura/src/ch34x_mphsi_master_linux/driver install
    ok "模块已安装到 /lib/modules/$(uname -r)"
fi

echo "ch34x_mphsi_master" > /etc/modules-load.d/ch34x.conf

root="$(repo_root || true)"
if [ -n "${root:-}" ] && [ -f "$root/deploy/99-aura-ch34x.conf" ]; then
    install -m 644 "$root/deploy/99-aura-ch34x.conf" /etc/modprobe.d/99-aura-ch34x.conf
    ok "已固定 SPI 总线号为 8（下次重启生效）"
fi

modprobe ch34x_mphsi_master 2>/dev/null || die "modprobe 失败，检查 dmesg 和内核头文件版本"
module_loaded ch34x_mphsi_master || die "模块未加载"

if ls /sys/bus/spi/devices 2>/dev/null | grep -q '^spi8\.'; then
    ok "SPI 总线 spi8 已就绪"
else
    warn "未发现 spi8.*。请拔插 CH347，然后执行 bash /usr/local/bin/aura-ch347-setup.sh"
    warn "可用总线号：$(ls /sys/bus/spi/devices 2>/dev/null | tr '\n' ' ')"
fi

for dev in /dev/spidev8.0 /dev/ttyACM0; do
    [ -e "$dev" ] && ok "设备节点 $dev" || warn "缺少 $dev"
done

ok "下一步执行 03-drogon.sh"
