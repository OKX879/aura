# AURA 非接触式生命体征监测产品线 · 技术规格书

| 项目 | 内容 |
|---|---|
| 文档编号 | AURA-TS-001 |
| 版本 | 1.1（按代码基线重写） |
| 代码基线 | `aura/algorithm/csrc`（libaura，C99） + `aura/service`（采集与服务） |
| 雷达平台 | 正和微芯 RS6240，60 GHz FMCW，2 Tx × 4 Rx |
| 文档性质 | 实现规格。每个公式、常量、判据均可追溯到源文件与函数 |

---

## 1. 范围与实现口径

### 1.1 产品线形态

| 形态 | 交付物 | 运行位置 | 技术载体 |
|---|---|---|---|
| AURA 板端 | 采集 + 体征计算 + REST | RDK X5（aarch64 Linux） | `libaura` + `AuraService` + `RadarLive` |
| AURA 手机端 | 客户端 | Android 手机 | REST 只读快照；不参与体征计算 |

本文只描述随产品交付的 **libaura C 内核** 与采集/服务边界，不含未随产品交付的任何实现。

### 1.2 代码基线与模块映射

| 文件 | 关键函数 | 职责 |
|---|---|---|
| `aura_pipeline.c` | `aura_pipeline_create/push` | 组窗、打分、锁定、融合、呼吸/心率调度 |
| `aura_bin_scorer.c` | `aura_score_bin/window` | 距离门 9 维质量评分 |
| `aura_spatial.c` | `aura_viterbi`、`aura_distance_lock_update` | 轨迹 DP、在线滞后锁定 |
| `aura_complex_fusion.c` | `aura_fuse_trajectory` | 多门多通道相干融合、相位冻结 |
| `aura_hr.c` | `aura_estimate_resp`、`aura_rhc_notch`、`aura_estimate_fft/acf/ppi`、`aura_estimate_all` | 呼吸、陷波、三路心率与融合 |
| `aura_rate.c` | `aura_rate_tracker_update` | 中位数目标 + 限幅 + 重锁 |
| `aura_event.c` | `aura_event_detect` | 逐拍事件、IBI、分段 |
| `aura_hrv.c` | `aura_hrv_compute` | RMSSD / SDRR / pNN50 / 覆盖率 |
| `aura_fast_motion.c` | `aura_fast_motion_update` | 0.5 s 复数相位快运动检测 |
| `aura_kernels.c` | `aura_welch`、`aura_acf`、`aura_find_peaks` | 数值内核 |
| `service/src/radar_live.cpp` | — | 板端采集入口：fs=90，窗 922，步 90 |
| `service/src/live.cpp` | — | 回放入口：fs=100，窗 1024，步 100 |

### 1.3 两套入口的窗口参数

| 入口 | $f_s$ | `window_n` | `step_n` | 用途 |
|---|---:|---:|---:|---|
| `radar_live.cpp` | 90 Hz | $10.24f_s=922$ | $f_s=90$ | 板端实时 |
| `live.cpp` | 100 Hz | 1024 | 100 | 回放/联调 |

**同一段代码在两种采样率下的滤波器频带不同**：SOS 系数按 $f_s$ 设计（§7.1）。本文凡标"@90 Hz"的数值，均指板端实时入口。

### 1.4 符号

| 符号 | 含义 | 单位 | 代码位置 |
|---|---|---|---|
| $f_s$ | 帧率 | Hz | `pipeline->fs` |
| $T_s$ | 帧周期 | s | $1/f_s$ |
| $C$ | 虚拟通道数 | — | 8 |
| $R$ | 距离门数 | — | 16 |
| $N$ | 窗点数 | — | `window_n` |
| $\phi_i$ | 融合相位 | rad | `ph[i]` |
| $d_i$ | 位移 | mm | `mm[i]` |
| $Q_t(k)$ | 门质量 | — | `total_score` |
| $f_r$ | 呼吸基频 | Hz | `f_resp` |

### 1.5 关键常数（源码原值）

| 常数 | 值 | 位置 |
|---|---:|---|
| 位移换算 | $5/(4\pi)=0.3979$ mm/rad | `aura_pipeline.c:356`、`aura_bin_scorer.c:191` |
| 杂波跟踪 $\alpha$ | 0.003 | `aura_pipeline.c:161` |
| 呼吸历史 | 50 s | `wbcap=(int)(50fs+0.5)` |
| 心率历史 | 120 s | `hrhist_cap=(int)(120fs+0.5)` |
| 呼吸起始 | 30 s | `wblen >= 30fs` |
| 心率预热 | 20 s | `hrhist_len >= 20fs` |
| 心率分析窗 | 30 s | `span=30fs` |
| 输出小数位 | 1 位 | `round1()` |

> 位移换算用的是名义波长 5 mm，不是 60.5 GHz 对应的 4.955 mm。二者差 0.9%，但代码里所有幅度门限、冻结阈值都建立在 5 mm 之上，本文一律按 5 mm 写。改用精确波长会把同一段相位读成更小的位移，门限需整体重标定。

---

## 2. 系统架构与数据流

### 2.1 分层

```
L4 应用   手机 App                      只读快照，不重算
L3 估计   aura_estimate_all / event / hrv  体征与质量
L2 信号   score_bin → distance_lock → fuse_trajectory → phase
L1 采集   SPI 读帧 + 帧审计 + 时间基
L0 硬件   RS6240 + CH347 + 主控
```

### 2.2 处理链（函数级）

```
aura_pipeline_push(z_re, z_im)
  ├─ 复数杂波跟踪：C ← 0.997·C + 0.003·X
  ├─ aura_score_window()            每门 9 维质量
  ├─ aura_viterbi()                 候选轨迹
  ├─ aura_distance_lock_update()    在线锁定 + 置信度
  ├─ aura_fuse_trajectory()         多门多通道融合 → ph, am, reliable
  ├─ mm[i] = ph[i] · 5/(4π)         位移
  ├─ aura_estimate_resp(wide)       呼吸（≥30 s）
  ├─ aura_estimate_all(hrhist)      心率（≥20 s 预热，30 s 窗）
  │    ├─ aura_rhc_notch()          频域零化
  │    ├─ aura_estimate_fft()       HPS
  │    ├─ aura_estimate_acf()
  │    ├─ aura_estimate_ppi()       三级回退
  │    └─ 融合 + 2:1 救援
  └─ aura_rate_tracker_update()     中位数 + ±1.5 BPM 限幅
```

### 2.3 数据率

单帧复数点：

$$
N_{pt}=C\cdot R=8\times16=128
\tag{2.1}
$$

载荷（每点 4 B）：

$$
B_{frame}=32+4N_{pt}=544\ \text{B}
\tag{2.2}
$$

原始数据率：

$$
\dot B=B_{frame}f_s=544\times90=48.96\ \text{kB/s}\approx176\ \text{MB/h}
\tag{2.3}
$$

按 100 Hz 入口则为 54.4 kB/s、196 MB/h。产品默认只落特征与事件，原始 cube 仅在显式抓取时保存。

### 2.4 时间基

慢时间按帧序号推演：

$$
t_i=iT_s
\tag{2.4}
$$

SRAM 里不存绝对时间。帧审计检查 `frame_id` 连续性与 `queue_overrun`，缺口超过 1 帧即在快照中标记，避免把丢帧当成心率变化。

---

## 3. 雷达前向模型

### 3.1 发射与去斜

单 chirp：

$$
s_T(t)=A_T\cos\left(2\pi f_0t+\pi Kt^2+\phi_0\right),\quad 0\le t<T_c
\tag{3.1}
$$

$K=B/T_c$。目标 $R(t)=R_0+x(t)$，时延 $\tau=2R/c$。混频低通后：

$$
s_{IF}(t)=A\cos\left(2\pi K\tau\,t+2\pi f_0\tau-\pi K\tau^2\right)
\tag{3.2}
$$

