# 硬件与接线

## 物料

| 部件 | 型号 | 说明 |
|---|---|---|
| 主控 | 地瓜派 RDK X5（D-Robotics） | aarch64，Ubuntu 22.04，跑 `AuraService` |
| 雷达 | 正和微芯 RS6240 模组 | 60 GHz 毫米波，雷达命令里配置为 57–66 GHz |
| 转接 | CH347 | USB 转 SPI + UART，`1a86:55db` |
| 手机 | Android 8.0+ | 跑 App，与板卡同一局域网 |

## 设备节点

| 节点 | 用途 |
|---|---|
| `/dev/spidev8.0` | 雷达数据通道，8 MHz，SPI mode 0 |
| `/dev/spidev8.1` | CH347 的第二个 SPI 通道，本项目未使用 |
| `/dev/ttyACM0` | 雷达命令行，115200 8N1 |

`spidev8.0` 不是板载 SPI，而是 CH347 驱动 `ch34x_mphsi_master` 枚举出来的。上电顺序不对时驱动可能没绑定，表现为"设备节点存在但打开失败"。

## 初始化

`deploy/aura-ch347-setup.sh` 做三件事：

1. `modprobe spidev` 和 `ch34x_mphsi_master`
2. 遍历 `/sys/bus/spi/devices`，把 modalias 为 `spidev` 的设备绑定到 spidev 驱动
3. 把 `/dev/spidev8.*` 权限改成 666

udev 规则让 USB 重新插拔后自动重跑这个脚本：

```
SUBSYSTEM=="usb", ACTION=="add", ATTR{idVendor}=="1a86", ATTR{idProduct}=="55db", RUN+="/bin/systemctl restart aura-ch347.service"
```

## 雷达启动序列

服务启动后按下面的顺序发命令（UART 命令行）：

```
mmwc open
mmwc report disable
mmwc roi disable
mmwc mode 4 1
mmwc freq 57000 66000
mmwc range 3072 24
mmwc frame 10 0
mmwc start
mmwc stop
mmwc comtype 1        # 把数据输出切到 SPI
mmwc report points 1
mmwc report cube 1
mmwc report disable
mmwc roi 8 16
mmwc start
```

然后进入 HIF 唤醒循环，读回 `0x79` 后开始按 block 读 cube。

## 已知硬件问题

### 雷达 HIF 挂死

现象：

```
RadarLive: no reply to 'mmwc start'
RadarLive: HIF wake failed: -3 attempts=300
```

此时 SPI 总线上仍有传输（`dmesg` 能看到 xfer），但雷达不再回包。软件侧的 DTR/RTS 复位、USB 重新枚举、spidev 重新绑定都无效。**只能给雷达模组断电再上电。**

### USB 重新枚举后丢 spidev

对 CH347 做 `authorized 0/1` 或物理拔插后：

- `/dev/ttyACM0` 会回来
- `/dev/spidev8.*` 不会自动回来

需要手动重跑 `aura-ch347-setup.sh`，然后重启服务。

### 手机热点客户端隔离

部分手机开热点后会隔离客户端，板卡和手机互相不可见，但手机自己可以访问板卡。调试时可以用端口转发绕过：

```bash
# 手机上执行，把手机的 2222 转发到板卡的 22
nc -L -p 2222 nc <board-ip> 22
# 电脑上
adb forward tcp:2222 tcp:2222
ssh -p 2222 root@127.0.0.1
```

如果板卡和电脑在同一网段，直接 `ssh root@<board-ip>` 更简单。
