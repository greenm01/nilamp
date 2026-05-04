#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare nilamp pre-tube drive signals against a Python oracle.

Renders an 8-channel diagnostic WAV via `nilamp_drive_probe_render`, then
reconstructs channels 5-7 (the linear pre-chain drive signals into T4/T5)
from rendered channels 0-3 (the T3 outputs) using the Python oracle filters
in `keller_oracle`.  Reports per-tap max abs error and RMS error in dB
relative to the tap's own peak, applying the same 100 ms warm-up trim used
by `tools/abx_compare.py`.

Strategy:
- Channels 0-3 (`res4_v_public`, `res4_vk_public`, `res4_backend_v`,
  `res4_backend_vk`) come from nonlinear tube stages and are taken as-is
  from the nilamp render -- no oracle counterpart.
- Channels 4-7 are *linear* functions of channels 0/2 (T4 candidates) or
  channel 1 (public T5).  These the oracle reproduces.

If oracle vs. rendered match to <= ~1e-6, the regression is downstream of
the tubes (state / PSS / branch mix), and pre-chain filter coefficients are
not the cause.  If they diverge, the divergent tap localizes the upstream
mismatch (filter implementation, smoothing, or block-boundary effect).
"""

from __future__ import annotations

import argparse
import math
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools"))

import keller_oracle as ko  # noqa: E402
from abx_compare import (  # noqa: E402
    JSFX_WARMUP_S,
    write_wav_f32,
)

PROBE_BIN = ROOT / "target" / "release" / "nilamp_drive_probe_render"
SAMPLE_RATE = 48_000

# Mode-0 power-chain constants -- mirror dsp/diagnostics/nilamp_drive_taps.dsp.
K1 = 0.797
K2 = 0.940
HP3_HZ = 5.8
HP4_HZ = 6.4
KP1 = 1.1220184543
FP_HZ = 80.0
QP1 = 2.6685237666
KS1 = 1.4125375446
FS1_HZ = 2098.1359672

# Channel layout from nilamp_drive_taps.dsp.
CHANNEL_NAMES = [
    "res4_v_public",       # 0
    "res4_vk_public",      # 1
    "res4_backend_v",      # 2  (v6 source)
    "res4_backend_vk",     # 3  (v6 source, currently not used by oracle taps)
    "t4_in_public_drive",  # 4  (== ch0 by construction)
    "t5_in_public_drive",  # 5  (oracle: linear chain on ch1)
    "t4_in_v6_drive",      # 6  (oracle: linear chain on ch2)
    "t4_in_v10_drive",     # 7  (oracle: linear chain on ch0)
]


# --------------------------------------------------------------------------- #
# Multi-channel float32 WAV reader (the `read_wav_f32` in abx_compare is
# mono-only; we need N-channel here).
# --------------------------------------------------------------------------- #

def read_wav_f32_multi(path: Path) -> tuple[np.ndarray, int]:
    """Read a 32-bit float multi-channel WAV.

    Returns (samples, sr) where samples has shape (n_channels, n_frames).
    """
    data = path.read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"not a WAV: {path}")

    fmt_tag = None
    channels = None
    sr = None
    bits = None
    samples_bytes = b""
    i = 12
    while i + 8 <= len(data):
        cid = data[i:i + 4]
        sz = struct.unpack("<I", data[i + 4:i + 8])[0]
        body = data[i + 8:i + 8 + sz]
        if cid == b"fmt ":
            fmt_tag = struct.unpack("<H", body[0:2])[0]
            channels = struct.unpack("<H", body[2:4])[0]
            sr = struct.unpack("<I", body[4:8])[0]
            bits = struct.unpack("<H", body[14:16])[0]
            if fmt_tag == 0xFFFE and len(body) >= 26:
                fmt_tag = struct.unpack("<H", body[24:26])[0]
        elif cid == b"data":
            samples_bytes = body
        i += 8 + sz + (sz & 1)

    if fmt_tag != 3:
        raise ValueError(f"{path}: expected IEEE float (fmt_tag=3), got {fmt_tag}")
    if bits != 32:
        raise ValueError(f"{path}: expected 32-bit, got {bits}-bit")
    if sr is None or channels is None:
        raise ValueError(f"{path}: malformed WAV (missing fmt)")

    n_total = len(samples_bytes) // 4
    flat = np.asarray(struct.unpack(f"<{n_total}f", samples_bytes), dtype=np.float32)
    n_frames = n_total // channels
    # Interleaved -> (frames, channels) -> (channels, frames)
    samples = flat.reshape(n_frames, channels).T.copy()
    return samples, sr


# --------------------------------------------------------------------------- #
# Test signals (match abx_compare conventions)
# --------------------------------------------------------------------------- #

def gen_sine(sr: int, freq: float, dur_s: float, amp: float = 0.5) -> np.ndarray:
    n = int(round(dur_s * sr))
    t = np.arange(n, dtype=np.float64) / sr
    return (amp * np.sin(2.0 * np.pi * freq * t)).astype(np.float32)


def gen_log_sweep(sr: int, dur_s: float, f0: float = 20.0, f1: float = 20_000.0,
                  amp: float = 0.5) -> np.ndarray:
    n = int(round(dur_s * sr))
    t = np.arange(n, dtype=np.float64) / sr
    k = math.log(f1 / f0)
    phase = 2.0 * math.pi * f0 * dur_s / k * (np.exp(k * t / dur_s) - 1.0)
    return (amp * np.sin(phase)).astype(np.float32)


# --------------------------------------------------------------------------- #
# Oracle reconstructions
# --------------------------------------------------------------------------- #

def oracle_t5_public(res4_vk: np.ndarray, sr: int) -> np.ndarray:
    """Public T5 drive: x * K2 -> hp(HP4_HZ) -> peq -> hs."""
    x = (res4_vk * K2).astype(np.float32)
    x = ko.flt_ii1_hp_block(HP4_HZ, sr, x)
    x = ko.flt_sv2_peq_block(KP1, FP_HZ, QP1, 1, 1, sr, x)
    x = ko.flt_sv1_hs_block(KS1, FS1_HZ, 1, sr, x)
    return x.astype(np.float32)


def oracle_t4_v_chain(t3_plate: np.ndarray, sr: int) -> np.ndarray:
    """T4 drive for v6/v10: x * K1 -> hp(HP3_HZ) -> peq -> hs."""
    x = (t3_plate * K1).astype(np.float32)
    x = ko.flt_ii1_hp_block(HP3_HZ, sr, x)
    x = ko.flt_sv2_peq_block(KP1, FP_HZ, QP1, 1, 1, sr, x)
    x = ko.flt_sv1_hs_block(KS1, FS1_HZ, 1, sr, x)
    return x.astype(np.float32)


# --------------------------------------------------------------------------- #
# Comparison
# --------------------------------------------------------------------------- #

@dataclass
class TapResult:
    name: str
    peak: float
    max_abs_err: float
    rms_err: float
    rms_err_db: float  # vs peak of the tap

    def fmt(self, max_abs_rel_thresh: float, rms_db_thresh: float) -> str:
        rel = (self.max_abs_err / self.peak) if self.peak > 0 else math.inf
        ok = rel <= max_abs_rel_thresh and self.rms_err_db <= rms_db_thresh
        verdict = "OK  " if ok else "FAIL"
        return (
            f"  {verdict} {self.name:<22} "
            f"peak={self.peak:.4e}  "
            f"max|d|={self.max_abs_err:.3e} (rel {rel:.2e})  "
            f"rms_d={self.rms_err:.3e} ({self.rms_err_db:+.1f} dB)"
        )


def compare_tap(name: str, rendered: np.ndarray, expected: np.ndarray) -> TapResult:
    n = min(len(rendered), len(expected))
    a = rendered[:n].astype(np.float64)
    b = expected[:n].astype(np.float64)
    diff = a - b
    peak = float(np.max(np.abs(a))) if n else 0.0
    max_abs = float(np.max(np.abs(diff))) if n else 0.0
    rms = float(np.sqrt(np.mean(diff * diff))) if n else 0.0
    if peak > 0 and rms > 0:
        rms_db = 20.0 * math.log10(rms / peak)
    else:
        rms_db = -math.inf
    return TapResult(name, peak, max_abs, rms, rms_db)


# --------------------------------------------------------------------------- #
# Driver
# --------------------------------------------------------------------------- #

def run_probe(input_wav: Path, output_wav: Path, gain_db: float,
              probe_bin: Path) -> None:
    cmd = [
        str(probe_bin),
        "--input", str(input_wav),
        "--output", str(output_wav),
        "--gain", str(gain_db),
        # Other params at defaults (50%) -- match abx_compare default tone stack.
    ]
    subprocess.run(cmd, check=True, capture_output=True)


def trim_warmup(x: np.ndarray, sr: int) -> np.ndarray:
    n = int(round(JSFX_WARMUP_S * sr))
    return x[n:]


def run_signal(label: str, signal: np.ndarray, sr: int, out_dir: Path,
               gain_db: float, probe_bin: Path,
               max_abs_rel_thresh: float, rms_db_thresh: float) -> bool:
    out_dir.mkdir(parents=True, exist_ok=True)
    in_wav = out_dir / f"{label}_input.wav"
    out_wav = out_dir / f"{label}_drive_taps.wav"
    write_wav_f32(in_wav, signal.tolist(), sr)

    print(f"[{label}] rendering drive-tap probe (gain={gain_db:+.2f} dB)...", flush=True)
    run_probe(in_wav, out_wav, gain_db, probe_bin)

    samples_full, sr_out = read_wav_f32_multi(out_wav)
    if sr_out != sr:
        raise RuntimeError(f"sample-rate mismatch: in={sr} out={sr_out}")
    if samples_full.shape[0] != 8:
        raise RuntimeError(
            f"expected 8 channels in {out_wav}, got {samples_full.shape[0]}"
        )

    # Run the oracle on the *untrimmed* T3 channels so its IIR state evolves
    # with the same warm-up history as the Faust renderer.  Trim the warm-up
    # afterwards so we compare the same time window the public ABX gate uses.
    res4_v_full = samples_full[0]
    res4_vk_full = samples_full[1]
    res4_backend_v_full = samples_full[2]

    expected_t4_public_full = res4_v_full  # by construction
    expected_t5_public_full = oracle_t5_public(res4_vk_full, sr)
    expected_t4_v6_full = oracle_t4_v_chain(res4_backend_v_full, sr)
    expected_t4_v10_full = oracle_t4_v_chain(res4_v_full, sr)

    # Now trim warm-up uniformly across rendered + oracle outputs.
    samples = np.stack([trim_warmup(samples_full[c], sr)
                        for c in range(samples_full.shape[0])])
    rendered_t4_public = samples[4]
    rendered_t5_public = samples[5]
    rendered_t4_v6 = samples[6]
    rendered_t4_v10 = samples[7]
    expected_t4_public = trim_warmup(expected_t4_public_full, sr)
    expected_t5_public = trim_warmup(expected_t5_public_full, sr)
    expected_t4_v6 = trim_warmup(expected_t4_v6_full, sr)
    expected_t4_v10 = trim_warmup(expected_t4_v10_full, sr)

    results = [
        compare_tap("ch4_t4_in_public",     rendered_t4_public, expected_t4_public),
        compare_tap("ch5_t5_in_public",     rendered_t5_public, expected_t5_public),
        compare_tap("ch6_t4_in_v6_drive",   rendered_t4_v6,     expected_t4_v6),
        compare_tap("ch7_t4_in_v10_drive",  rendered_t4_v10,    expected_t4_v10),
    ]

    # Also report the raw T3 channel peaks for context (no oracle compare).
    print(f"[{label}] T3 outputs (no oracle compare):")
    for idx, name in [(0, "res4_v_public"), (1, "res4_vk_public"),
                      (2, "res4_backend_v"), (3, "res4_backend_vk")]:
        peak = float(np.max(np.abs(samples[idx])))
        rms = float(np.sqrt(np.mean(samples[idx].astype(np.float64) ** 2)))
        print(f"    ch{idx} {name:<22} peak={peak:.4e}  rms={rms:.4e}")

    print(f"[{label}] oracle-vs-rendered drive taps:")
    all_ok = True
    for r in results:
        rel = (r.max_abs_err / r.peak) if r.peak > 0 else math.inf
        ok = rel <= max_abs_rel_thresh and r.rms_err_db <= rms_db_thresh
        all_ok = all_ok and ok
        print(r.fmt(max_abs_rel_thresh, rms_db_thresh))
    return all_ok


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out-dir", type=Path, default=Path("/tmp/abx_compare/drive_taps"))
    ap.add_argument("--gain", type=float, default=6.0,
                    help="Rust gain_db for the probe render (default: +6.0).")
    ap.add_argument("--sample-rate", type=int, default=SAMPLE_RATE)
    ap.add_argument("--sine-freq", type=float, default=440.0)
    ap.add_argument("--sine-dur", type=float, default=2.0)
    ap.add_argument("--sine-amp", type=float, default=0.5)
    ap.add_argument("--sweep-dur", type=float, default=5.0)
    ap.add_argument("--sweep-amp", type=float, default=0.5)
    ap.add_argument("--probe-bin", type=Path, default=PROBE_BIN)
    ap.add_argument("--max-abs-rel", type=float, default=1e-5,
                    help="Pass threshold for max abs error relative to tap "
                         "peak (default 1e-5; f32 precision floor is ~1e-6).")
    ap.add_argument("--rms-db", type=float, default=-100.0,
                    help="Pass threshold for RMS error vs peak in dB "
                         "(default -100 dB).")
    ap.add_argument("--skip-sine", action="store_true")
    ap.add_argument("--skip-sweep", action="store_true")
    args = ap.parse_args()

    if not args.probe_bin.exists():
        print(f"error: probe bin not found: {args.probe_bin}", file=sys.stderr)
        print("hint: cargo build --release --bin nilamp_drive_probe_render",
              file=sys.stderr)
        return 2

    sr = args.sample_rate
    all_ok = True

    if not args.skip_sine:
        sine = gen_sine(sr, args.sine_freq, args.sine_dur, args.sine_amp)
        ok = run_signal("sine440", sine, sr, args.out_dir, args.gain,
                        args.probe_bin, args.max_abs_rel, args.rms_db)
        all_ok = all_ok and ok

    if not args.skip_sweep:
        sweep = gen_log_sweep(sr, args.sweep_dur, amp=args.sweep_amp)
        ok = run_signal("sweep5s", sweep, sr, args.out_dir, args.gain,
                        args.probe_bin, args.max_abs_rel, args.rms_db)
        all_ok = all_ok and ok

    print(f"\nverdict: {'PASS' if all_ok else 'FAIL'} "
          f"(thresholds: max_abs_rel <= {args.max_abs_rel:.1e}, "
          f"rms_db <= {args.rms_db:+.1f})")
    return 0 if all_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