拍频与距离：

$$
f_b=K\tau=\frac{2KR}{c}\ \Longrightarrow\ R=\frac{c}{2K}f_b
\tag{3.3}
$$

RVP 项 $-\pi K\tau^2$ 在 $R=1$ m、$B=6.25$ GHz 下相对主项约 $3\times10^{-6}$，代码不补偿。

### 3.2 距离像

$$
X[m]=\sum_{n=0}^{N_{fast}-1}s_{IF}[n]w[n]e^{-j2\pi mn/N_{fast}}
\tag{3.4}
$$

分辨率：

$$
\Delta R=\frac{c}{2B}
\tag{3.5}
$$

实机有效带宽 6.249 GHz，$\Delta R=24.0$ mm，与固件 24 mm/Bin 一致。

### 3.3 相位—位移

$$
\phi(t)=\frac{4\pi R(t)}{\lambda}
\tag{3.6}
$$

代码取 $\lambda=5$ mm：

$$
d_i=\phi_i\cdot\frac{5}{4\pi}=0.3979\,\phi_i\ \text{mm}
\tag{3.7}
$$

180° 相位对应 1.25 mm 位移。这一步是整条链唯一的量纲入口，之后所有阈值都以 mm 或 rad 表达。

### 3.4 相位噪声

复观测 $z=Ae^{j\phi}+n$，$n$ 每维方差 $\sigma_n^2$：

$$
\sigma_\phi\approx\frac{\sigma_n}{\sqrt2 A}=\frac{1}{\sqrt{2\rho}},\qquad
\sigma_d=\frac{5}{4\pi}\sigma_\phi
\tag{3.8}
$$

$\rho=30$ dB 时 $\sigma_\phi=0.0224$ rad，$\sigma_d=8.9\ \mu$m。心搏 50–200 $\mu$m，余量 15–27 dB。

### 3.5 TDM-MIMO

2 Tx 交替发射，第 $c$ 通道的固有时间偏移 $\Delta t_c$ 引入多普勒相位：

$$
\phi_c=\phi_{c,0}+2\pi f_d\Delta t_c
\tag{3.9}
$$

补偿：

$$
Z_c\leftarrow Z_c e^{-j2\pi f\Delta t_c}
\tag{3.10}
$$

`aura_complex_fusion.c` 的对齐项 $\Delta\varphi_c=\arg\sum_i z_c\overline{z_{ref}}$ 在窄带内等效于该补偿，因此板端不再单独跑 TDM 校准。

### 3.6 位移量级与解缠极限

| 成分 | 幅度 | 频带 |
|---|---|---|
| 呼吸 | 1–10 mm | 0.1–0.5 Hz |
| 心搏 | 0.05–0.2 mm | 0.9–2.25 Hz@90 Hz |
| 体位台阶 | 0.8–1.9 mm | 宽带 |
| 解缠极限 | $\lambda/4=1.25$ mm | — |

台阶已越过解缠极限，这是 §5 必须先保相位连续的原因。

---

## 4. 空间域

### 4.1 静态杂波抑制

`aura_pipeline.c:161`：

$$
C_m[n]=\alpha C_m[n-1]+(1-\alpha)X_m[n],\qquad \alpha=0.003
\tag{4.1}
$$

$$
Y_m[n]=X_m[n]-C_m[n]
\tag{4.2}
$$

传递函数：

$$
H(z)=\frac{\alpha(1-z^{-1})}{1-\alpha z^{-1}}
\tag{4.3}
$$

截止频率：

$$
f_c\approx\frac{\alpha f_s}{2\pi}=0.043\ \text{Hz}\quad(f_s=90)
\tag{4.4}
$$

低于呼吸下边界 0.18 Hz。$\alpha$ 每翻一倍，$f_c$ 同步翻倍；$\alpha=0.02$ 时 $f_c=0.29$ Hz，会直接吃掉呼吸基频。

### 4.2 距离门质量评分

`aura_score_bin()` 对每个门、每个通道计算 9 个特征，再合成总分。

**能量**

$$
Q_E=\mathrm{clip}_{[0,1]}\left(0.5+0.1\log_{10}\big(\operatorname{median}|z|^2+10^{-6}\big)\right)
\tag{4.5}
$$

**幅度稳定性**

$$
Q_A=\exp(-\mathrm{CV}),\qquad
\mathrm{CV}=\frac{\operatorname{median}\big||z|-\operatorname{median}|z|\big|}{\operatorname{median}|z|+10^{-6}}
\tag{4.6}
$$

**相位有效率**：逐通道展开后，样本被判为坏点则不计入：

$$
Q_P=\frac{1}{NC}\sum_{c=1}^{C}\sum_{i=1}^{N}\mathbb{1}\big[\text{good}_{c,i}\big]
\tag{4.7}
$$

坏点判据（三者取或，再向两侧膨胀 2 点）：

$$
\text{bad}_{c,i}=\mathbb{1}\big[|z_{c,i}|<0.3\,\tilde z_c\big]
\vee\mathbb{1}\big[|\Delta\phi_{c,i}|>2.0\big]
\vee\mathbb{1}\big[|\Delta^2\phi_{c,i}|>0.7\big]
\tag{4.8}
$$

**跳变率**

$$
Q_J=\frac{1}{NC}\sum_{c,i}\mathbb{1}\big[|\Delta\phi_{c,i}|>2.0\big]
\tag{4.9}
$$

**通道相干性**：先对每通道做心搏带零相位滤波，再取两两相关系数的中位数（负值截 0）：

$$
Q_C=\operatorname{median}_{a<b}\left[\frac{\frac1N\sum_i(\tilde x_{a,i}-\bar x_a)(\tilde x_{b,i}-\bar x_b)}{\sigma_a\sigma_b}\right]_{+}
\tag{4.10}
$$

**周期性**：中位通道信号的 ACF 峰高，lag 搜索 $[f_s/3,\ f_s/0.7]$：

$$
Q_T=\max_{\ell\in[f_s/3,\,f_s/0.7]}R[\ell]
\tag{4.11}
$$

**谱峰清晰度**（权重为 0，仅导出诊断）：

$$
Q_S=\mathrm{clip}_{[0,1]}\left(\frac{\rho}{\rho+2}\right),\qquad
\rho=\frac{\max P(f)}{\operatorname{median}P(f)},\ f\in[0.7,3.0]
\tag{4.12}
$$

**运动惩罚**

$$
Q_M=\mathrm{clip}_{[0,1]}\left(\frac{\operatorname{median}_c \mathrm{RMS}(\tilde x_{motion,c})}{0.5}\right)
\tag{4.13}
$$

**零点质控**

$$
D_{ratio}=\operatorname{median}_c\left(\frac{1}{N}\sum_i\mathbb{1}\big[|z_{c,i}|<0.3\,\tilde z_c\big]\right)
\tag{4.14}
$$

$$
D_{term}=\mathrm{clip}_{[0,1]}\left(\frac{D_{ratio}}{0.05}\right)
\tag{4.15}
$$

**总分**

$$
\begin{aligned}
Q_t(k)=\mathrm{clip}_{[0,1]}\Big[&0.10Q_E+0.05Q_A+0.15Q_P+0.20Q_C+0.10Q_T\\
&+0.00Q_S-0.05Q_J-0.05Q_M-0.25D_{term}\Big]
\end{aligned}
\tag{4.16}
$$

权重来自 `aura_bin_scorer.c:13-21`。$Q_S$ 的权重是 0.00，不是遗漏，而是该特征与 $Q_E$、$Q_T$ 高度共线，加入后只会放大能量门的优势。

### 4.3 凹陷与相位质量

单门回波是静止分量与体表微动分量的复数和。当两者幅度接近时，复平面轨迹靠近原点，幅度塌陷，相位估计的方差随信噪比恶化而发散。因此**能量最大的门未必是相位最干净的门**，这是用多特征评分而不是直接取幅度最大的根本理由。

