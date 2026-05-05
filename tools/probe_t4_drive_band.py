#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Localize WHICH band of the v6/v10 drive drives the T4 bias-loop divergence.

Builds on probe_t4_dia.py finding: variant T4 advk DC differs from public
by ~4.5 V on a 440 Hz sine, suggesting the variants' pre-T4 drive has a
different sub-audio envelope after `lp(t4_avg_f)` (~23.58 Hz cutoff).

This script renders one probe pass and analyzes the existing channels:
   ch4: public T4 drive (raw res4_v)
   ch6: v6 T4 drive
   ch7: v10 T4 drive

For each:
  1. Apply flt_ii1_lp(t4_avg_f) to the drive signal directly (linear
     envelope of the drive itself).  Report DC mean and RMS.
  2. Apply flt_ii1_lp(t4_avg_f) to drive - dvs analog: drive itself
     (since pre-tube has no PSS subtraction yet, this is the same).
  3. Look at the broadband DC of the drive (non-LP'd).  If the drive
     itself has different DC, that is the root.

Then split the spectral cause: report drive RMS in three bands using
high-pass / band-pass / low-pass on the same chain so we can see which
band of the variant drive contributes the rectified DC the averager
catches.  (Linear filters shouldn't create DC, but if the drive is
pre-T4 and its sub-audio content was reshaped by hp3/peq/hs phase
response of an asymmetric T3 output, the result IS different DC.)

No Faust rebuild needed -- this is pure post-processing on the existing
20-channel probe output.
"""

from __future__ import annotations

import argparse
import math
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
    gen_log_sweep,
    gen_sine,
    read_wav_f32_multi,
    run_probe,
    trim_warmup,
)
from abx_compare import write_wav_f32  # noqa: E402
from keller_oracle import flt_ii1_lp_block, flt_ii1_hp_block  # noqa: E402

# t4_avg_f = 1/(2*pi*0.00675) Hz ~= 23.5786
T4_AVG_F = 1.0 / (2.0 * math.pi * 0.00675)
T4_KFB = 0.18144

# Channel indices.
CH_DRV_PUBLIC = 4
CH_DRV_V6 = 6
CH_DRV_V10 = 7
CH_T4_PUBLIC = 8
CH_T4_V6 = 10
CH_T4_V10 = 12

VARIANTS = (
    ("public", CH_DRV_PUBLIC, CH_T4_PUBLIC),
    ("v6",     CH_DRV_V6,     CH_T4_V6),
    ("v10",    CH_DRV_V10,    CH_T4_V10),
    ("v13",    20,            21),
    ("v15",    22,            23),
)


def lp(x: np.ndarray, fc: float, sr: int) -> np.ndarray:
    return flt_ii1_lp_block(fc, sr, x)


def hp(x: np.ndarray, fc: float, sr: int) -> np.ndarray:
    return flt_ii1_hp_block(fc, sr, x)


def analyse(samples: np.ndarray, sr: int, label: str) -> None:
    print(f"\n=== {label} ===")
    # Drive itself: DC, RMS, then DC of LP(t4_avg_f) (== averager input
    # if there were no PSS subtraction; also equals the long-term mean).
    print(f"  {'variant':<8} "
          f"{'drv.DC':>10} {'drv.RMS':>10} "
          f"{'lp(drv).DC':>12} {'lp(drv).RMS':>14} "
          f"{'lp(drv-pubLF).DC':>18}")
    pub_drv_lp = None
    for name, ch_drv, _ in VARIANTS:
        drv = samples[ch_drv]
        drv_lp = lp(drv, T4_AVG_F, sr)
        drv_dc = float(np.mean(drv))
        drv_rms = float(np.sqrt(np.mean(drv * drv)))
        drv_lp_dc = float(np.mean(drv_lp))
        drv_lp_rms = float(np.sqrt(np.mean(drv_lp * drv_lp)))
        if pub_drv_lp is None:
            pub_drv_lp = drv_lp
            delta = 0.0
        else:
            delta = float(np.mean(drv_lp - pub_drv_lp))
        print(f"  {name:<8} "
              f"{drv_dc:+10.4f} {drv_rms:10.4e} "
              f"{drv_lp_dc:+12.4f} {drv_lp_rms:14.4e} "
              f"{delta:+18.4f}")

    # Spectral split: hp(50) gives audio-band, lp(50) gives sub-audio.
    # The sub-audio band IS what the averager sees, in steady state.
    print(f"\n  band split (RMS) using lp/hp at 50 Hz boundary:")
    print(f"  {'variant':<8} {'sub50.RMS':>12} {'sub50.DC':>12} "
          f"{'sub50-pub.DC':>14} {'aud.RMS':>12}")
    pub_sub_dc = None
    for name, ch_drv, _ in VARIANTS:
        drv = samples[ch_drv]
        sub = lp(drv, 50.0, sr)
        aud = drv - sub  # complement (same number of poles, approximate)
        sub_rms = float(np.sqrt(np.mean(sub * sub)))
        sub_dc = float(np.mean(sub))
        aud_rms = float(np.sqrt(np.mean(aud * aud)))
        if pub_sub_dc is None:
            pub_sub_dc = sub_dc
            delta = 0.0
        else:
            delta = sub_dc - pub_sub_dc
        print(f"  {name:<8} {sub_rms:12.4e} {sub_dc:+12.4f} "
              f"{delta:+14.4f} {aud_rms:12.4e}")

    # Compare T4 output - dvs LP (this is what advk actually integrates,
    # but we can't see dvs here -- approximate by using the drive.  Better
    # to look at the post-tube voltages.)
    print(f"\n  post-tube T4 DC and lp(t4_avg_f) DC of (t4_v - mean):")
    print(f"  {'variant':<8} {'t4.DC':>10} {'t4.RMS':>12} "
          f"{'lp(t4_v).DC':>14}")
    for name, _, ch_v in VARIANTS:
        v = samples[ch_v]
        v_lp = lp(v, T4_AVG_F, sr)
        v_dc = float(np.mean(v))
        v_rms = float(np.sqrt(np.mean(v * v)))
        v_lp_dc = float(np.mean(v_lp))
        print(f"  {name:<8} {v_dc:+10.4f} {v_rms:12.4e} "
              f"{v_lp_dc:+14.4f}")


def render_probe(label: str, signal: np.ndarray, sr: int, out_dir: Path,
                  gain_db: float, probe_bin: Path) -> np.ndarray:
    out_dir.mkdir(parents=True, exist_ok=True)
    in_wav = out_dir / f"{label}_input.wav"
    out_wav = out_dir / f"{label}.wav"
    write_wav_f32(in_wav, signal.tolist(), sr)
    print(f"[{label}] rendering at gain={gain_db:+.4f} dB...", flush=True)
    run_probe(in_wav, out_wav, gain_db, probe_bin)
    samples_full, sr_out = read_wav_f32_multi(out_wav)
    if sr_out != sr:
        raise RuntimeError(f"sample-rate mismatch: in={sr} out={sr_out}")
    if samples_full.shape[0] != NUM_CHANNELS:
        raise RuntimeError(
            f"expected {NUM_CHANNELS} channels in {out_wav}, "
            f"got {samples_full.shape[0]}"
        )
    return np.stack([trim_warmup(samples_full[c], sr)
                     for c in range(samples_full.shape[0])])


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--gain", type=float, default=6.0)
    p.add_argument("--sr", type=int, default=SAMPLE_RATE)
    p.add_argument("--sine-f0", type=float, default=440.0)
    p.add_argument("--sine-dur", type=float, default=2.0)
    p.add_argument("--sine-amp", type=float, default=0.5)
    p.add_argument("--sweep-dur", type=float, default=5.0)
    p.add_argument("--sweep-amp", type=float, default=0.5)
    p.add_argument("--out-dir", type=Path,
                    default=Path("target") / "probe_t4_drive_band")
    p.add_argument("--probe-bin", type=Path, default=PROBE_BIN)
    args = p.parse_args(argv)

    if not args.probe_bin.exists():
        print(f"error: probe binary not found: {args.probe_bin}",
              file=sys.stderr)
        return 1

    sine = gen_sine(args.sr, args.sine_f0, args.sine_dur, args.sine_amp)
    s_sine = render_probe("drv_band_sine", sine, args.sr, args.out_dir,
                           args.gain, args.probe_bin)
    analyse(s_sine, args.sr,
             f"sine {args.sine_f0:.0f} Hz, gain={args.gain:+.2f} dB")

    sweep = gen_log_sweep(args.sr, 20.0, args.sr / 2.0 * 0.95,
                           args.sweep_dur, args.sweep_amp)
    s_sweep = render_probe("drv_band_sweep", sweep, args.sr, args.out_dir,
                            args.gain, args.probe_bin)
    analyse(s_sweep, args.sr,
             f"log sweep 20-{args.sr/2.0*0.95:.0f} Hz, "
             f"gain={args.gain:+.2f} dB")

    return 0


if __name__ == "__main__":
    sys.exit(main())
