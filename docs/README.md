# AURA 文档

按"想知道什么"挑一篇，不用从头读。

## 原理

| 文档 | 回答的问题 |
|---|---|
| [algorithm.md](algorithm.md) | 每一级为什么这么写？参数取多少？坏了怎么查？ |
| [verification.md](verification.md) | 怎么证明数字是对的？各轮改动分别解决了什么 |

`algorithm.md` 是参数手册，`verification.md` 讲验证方法本身。想要更完整的推导、公开数据集验证和设计取舍，看 [blog-algorithm.md](blog-algorithm.md)（偏论文体，列在下面「对外文稿」里）。三者冲突时以 `algorithm.md` 和代码为准。

## 操作

| 文档 | 回答的问题 |
|---|---|
| [hardware.md](hardware.md) | 买什么、怎么接、设备节点对不对、雷达挂死了怎么办 |
| [deploy.md](deploy.md) | 从裸机到能跑服务、排障表、环境变量总表 |
| [architecture.md](architecture.md) | 进程与线程怎么分、表结构、26 个 REST 路由、App 目录 |
| [remote-access.md](remote-access.md) | 出门在外怎么看数据，以及每种方案的暴露面 |

## 对外文稿

| 文档 | 定位 | 篇幅 |
|---|---|---|
| [blog-juejin.md](blog-juejin.md) | 开源发布文，覆盖能力、架构、部署，最全 | 长 |
| [blog-technical.md](blog-technical.md) | 踩坑复盘，四个坑，以及板端与离线复算的偏差怎么压到 0.3 BPM | 中 |
| [blog-heart-rate.md](blog-heart-rate.md) | 心率专题：0.05 mm 的量级、三个混叠源、30 s 窗的分辨率预算，以及"一致 ≠ 准确" | 中 |
| [blog-algorithm.md](blog-algorithm.md) | 五层处理链的推导与验证，偏论文体 | 长 |

`blog-heart-rate.md` 的四张图由仓库根目录 `_make_hr_post_figs.py` 从 `phase7.csv` / `snap7.csv` 现场生成，输出到 `docs/images/`。

## 其它

- `tools/verify/README.md`：三个离线复算脚本的用法
- `../.trae/skills/aura-deploy/`：给 agent 用的部署 Skill，七步，每步有通过标准