板端不估计幅度比，而是统计「幅度低于本通道中位数 0.3 倍」的样本占比（式 4.14），再除以 0.05 饱和（式 4.15）。选占比而非几何量，是因为占比对采样不均与多径叠加更稳健，且无需知道静止分量的绝对幅度。

### 4.4 轨迹求解

**离线 DP**（`aura_viterbi()`）：

$$
\min_{\{k_t\}}\sum_{t=1}^{T}\Big[-Q_t(k_t)+\lambda_1|k_t-k_{t-1}|+\lambda_2|k_t-2k_{t-1}+k_{t-2}|\Big]
\tag{4.17}
$$

约束 $|k_t-k_{t-1}|\le d$。递推：

$$
V_t(k)=-Q_t(k)+\min_{j:\,|j-k|\le d}\Big[V_{t-1}(j)+\lambda_1|j-k|+\lambda_2|k-2j+\Pi_{t-1}(j)|\Big]
\tag{4.18}
$$

状态数 $R$、每步候选 $2d+1$，复杂度 $O(TR(2d+1))$。$R=16$、$d=2$、$T=100$ 时约 8000 次比较。

**在线锁定**（`aura_distance_lock_update()`，板端实际使用）：

| 参数 | 值 | 含义 |
|---|---:|---|
| `min_quality` | 0.12 | 低于此判丢失 |
| `switch_margin` | 0.08 | 瞬时门需领先才参选 |
| `adjacent_confirm_windows` | 2 | 邻门确认窗数 |
| `jump_confirm_windows` | 3 | 跳门确认窗数 |
| `lost_hold_windows` | 3 | 丢失保持窗数 |
| `smooth_alpha` | 0.38 | 连续门平滑 |

候选门：

$$
k^{cand}_t=
\begin{cases}
\arg\max_k Q_t(k), & Q_t(k^{top})\ge Q_t(k^{vit})+0.08\\
k^{vit}, & \text{否则}
\end{cases}
\tag{4.19}
$$

切换所需确认窗数：

$$
n_{req}=
\begin{cases}
2, & |k^{cand}-k^{lock}|\le1\\
3, & |k^{cand}-k^{lock}|>1
\end{cases}
\tag{4.20}
$$

若当前锁定门质量低于 0.12 而瞬时门高出 0.18 以上，跳门确认数降到 2（快速接管）。

锁定置信度：

$$
c_t=\mathrm{clip}_{[0,1]}\left(0.72\,Q_t(k^{lock})+0.28\min\left(\frac{\Delta Q_t}{0.20},1\right)\right)
\tag{4.21}
$$

$$
\Delta Q_t=Q_t(k^{top})-Q_t(k^{second})
\tag{4.22}
$$

丢失时再乘衰减：

$$
c_t\leftarrow c_t\cdot\max\left(0,\ 1-\frac{n_{lost}}{3+2}\right)
\tag{4.23}
$$

### 4.5 亚门插值与平滑

$$
\hat k=k+\frac{1}{2}\frac{Q_{k-1}-Q_{k+1}}{Q_{k-1}-2Q_k+Q_{k+1}},\qquad |\hat k-k|\le0.5
\tag{4.24}
$$

$$
\tilde k_t=(1-\alpha)\tilde k_{t-1}+\alpha\hat k_t,\qquad \alpha=0.38
\tag{4.25}
$$

亚门插值不提高距离分辨率（那由 $B$ 决定），它把跨门时的相位台阶摊成连续过渡，降低解缠失败率。

### 4.6 多门多通道融合

`aura_fuse_trajectory()` 的调用参数（`aura_pipeline.c:342`）：

| 参数 | 值 |
|---|---:|
| `neighborhood` | 2 |
| `sigma_r` | 1.0 |
| `beta` | 3.0 |
| `coherence_threshold` | 0.5 |
| `channel_coh_power` | 2.0 |
| `bin_coh_power` | 2.0 |
| `bin_coh_min` | 0.0 |
| `amp_floor_frac` | 0.3 |
| `warmup_s` | 3.0 |
| `phase_freeze_max_gap` | 30 |
| `tau_phi_jump` | 1.0 rad |
| `tau_phi_acc` | 0.7 rad |

**门权重**

$$
w_r=\exp(\beta Q_r)\cdot\exp\left(-\frac{(r-r_0)^2}{2\sigma_r^2}\right),\qquad \beta=3
\tag{4.26}
$$

**门相干因子**

$$
b_r=\max\big(\mathrm{coh}_r,\ b_{min}\big),\qquad
b_{r_0}=\max(\mathrm{coh}_{r_0},0.20)
\tag{4.27}
$$

$$
w_r^{eff}=w_r\cdot b_r^{2}
\tag{4.28}
$$

**通道相干**

$$
\mathrm{coh}_c=
\frac{\left|\sum_i z_{c,i}\overline{z_{ref,i}}\right|}
{\sqrt{\sum_i|z_{c,i}|^2\sum_i|z_{ref,i}|^2}}
\tag{4.29}
$$

$\mathrm{coh}_c<0.5$ 的通道直接剔除。

**对齐与融合**

$$
\Delta\varphi_c=\arg\left(\sum_i z_{c,i}\overline{z_{ref,i}}\right)
\tag{4.30}
$$

$$
\tilde z_i=
\frac{\sum_{r\in\mathcal N}\sum_c w_r^{eff}\,\mathrm{coh}_c^{2}\,
z_{c,i}e^{-j\Delta\varphi_c}}
{\sum_{r\in\mathcal N}\sum_c w_r^{eff}\,\mathrm{coh}_c^{2}}
\tag{4.31}
$$

按总权重归一化，不是简单求和。归一化保证融合后幅度不随参与通道数变化，`am` 才能直接用于门限判断。

**幅度下限**：$|z_{c,i}|<0.3\,\tilde z_c$ 的样本标为不可靠，供 §5 掩码使用。

**相位冻结**：检测到 $|\Delta\phi|>1.0$ rad 或 $|\Delta^2\phi|>0.7$ rad 时冻结相位，最长 30 点（0.33 s @90 Hz）。冻结期内用上一有效相位填充，并置 `reliable=0`。

---

## 5. 相位重建

### 5.1 提取与解缠

$$
\phi_i=\operatorname{atan2}(Q_i,I_i)\in(-\pi,\pi]
\tag{5.1}
$$

$$
\phi^{u}_i=\phi_0+\sum_{m=1}^{i}\operatorname{wrap}(\phi_m-\phi_{m-1})
\tag{5.2}
$$

$$
\operatorname{wrap}(\delta)=\delta-2\pi\left\lfloor\frac{\delta+\pi}{2\pi}\right\rfloor
\tag{5.3}
$$

解缠正确的前提：

$$
|x_i-x_{i-1}|<\frac{\lambda}{4}=1.25\ \text{mm}
\tag{5.4}
$$

90 Hz 下正常呼吸帧间位移约 0.025 mm，心搏约 0.001 mm，均远低于极限；0.8–1.9 mm 的台阶会越过极限，产生 $\lambda/2=2.5$ mm 的整数倍偏移：

$$
\hat x_i=x_i\pm2.5k,\qquad k\in\mathbb{Z}
\tag{5.5}
$$

### 5.2 坏点与掩码

与式 (4.8) 同一套判据，融合阶段输出逐点 `reliable`：

$$
M_i=\mathbb{1}\Big[|z_i|\ge0.3\tilde z\ \wedge\ |\Delta\phi_i|\le1.0\ \wedge\ |\Delta^2\phi_i|\le0.7\Big]
\tag{5.6}
$$

可靠率：

$$
P_{valid}=\frac1N\sum_i M_i
\tag{5.7}
$$

