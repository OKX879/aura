#!/usr/bin/env python3
"""按板端 C 实现复现心率 FFT 路。"""
import argparse

import numpy as np
from scipy import signal

# AURA_SOS_HR 系数，每行 b0 b1 b2 a0 a1 a2
SOS_HR = np.array([
    [4.3726887978200706e-06, 8.7453775956401412e-06, 4.3726887978200706e-06,
     1.0, -1.886956052183046, 0.9009372762241582],
    [1.0, 2.0, 1.0, 1.0, -1.9259899215349412, 0.93238448321621692],
    [1.0, -2.0, 1.0, 1.0, -1.9278937945012951, 0.95064342256207324],
    [1.0, -2.0, 1.0, 1.0, -1.9746628424034367, 0.97878594030485733],
])

F_LO_BPM, F_HI_BPM = 40.0, 180.0
F_LO, F_HI = F_LO_BPM / 60.0, F_HI_BPM / 60.0
MAX_HARMONIC = 6
NOTCH_HALF_WIDTH = 0.06
ACF_EDGE_MARGIN = 18
ACF_EDGE_MIN_FACTOR = 0.35
ACF_MIN_HEIGHT = 0.05


def welch_psd(x, fs, nperseg=2048):
    """Hann 窗、50% 重叠的 Welch 功率谱。"""
    nper = min(len(x), nperseg)
    step = nper // 2
    nseg = 1 + (len(x) - nper) // step
    win = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(nper) / nper)
    scale = 1.0 / (fs * np.sum(win ** 2))
    nf = nper // 2 + 1
    acc = np.zeros(nf)
    for s in range(nseg):
        seg = x[s * step:s * step + nper]
        seg = (seg - seg.mean()) * win
        spec = np.fft.rfft(seg, nper)
        acc += (spec.real ** 2 + spec.imag ** 2) * scale
    acc /= nseg
    acc[1:nf - 1] *= 2.0
    return fs * np.arange(nf) / nper, acc


def acf_peak(x, fs):
    y = x - x.mean()
    if np.dot(y, y) <= 1e-18:
        return None
    ac = np.correlate(y, y, "full")[len(y) - 1:]
    lo = max(1, int(fs / F_HI))
    hi = min(len(ac) - 1, int(fs / F_LO))
    if lo >= hi:
        return None
    lag = lo + int(np.argmax(ac[lo:hi]))
    freq = fs / lag
    return freq if F_LO <= freq <= F_HI else None


def notch(sig, f_resp, fs):
    """去掉呼吸各阶谐波，候选峰附近跳过。"""
    if not np.isfinite(f_resp) or f_resp <= 0:
        return sig.copy()
    n = len(sig)
    spec = np.fft.rfft(sig)
    out = spec.copy()
    freqs = fs * np.arange(len(spec)) / n
    mag = np.abs(spec) ** 2

    band_idx = np.where((freqs >= F_LO) & (freqs <= F_HI))[0]
    cand = []
    for i in band_idx[np.argsort(mag[band_idx])[::-1]]:
        fc = freqs[i]
        if all(abs(fc - c) * 60 >= 8.0 for c in cand):
            cand.append(fc)
        if len(cand) >= 3:
            break
    fa = acf_peak(sig, fs)
    if fa is not None and all(abs(fa - c) * 60 >= 3.0 for c in cand):
        cand.append(fa)

    kmin = int(np.ceil(F_LO / f_resp))
    kmax = min(int(np.floor(F_HI / f_resp)), 8)
    for k in range(kmin, kmax + 1):
        fh = k * f_resp
        if 40.0 <= fh * 60.0 <= 60.0:
            continue
        if any(abs(fh - c) * 60.0 < 6.0 for c in cand):
            continue
        out[np.abs(freqs - fh) <= NOTCH_HALF_WIDTH] = 0.0
    return np.fft.irfft(out, n)


