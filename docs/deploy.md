# 在 RDK X5 上部署

这篇是完整的部署手册，按顺序做完就能得到一台能跑的服务。所有命令都在板卡上执行。

## 0. 最终状态

部署完成后板卡上会有：

```
/opt/aura/
├── app/
│   ├── AuraService            可执行文件
│   ├── data/
│   │   ├── aura.db            SQLite（需自带表结构，见第 6 节）
│   │   └── .pair-code         6 位配对码
│   └── ui/                    可选，Web 前端静态文件
├── algorithm/                 libaura 源码与 libaura.a
└── radar/                     RS6240 驱动源码

/etc/systemd/system/aura.service
/etc/systemd/system/aura-ch347.service
/etc/udev/rules.d/99-aura-ch347.rules
/etc/modprobe.d/99-aura-ch34x.conf
/usr/local/bin/aura-ch347-setup.sh
```

运行时设备节点：

| 节点 | 用途 |
|---|---|
| `/dev/spidev8.0` | 雷达数据 |
| `/dev/ttyACM0` | 雷达命令行 |

## 1. 确认系统

板端实测环境：

```
Ubuntu 22.04.5 LTS (aarch64)
Linux 6.1.83
gcc 11.2.0
cmake 3.22.1
内存 6.6 GB，根分区剩余 ≥ 5 GB
```

```bash
cat /etc/os-release | head -2
uname -r
gcc --version | head -1
cmake --version | head -1
free -m
df -h /
```

Drogon 编译大约占 1–2 GB，磁盘不够时先清理。

## 2. 安装系统依赖

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake pkg-config git \
  linux-headers-$(uname -r) \
  libssl-dev libsqlite3-dev libjsoncpp-dev \
  zlib1g-dev libbrotli-dev libc-ares-dev uuid-dev
```

`linux-headers` 是下一步编译 CH347 内核模块用的。RDK X5 的官方镜像一般已经带了 `linux-headers-6.1.83`，没有的话需要先安装对应内核头文件。

## 3. 编译 CH347 内核模块（关键）

RDK X5 自带的内核**没有** `ch34x_mphsi_master`。没有它，USB 插上 CH347 只会出现串口 `/dev/ttyACM0`，不会出现 `/dev/spidev8.0`，服务启动会报 `cannot open /dev/spidev8.0`。

模块来自 WCH 官方驱动仓库，需要按板卡内核版本自己编译：

```bash
cd /opt
sudo mkdir -p aura/src && cd aura/src
sudo git clone https://github.com/WCHSoftGroup/ch34x_mphsi_master_linux.git
cd ch34x_mphsi_master_linux/driver

sudo make            # 用当前内核头文件编译
sudo make install    # 复制到 /lib/modules/$(uname -r)/kernel/drivers/usb/misc 并 depmod
```

加载并设为开机自动加载：

```bash
echo "ch34x_mphsi_master" | sudo tee /etc/modules-load.d/ch34x.conf
sudo modprobe ch34x_mphsi_master
```

插上 CH347 后确认：

```bash
lsmod | grep ch34x
ls /dev/spidev8.*
ls /dev/ttyACM0
```

预期看到：

```
ch34x_mphsi_master     53248  0
/dev/spidev8.0  /dev/spidev8.1
crw-rw---- 1 root dialout 166, 0 ... /dev/ttyACM0
```

### 固定 SPI 总线号

驱动默认动态分配总线号，本次启动是 8，下次可能是别的。固定住才能让服务配置里的 `/dev/spidev8.0` 一直有效：

```bash
sudo cp deploy/99-aura-ch34x.conf /etc/modprobe.d/
sudo reboot
```

重启后如果 `ls /sys/bus/spi/devices` 里没有 `spi8.0`，说明 8 号被板载控制器占了，换一个空闲号并同步修改 `aura.service` 里的 `AURA_RADAR_SPIDEV`。

## 4. 编译安装 Drogon 1.9.11

板端实测版本是 1.9.11，安装到 `/usr/local`。Drogon 依赖 trantor，要先装 trantor。

```bash
cd /opt/aura/src
sudo git clone --recursive https://github.com/drogonframework/drogon.git
cd drogon

# trantor
cd trantor
sudo cmake -B build -DCMAKE_BUILD_TYPE=Release \
     -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_TESTING=OFF
sudo cmake --build build -j8
sudo cmake --install build

# drogon
cd ..
sudo cmake -B build -DCMAKE_BUILD_TYPE=Release \
     -DCMAKE_INSTALL_PREFIX=/usr/local \
     -DBUILD_EXAMPLES=OFF -DBUILD_CTL=OFF -DBUILD_TESTING=OFF \
     -DBUILD_ORM=OFF -DBUILD_REDIS=OFF -DBUILD_SQLITE=OFF \
     -DBUILD_MYSQL=OFF -DBUILD_POSTGRESQL=OFF
