# AURA · 非接触式毫米波体征监测

用地瓜派 RDK X5 和正和微芯 RS6240 60 GHz 毫米波雷达，在 1 米左右的距离上非接触地测心率、呼吸和体动，配套一个 Android App 和一个跑在开发板上的 C++ 服务。

项目最初是为了做一台"坐着就能看到自己状态"的桌面设备：不戴手环、不贴电极，雷达从胸腔的微小位移里把呼吸和心跳分离出来。

> 这是原型项目，不是医疗器械。测出来的数字不能用于诊断，也不要在任何安全攸关的场景里依赖它。

## 能做什么

- **呼吸率**：30 秒滑窗 Welch 谱估计，输出保留 0.1 次/分，实测与离线复算偏差 ≤ 0.1 次/分
- **心率**：1.0–2.5 Hz 因果带通 + FFT/HPS、自相关、峰值间期三路融合，心率平滑器带重锁保护
- **体动与信号质量**：快速体动检测、距离门锁定，体动时自动降低置信度
- **实时链路**：板端 1 秒更新一次，App 端 48 秒滚动曲线
- **设备侧**：配对码、Bearer Token 鉴权、局域网 UDP 自动发现、SQLite 存储、离线告警
- **App 侧**：中英双语、5 套内置壁纸 + 相册背景、设备管理、告警删除、个人资料

## 系统结构

```
┌──────────────┐   SPI 8 MHz   ┌──────────────┐
│  RS6240 雷达  │◄─────────────►│ CH347 USB-SPI │
│ 57–66 GHz    │   UART 115200 │   转接板       │
└──────────────┘               └───────┬───────┘
                                       │ USB
                               ┌───────▼────────┐
                               │  RDK X5 (aarch64) │
                               │  Ubuntu 22.04     │
                               │                  │
                               │  AuraService     │
                               │  ├─ radar_live    │  SPI 采帧、HIF 唤醒
                               │  ├─ libaura       │  距离门/相位/体征
                               │  ├─ Drogon HTTP   │  REST + 鉴权
                               │  └─ SQLite WAL    │  设备/告警/趋势
                               └───────┬──────────┘
                                       │ HTTP / 局域网发现
                               ┌───────▼──────────┐
                               │ Android App       │
                               │ Kotlin + Compose  │
                               └──────────────────┘
```

处理链路（板端每 1 秒推进一步）：

```
SPI 复数立方体 (8 通道 × 30 距离门)
  └─ 距离维 FFT + 杂波抑制
      └─ 距离门打分 + 距离锁定（防跳门）
          └─ 多通道复数融合 → 相位
              └─ 相位 → 毫米位移 (mm = phase × 5/4π)
                  ├─ 带通 0.18–0.50 Hz → 30 s Welch → 呼吸率
                  └─ 因果带通 1.0–2.5 Hz → 30 s 窗
                       ├─ FFT + 谐波积谱
                       ├─ 自相关
                       └─ 峰值间期 (PPI)
                           └─ 三路融合 → 速率平滑 → 心率
```

## 硬件参数

| 项目 | 参数 |
|---|---|
| 主控 | 地瓜派 RDK X5（D-Robotics），aarch64，Ubuntu 22.04.5 LTS，Linux 6.1.83 |
| 雷达 | 正和微芯 RS6240，57–66 GHz FMCW |
| 转接 | CH347，USB 转 SPI + UART（`1a86:55db`） |
| 阵列 | 8 通道 × 30 距离门 |
| 数据接口 | SPI 8 MHz，`/dev/spidev8.0` |
| 控制接口 | UART 115200 8N1，`/dev/ttyACM0` |
| 采样率 | 90 Hz |
| 分析窗 | 10.24 秒（922 点），步进 1 秒（90 点） |
| 输出更新 | 1 Hz |
| 有效距离 | 0.6–1.2 米（已验证） |

## 仓库结构

| 目录 | 内容 |
|---|---|
| `algorithm/` | 便携 C 算法库 `libaura`：核函数、距离门打分、空间融合、呼吸/心率估计、事件检测。无硬件依赖，可在 PC 上交叉编译 |
| `radar/` | RS6240 的 Linux 传输层：SPI 读写、HIF 唤醒、cube 解析、ROI 配置 |
| `service/` | Drogon C++ 服务：REST API、鉴权、SQLite、局域网发现、设备心跳 |
| `android/` | Kotlin + Jetpack Compose App |
| `deploy/` | systemd unit、udev 规则、CH347 初始化脚本、构建脚本 |
| `docs/` | 架构、算法、验证、硬件、部署、公网访问，以及发布用文稿（索引见下方「文档」） |
| `tools/verify/` | 用导出的原始位移做离线复算与偏差对比的脚本 |
| `.trae/skills/aura-deploy/` | 面向 agent 的全流程部署 Skill，含 7 个可重复执行的脚本 |

## 文档