$P_{valid}$ 不直接参与心率融合，只写入快照供展示与事后归因。

### 5.3 位移

$$
d_i=0.3979\,\phi^{u}_i\ \text{mm}
\tag{5.8}
$$

去均值后进入呼吸缓冲 `wide`（50 s）与心搏历史 `hrhist`（120 s）。

### 5.4 误差传播

单频分量的频率标准差：

$$
\sigma_f\approx\frac{\sigma_\phi}{2\pi T_w\sqrt{\rho}}
\tag{5.9}
$$

$\sigma_\phi=0.02$ rad、$T_w=30$ s、$\rho=1000$ 时 $\sigma_{HR}\approx0.003$ BPM。实际误差比这高 3–4 个数量级，来源是候选选择与呼吸谐波，不是相位噪声。把工程投入放在降噪上，收益远小于修候选逻辑。

---

## 6. 呼吸估计

### 6.1 触发条件

`aura_pipeline.c:392`：

$$
N_{wide}\ge 30f_s
\tag{6.1}
$$

满足后才调用 `aura_estimate_resp(p->wide, p->wblen, fs, SOS_RESP)`。`wide` 容量 50 s，因此实际参与估计的窗长在 30–50 s 之间滑动。

### 6.2 零相位滤波与 Welch

先零相位带通（`aura_sosfiltfilt`，`AURA_SOS_RESP`），再做 Welch：

$$
\hat P(f)=\frac{1}{K}\sum_{k=1}^{K}\frac{2}{f_s\sum_n w^2[n]}\left|\sum_{n=0}^{L-1}x_k[n]w[n]e^{-j2\pi fn/f_s}\right|^2
\tag{6.2}
$$

实现细节（`aura_welch()`）：

| 项 | 值 |
|---|---|
| 窗 | Hann，$0.5-0.5\cos(2\pi n/L)$ |
| 重叠 | $L/2$（50%） |
| 分段数 | $1+\lfloor(n-L)/(L/2)\rfloor$ |
| 每段去均值 | 是 |
| 单边谱 | 除 DC 与 Nyquist 外 ×2 |
| $L$ | $\min(n,2048)$ |

$f_s=90$、$L=2048$：

$$
\Delta f=\frac{90}{2048}=0.0439\ \text{Hz}=2.64\ \text{次/分}
\tag{6.3}
$$

### 6.3 峰值搜索

$$
f_r=\arg\max_{f\in[0.18,\,0.50]}\hat P(f)
\tag{6.4}
$$

输出范围检查：

$$
7\le 60f_r\le28\ \text{次/分}
\tag{6.5}
$$

显著性：

$$
\rho_{peak}=\frac{\hat P(f_r)}{\operatorname{median}_{f\in[0.18,0.50]}\hat P(f)}\ge1.8
\tag{6.6}
$$

任一条件不满足返回 `NAN`，由上层保持。

### 6.4 抛物线插值

$$
\delta=\frac12\frac{\hat P[k-1]-\hat P[k+1]}{\hat P[k-1]-2\hat P[k]+\hat P[k+1]},\qquad |\delta|\le0.5
\tag{6.7}
$$

$$
\hat f_r=f[k]+\delta\,(f[k+1]-f[k])
\tag{6.8}
$$

插值后仍需落在 7–28 次/分，否则丢弃插值结果。

### 6.5 保持机制

`aura_pipeline.c:397-403`：

$$
f_r^{(out)}=
\begin{cases}
f_r^{new}, & \text{估计成功}\\
f_r^{hold}, & \text{失败且 } age<8\\
\text{NAN}, & \text{失败且 } age\ge8
\end{cases}
\tag{6.9}
$$

保持期内 `resp_hold_age` 递增，最多沿用 8 个输出周期（约 8 s）。

### 6.6 为什么 8 秒窗不行

8 s、$f_s=90$：$L=720$，$\Delta f=0.125$ Hz，量化步进 7.5 次/分。0.18–0.50 Hz 内只落得下 0.25 与 0.375 Hz 两个格点，真实 0.308 Hz 只能被量化到 15 或 22.5 次/分：

$$
\hat f=\Delta f\arg\min_k|k\Delta f-f|
\tag{6.10}
$$

分辨率不足时，估计不是偏一点，而是落到错误的格点上。

### 6.7 另一条 API 入口

`aura_estimate_resp_api()` 是独立实现，参数不同：

| 项 | `aura_estimate_resp` | `aura_estimate_resp_api` |
|---|---|---|
| 滤波 | 零相位 SOS | 无（仅去均值 + Hann） |
| 频带 | 0.18–0.50 Hz | 0.12–0.60 Hz |
| 峰值判据 | 峰/中位 ≥1.8 | 峰/带内总能量 ≥0.20 |
| 基线保护 | 无 | 峰在带首则判为漂移并返回 NAN |
| 最小长度 | 256 点 | $8f_s$ |

两者不可混用。板端主链只用前者；`_api` 供外部宿主在无 SOS 系数时调用。

---

## 7. 心率估计

### 7.1 带通

`AURA_SOS_HR` 四级二阶节，因果滤波、状态跨窗（`aura_sosfilt_state`）：

$$
y[n]=b_0x[n]+b_1x[n-1]+b_2x[n-2]-a_1y[n-1]-a_2y[n-2]
\tag{7.1}
$$

用 `sosfreqz` 实测 $-3$ dB 频带：

| 通道 | @90 Hz | @100 Hz | 用途 |
|---|---|---|---|
| `AURA_SOS_HR` | 0.90–2.25 Hz | 1.00–2.50 Hz | 心率主链 |
| `AURA_SOS_CARDIAC` | 0.63–2.70 Hz | 0.70–3.00 Hz | 门评分/融合 |
| `AURA_SOS_RESP` | 0.126–0.450 Hz | 0.140–0.500 Hz | 呼吸预滤波 |
| `AURA_SOS_MOTION` | 0.045–9.0 Hz | 0.050–10.0 Hz | 运动能量 |

注：系数按采样率生成，"1.0–2.5"这个常被引用的区间只在 100 Hz 入口成立。板端 90 Hz 的实际通带是 0.90–2.25 Hz。

### 7.2 呼吸谐波零化

`aura_rhc_notch()`。先取候选峰：

<figure>
<img src="figures/fig_harmonic_collision.png" alt="呼吸谐波与心率搜索带的频域碰撞">
<figcaption>图 7-1　呼吸 18.5 次/分（0.308 Hz）时，4 次谐波 1.233 Hz = 73.9 BPM 落在心率搜索带中央，幅度约为真实心搏的 3 倍。零化半宽只有 ±0.06 Hz，呼吸估偏 0.9 次/分，这一阶就完全挖不到。</figcaption>
</figure>

- 心率带 $[40,180]$ BPM 内幅度前 3 的谱峰，间距 $\ge8$ BPM 去重；
- ACF 主峰（lag $[f_s/180,f_s/40]$），与已有候选 $<3$ BPM 视为重复。

谐波阶数：

$$
k\in\left[\left\lceil\frac{f_{lo}}{f_r}\right\rceil,\ \min\left(\left\lfloor\frac{f_{hi}}{f_r}\right\rfloor,8\right)\right],\quad f_{lo}=\frac{40}{60},\ f_{hi}=\frac{180}{60}
\tag{7.2}
$$

跳过规则：

$$
\text{skip}(k)=\mathbb{1}\big[40\le60kf_r\le60\big]\ \vee\ \mathbb{1}\big[\exists c:\ |60kf_r-c|<6\big]
\tag{7.3}
$$

第一条是低心率盲区保护，第二条是真峰碰撞保护。

零化：

$$
\hat X(f)\leftarrow0,\qquad |f-kf_r|<0.06\ \text{Hz}
\tag{7.4}
$$

