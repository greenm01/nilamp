#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Probe T4 internal state divergence between public and v6/v10 paths.

Background
----------
probe_t4_level.py established the post-tube T4 divergence between public
and v6/v10 is STRUCTURAL: matching H1 levels does NOT match H2 (public
H2~14.3, level-matched B.public H2~14.1; v6 H2~5.3, v10 H2~7.7) and the
+5 dB 8 kHz hump on the sweep survives level-matching.

So the variant T4 instances run at a different operating point than
public despite drawing identical PSS sag.  The two state signals that
set the T4 operating point inside `tube_ck_simple` are:

  1. dia (cathode current).  Drives PSS in the public path; here every
     variant's dia is exposed but only public's feeds PSS.
  2. advk = lp(t4_avg_f, v_out - dvs) * t4_kfb.  This is the averager
     feedback that biases the grid voltage on the next sample.  It is
     a one-pole LP (fc = t4_avg_f, sub-audio) of `(v_out - dvs)`,
     scaled by t4_kfb, fed into next sample's grid.

Both are exposed as channels 14-19 by nilamp_drive_taps.dsp.  The dia
proxy is the actual signal; the advk proxy is reconstructed in Faust
using the same `flt_ii1_lp(t4_avg_f) * kfb` (a fresh LP per variant).
This proxy reproduces what the variant T4's *own* internal averager
would converge to given identical params and identical post-tube
voltage sequences, which is sufficient to detect a steady-state bias
shift caused by altered drive spectrum.

Diagnostic axes
---------------
For each variant (public, v6, v10) we report:

  dia (cathode current):
    - DC mean (steady tube current; sets sag operating point).
    - RMS, peak.
    - H1, H2, H3 at 440 Hz (sine).
    - per-octave-bin RMS dB on sweep (where in frequency it differs).

  advk (averager-feedback proxy):
    - DC mean (steady grid bias contribution; THIS IS THE KEY).
    - RMS, peak.
    - For sweep: per-octave-bin RMS dB.

Decision rule
-------------
- advk DC differs >0.05 V across variants on sine
    -> bias-feedback divergence.  Variant T4s settle to a different
       grid bias under the altered post-T3 / pre-T4 chain.  Mitigation
       options: tune t4_kfb, t4_avg_f, or pre-shape the v6/v10 drive
       to match public's spectral envelope through the kfb LP.
- advk DC matches but dia DC differs significantly
    -> peak-detector / gain-shaping divergence.  Less common; would
       implicate pkd_process / kspre / kspost / kpk.
- both DC values match across variants
    -> the divergence is purely instantaneous (pre-T4 chain harmonic
       phase coloring T4's nonlinearity).  No state fix; would require
       chain redesign.
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
    OCTAVE_BIN_HZ,
    gen_log_sweep,
    gen_sine,
    goertzel_amp,
    read_wav_f32_multi,
    run_probe,
    stft_power_ratio_db,
    trim_warmup,
)
from abx_compare import write_wav_f32  # noqa: E402

# Channel indices from nilamp_drive_taps.dsp.
CH_T4_PUBLIC = 8
CH_T4_V6 = 10
CH_T4_V10 = 12
CH_DIA_PUBLIC = 14
CH_DIA_V6 = 15
CH_DIA_V10 = 16
CH_ADVK_PUBLIC = 17
CH_ADVK_V6 = 18
CH_ADVK_V10 = 19

VARIANTS = (
    ("public", CH_T4_PUBLIC, CH_DIA_PUBLIC, CH_ADVK_PUBLIC),
    ("v6",     CH_T4_V6,     CH_DIA_V6,     CH_ADVK_V6),
    ("v10",    CH_T4_V10,    CH_DIA_V10,    CH_ADVK_V10),
)


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
    samples = np.stack([trim_warmup(samples_full[c], sr)
                        for c in range(samples_full.shape[0])])
    return samples


def stats(x: np.ndarray) -> dict:
    return {
        "dc": float(np.mean(x)),
        "rms": float(np.sqrt(np.mean(x * x))),
        "peak": float(np.max(np.abs(x))),
    }


def fmt_v(x: float) -> str:
    return f"{x:+.4f}" if math.isfinite(x) else "  nan "


