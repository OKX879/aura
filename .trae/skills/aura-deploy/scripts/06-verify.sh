#!/usr/bin/env bash
# 步骤 7：验证服务与雷达状态
set -euo pipefail
. "$(dirname "$0")/lib.sh"

require_root
base="${AURA_BASE_URL:-http://127.0.0.1:3000}"

log "等待健康检查就绪"
healthy=0
for _ in $(seq 1 30); do
    if curl -fsS --max-time 2 "$base/api/v1/health" >/dev/null 2>&1; then
        healthy=1
        break
    fi
    sleep 1
done
[ "$healthy" -eq 1 ] || die "健康检查无响应，看 journalctl -u aura.service -n 80"

read -r running frames stalled < <(
    curl -fsS "$base/api/v1/health" | python3 -c '
import json, sys
d = json.load(sys.stdin).get("data", {})
print(int(bool(d.get("radar_running"))), int(d.get("radar_frames", 0)), int(d.get("radar_stalled_ms", 0)))
'
)

[ "$running" -eq 1 ] && ok "radar_running=true" || die "radar_running=false，雷达没起来"
ok "radar_frames=$frames  radar_stalled_ms=$stalled"

sleep 5
frames2="$(curl -fsS "$base/api/v1/health" | python3 -c 'import json,sys; print(int(json.load(sys.stdin).get("data",{}).get("radar_frames",0)))')"
if [ "$frames2" -gt "$frames" ]; then
    ok "帧计数在增长（$frames → $frames2）"
else
    die "帧计数没有增长（$frames → $frames2），采帧线程可能卡住"
fi

[ "$stalled" -eq 0 ] && ok "数据未停滞" || warn "radar_stalled_ms=$stalled，雷达有间断"

if [ -f /opt/aura/app/data/.pair-code ]; then
    ok "配对码 $(cat /opt/aura/app/data/.pair-code)"
fi

log "服务状态"
systemctl is-active aura.service
systemctl is-active aura-ch347.service

ok "部署完成"
warn "请让用户坐到雷达正前方 0.6–1.2 米、保持静止 20 秒，再在 App 里确认心率非零"
