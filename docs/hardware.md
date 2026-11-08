# AURA 非接触式生命体征监测产品线 · 硬件规格书

| 项目 | 内容 |
|---|---|
| 文档编号 | AURA-HS-001 |
| 版本 | 1.0 |
| 代码基线 | `aura/radar`（采集层）+ `aura/service/src/radar_live.cpp`（实时入口）+ `aura/deploy`（驱动与服务） |
| 硬件平台 | 正和微芯 RS6240，60 GHz FMCW，2 Tx × 4 Rx；CH347 USB-SPI 转接；地瓜派 RDK X5 主控 |
| 文档性质 | 硬件规格。每个参数、命令、时序均可追溯到源文件或部署脚本 |

---

## 1. 物料与拓扑

### 1.1 物料清单

| 部件 | 型号 | 关键参数 | 来源 |
|---|---|---|---|
| 主控 | 地瓜派 RDK X5（D-Robotics） | aarch64，Ubuntu 22.04，运行 `AuraService` | `aura/docs/hardware.md` |
| 雷达 | 正和微芯 RS6240 模组 | 60 GHz FMCW，命令配置为 57–66 GHz；2 Tx × 4 Rx | `mmwc freq 57000 66000` |
| 转接 | CH347 | USB 转 SPI + UART，VID:PID = `1a86:55db` | `aura/deploy/99-aura-ch347.rules` |
| 手机 | Android 8.0+ | 与板卡同一局域网，只读快照 | `aura/android` |

### 1.2 拓扑

```
        SPI 8 MHz, mode 0            USB 2.0
RS6240 ──────────────── CH347 ──────────────── RDK X5
   │                      │                      │
   └── UART 115200 8N1 ───┘               AuraService
                                            ├─ radar_live    SPI 采帧 + HIF 唤醒
                                            ├─ libaura       体征计算
                                            └─ Drogon HTTP   REST + SQLite
                                                     │
                                              Wi-Fi 局域网
                                                     │
                                              Android 手机
```

两个物理通道分工固定：**UART 只下发命令，SPI 只取数据**。命令下行后由 `mmwc comtype 1` 把数据输出切到 SPI，此后 UART 不再承载数据。

---

## 2. 设备节点与驱动

### 2.1 节点

| 节点 | 用途 | 参数 |
|---|---|---|
| `/dev/spidev8.0` | 雷达数据通道 | 8 MHz，SPI mode 0 |
| `/dev/spidev8.1` | CH347 第二个 SPI 通道 | 本项目未使用 |
| `/dev/ttyACM0` | 雷达命令行 | 115200 8N1 |

`spidev8.0` **不是板载 SPI**，而是 CH347 驱动 `ch34x_mphsi_master` 枚举出来的虚拟控制器。因此它依赖内核模块加载顺序，而不是设备树。

### 2.2 驱动装载

`aura-ch347-setup.sh` 做三件事，顺序不能变：

1. `modprobe spidev` 与 `modprobe ch34x_mphsi_master`；
2. 遍历 `/sys/bus/spi/devices`，把 `modalias` 为 `spidev` 的设备显式绑定到 spidev 驱动；
3. 把 `/dev/spidev8.*` 权限改为 `666`，供服务进程无 root 访问。

`modprobe` 参数写在 `deploy/99-aura-ch34x.conf`，udev 规则写在 `deploy/99-aura-ch347.rules`：

```
SUBSYSTEM=="usb", ACTION=="add", ATTR{idVendor}=="1a86", ATTR{idProduct}=="55db",
  RUN+="/bin/systemctl restart aura-ch347.service"
```

即：**USB 每次重新插入都会重跑上面的三步**，然后由 `aura-ch347.service` 拉起/重启 `aura.service`。

### 2.3 上电顺序

错误的顺序会得到「节点存在但打开失败」：

- 先上雷达、后插 CH347 → 正常；
- 先插 CH347、雷达未上电 → `spidev8.0` 会被枚举出来，但打开后读写超时；
- 雷达上电瞬间 USB 掉一次 → 触发上面的 udev 规则，需等待脚本跑完再启动服务。

---

## 3. 射频配置

配置全部通过 UART 命令行下发，源值如下（`radar_live.cpp` 启动序列）：

| 命令 | 含义 | 取值 |
|---|---|---|
| `mmwc mode 4 1` | 工作模式 | 模式 4，参数 1 |
| `mmwc freq 57000 66000` | 扫频起止 | 57 000–66 000 MHz（9 GHz 带宽） |
| `mmwc range 3072 24` | 距离维参数 | 3072 / 24 |
| `mmwc frame 10 0` | 帧周期 | 10 ms 标称（≈100 Hz 上限） |
| `mmwc comtype 1` | 数据出口 | 1 = SPI |
| `mmwc report points 1` | 点云报告 | 开 |
| `mmwc report cube 1` | Cube 报告 | 开 |
| `mmwc roi 8 16` | 感兴趣区 | 起始 bin 8，共 16 bin |

