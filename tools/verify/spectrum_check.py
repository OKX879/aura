#!/usr/bin/env python3
"""检查呼吸频带、倍频程误判与呼吸谐波。"""
import argparse

import numpy as np
from scipy import signal


def band_spectrum(seg, lo, hi, fs, nperseg=2048):
    sos = signal.butter(4, [lo, hi], btype="bandpass", fs=fs, output="sos")
    y = signal.sosfiltfilt(sos, signal.detrend(seg))
    nper = min(len(y), nperseg)
    freqs, power = signal.welch(
        y, fs=fs, nperseg=nper, noverlap=nper // 2,
        window="hann", detrend=False, scaling="density",
    )
    band = (freqs >= lo) & (freqs <= hi)
    idx = np.where(band)[0]
    return freqs, power, idx


def top_peaks(freqs, power, idx, count=3):
    order = idx[np.argsort(power[idx])[::-1]][:count]
    return [(freqs[i] * 60.0, power[i]) for i in order]


def resp_sliding(x, lo, hi, fs, window=30.0, step=3.0):
    print(f"\n== 呼吸滑窗（带宽 {lo:.2f}-{hi:.2f} Hz，窗 {window:.0f}s）==")
    print("t(s)  前三个峰（次/分，功率）")
    picks = []
    for start in range(0, int(len(x) / fs) - int(window), int(step)):
        seg = x[int(start * fs):int((start + window) * fs)]
        freqs, power, idx = band_spectrum(seg, lo, hi, fs)
        peaks = top_peaks(freqs, power, idx)
        picks.append(peaks[0][0])
        line = "  ".join(f"{f:5.1f}({p:.1f})" for f, p in peaks)
        print(f"{start:4d}  {line}")

    arr = np.array(picks)
    if len(arr) > 1:
        jumps = np.abs(np.diff(arr))
        if (jumps > 4.0).any():
            print(f"\n[!] 主峰在 {arr.min():.1f}-{arr.max():.1f} 之间跳变，"
                  f"最大跳变 {jumps.max():.1f} 次/分，疑似倍频程误判")
        else:
            print(f"\n[ok] 主峰稳定在 {np.median(arr):.1f} 次/分")


def harmonic_check(x, fs):
    print("\n== 呼吸谐波对心率带的污染 ==")
    seg = x[int(10 * fs):]
    freqs, power, idx = band_spectrum(seg, 0.10, 0.60, fs, nperseg=4096)
    f_resp = freqs[idx[np.argmax(power[idx])]]
    print(f"呼吸基频 {f_resp * 60:.1f} 次/分")

    c_freqs, c_power, c_idx = band_spectrum(seg, 0.9, 3.0, fs)
    order = c_idx[np.argsort(c_power[c_idx])[::-1]][:6]
    print("心率带主要谱峰：")
    for i in order:
        fb = c_freqs[i]
        ratio = fb / f_resp
        dev = abs(ratio - round(ratio)) * f_resp * 60
        flag = "  <-- 贴近呼吸谐波" if dev < 2.0 and round(ratio) >= 2 else ""
        print(f"  {fb * 60:6.1f} 次/分 = {ratio:5.2f}×呼吸  "
              f"最接近整数阶偏差 {dev:4.1f}{flag}")

    t = np.arange(len(seg)) / fs
    cols = [np.ones_like(t)]
    for k in range(1, 9):
        cols.append(np.cos(2 * np.pi * f_resp * k * t))
        cols.append(np.sin(2 * np.pi * f_resp * k * t))
    design = np.column_stack(cols)
    coef, *_ = np.linalg.lstsq(design, signal.detrend(seg), rcond=None)
    residual = signal.detrend(seg) - design @ coef
    r_freqs, r_power = signal.welch(
        residual, fs=fs, nperseg=min(len(residual), 2048),
        noverlap=min(len(residual), 2048) // 2, window="hann",
    )
    mask = (r_freqs >= 0.9) & (r_freqs <= 2.5)
    i = np.where(mask)[0][np.argmax(r_power[mask])]
    print(f"扣除呼吸 1-8 阶谐波后的残差主峰: {r_freqs[i] * 60:.1f} 次/分")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--phase", required=True)
    parser.add_argument("--fs", type=float, default=90.0)
    parser.add_argument("--lo", type=float, default=0.14)
    parser.add_argument("--hi", type=float, default=0.50)
    args = parser.parse_args()

    x = np.loadtxt(args.phase, delimiter=",", skiprows=1)[:, 1]
    print(f"样本 {len(x)}  时长 {len(x) / args.fs:.0f}s")
    resp_sliding(x, args.lo, args.hi, args.fs)
    harmonic_check(x, args.fs)


if __name__ == "__main__":
    main()