| 文档 | 内容 | 什么时候看 |
|---|---|---|
| [docs/architecture.md](docs/architecture.md) | 进程模型、配置项、REST 接口、数据模型、部署 | 改代码、对接接口 |
| [docs/algorithm.md](docs/algorithm.md) | 每一级的参数、为什么这么取、失效模式、公开数据集精度 | 调算法、查参数 |
| [docs/hardware.md](docs/hardware.md) | 物料、设备节点、射频配置、HIF 协议、帧格式、验收 | 第一次接线 |
| [docs/deploy.md](docs/deploy.md) | 从裸机到能跑服务、排障表、环境变量总表 | 要部署一台 |
| [docs/remote-access.md](docs/remote-access.md) | 5 种公网访问方案与安全清单 | 要在外面看数据 |

## 快速开始

### 1. 编译算法库

```bash
make -C algorithm          # 生成 algorithm/libaura.a
make -C algorithm CC=aarch64-linux-gnu-gcc   # 交叉编译
```

### 2. 编译板端服务

依赖 Drogon、OpenSSL、sqlite3。CMake 默认从仓库内的 `../algorithm` 和 `../radar` 找算法库和雷达驱动，也可以在配置时覆盖：

```bash
cmake -S service -B service/build -DCMAKE_BUILD_TYPE=Release \
      -DAURA_LIVE_CORE_DIR=$PWD/algorithm \
      -DAURA_RADAR_DIR=$PWD/radar
cmake --build service/build -j
```

或者直接用封装脚本：

```bash
INSTALL_DIR=/opt/aura/app ./deploy/build_service.sh
```

### 3. 部署到开发板

前置条件：RDK X5 需要先编译安装 CH347 内核模块 `ch34x_mphsi_master`，否则不会出现 `/dev/spidev8.0`；`/opt/aura/app/data/aura.db` 也要自带表结构，服务只建库不建表。这一步和 Drogon 的编译安装都在部署手册里：

```bash
sudo install -m 755 deploy/aura-ch347-setup.sh /usr/local/bin/
sudo cp deploy/aura.service deploy/aura-ch347.service /etc/systemd/system/
sudo cp deploy/99-aura-ch347.rules /etc/udev/rules.d/
sudo cp deploy/99-aura-ch34x.conf /etc/modprobe.d/
sudo udevadm control --reload && sudo systemctl daemon-reload
sudo systemctl enable --now aura-ch347.service aura.service
```

完整的逐步部署、设备节点确认、验证和排障见 [docs/deploy.md](docs/deploy.md)。

如果使用支持 Skill 的 agent（Trae / Claude Code 等），可以直接加载 `.trae/skills/aura-deploy`，它会按顺序执行环境检查、依赖安装、CH347 内核模块、Drogon、编译、安装和验证七步，每步都有通过标准，失败即停。

### 4. 编译 App

```bash
cd android
./gradlew assembleDebug
```

发布签名读取仓库外的 `keystore.properties`；文件不存在时自动跳过签名，不影响 debug 构建。

## 算法要点

完整推导、参数和失效模式见 [docs/algorithm.md](docs/algorithm.md)。

1. **距离门必须锁住**。人在呼吸时胸廓位移会让回波能量在相邻距离门之间移动，直接取最大能量门会导致相位阶跃。项目用打分 + 滞后锁定 + 跳门惩罚把门稳定住。

2. **呼吸不能用短窗**。8 秒窗的频率分辨率只有约 7.5 次/分，而呼吸周期本身就有 3–5 秒。改成 30 秒 Welch 后，分辨率提升到 2.6 次/分。

3. **呼吸频带下边界不能太低**。0.14 Hz（8.4 次/分）会把缓慢的基线漂移当成呼吸。实测中输出会在 10.5 和 18.5 次/分之间来回跳，抬到 0.18 Hz 后稳定。

4. **心率谱峰很可能是呼吸谐波**。呼吸 18.5 次/分时，它的 4 次谐波正好是 74 次/分，落在心率搜索带内，而且能量往往比真实心搏还大。必须先估准呼吸，再用陷波器压掉各阶谐波，否则心率会被稳定地"吸"到谐波上。

5. **PPI 在搏动少时不可信**。10 秒窗里只有 8–10 个搏动，漏一拍就会让间期翻倍。融合时按搏动数给 PPI 降权，并在 FFT 与自相关一致、PPI 离群时直接丢弃 PPI。

## 实测验证

在同一段 60 秒原始位移上，用板端完全相同的估计器离线复算，与板端在线输出对比：

| 指标 | 板端输出（中位） | 离线复算 | 偏差 |
|---|---|---|---|
| 心率 | 64.3 BPM | 64.0 BPM | +0.3 BPM |
| 呼吸 | 18.4 次/分 | 18.5 次/分 | −0.0 次/分 |

指标定义、真值层级与公开数据集精度见 [docs/algorithm.md](docs/algorithm.md) 第 11 章。原始位移数据不随仓库发布。

## 已知限制

- 心率对体动非常敏感。`motion` 标记为 1 时读数仅供参考。
- 原始位移里仍有每分钟数十次 0.8–1.9 mm 的台阶跳变，是当前精度的主要上限。
- 只验证过 0.6–1.2 米、正面坐姿、单人场景。
- 心率与呼吸都是短时估计，不做 HRV 医学分析。

## License

[MIT](LICENSE)
