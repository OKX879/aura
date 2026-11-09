# AURA 非接触式生命体征监测产品线 · 软件规格书

| 项目 | 内容 |
|---|---|
| 文档编号 | AURA-SS-001 |
| 版本 | 1.0 |
| 代码基线 | `aura/service`（AuraService，C++/Drogon）+ `aura/algorithm`（libaura）+ `aura/android` |
| 运行平台 | RDK X5（aarch64，Ubuntu 22.04）；Android 8.0+ 手机 |
| 文档性质 | 软件规格。接口、表结构、配置项、时序均可追溯到源文件 |

---

## 1. 范围与进程模型

产品线软件只有两个可交付程序：**板端 `AuraService`** 与 **手机 App**。板端是唯一计算方，手机只读。

```
AuraService（单进程）
 ├─ Drogon HTTP 线程池      REST + 静态 UI
 ├─ 雷达采集线程            RadarLive：SPI 取帧 → libaura → 快照
 ├─ 设备心跳线程            每 5 s 检查帧计数，刷新 devices.last_seen_at
 └─ 局域网发现线程          UDP 37020，应答 AURA_PROBE 并周期广播
```

一条硬约束贯穿全篇：**服务层不重算算法**。体征只由 `libaura` 在采集线程内产生，HTTP 只读快照、落库与鉴权。任何"服务端再算一次心率"的设计都会引入第二套参数，与板端不一致。

---

## 2. 代码结构与构建

### 2.1 目录职责

| 目录 | 内容 |
|---|---|
| `aura/algorithm` | `libaura` 静态库（C99），无硬件依赖，可在 PC 编译验证 |
| `aura/radar` | RS6240 传输层：SPI 读写、HIF 解析、Cube/ROI 解码 |
| `aura/service` | Drogon 服务：REST、鉴权、SQLite、局域网发现、采集调度 |
| `aura/android` | Kotlin + Jetpack Compose 客户端 |
| `aura/deploy` | systemd unit、udev 规则、CH347 初始化脚本、构建脚本 |

### 2.2 构建

`deploy/build_service.sh`：

```bash
make -C algorithm                 # 生成 libaura.a
cmake -S service -B service/build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build service/build -j$(nproc)
install -Dm755 service/build/AuraService ${INSTALL_DIR:-/opt/aura/app}/AuraService
```

交叉编译只需在第一条命令指定 `CC=aarch64-linux-gnu-gcc`；服务层依赖 Drogon、OpenSSL、sqlite3，由 `service/CMakeLists.txt` 查找。

---

## 3. 运行时配置

全部通过环境变量注入（`deploy/aura.service` 给出生产值）：

| 变量 | 生产值 | 说明 |
|---|---|---|
| `AURA_BIND` | `0.0.0.0` | HTTP 监听地址 |
| `AURA_PORT` | `3000` | HTTP 端口 |
| `AURA_DB_PATH` | `/opt/aura/app/data/aura.db` | SQLite 路径；同目录存鉴权密钥与配对码 |
| `AURA_UI_ROOT` | `/opt/aura/app/ui` | 静态页面根目录 |
| `AURA_RADAR_ENABLE` | `1` | 0 = 不启采集线程 |
| `AURA_RADAR_SPIDEV` | `/dev/spidev8.0` | 数据通道 |
| `AURA_RADAR_UART` | `/dev/ttyACM0` | 命令通道 |
| `AURA_RADAR_SPI_HZ` | `8000000` | SPI 时钟 |
| `AURA_RADAR_POLL_DELAY_US` | `4000` | 轮询指令与 SPI 读之间的微秒延时 |
| `AURA_RADAR_READY_GPIO` | `-1` | 负值表示不用就绪 GPIO |
| `AURA_RADAR_SIMPLEX` | `0` | 0 = 主机轮询；1 = 设备主动推送 |
| `AURA_DEVICE_SERIAL` | `AURA-6240-001` | 设备序列号，写库与发现报文用 |
| `AURA_DEVICE_MODEL` | `MRS6240-P2512` | 型号，写入 `devices.model` 与发现报文 |
| `AURA_DEVICE_FIRMWARE` | `r3_databox_v2.0.6` | 固件版本，写入 `devices.firmware_version` |
| `AURA_DEVICE_ENDPOINT` | 空 | 对外地址；空则由发现线程按本机地址补 |
| `AURA_VITALS_INTERVAL_MIN` | `10` | 体征定时落库间隔（**分钟**，最小 1） |
| `AURA_LIVE_CUBE` / `AURA_LIVE_META` | 注释 | 回放模式：给文件即不启雷达 |
| `AURA_LIVE_SPEED` | `1.0` | 回放倍速 |

