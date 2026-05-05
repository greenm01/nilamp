#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Probe whether the post-tube T4 divergence between public and v6/v10 is
explained by the +1.5 dB drive-level increase at the variant T4 inputs.

Approach (cheap, no Faust rebuild):
  Run A: nilamp_drive_probe_render at --gain 6.0 (baseline).
         Read post-tube channels:
           public_T4 (ch8), v6_T4 (ch10), v10_T4 (ch12).
         Compute delta_db = 20*log10(H1(v6_T4) / H1(public_T4)) on the
         440 Hz sine to identify the H1-match level offset.
  Run B: nilamp_drive_probe_render at --gain (6.0 - delta_db).
         The reduced-gain run's ch8 (public_T4) is the H1-level-matched
         counterpart for the variants.

Decision rule:
  If B.public_T4_attenuated H2 lands within +-10% of A.v6_T4 H2
  (and similarly for v10_T4), AND the sweep-residual after scalar fit
  is within 6 dB of the original variant residual:
    -> LEVEL-EXPLAINED divergence; v6/v10 differ from public mostly
       because their drive level is higher.  Mitigation: pre-attenuate.
  Else:
    -> STRUCTURAL divergence; escalate to dia / kfb / pk-state probe.

Caveat: the two probe runs do not share PSS state bit-for-bit because
their inputs differ by a small scalar.  PSS time constant is 50 ms and
we trim the same 100 ms warm-up applied by tools/abx_compare.py, so the
residual PSS bias on a steady sine / sweep is negligible for H2-shape
comparison.
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
    scalar_fit_residual,
    stft_power_ratio_db,
    trim_warmup,
)
from abx_compare import write_wav_f32  # noqa: E402

# Channel indices from nilamp_drive_taps.dsp.
CH_T4_PUBLIC = 8
CH_T4_V6 = 10
CH_T4_V10 = 12


def render_probe(label: str, signal: np.ndarray, sr: int, out_dir: Path,
                 gain_db: float, probe_bin: Path) -> np.ndarray:
    """Render one probe run; return trimmed (NUM_CHANNELS, n_frames) array."""
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


def harm_levels(x: np.ndarray, sr: int, f0: float
                ) -> tuple[float, float, float, float]:
    return (
        goertzel_amp(x, sr, f0),
        goertzel_amp(x, sr, 2 * f0),
        goertzel_amp(x, sr, 3 * f0),
        goertzel_amp(x, sr, 5 * f0),
    )


def fmt_db(x: float) -> str:
    return f"{x:+.2f}" if math.isfinite(x) else "  -inf"


