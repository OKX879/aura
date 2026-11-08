# 公网访问

板卡通常在局域网里，App 出门在外就连不上。这篇讲怎么把 `AuraService` 暴露到公网，以及每种方式的安全边界。

## 先判断网络环境

```bash
# 板卡看到的出口 IP
curl -4 -s https://ifconfig.me

# 路由器的 WAN 口 IP
# 登录路由器后台查看，或从管理页的"外网地址"读取
```

两者一致，说明有公网 IPv4，可以用端口转发；不一致说明运营商做了 CGNAT，端口转发无效，只能走隧道方案。

> 即使有公网 IP，很多运营商也会封 80/443 入站。先用手机 4G 访问 `http://<公网IP>:端口` 验证。

## 方案对比

| 方案 | 需要域名 | 需要 VPS | 公网可见 | 难度 | 适用 |
|---|---|---|---|---|---|
| Tailscale Funnel | 不需要 | 不需要 | 是 | 低 | 个人远程查看（本项目实测） |
| Tailscale Serve | 不需要 | 不需要 | 否，仅 tailnet | 低 | 只给自己设备访问 |
| Cloudflare Tunnel | 需要 | 不需要 | 是 | 中 | 想要自己的域名和访问控制 |
| frp + VPS | 需要 | 需要 | 是 | 中高 | 完全自控、固定端口 |
| 端口转发 + DDNS | 需要 | 不需要 | 是 | 中 | 有公网 IPv4 |

## 方案 A：Tailscale Funnel（推荐）

板端实测就是这套。Tailscale 负责打洞，Funnel 负责把本机端口映射到一个 `*.ts.net` 域名，自动带 HTTPS。

```bash
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up

# 把本机 3000 端口映射到公网
sudo tailscale funnel --bg 3000

tailscale funnel status
```

输出：

```
https://<board>.<tailnet>.ts.net (Funnel on)
|-- / proxy http://127.0.0.1:3000
```

这个地址就是 App 里要填的设备地址。公网任何拿到 URL 的人都能访问。

关闭：

```bash
sudo tailscale funnel reset
```

注意：

- 首次使用需要在 Tailscale 管理台的 Access Controls 里允许 Funnel
- Funnel 只支持 443、8443、10000 三个公网端口，`funnel 3000` 会自动映射到 443
- 域名固定为 `*.ts.net`，不能自定义
- 板卡必须能连上 Tailscale 控制面，断网后公网地址失效

## 方案 B：Tailscale Serve（更安全）

如果只是自己手机看，不需要公网任何人访问，用 Serve：

```bash
sudo tailscale serve --bg 3000
```

地址还是 `https://<board>.<tailnet>.ts.net`，但只有加入同一个 tailnet 的设备能访问。手机装 Tailscale 并登录同一个账号即可。

比 Funnel 安全得多，建议优先用这个。

## 方案 C：Cloudflare Tunnel

适合已经有域名、希望用自己的域名访问的场景。

```bash
curl -L -o cloudflared.deb \
  https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-arm64.deb
sudo dpkg -i cloudflared.deb

# 先临时试一下，会生成一个 trycloudflare.com 地址
cloudflared tunnel --url http://127.0.0.1:3000
```

正式使用：

```bash
cloudflared tunnel login
cloudflared tunnel create aura
cloudflared tunnel route dns aura aura.example.com
```

写配置 `/etc/cloudflared/config.yml`：

```yaml
tunnel: aura
credentials-file: /etc/cloudflared/<tunnel-id>.json
ingress:
  - hostname: aura.example.com
    service: http://127.0.0.1:3000
  - service: http_status:404
```

```bash
sudo cp ~/.cloudflared/<tunnel-id>.json /etc/cloudflared/
sudo cloudflared service install
sudo systemctl enable --now cloudflared
```

Cloudflare 侧可以再加 Access 策略，只允许指定邮箱登录，适合给多人用。

## 方案 D：frp + VPS

适合有自己的服务器、想要固定入口和完全控制权的场景。

VPS 上 `/etc/frp/frps.toml`：

```toml
bindPort = 7000
auth.method = "token"
auth.token = "换成随机长串"
```

板卡上 `/etc/frp/frpc.toml`：

```toml
serverAddr = "1.2.3.4"
serverPort = 7000
auth.method = "token"
auth.token = "换成随机长串"

[[proxies]]
name = "aura"
type = "tcp"
localIP = "127.0.0.1"
localPort = 3000
remotePort = 13000
```

两边分别 `systemctl enable --now frps` / `frpc`。

然后在 VPS 上用 Nginx 或 Caddy 做 TLS 终止，把 `https://aura.example.com` 反代到 `127.0.0.1:13000`。Caddy 最简单：

```
aura.example.com {
    reverse_proxy 127.0.0.1:13000
}
```

## 方案 E：端口转发 + DDNS

只有确认有公网 IPv4 才可用。

1. 路由器把 80、443 转发到板卡
2. 域名 A 记录指向公网 IP，或用 DDNS 客户端自动更新
3. 板卡上跑 Caddy 自动申请证书：

```
aura.example.com {
    reverse_proxy 127.0.0.1:3000
}
```

不要直接把 3000 端口暴露到公网，HTTP 明文会泄露 token。

## 安全清单

公网暴露前逐条确认：

- **必须 HTTPS**。登录返回的 Bearer token 在 HTTP 下是明文，同网络任何人都能抓。
- **只暴露业务端口**。SSH、SQLite、调试端口一律不开。
- **用强密码**。服务端是 bcrypt 哈希，但密码本身要够强。
- **配对接口有频率限制**。`/api/v1/pair/verify` 按 IP 限制每分钟 10 次；经过反向代理后所有请求的来源 IP 都是 `127.0.0.1`，会共享同一个配额。
- **优先 Serve 而不是 Funnel**。Tailscale Serve 只对 tailnet 内设备可见，暴露面小一个数量级。
- **公网场景建议加一层访问控制**。Cloudflare Access、Tailscale ACL、或 Nginx 的 IP 白名单。
- **`/api/v1/health` 是公开的**，只返回帧计数和雷达状态，不含任何体征数据，这是有意设计。

## 让 App 用上公网地址

App 里添加设备时直接填公网地址即可，例如 `https://<board>.<tailnet>.ts.net`。

如果同时希望局域网内自动发现，在 `aura.service` 里设置：

```ini
Environment=AURA_DEVICE_ENDPOINT=https://<board>.<tailnet>.ts.net
```

这个值会写进 UDP 发现报文，App 扫到设备后可以直接拿到公网地址。

## 排障

| 现象 | 原因 | 处理 |
|---|---|---|
| Funnel 返回 403 | tailnet 未启用 Funnel | 管理台 Access Controls 开启 |
| Funnel 地址打不开 | 板卡掉线或 tailscaled 未启动 | `tailscale status`、`systemctl status tailscaled` |
| Cloudflare 521 | 源站没起来 | `curl 127.0.0.1:3000/api/v1/health` |
| frp 连不上 | token 不一致或 7000 端口被防火墙拦 | 看 frpc 日志 |
| 端口转发无效 | 运营商 CGNAT 或封端口 | 换隧道方案 |
| App 一直转圈 | 地址少了 `https://`，或证书不被信任 | 用浏览器先访问一次 |
| 配对总提示频繁 | 代理后所有请求共用 `127.0.0.1` 配额 | 等一分钟，或改用 Tailscale Serve |