def psd_at(freqs, power, fmax, query):
    if len(power) == 0:
        return 1e-12
    if query <= 0:
        return max(power[0], 1e-12)
    if query >= fmax:
        return max(power[-1], 1e-12)
    pos = query / fmax * (len(power) - 1)
    i0 = int(pos)
    w = pos - i0
    i1 = min(i0 + 1, len(power) - 1)
    p0 = max(power[i0], 1e-12)
    p1 = max(power[i1], 1e-12)
    return p0 + w * (p1 - p0)


def board_fft(sig, f_resp, fs):
    """复现 aura_estimate_fft 的选峰逻辑。"""
    x = sig - sig.mean()
    freqs, power = welch_psd(x, fs)
    nf = len(freqs)
    band_idx = np.where((freqs >= F_LO) & (freqs <= F_HI))[0]
    if len(band_idx) == 0:
        return np.nan, np.nan

    band = power[band_idx]
    hps = band.copy()
    for h in range(2, MAX_HARMONIC + 1):
        for i, fh in enumerate(freqs[band_idx] * h):
            if fh <= 0.5 * fs:
                hps[i] *= psd_at(freqs, power, 0.5 * fs, fh)

    strong = np.where(band >= 0.15 * band.max())[0]
    if len(strong) == 0:
        return np.nan, np.nan

    if np.isfinite(f_resp) and f_resp > 0 and len(strong) > 1:
        fk = freqs[band_idx[strong]]
        collide = np.array([
            any(abs(fv - k * f_resp) < 3.0 / 60.0 for k in range(2, 7))
            for fv in fk
        ])
        if collide.any() and not collide.all():
            best_other = hps[strong[~collide]].max()
            keep = np.array([(not c) or hps[s] > 10.0 * best_other
                             for s, c in zip(strong, collide)])
            strong = strong[keep]

    best = strong[np.argmax(band[strong])]
    g = band_idx[best]
    peak = freqs[g]
    if 0 < g < nf - 1:
        bg = np.median(band)
        y0, y1, y2 = (np.log(max(power[g + d] - bg, 1e-15)) for d in (-1, 0, 1))
        den = y0 - 2 * y1 + y2
        if abs(den) > 1e-12:
            delta = 0.5 * (y0 - y2) / den
            if abs(delta) <= 0.5:
                peak = freqs[g] + delta * (freqs[g + 1] - freqs[g])
    snr = band[best] / (np.median(band) + 1e-12)
    return peak * 60.0, snr


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--phase", required=True)
    parser.add_argument("--snap", required=True)
    parser.add_argument("--fs", type=float, default=90.0)
    parser.add_argument("--window", type=float, default=30.0, help="分析窗秒数")
    parser.add_argument("--warmup", type=float, default=20.0, help="从第几秒开始输出")
    args = parser.parse_args()

    phase = np.loadtxt(args.phase, delimiter=",", skiprows=1)
    mm = phase[:, 1]
    snap = np.genfromtxt(args.snap, delimiter=",", names=True)

    hr_signal = signal.sosfilt(SOS_HR, mm - mm.mean())

    print("t(s)  窗口  f_resp  复现FFT  板端输出")
    values = []
    for i in range(int(args.warmup * args.fs), len(mm), int(args.fs)):
        j = min(int(i / args.fs), len(snap) - 1)
        f_resp = snap["resp_hz"][j]
        f_resp = f_resp if np.isfinite(f_resp) else np.nan
        span = min(int(args.window * args.fs), i + 1)
        seg = hr_signal[i + 1 - span:i + 1]
        hr, snr = board_fft(notch(seg, f_resp, args.fs), f_resp, args.fs)
        if np.isfinite(hr):
            values.append(hr)
        print(f"{i / args.fs:4.0f}  {span / args.fs:4.0f}s  "
              f"{f_resp * 60:5.1f}  {hr:7.1f}  {snap['hr'][j]:7.1f}")

    if values:
        # 只复现 FFT 单路，不含融合与平滑
        print(f"\n复现 FFT 单路中位 {np.median(values):.1f} BPM  "
              f"范围 {min(values):.1f}-{max(values):.1f}")


if __name__ == "__main__":
    main()