def analyse_sine(out_dir: Path, sr: int, f0: float, dur: float, amp: float,
                 gain_db: float, probe_bin: Path,
                 h2_tolerance_pct: float) -> tuple[bool, float]:
    """Returns (level_explained, delta_db)."""
    sine = gen_sine(sr, f0, dur, amp)

    # Run A: baseline gain.
    a = render_probe("level_probe_sine_A", sine, sr, out_dir, gain_db,
                     probe_bin)

    a_pub = a[CH_T4_PUBLIC]
    a_v6 = a[CH_T4_V6]
    a_v10 = a[CH_T4_V10]

    h1_pub, h2_pub, h3_pub, h5_pub = harm_levels(a_pub, sr, f0)
    h1_v6, h2_v6, h3_v6, h5_v6 = harm_levels(a_v6, sr, f0)
    h1_v10, h2_v10, h3_v10, h5_v10 = harm_levels(a_v10, sr, f0)

    if h1_pub <= 0:
        raise RuntimeError("public T4 H1 is zero; cannot compute delta")
    delta_db_v6 = 20.0 * math.log10(h1_v6 / h1_pub)
    delta_db_v10 = 20.0 * math.log10(h1_v10 / h1_pub)
    # Use the average across v6 and v10 (they're nearly identical).
    delta_db = 0.5 * (delta_db_v6 + delta_db_v10)
    print(f"\n[sine] H1 deltas: v6={delta_db_v6:+.3f} dB, "
          f"v10={delta_db_v10:+.3f} dB, mean={delta_db:+.3f} dB")

    # Run B: gain reduced by delta_db so public_T4 H1 matches variants.
    b = render_probe("level_probe_sine_B", sine, sr, out_dir,
                     gain_db - delta_db, probe_bin)
    b_pub = b[CH_T4_PUBLIC]
    h1_pubB, h2_pubB, h3_pubB, h5_pubB = harm_levels(b_pub, sr, f0)

    print(f"\n[sine] post-tube T4 harmonic table (440 Hz):")
    print(f"  {'tap':<22} {'H1':>10} {'H2':>10} {'H3':>10} {'H5':>10} "
          f"{'H2/H1':>8}")
    rows = [
        ("A.public_T4", h1_pub, h2_pub, h3_pub, h5_pub),
        ("A.v6_T4", h1_v6, h2_v6, h3_v6, h5_v6),
        ("A.v10_T4", h1_v10, h2_v10, h3_v10, h5_v10),
        ("B.public_T4_atten", h1_pubB, h2_pubB, h3_pubB, h5_pubB),
    ]
    for name, h1, h2, h3, h5 in rows:
        ratio = h2 / h1 if h1 > 0 else 0.0
        print(f"  {name:<22} {h1:10.4e} {h2:10.4e} {h3:10.4e} {h5:10.4e} "
              f"{ratio:8.5f}")

    # Decision: does the level-matched public match v6/v10 H2?
    def within_pct(a: float, b: float, pct: float) -> bool:
        if a <= 0 or b <= 0:
            return False
        return abs(a - b) / max(a, b) <= pct / 100.0

    h2_match_v6 = within_pct(h2_pubB, h2_v6, h2_tolerance_pct)
    h2_match_v10 = within_pct(h2_pubB, h2_v10, h2_tolerance_pct)
    print(f"\n[sine] level-match B.public_T4_atten vs variants "
          f"(tolerance +-{h2_tolerance_pct:.0f}%):")
    print(f"  H2 match v6:  {h2_match_v6} "
          f"(B={h2_pubB:.4e} vs v6={h2_v6:.4e})")
    print(f"  H2 match v10: {h2_match_v10} "
          f"(B={h2_pubB:.4e} vs v10={h2_v10:.4e})")

    # Scalar-fit residual: variant vs B.public_T4_atten.
    s_v6, r_v6, lag_v6 = scalar_fit_residual(b_pub, a_v6, sr)
    s_v10, r_v10, lag_v10 = scalar_fit_residual(b_pub, a_v10, sr)
    pubB_peak = float(np.max(np.abs(b_pub))) or 1.0
    rdb_v6 = (20.0 * math.log10(r_v6 / pubB_peak)
              if r_v6 > 0 else -math.inf)
    rdb_v10 = (20.0 * math.log10(r_v10 / pubB_peak)
               if r_v10 > 0 else -math.inf)
    print(f"\n[sine] scalar-fit B.public_T4_atten vs variant:")
    print(f"  v6:  s={s_v6:.5f} resid={fmt_db(rdb_v6)} dB lag={lag_v6}")
    print(f"  v10: s={s_v10:.5f} resid={fmt_db(rdb_v10)} dB lag={lag_v10}")

    return (h2_match_v6 and h2_match_v10), delta_db