IFFT 回时域后送入三路估计。**不是最小二乘回归，也不是相位同步消除**；实现就是频域置零，代价是陷波带内信息不可恢复，收益是实现简单、行为可预测。

频率误差敏感度：$k$ 阶谐波位置偏差 $k\epsilon$，当

$$
k\epsilon>0.06\ \text{Hz}
\tag{7.5}
$$

陷波完全落空。$k=4$ 时 $\epsilon>0.015$ Hz（0.9 次/分）。呼吸估偏 3%，陷波就白做。

### 7.3 FFT + HPS

搜索带 $[40,180]$ BPM。HPS：

$$
P_{HPS}(f)=\prod_{h=1}^{6}\hat P(hf)
\tag{7.6}
$$

实现上先取 $P(f)$，再依次乘 $P(2f)\dots P(6f)$，$hf>f_s/2$ 的阶跳过。

**强峰筛选**：

$$
\mathcal S=\{k:\hat P(f_k)\ge0.15\max\hat P\}
\tag{7.7}
$$

**碰撞剔除**：若 $\mathcal S$ 中同时存在与 $k f_r$ 距离 $<3$ BPM 的峰和不碰撞的峰，则移除碰撞峰，除非其 HPS 值超过未碰撞峰最大值的 10 倍：

$$
\text{keep}=\mathbb{1}\big[\text{no collision}\big]\ \vee\ \mathbb{1}\big[P_{HPS}>10\max_{\text{no col}}P_{HPS}\big]
\tag{7.8}
$$

**选峰与插值**：在保留的强峰中取 $P(f)$ 最大者，再用对数域抛物线：

$$
y_j=\log\max\big(P[k+j]-\operatorname{median}\hat P,\,10^{-15}\big),\quad j\in\{-1,0,1\}
\tag{7.9}
$$

$$
\delta=\frac12\frac{y_{-1}-y_{+1}}{y_{-1}-2y_0+y_{+1}},\qquad |\delta|\le0.5
\tag{7.10}
$$

$$
HR_{FFT}=60\left(f[k]+\delta(f[k+1]-f[k])\right)
\tag{7.11}
$$

SNR 定义为峰值除以带内中位数，写入 `snr_fft`。

### 7.4 自相关

`aura_estimate_acf()`：

$$
R[\ell]=\sum_{n=0}^{N-\ell-1}x[n]x[n+\ell]
\tag{7.12}
$$

候选 lag：

$$
\ell\in\left[\frac{f_s}{3.0},\ \frac{f_s}{0.6667}\right]
\tag{7.13}
$$

边缘折扣：距区间端点不足 3 点时线性压到 0.55：

$$
R'[\ell]=R[\ell]\cdot\left(0.55+0.45\frac{d_{edge}}{3}\right),\qquad d_{edge}<3
\tag{7.14}
$$

$$
\ell^*=\arg\max_\ell R'[\ell],\qquad HR_{ACF}=\frac{60f_s}{\ell^*}
\tag{7.15}
$$

峰高低于 0.25 时不参与融合（返回 `NAN`）。

lag 量化误差：

$$
\Delta HR\approx\frac{HR^2}{60f_s}
\tag{7.16}
$$

60 BPM 时 0.67 BPM，150 BPM 时 4.2 BPM。高心率段 ACF 分辨率明显变粗，必须与 FFT 互补。

### 7.5 PPI 三级回退

`aura_estimate_ppi()` 依次尝试：

| 顺序 | 函数 | `source` | 方法 |
|---|---|---:|---|
| 1 | `aura_estimate_ppi_event` | 0 | 事件检测 + IBI 中位数 |
| 2 | `aura_estimate_ppi_template` | 1 | ACF 定周期 → 平均模板 → 相关峰 |
| 3 | `aura_estimate_ppi_findpeaks` | 2 | 直接寻峰 |

**事件检测**（`aura_event_detect`）：

1. 镜像延拓 2 s，裁掉两端 1.5 s；
2. 1.5 s 居中滑动 RMS 包络：

$$
e[n]=\sqrt{\frac{1}{W}\sum_{k=n-W/2}^{n+W/2}x^2[k]},\qquad W=1.5f_s
\tag{7.17}
$$

3. 归一化：

$$
\tilde x[n]=\frac{x[n]}{\max\big(e[n],\,q_{0.02}(e)\big)}
\tag{7.18}
$$

4. 寻峰：`distance`$=d_{min}f_s$，`height`$=0.60$，`prominence`$=0.20$，`wlen`$=2d_{min}f_s$；
5. 峰位抛物线插值到亚采样；
6. 去重（0.05 s）后估计中位 IBI，令

$$
d_{min}=\mathrm{clip}(0.6\,\widetilde{IBI},\ 0.383,\ 0.9)\ \text{s}
\tag{7.19}
$$

并用新间隔重跑一遍。

**去重搏波**：对相邻两间期 $(a,b)$，若满足任一组比例条件，删除中间那个事件：

$$
(a+b)\in[1.5,2.5]\bar IBI\ \wedge\ \frac{a}{a+b}\in[0.25,0.40]
\tag{7.20}
$$

$$
(a+b)\in[0.9,1.4]\bar IBI\ \wedge\ a<0.7\bar IBI\ \wedge\ \frac{a}{a+b}\in[0.25,0.42]
\tag{7.21}
$$

$$
(a+b)\in[0.8,1.8]\bar IBI\ \wedge\ \frac{a}{a+b}\in[0.42,0.58]
\tag{7.22}
$$

**去半拍**：若 $a<0.8\bar IBI$ 且

$$
a+b\in[0.9,1.35]\bar IBI\ \vee\ a+b\in[1.65,2.45]\bar IBI
\tag{7.23}
$$

删除中间事件。

**IBI 输出**：

$$
IBI_i=1000\,(t_i-t_{i-1})
\tag{7.24}
$$

只保留 $[333,1500]$ ms，超出即断段。心率：

$$
HR_{PPI}=\frac{60000}{\operatorname{median}\{IBI_i\}}
\tag{7.25}
$$

中位数击穿点 50%。漏一拍使该间期翻倍，直接导致半频：

$$
IBI_i\to2IBI_i\ \Rightarrow\ HR_{PPI}\to HR_{PPI}/2
\tag{7.26}
$$

### 7.6 三路融合

权重：

$$
w_{FFT}=\mathrm{clip}\left(\frac{\mathrm{SNR}}{5},0.1,1\right)
\tag{7.27}
$$

$$
w_{ACF}=\mathrm{clip}(h_{ACF},0.1,1)
\tag{7.28}
$$

$$
w_{PPI}=\mathrm{clip}\left(\frac{n_p-4}{12},0.05,1\right)
\tag{7.29}
$$

$n_p$ 是 PPI 路峰数。$n_p\le4$ 时权重压到 0.05。

$h_{ACF}$ 来自未归一化的 `aura_acf()`，量纲随信号能量变化；信号稍强时 $h_{ACF}\gg1$，权重被 clip 饱和到 1.0。也就是说 ACF 路的权重实际上只有"有没有峰"两档，不随峰高连续变化。

一致判据：

$$
\Delta_{max}=\max_i HR_i-\min_i HR_i<8\ \text{BPM}
\tag{7.30}
$$

**一致**：按有效路加权平均：

$$
HR_{fused}=\frac{\sum_i w_iHR_i}{\sum_i w_i}
\tag{7.31}
$$

**不一致**（$f_s\ge30$）：若三路齐全且

$$
|HR_{FFT}-HR_{ACF}|\le10\quad\wedge\quad\left|HR_{PPI}-\frac{HR_{FFT}+HR_{ACF}}2\right|>8
\tag{7.32}
$$

取 FFT/ACF 均值；否则取三路中位数。

**不一致且 $f_s<30$**：仅当极差 $\le40$ BPM 时取中位数，否则不输出。

**只剩一路**：按 PPI → ACF → FFT 的优先级输出。

