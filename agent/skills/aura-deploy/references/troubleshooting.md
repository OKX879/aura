# 部署排障

按现象查表。每条给出原因和动作，动作按顺序执行，不要跳步。

## 设备节点

| 现象 | 原因 | 动作 |
|---|---|---|
| `cannot open /dev/spidev8.0` | CH347 模块没加载，或 SPI 总线号不是 8 | `lsmod \| grep ch34x`；`ls /sys/bus/spi/devices`；重跑 `bash /usr/local/bin/aura-ch347-setup.sh` |
| 只有 `/dev/ttyACM0`，没有 spidev | 装成了 `ch341` 串口驱动，不是 `ch34x_mphsi_master` | `modinfo ch34x_mphsi_master`；重新执行 `02-ch347-module.sh` |
| 总线号不是 8 | 驱动动态分配 | 确认 `/etc/modprobe.d/99-aura-ch34x.conf` 存在，重启后看 `ls /sys/bus/spi/devices` |
| USB 重插后 spidev 消失 | 驱动绑定丢失 | `bash /usr/local/bin/aura-ch347-setup.sh`，再 `systemctl restart aura.service` |

## 雷达通信

| 现象 | 原因 | 动作 |
|---|---|---|
| `no reply to 'mmwc ...'` | 串口被占用，或雷达没上电 | `fuser -v /dev/ttyACM0`；检查雷达供电和接线 |
| `HIF wake failed: -3` | 雷达固件挂死 | **必须让用户给雷达模组断电再上电**，软件复位无效 |
| `HIF wake=0x79` 之后没有数据 | SPI 时钟或 ROI 配置不对 | 检查 `AURA_RADAR_SPI_HZ=8000000`、`AURA_RADAR_SPIDEV` |
| `radar_frames` 不增长 | 采帧线程卡住 | `journalctl -u aura.service -f`；重启服务；仍不行则断电重启雷达 |

## 编译

| 现象 | 原因 | 动作 |
|---|---|---|
| `DrogonConfig.cmake` 找不到 | Drogon 未安装或装到非默认前缀 | 重跑 `03-drogon.sh`；确认 `-DCMAKE_INSTALL_PREFIX=/usr/local` |
| Drogon 编译报 Brotli/c-ares 缺失 | 依赖没装全 | 重跑 `01-install-deps.sh` |
| CH347 模块编译报头文件缺失 | 内核头文件版本不匹配 | `apt install linux-headers-$(uname -r)`，确认 `/lib/modules/$(uname -r)/build` 存在 |
| `libaura.a` 未生成 | 算法库编译失败 | 单独执行 `make -C algorithm` 看完整错误 |

## 服务

| 现象 | 原因 | 动作 |
|---|---|---|
| `aura.service` 起不来 | 配置或数据库路径错误 | `journalctl -u aura.service -n 80 --no-pager` |
| 端口 3000 被占用 | 已有实例在跑 | `ss -ltnp \| grep 3000`，先停掉旧进程 |
| 数据库报错 | 目录权限或磁盘满 | `df -h /opt`；`ls -ld /opt/aura/app/data` |
| App 连不上 | 不在同一局域网，或热点开了客户端隔离 | 换普通路由器；或用手机热点但关闭 AP 隔离 |
| 登录返回 401 | token 过期（默认 24 小时） | 重新登录 |

## 数据

| 现象 | 原因 | 动作 |
|---|---|---|
| 心率长期为 0 | 没人、距离不对、频繁移动 | 坐到 0.6–1.2 米内静止 20 秒以上 |
| 心率偶发 130+ | 体动导致倍频，平滑器重锁 | 属于已知限制，提示用户保持静止 |
| 呼吸在 10 / 18 之间跳 | 旧版本频带下边界过低 | 确认使用当前版本，呼吸频带为 0.18–0.50 Hz |

## 停止与回滚

```bash
systemctl stop aura.service
systemctl disable aura.service
rm -f /etc/systemd/system/aura.service /etc/systemd/system/aura-ch347.service
rm -f /etc/udev/rules.d/99-aura-ch347.rules /etc/modprobe.d/99-aura-ch34x.conf
rm -rf /opt/aura
systemctl daemon-reload
```

`/opt/aura/app/data/aura.db` 里是历史数据，删除前先备份。