def analyse_sweep(out_dir: Path, sr: int, dur: float, amp: float,
                  gain_db: float, delta_db: float,
                  probe_bin: Path,
                  ratio_tolerance_db: float) -> bool:
    sweep = gen_log_sweep(sr, dur, amp=amp)

    a = render_probe("level_probe_sweep_A", sweep, sr, out_dir, gain_db,
                     probe_bin)
    b = render_probe("level_probe_sweep_B", sweep, sr, out_dir,
                     gain_db - delta_db, probe_bin)

    a_pub = a[CH_T4_PUBLIC]
    a_v6 = a[CH_T4_V6]
    a_v10 = a[CH_T4_V10]
    b_pub = b[CH_T4_PUBLIC]
    pubB_peak = float(np.max(np.abs(b_pub))) or 1.0

    s_v6, r_v6, _ = scalar_fit_residual(b_pub, a_v6, sr)
    s_v10, r_v10, _ = scalar_fit_residual(b_pub, a_v10, sr)
    rdb_v6 = (20.0 * math.log10(r_v6 / pubB_peak)
              if r_v6 > 0 else -math.inf)
    rdb_v10 = (20.0 * math.log10(r_v10 / pubB_peak)
               if r_v10 > 0 else -math.inf)
    # Prior residuals (variant vs A.public, from compare_drive_taps run):
    s_v6A, r_v6A, _ = scalar_fit_residual(a_pub, a_v6, sr)
    s_v10A, r_v10A, _ = scalar_fit_residual(a_pub, a_v10, sr)
    apub_peak = float(np.max(np.abs(a_pub))) or 1.0
    rdb_v6A = (20.0 * math.log10(r_v6A / apub_peak)
               if r_v6A > 0 else -math.inf)
    rdb_v10A = (20.0 * math.log10(r_v10A / apub_peak)
                if r_v10A > 0 else -math.inf)

    print(f"\n[sweep] scalar-fit residual dB:")
    print(f"  variant vs A.public:        v6={fmt_db(rdb_v6A)} "
          f"v10={fmt_db(rdb_v10A)}")
    print(f"  variant vs B.public_atten:  v6={fmt_db(rdb_v6)} "
          f"v10={fmt_db(rdb_v10)}")

    # Per-bin level ratio v6 vs B.public_atten and v10 vs B.public_atten.
    r_v6_bins = stft_power_ratio_db(b_pub, a_v6, sr)
    r_v10_bins = stft_power_ratio_db(b_pub, a_v10, sr)
    header = "  " + " " * 22 + "".join(f"{f:>9.0f}" for f in OCTAVE_BIN_HZ)
    print(f"\n[sweep] per-octave level ratio dB (variant - B.public_atten):")
    print(header)
    for name, row in (("v6 - B.public_atten", r_v6_bins),
                      ("v10 - B.public_atten", r_v10_bins)):
        cells = "".join(
            (f"{v:+9.2f}" if math.isfinite(v) else "      nan")
            for v in row
        )
        print(f"  {name:<22}{cells}")

    # Tightness across bins: max abs ratio.
    spans = []
    for row in (r_v6_bins, r_v10_bins):
        finite = [abs(v) for v in row if math.isfinite(v)]
        if finite:
            spans.append(max(finite))
    worst_bin_dev = max(spans) if spans else 0.0
    print(f"\n[sweep] worst |ratio| across bins after level-match: "
          f"{worst_bin_dev:.2f} dB "
          f"(tolerance: <= {ratio_tolerance_db:.2f} dB)")
    return worst_bin_dev <= ratio_tolerance_db


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out-dir", type=Path,
                    default=Path("/tmp/abx_compare/drive_taps/t4_level"))
    ap.add_argument("--gain", type=float, default=6.0)
    ap.add_argument("--sample-rate", type=int, default=SAMPLE_RATE)
    ap.add_argument("--sine-freq", type=float, default=440.0)
    ap.add_argument("--sine-dur", type=float, default=2.0)
    ap.add_argument("--sine-amp", type=float, default=0.5)
    ap.add_argument("--sweep-dur", type=float, default=5.0)
    ap.add_argument("--sweep-amp", type=float, default=0.5)
    ap.add_argument("--probe-bin", type=Path, default=PROBE_BIN)
    ap.add_argument("--h2-tolerance-pct", type=float, default=10.0,
                    help="H2 magnitude tolerance vs variant (default 10%%).")
    ap.add_argument("--ratio-tolerance-db", type=float, default=0.5,
                    help="Per-bin sweep ratio tolerance after level-match "
                         "(default 0.5 dB).")
    args = ap.parse_args()

    if not args.probe_bin.exists():
        print(f"error: probe bin not found: {args.probe_bin}",
              file=sys.stderr)
        return 2

    sine_ok, delta_db = analyse_sine(
        args.out_dir, args.sample_rate, args.sine_freq, args.sine_dur,
        args.sine_amp, args.gain, args.probe_bin, args.h2_tolerance_pct,
    )
    sweep_ok = analyse_sweep(
        args.out_dir, args.sample_rate, args.sweep_dur, args.sweep_amp,
        args.gain, delta_db, args.probe_bin, args.ratio_tolerance_db,
    )

    print(f"\n=== verdict ===")
    print(f"  H1 delta (variant - public, mean): {delta_db:+.3f} dB")
    print(f"  sine  H2 level-match: {'YES' if sine_ok else 'NO'}")
    print(f"  sweep per-bin ratio level-match: {'YES' if sweep_ok else 'NO'}")
    if sine_ok and sweep_ok:
        print(f"  classification: LEVEL-EXPLAINED")
        print(f"  next: try a v6/v10 candidate with a "
              f"{10**(-delta_db/20):.5f} pre-attenuation on the T4 drive "
              f"and re-run public ABX.")
    else:
        print(f"  classification: STRUCTURAL")
        print(f"  next: probe T4 dia / kfb / peak-detector state via "
              f"a 16+-channel diagnostic exposing t4_dia_public, "
              f"t4_dia_v6, t4_dia_v10.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