def analyse_sine(out_dir: Path, sr: int, f0: float, dur: float, amp: float,
                  gain_db: float, probe_bin: Path) -> dict:
    sine = gen_sine(sr, f0, dur, amp)
    samples = render_probe("dia_probe_sine", sine, sr, out_dir, gain_db,
                            probe_bin)

    print(f"\n[sine {f0:.0f} Hz] T4 dia + advk by variant:")
    print(f"  {'variant':<8} "
          f"{'dia.DC':>10} {'dia.RMS':>10} {'dia.Pk':>10} "
          f"{'dia.H1':>10} {'dia.H2':>10} {'dia.H3':>10} | "
          f"{'advk.DC':>10} {'advk.RMS':>10} {'advk.Pk':>10}")

    results = {}
    for name, ch_v, ch_dia, ch_advk in VARIANTS:
        v = samples[ch_v]
        dia = samples[ch_dia]
        advk = samples[ch_advk]
        ds = stats(dia)
        as_ = stats(advk)
        dia_h1 = goertzel_amp(dia, sr, f0)
        dia_h2 = goertzel_amp(dia, sr, 2 * f0)
        dia_h3 = goertzel_amp(dia, sr, 3 * f0)
        v_dc = float(np.mean(v))
        results[name] = {
            "v_dc": v_dc,
            "dia": ds | {"h1": dia_h1, "h2": dia_h2, "h3": dia_h3},
            "advk": as_,
        }
        print(f"  {name:<8} "
              f"{fmt_v(ds['dc']):>10} {ds['rms']:10.4e} {ds['peak']:10.4e} "
              f"{dia_h1:10.4e} {dia_h2:10.4e} {dia_h3:10.4e} | "
              f"{fmt_v(as_['dc']):>10} {as_['rms']:10.4e} {as_['peak']:10.4e}")

    # Cross-variant deltas.
    pub = results["public"]
    print(f"\n[sine] deltas vs public:")
    print(f"  {'variant':<8} "
          f"{'dDC.dia':>10} {'dDC.advk':>10} {'dDC.v_out':>10} "
          f"{'dRMS.dia%':>10} {'dRMS.advk%':>10}")
    for name in ("v6", "v10"):
        r = results[name]
        d_dia_dc = r["dia"]["dc"] - pub["dia"]["dc"]
        d_advk_dc = r["advk"]["dc"] - pub["advk"]["dc"]
        d_v_dc = r["v_dc"] - pub["v_dc"]
        # RMS percent deltas.
        def pct(a, b):
            return 100.0 * (a - b) / b if b > 0 else float("nan")
        d_dia_rms = pct(r["dia"]["rms"], pub["dia"]["rms"])
        d_advk_rms = pct(r["advk"]["rms"], pub["advk"]["rms"])
        print(f"  {name:<8} "
              f"{d_dia_dc:+10.4f} {d_advk_dc:+10.4f} {d_v_dc:+10.4f} "
              f"{d_dia_rms:+10.2f} {d_advk_rms:+10.2f}")

    return results


