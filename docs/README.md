# AURA 文档

按"想知道什么"挑一篇，不用从头读。

## 规格书

| 文档 | 回答的问题 |
|---|---|
| [algorithm.md](algorithm.md) | 每一级为什么这么写？参数取多少？坏了怎么查？ |
| [hardware.md](hardware.md) | 买什么、怎么接、设备节点对不对、雷达挂死了怎么办 |
| [architecture.md](architecture.md) | 进程与线程怎么分、表结构、REST 路由、App 目录 |

三篇规格书都以 `aura/algorithm`、`aura/radar`、`aura/service` 源码为准；三者在参数上冲突时，以代码为准。

## 操作

| 文档 | 回答的问题 |
|---|---|
| [deploy.md](deploy.md) | 从裸机到能跑服务、排障表、环境变量总表 |
| [remote-access.md](remote-access.md) | 出门在外怎么看数据，以及每种方案的暴露面 |

## 其它

- `tools/verify/README.md`：三个离线复算脚本的用法
- `../.trae/skills/aura-deploy/`：给 agent 用的部署 Skill，七步，每步有通过标准
