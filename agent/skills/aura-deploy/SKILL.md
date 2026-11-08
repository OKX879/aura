---
name: aura-deploy
description: Deploy the AURA board-side service on an RDK X5 from a clean system. Use when the user asks to deploy AURA, set up the RDK X5 board, install the RS6240 radar stack, or bring AuraService up end to end. Do not use for app-only or algorithm-only changes.
---

# AURA 板端部署

在 RDK X5 上从零部署 `AuraService`。全部脚本都在 `scripts/` 下，按编号顺序执行。

## 执行原则

1. **一次只跑一个脚本**，读完输出再决定下一步。
2. **任一步失败就停**，不要跳过失败步骤继续。失败原因看 `references/troubleshooting.md`。
3. **不要重复执行已经通过的步骤**。所有脚本都做了幂等检查，重跑会跳过已完成的部分。
4. **不要修改仓库源码**。部署只读取 `algorithm/`、`service/`、`deploy/`。
5. 需要人工介入的只有两件事：给雷达模组断电重启、把手机连到同一局域网。遇到这两件事就停下来等用户。

## 最终状态

```
/opt/aura/
├── app/AuraService
├── app/data/aura.db
└── app/data/.pair-code

/etc/systemd/system/aura.service
/etc/systemd/system/aura-ch347.service
/etc/udev/rules.d/99-aura-ch347.rules
/etc/modprobe.d/99-aura-ch34x.conf
/usr/local/bin/aura-ch347-setup.sh

设备节点: /dev/spidev8.0  /dev/ttyACM0
监听:     0.0.0.0:3000
```

## 前置条件

- RDK X5，Ubuntu 22.04，aarch64，能以 root 登录
- 雷达模组和 CH347 已经接好
- 磁盘剩余 ≥ 5 GB（Drogon 编译占 1–2 GB）

## 步骤

| 步 | 脚本 | 作用 | 通过标准 |
|---|---|---|---|
| 1 | `scripts/00-preflight.sh` | 检查系统、权限、磁盘、设备 | 全部 `[ok]` |
| 2 | `scripts/01-install-deps.sh` | 安装编译依赖与内核头文件 | 退出码 0 |
| 3 | `scripts/02-ch347-module.sh` | 编译安装 CH347 内核模块并固定 SPI 总线号 | `lsmod` 有 `ch34x_mphsi_master`，`/dev/spidev8.0` 存在 |
| 4 | `scripts/03-drogon.sh` | 源码编译安装 Drogon 1.9.11 | `/usr/local/lib/cmake/Drogon` 存在 |
| 5 | `scripts/04-build-aura.sh` | 编译 `libaura.a` 和 `AuraService` | 生成 `service/build/AuraService` |
| 6 | `scripts/05-install-service.sh` | 安装二进制、systemd、udev | 两个服务 `active` |
| 7 | `scripts/06-verify.sh` | 探活、帧计数、配对码 | 健康检查通过 |

执行方式：

```bash
bash agent/skills/aura-deploy/scripts/00-preflight.sh
```

## 关键判断

**第 3 步是整条流程最容易失败的地方。** RDK X5 官方镜像不自带 `ch34x_mphsi_master`，没有它就只有 `/dev/ttyACM0`，没有 `/dev/spidev8.0`，服务会报 `cannot open /dev/spidev8.0`。

如果第 3 步之后没有出现 `/dev/spidev8.0`：

1. 让用户把 CH347 拔插一次
2. 重新执行 `bash /usr/local/bin/aura-ch347-setup.sh`
3. 再看 `ls /sys/bus/spi/devices`

**第 6 步启动后如果日志出现 `HIF wake failed`**，说明雷达固件挂死。软件复位无效，必须让用户给雷达模组断电再上电，然后重新执行第 6 步。

## 安全约束

- 不要把 3000 端口直接映射到公网。公网访问必须走 HTTPS，见 `docs/remote-access.md`。
- 不要修改 `/etc/systemd/system/aura.service` 里的 `AURA_DEVICE_ENDPOINT` 为真实域名以外的值。
- 不要把 `/opt/aura/app/data/aura.db` 或导出 CSV 提交到仓库。

## 验收

`scripts/06-verify.sh` 会自动检查：

- `/api/v1/health` 返回 200
- `radar_running` 为 true
- `radar_frames` 在 5 秒内增长
- `radar_stalled_ms` 为 0
- 配对码文件存在

手动确认一项：让用户坐在雷达正前方 0.6–1.2 米、保持静止 20 秒，`/api/v1/live/snapshot` 应出现非零心率。

## 排障

按现象查 `references/troubleshooting.md`。里面覆盖：设备节点缺失、UART 无响应、HIF 唤醒失败、服务起不来、心率长期为 0。