改任一变量都只影响本进程，不需要重新编译；但帧率相关变量（`SPI_HZ`、`POLL_DELAY_US`）会改变实际采样率，必须与 `libaura` 的窗长/步进一起改。

---

## 4. 线程模型与数据流

### 4.1 采集线程

```
RadarLive::start()
  ├─ 打开 spidev（8 MHz）与 UART（115200）
  ├─ 下发射频与 ROI 命令（见硬件规格书第 4 章）
  ├─ HIF 唤醒循环，读回 0x79
  └─ 循环：poll_roi → 解 0xC4 帧 → aura_pipeline_push()
       └─ 每发布一窗：aura_pipeline_snapshot() → 写入线程安全快照
```

关键设计：`aura_pipeline_push()` 是**唯一写入口**，`aura_pipeline_snapshot()` 读快照时由服务侧加锁，避免体征结构体被撕裂。采集线程不做 JSON、不碰数据库。

### 4.2 HTTP 线程

Drogon 按 CPU 核数起 IO 线程。所有接口只读快照或读写 SQLite，单次请求内不做重计算；`/live/snapshot` 的耗时应与帧率无关。

### 4.3 心跳与发现

| 线程 | 周期 | 动作 |
|---|---|---|
| 心跳 | 5 s | 比较帧计数是否增长，更新 `devices.last_seen_at`；不增长视为离线 |
| 发现 | 收包即回；5 s 广播 | UDP 37020，收到 `AURA_PROBE` 回设备 JSON，含序列号、型号、固件版本、REST 地址 |

---

## 5. REST API

除 `/api/v1/health`、`/api/v1/pair/verify`、`/api/v1/auth/login`、`/api/v1/auth/signup`、`/api/v1/auth/pair` 外，其余接口均要求 `Authorization: Bearer <token>`。

| 方法 | 路径 | 鉴权 | 说明 |
|---|---|---|---|
| GET | `/api/v1/health` | 否 | 探活：帧计数、雷达状态；**不含体征** |
| GET | `/api/v1/live/snapshot` | 是 | 实时快照：心率、呼吸、置信度、体动、距离门 |
| POST | `/api/v1/pair/verify` | 否 | 配对码校验，换取设备凭证 |
| POST | `/api/v1/auth/login` | 否 | 邮箱 + 密码，仅局域网 |
| POST | `/api/v1/auth/signup` | 否 | 昵称 + 邮箱 + 密码，仅局域网，每小时同 IP 限 5 次 |
| POST | `/api/v1/auth/pair` | 否 | 仅配对码登录，仅局域网，自动登录本机归属账号 |
| POST | `/api/v1/auth/register` | 是（管理员） | 管理员创建账号 |
| POST | `/api/v1/auth/change-password` | 是 | 改密 |
| GET | `/api/v1/auth/me` | 是 | 当前用户 |
| GET/PUT | `/api/v1/auth/profile` | 是 | 个人资料 |
| GET/POST | `/api/v1/devices` | 是 | 设备列表 / 新增 |
| GET/DELETE | `/api/v1/devices/{id}` | 是 | 单设备 |
| GET | `/api/v1/departments` | 是 | 部门列表 |
| GET | `/api/v1/departments/building-diagram` | 是 | 楼层/房间拓扑 |
| GET | `/api/v1/users` | 是（admin） | 用户列表 |
| GET/PUT | `/api/v1/users/{id}` | 是（admin） | 单用户 |
| GET | `/api/v1/alerts` | 是 | 告警列表 |
| GET | `/api/v1/alerts/unresolved` | 是 | 未处理告警 |
| POST | `/api/v1/alerts/{id}/resolve` | 是 | 处理告警 |
| GET | `/api/v1/focus-data/latest` | 是 | 最近体征记录（历史命名，语义为体征时序） |
| GET | `/api/v1/focus-data/personal-stats` | 是 | 个人统计 |
| GET | `/api/v1/focus-data/event-stats` | 是 | 事件统计 |
| POST | `/api/v1/focus-data/ingest` | 是 | 单条入库 |
| POST | `/api/v1/focus-data/bulk-ingest` | 是 | 批量入库 |
| GET/PUT | `/api/v1/settings/fatigue-alert` | 是 | 疲劳告警阈值 |
| GET | `/api/v1/mobile/dashboard` | 是 | 手机首页聚合 |
| POST | `/api/v1/mobile/chat` | 是 | 可选：手机端问答代理 |

