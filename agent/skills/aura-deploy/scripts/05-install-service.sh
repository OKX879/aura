#!/usr/bin/env bash
# 步骤 6：安装二进制、systemd 与 udev
set -euo pipefail
. "$(dirname "$0")/lib.sh"

require_root
root="$(repo_root)" || die "找不到仓库根目录"
install_dir="${INSTALL_DIR:-/opt/aura/app}"

[ -x "$root/service/build/AuraService" ] || die "先执行 04-build-aura.sh"

log "安装到 $install_dir"
mkdir -p "$install_dir/data"
install -m 755 "$root/service/build/AuraService" "$install_dir/AuraService"
install -m 755 "$root/deploy/aura-ch347-setup.sh" /usr/local/bin/aura-ch347-setup.sh
install -m 644 "$root/deploy/aura.service" /etc/systemd/system/aura.service
install -m 644 "$root/deploy/aura-ch347.service" /etc/systemd/system/aura-ch347.service
install -m 644 "$root/deploy/99-aura-ch347.rules" /etc/udev/rules.d/99-aura-ch347.rules
install -m 644 "$root/deploy/99-aura-ch34x.conf" /etc/modprobe.d/99-aura-ch34x.conf

udevadm control --reload-rules
systemctl daemon-reload

systemctl enable --now aura-ch347.service
systemctl enable --now aura.service
sleep 4

systemctl is-active --quiet aura-ch347.service && ok "aura-ch347.service 已启动" || warn "aura-ch347.service 未启动"
systemctl is-active --quiet aura.service && ok "aura.service 已启动" || die "aura.service 启动失败，看 journalctl -u aura.service -n 50"

if [ -f "$install_dir/data/.pair-code" ]; then
    ok "配对码 $(cat "$install_dir/data/.pair-code")"
fi

ok "下一步执行 06-verify.sh"
