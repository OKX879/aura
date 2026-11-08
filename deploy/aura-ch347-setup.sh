#!/bin/bash
set -u
modprobe spidev 2>/dev/null || true
modprobe ch34x_mphsi_master 2>/dev/null || true
for attempt in $(seq 1 20); do
  bound=0
  for d in /sys/bus/spi/devices/*; do
    [ -e "$d/modalias" ] || continue
    modalias=$(cat "$d/modalias" 2>/dev/null || true)
    case "$modalias" in
      spidev|spi:spidev) ;;
      *) continue ;;
    esac
    name=$(basename "$d")
    if [ -L "$d/driver" ]; then
      bound=$((bound + 1))
      continue
    fi
    echo spidev > "$d/driver_override" 2>/dev/null || true
    echo "$name" > /sys/bus/spi/drivers/spidev/bind 2>/dev/null || true
    [ -L "$d/driver" ] && bound=$((bound + 1))
  done
  [ "$bound" -gt 0 ] && break
  sleep 0.2
done
for dev in /dev/spidev8.*; do
  [ -e "$dev" ] && chmod 666 "$dev" 2>/dev/null || true
done
echo "CH347 setup done: $(ls /dev/spidev8.* 2>/dev/null | tr '\n' ' ')"