def analyse_sweep(out_dir: Path, sr: int, dur: float, amp: float,
                   gain_db: float, probe_bin: Path) -> None:
    sweep = gen_log_sweep(sr, 20.0, sr / 2.0 * 0.95, dur, amp)
    samples = render_probe("dia_probe_sweep", sweep, sr, out_dir, gain_db,
                            probe_bin)

    print(f"\n[sweep] dia per-octave-bin RMS dB (variant - public):")
    print(f"  {'bin Hz':>10} | {'v6':>10} {'v10':>10}")
    pub_dia = samples[CH_DIA_PUBLIC]
    v6_dia = samples[CH_DIA_V6]
    v10_dia = samples[CH_DIA_V10]
    rats_v6 = stft_power_ratio_db(v6_dia, pub_dia, sr,
                                   list(OCTAVE_BIN_HZ))
    rats_v10 = stft_power_ratio_db(v10_dia, pub_dia, sr,
                                    list(OCTAVE_BIN_HZ))
    for f, r6, r10 in zip(OCTAVE_BIN_HZ, rats_v6, rats_v10):
        print(f"  {f:>10.1f} | {r6:+10.2f} {r10:+10.2f}")

    print(f"\n[sweep] advk per-octave-bin RMS dB (variant - public):")
    print(f"  {'bin Hz':>10} | {'v6':>10} {'v10':>10}")
    pub_advk = samples[CH_ADVK_PUBLIC]
    v6_advk = samples[CH_ADVK_V6]
    v10_advk = samples[CH_ADVK_V10]
    rats_v6 = stft_power_ratio_db(v6_advk, pub_advk, sr,
                                   list(OCTAVE_BIN_HZ))
    rats_v10 = stft_power_ratio_db(v10_advk, pub_advk, sr,
                                    list(OCTAVE_BIN_HZ))
    for f, r6, r10 in zip(OCTAVE_BIN_HZ, rats_v6, rats_v10):
        print(f"  {f:>10.1f} | {r6:+10.2f} {r10:+10.2f}")

    # advk DC trajectory across the sweep -- does the bias track input
    # frequency differently per variant?  Report mean advk over 4
    # contiguous quarters of the sweep.
    print(f"\n[sweep] advk DC by sweep quarter (V):")
    n = pub_advk.shape[0]
    print(f"  {'quarter':<8} {'public':>10} {'v6':>10} {'v10':>10}")
    for i in range(4):
        seg_p = pub_advk[i * n // 4:(i + 1) * n // 4]
        seg_6 = v6_advk[i * n // 4:(i + 1) * n // 4]
        seg_10 = v10_advk[i * n // 4:(i + 1) * n // 4]
        print(f"  Q{i}       "
              f"{float(np.mean(seg_p)):+10.4f} "
              f"{float(np.mean(seg_6)):+10.4f} "
              f"{float(np.mean(seg_10)):+10.4f}")


def decide(sine_results: dict) -> str:
    pub = sine_results["public"]
    advk_diffs = []
    dia_dc_diffs = []
    for name in ("v6", "v10"):
        r = sine_results[name]
        advk_diffs.append(abs(r["advk"]["dc"] - pub["advk"]["dc"]))
        dia_dc_diffs.append(abs(r["dia"]["dc"] - pub["dia"]["dc"]))
    max_advk = max(advk_diffs)
    max_dia_dc = max(dia_dc_diffs)
    print(f"\n[decision] max |advk DC delta| = {max_advk:.4f} V; "
          f"max |dia DC delta| = {max_dia_dc:.4f} V")
    if max_advk > 0.05:
        return ("BIAS-FEEDBACK divergence: averager-feedback DC differs "
                f">{max_advk:.3f} V across variants. T4 grid bias settles "
                "to different operating points under the altered "
                "post-T3 / pre-T4 spectrum. Mitigation: tune t4_kfb / "
                "t4_avg_f, or spectrally pre-shape v6/v10 drive to match "
                "public's envelope through the kfb LP.")
    if max_dia_dc > 0.001:
        return ("PEAK-DETECTOR / GAIN-SHAPING divergence: dia DC differs "
                f">{max_dia_dc:.5f} V but advk matches. Implicates "
                "pkd_process / kspre / kspost / kpk path.")
    return ("INSTANTANEOUS divergence: T4 internal state matches across "
            "variants (both advk and dia DC nearly identical). The "
            "post-tube voltage divergence is driven purely by the "
            "instantaneous nonlinearity acting on differently-shaped "
            "pre-T4 waveforms (harmonic phase / envelope coloring). "
            "No state-tuning fix; would require chain redesign.")


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--gain", type=float, default=6.0,
                    help="Input Gain in dB for both probe runs")
    p.add_argument("--sr", type=int, default=SAMPLE_RATE)
    p.add_argument("--sine-f0", type=float, default=440.0)
    p.add_argument("--sine-dur", type=float, default=2.0)
    p.add_argument("--sine-amp", type=float, default=0.5)
    p.add_argument("--sweep-dur", type=float, default=5.0)
    p.add_argument("--sweep-amp", type=float, default=0.5)
    p.add_argument("--out-dir", type=Path,
                    default=Path("target") / "probe_t4_dia")
    p.add_argument("--probe-bin", type=Path, default=PROBE_BIN)
    args = p.parse_args(argv)

    if not args.probe_bin.exists():
        print(f"error: probe binary not found: {args.probe_bin}",
              file=sys.stderr)
        return 1

    sine_res = analyse_sine(args.out_dir, args.sr, args.sine_f0,
                              args.sine_dur, args.sine_amp,
                              args.gain, args.probe_bin)
    analyse_sweep(args.out_dir, args.sr, args.sweep_dur, args.sweep_amp,
                   args.gain, args.probe_bin)
    verdict = decide(sine_res)
    print("\n=== verdict ===")
    print(verdict)
    return 0


if __name__ == "__main__":
    sys.exit(main())