### 3.1 帧率与时间基

- **配置帧周期 10 ms**，理论 100 Hz；
- **服务按 90 Hz 处理**：`radar_live.cpp` 以 `fs = 90 Hz` 建管线，分析窗 `922` 点（10.24 s × 90），步进 `90` 点（1 s），输出 1 Hz；
- 实测帧号连续、无丢帧时，有效帧率落在 90 Hz 附近；帧号与设备计时（`core_ticks`）随每帧下发，用于审计丢帧与重复。

> 帧率是整条链路的分母：分析窗、滤波系数、HRV 时长门槛全部按 90 Hz 设计。改帧率必须同步重算这些参数，不能只改一处。

### 3.2 数据率

单帧 ROI 载荷（16 bin × 8 通道，每点 4 B）：

$$
N_{pt}=C\cdot R=8\times16=128,\qquad
B_{frame}=32+4N_{pt}=544\ \text{B}
$$

$$
\dot B=B_{frame}\,f_s=544\times90=48.96\ \text{kB/s}\approx176\ \text{MB/h}
$$

服务默认只落特征与事件，原始帧仅在显式抓取时保存。

---

## 4. 启动序列

`radar_live.cpp` 的下发顺序（括号为等待时间）：

```
mmwc stop                 (250 ms)   收掉上一次可能残留的会话
mmwc roi disable          (200 ms)
mmwc report disable       (250 ms)
mmwc hif_simplex 0        (200 ms)   关闭单工回环
mmwc close / mmwc open    (600/1200 ms，仅状态可疑时)
mmwc mode 4 1             (350 ms)
mmwc freq 57000 66000     (250 ms)
mmwc range 3072 24        (250 ms)
mmwc frame 10 0           (250 ms)
mmwc comtype 1                        数据切到 SPI
mmwc report points 1
mmwc report cube 1
mmwc roi 8 16             (150 ms)   挂接 ROI 8+16
mmwc start                           起帧
```

启动成功的判据只有一个：**HIF 唤醒读回 `0x79`**。读不到就重试；连续失败按 §7.1 处理。

---

## 5. HIF 协议

### 5.1 帧结构

`aura/radar/include/rs6240_hif.h` 定义：

| 项 | 值 |
|---|---|
| PHY 魔数 | `0xA5` |
| 消息类型 | `0xC0` ADC / `0xC1` FFT Cube / `0xC4` ROI |
| 校验 | 头校验 + 32 位数据校验 |
| 接收缓冲 | 流 16 KB，单帧 8 KB |
| SPI 变体 | 头与校验之后各有一个填充字节（`rs6240_hif_decode_spi`） |

解析器遇到坏魔数、坏校验、超长都会跳过并计数，不会阻塞后续帧；统计字段为 `bad_headers`、`bad_checksums`、`discarded_bytes`，可直接用于现场判据。

### 5.2 ROI 帧格式（0xC4）

`aura/radar/include/rs6240_roi.h`：

| 字段 | 类型 | 说明 |
|---|---|---|
| magic | `u32` | 小端 `"ROI1"`（`0x31494F52`） |
| version | `u16` | 1 |
| header_size | `u16` | 32 |
| frame_index | `u32` | 帧序号 |
| core_ticks | `u32` | 设备计时 |
| queue_overrun | `u32` | 累积溢出计数 |
| range_start | `u16` | 8 |
| range_count | `u16` | 16 |
| tx_count / rx_count | `u8` | 2 / 4 |
| doppler_index / flags | `u8` | 保留 |
| point_count | `u32` | 128 |

点数据排列为 **TX → RX → 距离**，每点 `int16 imag` + `int16 real`（`rs6240_complex16_t`，注意 imag 在前）。索引：

$$
i=\big((tx\times4)+rx\big)\times16+(\text{range}-8)
$$

三帧格式的取舍：ADC（0xC0）最原始但带宽最高；FFT Cube（0xC1）覆盖全距离；**产品线走 ROI（0xC4）**，因为体征只需要 16 个距离门，带宽降到 49 kB/s，同时保留多通道复数值，供空间域评分与融合使用。

---

## 6. 电源、复位与接地（工程约束）

| 项 | 约束 |
|---|---|
| 雷达供电 | 必须独立可控，最坏情况需要断电才能恢复（见 §7.1） |
| USB 供电 | CH347 与雷达不要共用一个易掉电的 HUB；USB 掉电会重建 `spidev` 节点 |
| 接地 | 雷达模组与主控共地，否则 SPI 读回随机错位，表现为 `bad_checksums` 飙升 |
| 线长 | SPI 走线尽量短；8 MHz 下长排线会引入时钟串扰 |