sudo cmake --build build -j8
sudo cmake --install build
```

> 这里刻意关掉了 Drogon 自带的 SQLite 和 ORM：AURA 直接链接系统 sqlite3，不走 Drogon ORM。

验证：

```bash
ls /usr/local/lib/cmake/Drogon/DrogonConfig.cmake
```

## 5. 编译 AURA

```bash
cd /opt/aura
sudo git clone <repo-url> source
cd source

# 算法库
make -C algorithm

# 服务
cmake -S service -B service/build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build service/build -j8
```

算法库必须先生成 `algorithm/libaura.a`，服务 CMake 默认从 `../algorithm` 找它。

## 6. 安装

```bash
sudo mkdir -p /opt/aura/app/data
sudo install -m 755 service/build/AuraService /opt/aura/app/AuraService
sudo install -m 755 deploy/aura-ch347-setup.sh /usr/local/bin/
sudo cp deploy/aura.service deploy/aura-ch347.service /etc/systemd/system/
sudo cp deploy/99-aura-ch347.rules /etc/udev/rules.d/
sudo cp deploy/99-aura-ch34x.conf /etc/modprobe.d/
sudo udevadm control --reload-rules
sudo systemctl daemon-reload
```

### 数据库需要自带表结构

`AuraService` 启动时只建库文件、不建表：它用 `SQLITE_OPEN_CREATE` 打开 `AURA_DB_PATH`，然后设 PRAGMA，并对 `devices.endpoint_url`、`users.avatar` 各补一列兜底。表结构得由现成的 `aura.db` 带进来，否则 `/auth/*`、`/devices`、`/focus-data/*` 全部返回 `no such table`。

启动前先确认：

```bash
sqlite3 /opt/aura/app/data/aura.db ".tables"
# 期望：alerts  devices  focus_records  rooms  settings  users
```

少任何一张，服务能起来、`/health` 也是 `ok`，但除了探活以外的接口全废——这是这套服务最容易误判的失败模式。

`/opt/aura/app/ui` 是可选的 Web 前端目录，没有也能跑，App 不依赖它。

## 7. 启动

```bash
sudo systemctl enable --now aura-ch347.service
sudo systemctl enable --now aura.service
systemctl status aura.service --no-pager
```

启动日志里应该能看到：

```
RadarLive: HIF wake=0x79
RadarLive: HIF simplex disabled; polling mode
AURA Drogon service
  database: /opt/aura/app/data/aura.db
  listen:   http://0.0.0.0:3000
  pairing:  123456
```

`pairing` 就是 App 添加设备时要输入的 6 位配对码，保存在 `/opt/aura/app/data/.pair-code`。删掉文件重启会重新生成。

## 8. 验证

### 探活

```bash
curl -s http://127.0.0.1:3000/api/v1/health
```

```json
{"success":true,"data":{"status":"ok","service":"aura-drogon","database":"ok",
"radar_running":true,"radar_fs":90.0,"radar_frames":123456,"radar_stalled_ms":0}}
```

三个关键字段：

- `radar_running` 必须是 `true`
- `radar_frames` 每次请求都应该增长
- `radar_stalled_ms` 应该是 0，持续增长说明采帧卡住

### 实时快照

这个接口需要登录 token：

```bash
TOKEN=$(curl -s -X POST http://127.0.0.1:3000/api/v1/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"email":"you@example.com","password":"your-password"}' \
  | python3 -c 'import sys,json;print(json.load(sys.stdin)["data"]["token"])')

curl -s http://127.0.0.1:3000/api/v1/live/snapshot -H "Authorization: Bearer $TOKEN"
```

返回里重点看 `hr`、`resp_hz`、`conf`、`motion`。人坐在雷达正前方 0.6–1.2 米、保持静止，10 秒内应该出现非零心率。

### App 连接

1. 手机和板卡连同一个局域网
2. App 登录后进入「设备 → 添加设备」
3. 填入板卡地址 `http://<板卡IP>:3000` 和配对码
4. 保存后回到首页，应该能看到实时数据

App 允许明文 HTTP（`network_security_config.xml`），局域网内不需要证书。

需要在公网访问时，不要直接把 3000 端口映射出去，先看 [remote-access.md](remote-access.md)。

## 9. 升级

只改算法或服务时不需要重装依赖：

```bash
cd /opt/aura/source
git pull
make -C algorithm
cmake --build service/build -j8
sudo systemctl stop aura.service
sudo install -m 755 service/build/AuraService /opt/aura/app/AuraService
sudo systemctl start aura.service
```

数据库是 WAL 模式，直接替换二进制不会丢数据；但升级前建议备份：

```bash
sudo sqlite3 /opt/aura/app/data/aura.db ".backup '/opt/aura/backup/aura-$(date +%F).db'"
```

## 10. 排障

| 现象 | 原因 | 处理 |
|---|---|---|
| 启动即退出，日志 `Cannot find backend/database/aura.db` | 没设 `AURA_DB_PATH`，默认会在 `./backend/database/` 下找 | 确认 `aura.service` 里的 `AURA_DB_PATH` 生效；手跑时手动 export |
| 接口报 `no such table`，但 `/health` 是 `ok` | `aura.db` 没有表结构 | 见第 6 节，换成带 schema 的库 |
| `cannot open /dev/spidev8.0` | 内核模块没加载，或总线号变了 | `lsmod \| grep ch34x`；`ls /sys/bus/spi/devices`；重跑 `aura-ch347-setup.sh` |
| 只有 `/dev/ttyACM0`，没有 spidev | CH347 只枚举了串口 | 确认驱动装的是 `ch34x_mphsi_master`，不是 `ch341` 串口驱动 |
| `no reply to 'mmwc ...'` | 串口被占用，或雷达没上电 | `fuser -v /dev/ttyACM0`；检查雷达供电 |
| `HIF wake failed: -3` | 雷达固件挂死 | 给雷达模组断电再上电，软件复位无效 |
| `radar_frames` 不增长 | 采帧线程卡住 | 看 `journalctl -u aura.service -f`；重启服务 |
| 心率长期为 0 | 没人、距离不对、或大幅移动 | 坐到 0.6–1.2 米内保持静止 20 秒以上 |
| App 连不上 | 手机热点做了客户端隔离 | 换普通路由器，或用手机热点但关闭「AP 隔离」 |
| 登录返回 401 | token 过期（默认 24 小时） | 重新登录 |

## 11. 日常运维

```bash
# 看实时日志
journalctl -u aura.service -f

# 看最近一次体征落库
journalctl -u aura.service | grep "vitals recorded" | tail -5

# 重启
systemctl restart aura.service

# 关闭
systemctl stop aura.service

# 导出数据库
sqlite3 /opt/aura/app/data/aura.db ".backup '/tmp/aura.db'"
```

体征落库间隔由 `AURA_VITALS_INTERVAL_MIN` 控制，默认 10 分钟一条。

## 12. 环境变量

`aura.service` 里已经把板端要用的都设好了，走 systemd 就不用管这一节；手跑二进制、换机器或改端口时才需要自己给。默认值取自代码，不设就是这个。

| 变量 | 默认 | 作用 |
|---|---|---|
| `AURA_DB_PATH` | 无（依次找 `./backend/database/aura.db` 等三处） | SQLite 路径，找不到直接退出 |
| `AURA_BIND` | `127.0.0.1` | 监听地址。默认只有本机能访问，`aura.service` 里改成了 `0.0.0.0`；手跑时不改，App 连不上 |
| `AURA_PORT` | `3000` | 监听端口 |
| `AURA_UI_ROOT` | 无（找含 `index.html` 的目录） | 可选的 Web 前端根目录 |
| `AURA_RADAR_ENABLE` | 未设即关闭 | 设 `1` 才启动雷达采帧线程 |
| `AURA_RADAR_SPIDEV` | `/dev/spidev8.0` | 雷达数据通道 |
| `AURA_RADAR_UART` | `/dev/ttyACM0` | 雷达命令行 |
| `AURA_RADAR_SPI_HZ` | `8000000` | SPI 时钟 |
| `AURA_RADAR_POLL_DELAY_US` | `4000` | 采帧线程两次读取之间的等待 |
| `AURA_RADAR_READY_GPIO` | `-1`（不用） | 雷达 ready 引脚 |
| `AURA_RADAR_SIMPLEX` | `0` | 设 `1` 启用 HIF 单工模式 |
| `AURA_LIVE_CUBE` / `AURA_LIVE_META` | 未设即不启用 | 用录好的 cube 回放代替硬件，硬件雷达启用时被忽略 |
| `AURA_LIVE_SPEED` | `1.0` | 回放倍速 |
| `AURA_DEVICE_SERIAL` | `AURA-6240-001` | 设备序列号，写进 `devices` 表 |
| `AURA_DEVICE_NAME` / `AURA_DEVICE_MODEL` / `AURA_DEVICE_FIRMWARE` | `AURA 雷达（本机）` / `MRS6240-P2512` / `r3_databox_v2.0.6` | 设备元信息 |
| `AURA_DEVICE_ENDPOINT` | 空 | 对外地址，写进局域网发现报文 |
| `AURA_OWNER_EMAIL` | `mobile@aura.local` | 体征落库归属的账号 |
| `AURA_VITALS_INTERVAL_MIN` | `10` | 体征落库间隔（分钟） |
| `AURA_PHASE_DUMP` / `AURA_SNAP_DUMP` | 未设即不导出 | 诊断导出开关，见 [verification.md](verification.md) |
