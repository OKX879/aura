# 离线复算工具

这三个脚本用来把板端导出的原始位移拿回电脑上重算，判断板端输出是"自洽"还是"正确"。

## 数据来源

板端通过环境变量打开导出（详见 `docs/verification.md`）：

| 变量 | 输出 |
|---|---|
| `AURA_PHASE_DUMP` | `n,mm` 逐样本毫米位移 |
| `AURA_SNAP_DUMP` | `frame,hr,resp_hz,conf,motion` 逐帧结果 |

导出文件包含个人体征，不要提交到仓库。

## 脚本

| 脚本 | 用途 |
|---|---|
| `compare_export.py` | 用 scipy 独立算参考值，与板端输出对比 |
| `replay_board_hr.py` | 按板端 C 实现逐条复现心率估计，逐秒打印 |
| `spectrum_check.py` | 检查呼吸频带、倍频程误判和呼吸谐波 |

## 用法

```bash
pip install numpy scipy

python compare_export.py --phase phase.csv --snap snap.csv
python replay_board_hr.py --phase phase.csv --snap snap.csv
python spectrum_check.py --phase phase.csv --lo 0.14 --hi 0.50
```

`compare_export.py` 会打印：

```
参考值（后 30 秒）: 心率 64.0 BPM   呼吸 18.5 次/分
板端输出: 心率中位 64.3   呼吸中位 18.4
偏差: 心率 +0.3 BPM   呼吸 -0.0 次/分
```

## 注意

**不要把"1.0–2.5 Hz 带内最强谱峰"直接当心率真值。** 呼吸的第 4、5 次谐波正好落在这个带里，而且幅度通常比心搏大。先用 `spectrum_check.py` 确认呼吸估对了，再看心率参考是否有意义。
