#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Quick post-tube comparison: v13 / v15 vs public on the existing 24-channel
drive probe.  Reports scalar-fit residual after lag-align and per-octave-bin
level ratio dB.  Reuses helpers from compare_drive_taps.
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools"))

from compare_drive_taps import (  # noqa: E402
    PROBE_BIN,
    SAMPLE_RATE,
    NUM_CHANNELS,
    OCTAVE_BIN_HZ,
    gen_log_sweep,
    gen_sine,
    goertzel_amp,
    read_wav_f32_multi,
    run_probe,
    scalar_fit_residual,
    stft_power_ratio_db,
    trim_warmup,
)
from abx_compare import write_wav_f32  # noqa: E402

CH_T4_PUBLIC = 8
CH_T4_V6 = 10
CH_T4_V10 = 12
CH_T4_V13 = 21
CH_T4_V15 = 23

VARIANTS = (
    ("v6",  CH_T4_V6),
    ("v10", CH_T4_V10),
    ("v13", CH_T4_V13),
    ("v15", CH_T4_V15),
)


def render(label, signal, sr, out_dir, gain_db, probe_bin):
    out_dir.mkdir(parents=True, exist_ok=True)
    in_wav = out_dir / f"{label}_in.wav"
    out_wav = out_dir / f"{label}.wav"
    write_wav_f32(in_wav, signal.tolist(), sr)
    print(f"[{label}] gain={gain_db:+.2f} dB", flush=True)
    run_probe(in_wav, out_wav, gain_db, probe_bin)
    s, sr_o = read_wav_f32_multi(out_wav)
    if sr_o != sr:
        raise RuntimeError("sr mismatch")
    if s.shape[0] != NUM_CHANNELS:
        raise RuntimeError(f"expected {NUM_CHANNELS} ch, got {s.shape[0]}")
    return np.stack([trim_warmup(s[c], sr) for c in range(s.shape[0])])


def fit_dB(pub, v, sr):
    s_fit, resid_rms, _lag = scalar_fit_residual(pub, v, sr)
    pub_rms = float(np.sqrt(np.mean(pub.astype(np.float64) ** 2)))
    if pub_rms <= 0 or resid_rms <= 0:
        return s_fit, float("-inf")
    import math
    return s_fit, 20.0 * math.log10(resid_rms / pub_rms)


def main():
    sr = SAMPLE_RATE
    gain = 6.0
    out_dir = Path("target") / "compare_v13_v15"

    print("\n=== sine 440 Hz, 4.0 s ===")
    sine = gen_sine(sr, 440.0, 4.0, 0.5)
    s = render("sine", sine, sr, out_dir, gain, PROBE_BIN)
    pub = s[CH_T4_PUBLIC]
    print(f"\n  public mean={float(np.mean(pub)):+.4f} V  rms="
          f"{float(np.sqrt(np.mean(pub*pub))):.4f}")
    pub_h1 = goertzel_amp(pub, sr, 440.0)
    pub_h2 = goertzel_amp(pub, sr, 880.0)
    pub_h3 = goertzel_amp(pub, sr, 1320.0)
    print(f"  public H1={pub_h1:.4f} H2={pub_h2:.4f} H3={pub_h3:.4f} "
          f"H2/H1={pub_h2/pub_h1:.5f}")
    print(f"\n  {'variant':<6} {'mean':>8} {'rms':>8} "
          f"{'H1':>8} {'H2':>8} {'H3':>8} {'H2/H1':>8} "
          f"{'s_fit':>8} {'resid_dB':>10}")
    for name, ch in VARIANTS:
        v = s[ch]
        m = float(np.mean(v))
        r = float(np.sqrt(np.mean(v * v)))
        h1 = goertzel_amp(v, sr, 440.0)
        h2 = goertzel_amp(v, sr, 880.0)
        h3 = goertzel_amp(v, sr, 1320.0)
        s_fit, resid_db = fit_dB(pub, v, sr)
        print(f"  {name:<6} {m:+8.3f} {r:8.3f} "
              f"{h1:8.4f} {h2:8.4f} {h3:8.4f} {h2/h1:8.5f} "
              f"{s_fit:8.5f} {resid_db:+10.2f}")

    print("\n=== sweep 8.0 s ===")
    sweep = gen_log_sweep(sr, 20.0, sr / 2 * 0.95, 8.0, 0.5)
    s = render("sweep", sweep, sr, out_dir, gain, PROBE_BIN)
    pub = s[CH_T4_PUBLIC]
    print(f"\n  {'variant':<6} {'s_fit':>8} {'resid_dB':>10}  "
          f"per-octave dB (variant - public):")
    print(f"  {'':<6} {'':>8} {'':>10}  "
          + "  ".join(f"{f:>5.0f}" for f in OCTAVE_BIN_HZ))
    for name, ch in VARIANTS:
        v = s[ch]
        s_fit, resid_db = fit_dB(pub, v, sr)
        rats = stft_power_ratio_db(v, pub, sr, list(OCTAVE_BIN_HZ))
        rats_str = "  ".join(f"{r:+5.1f}" for r in rats)
        print(f"  {name:<6} {s_fit:8.5f} {resid_db:+10.2f}  {rats_str}")


if __name__ == "__main__":
    main()
