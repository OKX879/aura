# 架构说明

## 分层

```
radar/       硬件传输层     SPI 命令、HIF 唤醒、cube 解析、ROI 配置
algorithm/   信号处理层     距离门、相位、呼吸/心率、体动、事件
service/     业务服务层     REST、鉴权、存储、设备心跳、局域网发现
android/     客户端        展示、配对、告警、趋势
```

三层之间只通过结构体和回调交互，算法库不依赖 Drogon，也不依赖任何 Linux 头文件，因此可以在 PC 上直接用 gcc 编译和跑单元验证。

## 板端进程

`AuraService` 是一个单进程 Drogon 服务，运行时有四类工作：

| 工作 | 位置 | 说明 |
|---|---|---|
| HTTP 请求 | Drogon IO 线程 | 默认按 CPU 核数起线程 |
| 雷达采帧 | 独立线程 | `RadarLive::start()` 里循环读 SPI，按 90 Hz 喂给 pipeline |
| 设备心跳 | 独立线程 | 每 5 秒检查帧计数是否增长，刷新 `devices.last_seen_at` |
| 局域网发现 | 独立线程 | UDP 37020，收到 `AURA_PROBE` 就回 JSON，同时每 5 秒广播一次 |

`aura_pipeline_push()` 是唯一的写入点，`aura_pipeline_snapshot()` 读快照时由服务侧加锁，避免体征结构体被撕裂。

## 采帧与唤醒

RS6240 的 SPI 从机需要先通过 HIF 协议唤醒，唤醒失败就收不到 cube：

1. 打开 UART，发 `mmwc comtype 1` 把输出切到 SPI
2. 发 `mmwc roi 8 16`、`mmwc start`
3. 循环发唤醒魔数，直到读回 `0x79`
4. 持续读固定长度 block（576 字节），解析成 8 通道 × 30 距离门的复数 cube

唤醒失败时会重试并重新打开设备；如果多次仍失败，说明雷达固件侧已经挂死，只能断电重启（见 README 的"已踩过的主要坑"）。

## 数据存储

SQLite（WAL 模式），主要表：

| 表 | 用途 |
|---|---|
| `users` | 账号、bcrypt 密码哈希、昵称/头像 |
| `devices` | 设备序列号、型号、固件版本、在线状态、最后心跳 |
| `rooms` | 设备所属房间 |
| `alerts` | 告警类型、等级、消息、是否已处理 |
| `focus_records` | 体征记录（心率、呼吸、专注度、压力） |
| `settings` | 用户级设置，如疲劳告警开关 |

## REST API

除 `/api/v1/health` 和配对接口外，其余都需要 `Authorization: Bearer <token>`。

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/v1/health` | 探活，只暴露帧计数、雷达状态，不含体征 |
| GET | `/api/v1/live/snapshot` | 实时快照：心率、呼吸、置信度、体动标记 |
| POST | `/api/v1/pair/verify` | 配对码校验，换取设备凭证 |
| POST | `/api/v1/auth/login` | 登录 |
| POST | `/api/v1/auth/register` | 注册 |
| GET/POST | `/api/v1/auth/me`、`/auth/profile` | 个人资料读写 |
| GET/POST | `/api/v1/devices` | 设备列表 / 注册设备 |
| DELETE | `/api/v1/devices/{id}` | 删除设备 |
| GET | `/api/v1/alerts` | 告警列表（可过滤已处理） |
| POST | `/api/v1/alerts/{id}/resolve` | 标记告警已处理 |
| GET | `/api/v1/focus-data/personal-stats` | 个人统计 |
| GET | `/api/v1/focus-data/event-stats` | 事件统计 |
| POST | `/api/v1/focus-data/ingest` | 写入体征记录 |
| GET | `/api/v1/mobile/dashboard` | 首页聚合数据 |
| POST | `/api/v1/mobile/chat` | App 内助手 |

`/api/v1/health` 刻意不返回任何体征数据：板卡可能暴露在公网，探活接口不应该成为数据泄露口。

## App 结构

```
data/
  api/          Retrofit 接口 + kotlinx.serialization
  datastore/    登录态、设备地址、语言、壁纸偏好
  model/        领域模型
  session/      Token 刷新与失效处理
ui/
  navigation/   单 Activity + Compose Navigation
  screens/      首页、实时、趋势、告警、设备、我的、登录
  components/   AuraCard、趋势图、告警项等
  i18n/         中英文案表
  theme/        配色、字体、背景
```

App 不缓存原始雷达数据，只保留最近一次快照和滚动曲线；历史数据由板端 SQLite 提供。