上电顺序与接地这两项没有软件兜底：一旦错，表现为 `spidev` 打得开但读不到有效帧。

---

## 7. 已知硬件缺陷与处置

### 7.1 HIF 挂死（最严重）

现象：

```
RadarLive: no reply to 'mmwc start'
RadarLive: HIF wake failed: -3 attempts=300
```

此时 SPI 总线上仍有传输（`dmesg` 可见 xfer），但雷达不再回包。

**软件侧全部无效**：DTR/RTS 复位、USB 重新枚举、spidev 重新绑定都试过，均不能恢复。

**唯一恢复手段：给雷达模组断电再上电**，然后确认 `COM/ttyACM` 与 `spidev` 都重新出现，再重启服务。

### 7.2 USB 重新枚举后丢 spidev

对 CH347 做 `authorized 0/1` 或物理拔插后：

- `/dev/ttyACM0` 会回来；
- `/dev/spidev8.*` **不会自动回来**。

处置：手动重跑 `aura-ch347-setup.sh`，再 `systemctl restart aura.service`。

### 7.3 手机热点客户端隔离

部分机型开热点后隔离客户端：手机能访问板卡，板卡不能反向访问手机。体征数据方向（板 → 手机）不受影响，但调试会受阻。绕过方式见 `aura/docs/hardware.md`（`nc` 转发 + `adb forward`）。

---

## 8. 验收清单

上电后按顺序检查，任一项不过不建议继续：

| # | 检查项 | 通过标准 | 方法 |
|---|---|---|---|
| 1 | 内核模块 | `ch34x_mphsi_master`、`spidev` 均在 | `lsmod` |
| 2 | 设备节点 | `/dev/spidev8.0`、`/dev/ttyACM0` 存在且可读写 | `ls -l` |
| 3 | UART 命令 | 发 `mmwc help` 有回包 | `screen /dev/ttyACM0 115200` |
| 4 | HIF 唤醒 | 读回 `0x79` | 服务日志 `HIF wake=0x79` |
| 5 | 帧连续性 | 帧号递增，`queue_overrun` 不增长 | `GET /api/v1/health` |
| 6 | 帧率 | 稳定在 90 Hz 附近 | 同上，看帧计数/秒 |
| 7 | 校验质量 | `bad_checksums` 不随时间增长 | 服务日志 |
| 8 | 有效距离 | 0.6–1.2 m 内能锁到目标门 | 快照里的距离门与置信度 |

---

## 9. 有效范围与未验证项

**已验证**

- 单人、正面坐姿、0.6–1.2 m；
- 8 通道 × 16 距离门 ROI，90 Hz 连续输出；
- CH347 冷启动、USB 重新枚举、udev 自动恢复脚本。

**未验证（不得对外宣称）**

| 项 | 状态 |
|---|---|
| 多人同时测量 | 未验证；当前 ROI 与门锁定只针对单目标 |
| 距离 > 1.2 m | 未验证；ROI 16 门覆盖范围内未做远距标定 |
| 温度漂移 | 未测；无温补 |
| 功耗与续航 | 未测；无功耗预算表 |
| EMC / 安规 | 未做 |
| 长期连续运行 | 未做 7×24 稳定性测试 |
| 雷达绝对位移标定 | 使用名义波长 5 mm，未按实际中心频率标定 |

---

## 10. 参数速查

| 参数 | 值 | 位置 |
|---|---|---|
| SPI 时钟 | 8 MHz，mode 0 | `rs6240_spidev_open()` |
| UART | 115200 8N1 | `radar_live.cpp` |
| 扫频 | 57 000–66 000 MHz | `mmwc freq` |
| 帧周期 | 10 ms（配置） | `mmwc frame 10 0` |
| 服务帧率 | 90 Hz | `radar_live.cpp` |
| ROI | 起始 bin 8，16 bin | `mmwc roi 8 16` |
| 通道 | 2 Tx × 4 Rx = 8 | `rs6240_roi_frame_t` |
| 单帧载荷 | 544 B | 32 + 128×4 |
| 数据率 | 48.96 kB/s ≈ 176 MB/h | 544 × 90 |
| 唤醒判据 | `0x79` | `rs6240_capture_wake()` |
| 分析窗 / 步进 | 922 点 / 90 点 | `radar_live.cpp` |

---

*文档结束。本文参数以 `aura/radar`、`aura/service/src/radar_live.cpp`、`aura/deploy` 为准；改硬件配置须同步更新本文与第 12 章参数表。*