### 7.7 2:1 高心率救援

`aura_pipeline.c:480`：

$$
|HR_{ACF}-HR_{PPI}|\le6
\ \wedge\ HR_{FFT}\ge80
\ \wedge\ \left|HR_{FFT}-2\cdot\frac{HR_{ACF}+HR_{PPI}}2\right|\le8
\tag{7.33}
$$

同时满足时，用 $HR_{FFT}$ 覆盖融合值，置信度改为：

$$
c=\max\left(0.35,\ 1-\frac{|HR_{ACF}-HR_{PPI}|}{24}\right)
\tag{7.34}
$$

救援针对的是"ACF/PPI 同时漏拍、FFT 取到二倍频"的场景。条件是三路同时满足的窄门，误触发率低。

### 7.8 速率跟踪器

`aura_rate_tracker_update()` 不是简单的指数平滑，而是"中位数目标 + 限幅"。

**首次锁定**：把连续值压入 6 样本 bank，取跨度不超过 8 BPM 的最大簇：

$$
\mathcal C^*=\arg\max_{\mathcal C\subseteq\mathcal B,\ \max\mathcal C-\min\mathcal C\le8}|\mathcal C|
\tag{7.35}
$$

簇内样本数 $\ge3$ 即锁定，取簇中位数；否则最多试 6 次后强制锁定。

**常规更新**：

$$
m_t=\operatorname{median}\big(v_{t-4},\dots,v_t\big)
\tag{7.36}
$$

$$
v_t=v_{t-1}+\mathrm{clip}\left(m_t-v_{t-1},-1.5,1.5\right)
\tag{7.37}
$$

输出前 `round1` 保留 1 位小数。

**重锁**：

$$
|m_t-v_{t-1}|>15\ \text{BPM}
\tag{7.38}
$$

且方向连续 3 次一致、重锁 bank 内 3 个值极差 $\le10$ BPM，才把 $v_t$ 直接设为 bank 中位数。三条同时满足，避免体动尖峰触发误重锁。

**预热**：`hrhist_len<20f_s` 时不调用跟踪器（`hr_warm=0`），输出保持 0。

### 7.9 一致性置信度

$$
c_t=\mathrm{clip}_{[0,1]}\left(1-\frac{\Delta_{max}}{24}\right)
\tag{7.39}
$$

这是板端唯一的置信度定义。它衡量"三路是否一致"，不衡量"是否准确"。三路同时锁到同一个谐波时，$\Delta_{max}$ 很小，置信度很高，结果是错的。第 9 章的门控无法识别这种共模错误。

---

## 8. 事件、IBI 与 HRV

### 8.1 事件检测的窗口结构

| 参数 | 值 |
|---|---:|
| 检测窗长 | 16 s |
| 步进 | 1 s |
| 镜像延拓 | 2 s |
| 边缘裁剪 | 1.5 s |
| 事件去重 | 0.05 s |
| 最小 IBI | 0.333 s |
| 最大 IBI | 1.5 s |

同一事件会在相邻窗重复检出，去重保留窗内最早出现者。

### 8.2 IBI 与分段

$$
IBI_i=1000(t_i-t_{i-1}),\qquad 333\le IBI_i\le1500
\tag{8.1}
$$

超出范围的间期不删除事件，而是把序列切成新段（`segment_ids` 递增）。跨段不做差分，避免把"离开再回来"的长间隔算进 HRV。

### 8.3 HRV 指标

`aura_hrv_compute()`：

$$
RMSSD=\sqrt{\frac{1}{N_d}\sum_{i}\big(IBI_{i+1}-IBI_i\big)^2}
\tag{8.2}
$$

求和只在同段内进行，$N_d$ 是同段差分总数。

$$
SDRR=\sqrt{\frac{1}{N-1}\sum_{i=1}^{N}\big(IBI_i-\overline{IBI}\big)^2}
\tag{8.3}
$$