约定：

- 请求体一律 JSON，字段缺失返回 400 且带 `error` 字段；
- 未带 token 返回 401，角色不足返回 403；
- `health` 只暴露帧计数与雷达状态，避免未鉴权泄露体征；
- 落库只发生在 `ingest` 与内部定时写；读取接口不回写。

---

## 6. 鉴权与配对

### 6.1 口令

密码用 bcrypt 哈希存储（`service/third_party` 内的 ow-crypt 实现），登录时比对，不回传哈希。`AuthService::login()` 返回 token 与用户资料。

### 6.2 Token

- 由 `AuthService::createToken(userId, role, ttl)` 生成，默认 TTL **86 400 s**；
- 签名密钥由 `loadOrCreateSecret(dbPath)` 在数据库同目录生成并持久化，**换机不通用**；
- 校验失败统一返回 401，不区分"过期"与"伪造"，避免探测。

### 6.3 配对

`POST /pair/verify` 用配对码换取设备凭证，配对码存放在 `data/` 目录（首次启动生成）。配对码是设备侧的一次性入口，不等同于用户口令，两者不可互换。

---

## 7. 数据模型

SQLite 单文件，初始化时执行：

```
PRAGMA foreign_keys=ON;
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;
PRAGMA busy_timeout=5000;
```

| 表 | 主要字段 | 说明 |
|---|---|---|
| `users` | id, name, email, password_hash, role, status | 账号与角色（admin/manager/employee） |
| `devices` | id, name, serial_number, model, firmware_version, room_id, status, last_seen_at | 设备与在线状态 |
| `rooms` | id, name, department_id | 房间 |
| `alerts` | id, type, severity, user_id, device_id, room_id, message, is_resolved, resolved_at, created_at | 告警 |
| `focus_records` | id, user_id, device_id, room_id, heart_rate, respiration_rate, focus_score, stress_level, fatigue_level, recorded_at | 体征时序（表名为历史命名） |
| `settings` | key, value | 用户级设置：`fatigue_threshold`、`fatigue_minutes` |

> `focus_score / stress_level / fatigue_level` 是**由体征换算出的人机工程评分列**，不是独立生理量，不参与体征计算，也不作为医学指标。服务端不做换算，写入什么就存什么。

索引与查询要点：体征按 `(user_id, recorded_at)` 取最近 N 条；告警按 `is_resolved` 与时间过滤；设备通过 `room_id` 关联楼层拓扑。写入用事务，批量入库限制单批条数。

---

## 8. 局域网发现

```
手机/电脑  ──UDP 37020──▶  广播 "AURA_PROBE"
板卡       ──UDP 37020──▶  {"proto":"aura-discovery","serial":"AURA-6240-001",
                            "name":"...","model":"MRS6240-P2512",
                            "endpoint":"http://<ip>:3000","port":3000}
```

- 设备每 5 s 主动广播一次，便于客户端免配置发现；
- 应答字段与 `devices` 表一致，避免两套命名；
- 发现只暴露地址与型号，不暴露体征与 token。

---

## 9. 客户端

`aura/android`：Kotlin + Jetpack Compose。

