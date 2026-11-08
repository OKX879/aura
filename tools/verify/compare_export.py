#!/usr/bin/env python3
"""用 scipy 独立复算呼吸/心率并与板端输出对比。"""
import argparse

import numpy as np
from scipy import signal


def hr_reference(seg, fs):
    """1.0-2.5 Hz 带通后取 FFT 峰值。"""
    sos = signal.butter(4, [1.0, 2.5], btype="bandpass", fs=fs, output="sos")
    y = signal.sosfiltfilt(sos, signal.detrend(seg))
    n = len(y)
    win = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(n) / n)
    power = np.abs(np.fft.rfft(y * win)) ** 2
    freqs = fs * np.arange(len(power)) / n
    band = (freqs >= 1.0) & (freqs <= 2.5)
    idx = np.where(band)[0]
    return freqs[idx[np.argmax(power[idx])]] * 60.0


def resp_reference(seg, fs):
    """0.18-0.50 Hz 带通后取 Welch 峰值。"""
    sos = signal.butter(4, [0.18, 0.50], btype="bandpass", fs=fs, output="sos")
    y = signal.sosfiltfilt(sos, signal.detrend(seg))
    nper = min(len(y), 2048)
    freqs, power = signal.welch(
        y, fs=fs, nperseg=nper, noverlap=nper // 2,
        window="hann", detrend=False, scaling="density",
    )
    band = (freqs >= 0.18) & (freqs <= 0.50)
    idx = np.where(band)[0]
    best = idx[np.argmax(power[idx])]
    snr = power[best] / (np.median(power[idx]) + 1e-12)
    return freqs[best] * 60.0, snr


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--phase", required=True, help="AURA_PHASE_DUMP 导出文件")
    parser.add_argument("--snap", required=True, help="AURA_SNAP_DUMP 导出文件")
    parser.add_argument("--fs", type=float, default=90.0)
    parser.add_argument("--tail", type=float, default=30.0, help="参考值取末尾多少秒")
    args = parser.parse_args()

    phase = np.loadtxt(args.phase, delimiter=",", skiprows=1)
    x = phase[:, 1]
    snap = np.genfromtxt(args.snap, delimiter=",", names=True)
    print(f"样本 {len(x)}  时长 {len(x) / args.fs:.0f}s  帧数 {len(snap)}")

    seg = x[-int(args.tail * args.fs):]
    hr_ref = hr_reference(seg, args.fs)
    resp_ref, resp_snr = resp_reference(seg, args.fs)
    print(f"参考值（后 {args.tail:.0f} 秒）: 心率 {hr_ref:.1f} BPM   "
          f"呼吸 {resp_ref:.1f} 次/分 (SNR {resp_snr:.1f})")

    hr = snap["hr"][snap["hr"] > 0]
    resp = snap["resp_hz"][np.isfinite(snap["resp_hz"])] * 60.0
    if len(hr):
        print(f"板端心率: 有效 {len(hr)}  中位 {np.median(hr):.1f}  "
              f"范围 {hr.min():.1f}-{hr.max():.1f}  标准差 {np.std(hr):.1f}")
        print(f"心率偏差: {np.median(hr) - hr_ref:+.1f} BPM")
    if len(resp):
        print(f"板端呼吸: 有效 {len(resp)}  中位 {np.median(resp):.1f}  "
              f"范围 {resp.min():.1f}-{resp.max():.1f}  标准差 {np.std(resp):.1f}")
        print(f"呼吸偏差: {np.median(resp) - resp_ref:+.1f} 次/分")


if __name__ == "__main__":
    main()