$$
pNN50=100\cdot\frac{\#\{|IBI_{i+1}-IBI_i|>50\ \text{ms}\}}{N_d}
\tag{8.4}
$$

单位是百分比，与常见定义一致。

$$
HR_{display}=\operatorname{median}\big(\text{最近 5 个有效 }HR\big)
\tag{8.5}
$$

### 8.4 数据充足性门槛

`mode=0`（整段）：

| 条件 | 阈值 | 不满足时的 reason |
|---|---:|---|
| 总时长 | $\ge30$ s | `duration_too_short` |
| IBI 数 | $\ge30$ | `total_ibis_too_few` |
| 覆盖率 | $\ge0.80$ | `coverage_too_low` |

`mode=1`（分段）：最长同段 IBI 数 $\ge240$，否则 `max_segment_too_short`。

注意 IBI 门槛是 30，不是 20。30 个 IBI 在 60 BPM 下约 30 s，与时长门槛同步。

### 8.5 伪峰对 HRV 的破坏

设某次漏检使一个间期由 $I$ 变为 $2I$，相邻两个差值变为 $I$ 与 $-I$。平方和增加 $2I^2$：

$$
RMSSD_{new}=\sqrt{\frac{(N-3)RMSSD_{old}^2+2I^2}{N-1}}
\tag{8.6}
$$

$N=30$、$RMSSD_{old}=30$ ms、$I=1000$ ms：

$$
RMSSD_{new}=\sqrt{\frac{27\times900+2\times10^6}{29}}\approx264\ \text{ms}
\tag{8.7}
$$

一次漏检把 RMSSD 从 30 ms 拉到 264 ms，放大约 8.8 倍。同一漏检对平均心率的影响只有 $1/N\approx3.3\%$。HRV 对伪峰的敏感度比平均心率高一个数量级，这是它必须过四道门（峰匹配、间期范围、最小 IBI 数、覆盖率）的原因。

---

## 9. 输出门控与实时性

### 9.1 快运动检测

`aura_fast_motion_update()`：

| 参数 | 值 |
|---|---:|
| 观察窗 | 0.5 s |
| 相位阈值 | 0.10 rad/帧 |
| 累积偏移阈值 | 2.5 rad |
| 静息清除窗数 | 4 |

判据：

$$
\overline{|\Delta\phi|}>0.10\ \text{rad}\ \vee\ \sum|\Delta\phi|>2.5\ \text{rad}
\tag{9.1}
$$

触发后 `motion=1`，需连续 4 个静息窗才清除。此外 `pipeline` 在 `mean_motion>0.6` 时也置 `motion=1`。

### 9.2 输出状态

| 字段 | 含义 | 无效值 |
|---|---|---|
| `hr_bpm` | 跟踪器输出 | 0 |
| `resp_hz` | 呼吸基频 | NAN |
| `presence` | `isfinite(hr_fused)` | 0 |
| `motion` | 快运动/门评分运动 | 0/1 |
| `hr_confident` | 跟踪器本次是否接受 | 0/1 |
| `hr_conf` | $1-\Delta_{max}/24$ | 0–1 |

`hr_bpm=0` 不是"心率为 0"，是"无有效输出"。前端必须按无效处理，不能用 0 参与均值或绘图。

### 9.3 延迟预算

分析窗 $[t-T_w,t]$ 的统计中心在 $t-T_w/2$：

$$
T_{lat}=\frac{T_w}{2}+T_{proc}+T_{queue}+T_{net}
\tag{9.2}
$$

| 环节 | 数值 | 说明 |
|---|---:|---|
| 心率窗中心 | 15 s | 30 s 窗 |
| 呼吸窗中心 | 15–25 s | 30–50 s 缓冲 |
| 算法单帧 | 5–15 ms | 16 门 × 8 通道 |
| 上报周期 | 3 s | 批量 |
| 显示刷新 | 1 s | 快照轮询 |

心率数值代表 15 s 前的平均状态，呼吸更久。产品文案不能写"实时心率"而不说明窗口。

### 9.4 时频不确定性

$$
\Delta f\cdot T_w\ge\frac{1}{4\pi}
\tag{9.3}
$$

工程可分辨间隔：

$$
\Delta HR_{min}=\frac{60}{T_w}
\tag{9.4}
$$

| $T_w$ | $\Delta HR_{min}$ |
|---:|---:|
| 8 s | 7.5 BPM |
| 10.24 s | 5.9 BPM |
| 30 s | 2.0 BPM |

分辨率与延迟是同一枚硬币的两面。

---

## 10. 服务与数据层

### 10.1 上报

板端每 3 s 聚合一条：

$$
n_{day}=\frac{86400}{3}=28800\ \text{条/设备/天}
\tag{10.1}
$$

单条约 200 B，日增 5.8 MB。

### 10.2 记录字段（与快照一一对应）

| 组 | 字段 | 来源 |
|---|---|---|
| 标识 | device_id, session_id, ts | 服务层 |
| 体征 | hr_bpm, resp_hz | `AuraSnapshot` |
| 三路 | hr_fft, hr_acf, hr_ppi | 同上 |
| 融合 | hr_fused, w_fft, w_acf, w_ppi | 同上 |
| 质量 | hr_conf, hr_max_diff_bpm, hr_consistent | 同上 |
| 门控 | presence, motion, hr_confident, hr_rescued | 同上 |
| 空间 | selected_bin, candidate_bin, fused_amp, mean_coherence | 同上 |
| 版本 | algo_version, config_hash | 构建期 |

三路候选必须落库。没有候选值，事后无法回答"这个心率是哪一路主导、为什么被接受"。

### 10.3 幂等

唯一键 `(device_id, session_id, ts)`，重复写入 upsert。跨期比较前先冻结去重口径，否则"数据变多"会被误读为"性能变化"。

### 10.4 接口边界

服务层不重算算法。任何"服务端重算心率"的设计都会引入第二套参数，与板端不一致。

---

## 11. 验证方法与指标

### 11.1 真值层级

| 级 | 参考 | 能回答 | 不能回答 |
|---|---|---|---|
| A | 同步 ECG | 准确性 | — |
| B | 指夹 PPG | 趋势一致性 | 运动段 |
| C | 离线复算 | 实现一致性 | 准确性 |
| D | 算法互比 | 稳定性 | 准确性 |

C/D 级证据不能用于准确性声明。

### 11.2 指标

$$
MAE=\frac1n\sum|\hat y_i-y_i|,\qquad
RMSE=\sqrt{\frac1n\sum(\hat y_i-y_i)^2}
\tag{11.1}
$$

$$
Bias=\frac1n\sum(\hat y_i-y_i),\qquad
命中率_{\pm\delta}=\frac1n\sum\mathbb{1}[|\hat y_i-y_i|\le\delta]
\tag{11.2}
$$

$$
CCC=\frac{2\rho\sigma_x\sigma_y}{\sigma_x^2+\sigma_y^2+(\mu_x-\mu_y)^2}
\tag{11.3}
$$

### 11.3 Bland–Altman

$$
d_i=x_i-y_i,\qquad LoA=\bar d\pm1.96s_d
\tag{11.4}
$$

重叠窗口使样本不独立，LoA 必须按会话/受试者做块 bootstrap：

$$
CI_{1-\alpha}=\left[\hat\theta^*_{(\alpha/2)},\hat\theta^*_{(1-\alpha/2)}\right]
\tag{11.5}
$$

### 11.4 切分泄漏

相邻窗重叠率：

$$
\text{overlap}=\frac{T_w-\Delta t}{T_w}
\tag{11.6}
$$

$T_w=10.24$ s、$\Delta t=1$ s 时重叠 90.2%。随机逐行切分会让测试集与训练集共享九成采样点，交叉验证成绩接近训练集。

### 11.5 已测结果与口径

| 指标 | 数值 | 口径 |
|---|---:|---|
| 板端 vs 离线复算（HR） | +0.3 BPM | 同一 60 s 位移 |
| 板端 vs 离线复算（RR） | 0.0 次/分 | 同上 |
| C PPI 会话内 SD | 11.56 BPM | 36 会话 3745 帧 |
| C PPI 相邻帧跳变中位 | 7.03 BPM | 同上 |
| C 引擎 vs 最终输出 Bias | −0.39 BPM | 实现一致性 |
| C 引擎 LoA | [−7.5, 8.2] BPM | 实现一致性 |

前两行说明实现路径已对齐；后四行说明逐帧稳定性尚未达标。两组数字不能互相替代。

### 11.6 公开数据集心率精度

以下为本产品线 `libaura` 在两个外部数据集上的实测结果。两者**输入层级不同**，不得合并成一个数字对外引用。

#### 数据集来源

| 数据集 | 本地标识 | 内容 | 金标 | 许可与引用 |
|---|---|---|---|---|
| Capsule | `capsule-4604022-data/Minimum_dataset` | 20 例 `patient_*.mat`，每例 3000 帧，100 Hz，8 通道 × 30 距离门复数 cube | 同步 ECG，R 峰 | 随附 **CC0 1.0**；引用时须注明数据集标识与本地版本，不得改写原始标签 |
| emo | `emo_dataset`（原始 `raw/_multi`，导出 `processed/02_processed_data/mmwave`） | 38 个 trial，每个约 20 s，100 Hz | 同步 PPG，峰间期中位数 | 本地副本**未随附许可证与引用信息**；仅限内部评估，不得再分发或对外引用 |

两个数据集均只用于**算法精度评估**，未参与任何参数调优以外的用途；本文不复制其原始数据，只报告指标。

#### 测试口径

| 项 | Capsule | emo |
|---|---|---|
| 输入层级 | **整链**：复数 cube → 空间域 → 融合相位 → 心率 | **仅估计器层**：数据集已融合的宽带位移 → 心率 |
| 采样率 | 100 Hz | 100 Hz |
| 分析窗 / 步进 | 10.24 s / 1 s | 每条 trial 一个 20 s 窗 |
| 单例输出 | 窗口内有效输出取中位数 | 单个输出 |
| 金标定义 | $60000/\operatorname{median}(IBI)$ | 同左，源为 PPG |
| 场景 | 单人静坐，0.6–1.2 m | 单人，坐/卧、静息与运动后 |

#### 结果

**emo（38 trial，估计器层，全部有输出）**

| 指标 | 数值 |
|---|---:|
| 有效样本 | 38 / 38 |
| MAE | **3.33 BPM** |
| 中位绝对误差 | 1.71 BPM |
| 最大绝对误差 | 23.70 BPM |
| $\le 5$ BPM | 31 / 38 |
| $\le 10$ BPM | 35 / 38 |

**Capsule（20 例，整链，需补足预热）**

该数据集的单例片段只有 30 s，短于启动门槛：

$$
\underbrace{3\ \text{s}}_{\text{预热}}+\underbrace{10.24\ \text{s}}_{\text{窗长}}+\underbrace{20\ \text{s}}_{\text{心率历史}}=33.2\ \text{s}>30\ \text{s}
$$

直接跑整链**不输出**。为得到可报告的整链参考值，采用**同一片段循环拼接 2 次**（60 s），只统计第二段内的窗口；第一段仅用于满足预热与历史累积。

| 指标 | 数值 |
|---|---:|
| 有效样本 | 18 / 20 |
| MAE | **8.15 BPM** |
| 中位绝对误差 | 3.89 BPM |
| 最大绝对误差 | 47.66 BPM |
| $\le 5$ BPM | 9 / 18 |
| $\le 10$ BPM | 13 / 18 |

20 个文件中 3 对内容完全相同，去重后为 17 例，其中 16 例有输出：MAE 8.40 BPM、中位 4.00 BPM。

#### 结果解读与使用边界

1. **Capsule 的 8.15 是"补预热后"的参考值，不是独立样本精度**。拼接引入接缝，且第二段与第一段同源，窗口不独立；该数字可用于判断整链能否工作，不能用于对外宣称精度。
2. **高心率段失效**。20 例中两例金标约 109 BPM 全程无输出，一例金标 105 BPM 锁到 57.6 BPM（半频）。整链在 >100 BPM 的可用性尚未解决。
3. **emo 只覆盖估计器层**。输入是数据集已融合的宽带位移，不含空间域；它证明的是"给定融合信号后的心率估计精度"，不等于整链能力。
4. **不得使用历史数字**。早期以另一份实现快照测得的整链结果与本内核不可比，已作废，不得引用。
5. **emo 的许可状态**。该数据集本地副本无许可证文件，**不得写入对外材料、不得再分发**；如需对外引用，必须先补齐来源与授权。
6. **不是医疗器械**。以上为算法工程评估，不构成临床验证；不得用于诊断、监护或任何安全攸关场景，也不得表述为"达到医疗精度"。
7. **不可外推**。仅覆盖单人、坐/卧、静息或运动后；多人、远距、长期连续运行均未验证。

---

## 12. 参数总表（源码值）

### 12.1 采集与窗口

| 参数 | 值 | 位置 |
|---|---:|---|
| $f_s$（板端） | 90 Hz | `radar.h:88` |
| $f_s$（回放） | 100 Hz | `live.h:41` |
| 窗长 | $10.24f_s$ | `radar_live.cpp:507` |
| 步进 | $f_s$ | 同上 |
| 呼吸缓冲 | 50 s | `pipeline.c:118` |
| 心率历史 | 120 s | `pipeline.c:123` |
| 呼吸起始 | 30 s | `pipeline.c:392` |
| 心率预热 | 20 s | `pipeline.c:450` |
| 心率窗 | 30 s | `pipeline.c:449` |
| 杂波 $\alpha$ | 0.003 | `pipeline.c:161` |

### 12.2 空间域

| 参数 | 值 |
|---|---:|
| $w_E,w_A,w_P,w_C,w_T,w_S$ | 0.10, 0.05, 0.15, 0.20, 0.10, 0.00 |
| $w_J,w_M,w_D$ | 0.05, 0.05, 0.25 |
| 幅度坏点 | $<0.3\tilde z$ |
| 相位坏点 | $|\Delta\phi|>2.0$ rad |
| 二阶坏点 | $|\Delta^2\phi|>0.7$ rad |
| 坏点膨胀 | 2 点 |
| 相干剔除 | $<0.5$ |
| 融合 $\beta$ | 3.0 |
| 融合 $\sigma_r$ | 1.0 |
| 冻结阈值 | 1.0 / 0.7 rad |
| 冻结上限 | 30 点 |

### 12.3 估计层

| 参数 | 值 |
|---|---:|
| 呼吸搜索带 | 0.18–0.50 Hz |
| 呼吸输出范围 | 7–28 次/分 |
| 呼吸 SNR | ≥1.8 |
| 呼吸保持 | 8 帧 |
| Welch 段长 | min(n,2048) |
| 心率带 @90 Hz | 0.90–2.25 Hz |
| 心率搜索 | 40–180 BPM |
| HPS 阶数 | 6 |
| 强峰阈值 | 15% 最大 |
| 碰撞阈值 | 3 BPM |
| 保峰倍数 | 10× |
| 陷波半宽 | 0.06 Hz |
| 跳过低心率 | 40–60 BPM |
| ACF lag | $f_s/3$–$f_s/0.6667$ |
| ACF 边缘 | 3 点 / 0.55 |
| ACF 最小峰高 | 0.25 |
| PPI IBI 范围 | 333–1500 ms |
| 一致门限 | 8 BPM |
| 不一致规则 | $\pm10$ / $>8$ |
| 2:1 救援 | 6 / 80 / 8 |
| 平滑限幅 | 1.5 BPM |
| 重锁 | 15 BPM / 3 次 / 极差 10 |
| 置信度 | $1-\Delta_{max}/24$ |

### 12.4 事件与 HRV

| 参数 | 值 |
|---|---:|
| 检测窗 / 步进 | 16 s / 1 s |
| 镜像 / 裁剪 | 2 s / 1.5 s |
| 包络窗 | 1.5 s |
| 峰高 / 突出度 | 0.60 / 0.20 |
| 自适应间隔 | clip(0.6·IBI, 0.383, 0.9) |
| HRV 最少 IBI | 30 |
| HRV 最短时长 | 30 s |
| HRV 覆盖率 | ≥0.80 |
| 分段最少 IBI | 240 |

---

## 13. 失效模式

| 现象 | 代码原因 | 处置 |
|---|---|---|
| 心率稳定偏低 8–15 BPM | 呼吸谐波未被完全零化，式 (7.5) | 先修 $f_r$；检查 40–60 与碰撞跳过是否过度 |
| 心率稳定偏高约一倍 | PPI 漏拍，式 (7.26)；或救援误触发 | 看 `n_beats` 与 `hr_acf/hr_ppi` 差 |
| 呼吸在 10/18 次分间跳 | 搜索带下边界含基线漂移 | 0.18 Hz 已排除；检查 $f_c$ 是否过大 |
| 启动读数爬升 | 预热前跟踪器未播种 | 保持 20 s 预热，勿提前显示 |
| 位移台阶 0.8–1.9 mm | 解缠越过 $\lambda/4=1.25$ mm | 冻结相位只能掩盖，需解缠补偿 |
| 三路"一致但同错" | 共模谐波，$c_t$ 无法识别 | 引入独立证据（形态/多门一致性） |
| 呼吸值陈旧 | 保持 8 帧 | 输出标 `stale` |
| 多人场景混叠 | 单门锁定，无空域分离 | 明确不支持 |

---

## 14. 附录

### 14.1 符号表

| 符号 | 含义 | 首次出现 |
|---|---|---|
| $Q_t(k)$ | 门质量 | (4.16) |
| $D_{ratio}$ | 凹陷样本占比 | (4.14) |
| $c_t$ | 锁定/一致性置信度 | (4.21)/(7.39) |
| $m_t$ | 心率中位数目标 | (7.36) |
| $n_p$ | PPI 峰数 | (7.29) |
| $N_d$ | 同段差分总数 | (8.2) |

### 14.2 与初稿/历史文档的差异

| 项目 | 初稿 | 代码基线 | 影响 |
|---|---|---|---|
| 波长 | 4.955 mm | **5.0 mm** | 位移系数 0.3943→0.3979 mm/rad |
| 位移系数 | λ/4π | **5/(4π)** | 所有幅度门限按 5 mm |
| 心率带 | 1.0–2.5 Hz | **0.90–2.25 Hz@90** | 按 $f_s$ 设计 |
| HRV 最少 IBI | 20 | **30** | 与 30 s 时长同步 |
| 陷波候选 | 3 峰 + ACF | 3 峰 + ACF（同） | 一致 |
| 融合权重 | 相干性幂 | $\exp(3Q)\cdot\mathrm{coh}^2$ | 一致 |

### 14.3 关键公式索引

| 公式 | 函数 |
|---|---|
| (4.16) | `aura_score_bin` |
| (4.18) | `aura_viterbi` |
| (4.21) | `aura_distance_lock_update` |
| (4.31) | `aura_fuse_trajectory` |
| (6.2) | `aura_welch` |
| (7.4) | `aura_rhc_notch` |
| (7.6) | `aura_estimate_fft` |
| (7.15) | `aura_estimate_acf` |
| (7.17) | `aura_event_detect` |
| (7.31) | `aura_estimate_all` |
| (7.37) | `aura_rate_tracker_update` |
| (8.2) | `aura_hrv_compute` |

---

*文档结束。本文以 `aura/algorithm/csrc` 为准；参数变更须同步更新第 12 章并递增版本。*