| 能力 | 实现要点 |
|---|---|
| 登录 | 邮箱 + 密码，token 落 DataStore，失效自动回登录页 |
| 服务器地址 | 登录页可改，便于换网段 |
| 首页 | 每 5 s 拉一次聚合接口 |
| 实时页 | 轮询 `/live/snapshot`，展示心率、呼吸、在场/体动、信号质量 |
| 历史 | 趋势与事件列表 |
| 设备/告警 | 列表、处理状态 |
| 数据口径 | 只展示后端已写入的字段，不自行复制或推算 |

客户端不做信号处理，也不缓存原始波形。

---

## 10. 部署与运维

`deploy/` 四个文件构成完整的开机链路：

| 文件 | 作用 |
|---|---|
| `99-aura-ch34x.conf` | modprobe 参数 |
| `99-aura-ch347.rules` | USB 插入触发 CH347 初始化服务 |
| `aura-ch347.service` | 重跑 `aura-ch347-setup.sh` |
| `aura.service` | 拉起 `AuraService`，`Restart=always`、`RestartSec=3`、`User=root` |

依赖顺序：`aura.service` 在 `aura-ch347.service` 之后启动（`After=`），否则 `spidev8.0` 尚不存在。

运维要点：

- 升级只替换 `AuraService` 二进制，`systemctl restart aura.service`；
- 数据库与密钥在 `data/`，随应用目录保留，**不要随二进制覆盖**；
- 日志走 stdout，由 systemd/journald 收集；
- 回放模式用 `AURA_LIVE_CUBE` + `AURA_LIVE_META` 注入，不接硬件即可联调全链路。

---

## 11. 故障模式

| 现象 | 定位 | 处置 |
|---|---|---|
| `/health` 帧计数不涨 | 采集线程已退出或雷达无回包 | 查日志 `HIF wake failed`，按硬件规格书 §7.1 断电复位 |
| `/health` 正常但快照为空 | 窗口未满（预热 20 s）或门控未通过 | 等待预热；查 `presence_state` |
| 手机连不上 | 不同网段或防火墙 | 确认同 Wi-Fi；发现报文能否收到 |
| 401 | token 过期或密钥被换 | 重新登录；确认 `data/` 未被清空 |
| 403 | 角色不足 | 用 admin 账号 |
| 服务反复重启 | 端口占用或数据库锁 | 查 journald；确认无第二个实例 |
| 时钟跳变 | 系统时间被 NTP 校正 | 体征时间轴不受影响（用帧号），但落库时间会跳 |

---

## 12. 验收清单

| # | 项 | 通过标准 |
|---|---|---|
| 1 | 编译 | `build_service.sh` 成功，产出可执行文件 |
| 2 | 启动 | `systemctl status aura.service` 为 active，无 strace 级错误 |
| 3 | 探活 | `GET /health` 返回帧计数且持续增长 |
| 4 | 鉴权 | 无 token 访问 `/live/snapshot` 返回 401 |
| 5 | 实时 | 快照含心率、呼吸、置信度、距离门 |
| 6 | 落库 | 运行 10 分钟后 `focus_records` 有新增，间隔不小于 `VITALS_INTERVAL_MIN` |
| 7 | 发现 | 局域网内能收到广播并解析出 REST 地址 |
| 8 | 客户端 | 手机登录后可看到实时与历史，断网恢复后自动重连 |
| 9 | 重启保持 | 重启服务后历史数据不丢，token 未失效 |

---

## 13. 边界与未承诺项

| 项 | 状态 |
|---|---|
| 多设备并发接入 | 单板单雷达；多设备需多实例 + 外部汇总 |
| 公网访问 | 默认只做局域网；跨网段需自行加隧道与 TLS |
| HTTPS | 服务默认 HTTP；生产需前置反向代理 |
| 用户规模 | 未做大并发压测，单机 SQLite |
| 移动端推送 | 无推送通道，靠轮询 |
| 医疗合规 | 原型系统，不提供诊断结论 |

---

*文档结束。本文接口与字段以 `aura/service/src`、`aura/deploy`、`aura/android` 为准；改接口须同步更新本章表格并递增版本。*
