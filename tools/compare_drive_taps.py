#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare nilamp pre-tube drive signals against a Python oracle, and
analyse post-tube voltages for v6 / v10 vs the public path.

Renders a 14-channel diagnostic WAV via `nilamp_drive_probe_render`.

Pre-tube oracle compare (regression guard, channels 4-7):
  Reconstructs channels 5-7 from rendered channels 0-3 (the T3 outputs)
  using `keller_oracle` filters and reports per-tap max abs error and RMS
  error in dB relative to the tap's own peak.  Channel 4 == channel 0 by
  construction.  If oracle vs. rendered match to <= ~1e-6, the regression
  is downstream of the tubes.

Post-tube analysis (channels 8-13):
  For each variant (public / v6 / v10) and signal (sine, sweep), prints:
    - peak / RMS
    - integer-sample lag vs the public counterpart (cross-correlation,
      +-5 ms search)
    - best least-squares scalar fit `s = <a, b> / <b, b>` after lag align,
      residual RMS in dB vs the public peak
    - on sine: single-bin DFT levels at f0 / 2f0 / 3f0 / 5f0 (THD% and
      delta-THD% vs public)
    - on sweep: STFT power-ratio dB at fixed-octave bin centres
      (31.5/63/125/250/500/1k/2k/4k/8k/16k Hz)

Decision rule at end:
  if max scalar-fit residual RMS over all (variant, signal) pairs
  is <= -60 dB and STFT ratios are smooth -> linear divergence
  (next probe: small-signal v0..v12 in nilamp_t5_balance).
  Otherwise -> nonlinear divergence
  (next probe: t4_dia / t5_dia taps).
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
    "res4_backend_vk",     # 3  (v6 source)
    "t4_in_public_drive",  # 4  (== ch0 by construction)
    "t5_in_public_drive",  # 5  (oracle: linear chain on ch1)
    "t4_in_v6_drive",      # 6  (oracle: linear chain on ch2)
    "t4_in_v10_drive",     # 7  (oracle: linear chain on ch0)
    "t4_v_public",         # 8
    "t5_v_public",         # 9
    "t4_v_v6",             # 10
    "t5_v_v6",             # 11
    "t4_v_v10",            # 12
    "t5_v_v10",            # 13 (== ch9 by construction; sanity slot)
    "t4_dia_public",       # 14
    "t4_dia_v6",           # 15
    "t4_dia_v10",          # 16
    "t4_advk_public",      # 17
    "t4_advk_v6",          # 18
    "t4_advk_v10",         # 19
    "t4_in_v13_drive",     # 20
    "t4_v_v13",            # 21
    "t4_in_v15_drive",     # 22
    "t4_v_v15",            # 23
]
NUM_CHANNELS = len(CHANNEL_NAMES)


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
               max_abs_rel_thresh: float, rms_db_thresh: float
               ) -> tuple[bool, np.ndarray]:
    """Render the probe, run the pre-tube oracle compare, and return
    (pre_tube_pass, trimmed_samples) where trimmed_samples has shape
    (NUM_CHANNELS, n_frames_trimmed).
    """
    out_dir.mkdir(parents=True, exist_ok=True)
    in_wav = out_dir / f"{label}_input.wav"
    out_wav = out_dir / f"{label}_drive_taps.wav"
    write_wav_f32(in_wav, signal.tolist(), sr)

    print(f"[{label}] rendering drive-tap probe (gain={gain_db:+.2f} dB)...", flush=True)
    run_probe(in_wav, out_wav, gain_db, probe_bin)

    samples_full, sr_out = read_wav_f32_multi(out_wav)
    if sr_out != sr:
        raise RuntimeError(f"sample-rate mismatch: in={sr} out={sr_out}")
    if samples_full.shape[0] != NUM_CHANNELS:
        raise RuntimeError(
            f"expected {NUM_CHANNELS} channels in {out_wav}, got {samples_full.shape[0]}"
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
    return all_ok, samples


# --------------------------------------------------------------------------- #
# Post-tube analysis
# --------------------------------------------------------------------------- #

# Variants: (label, t4_ch_idx, t5_ch_idx).  Public is the reference.
POST_TUBE_VARIANTS = [
    ("public", 8, 9),
    ("v6",     10, 11),
    ("v10",    12, 13),
]

OCTAVE_BIN_HZ = (31.5, 63.0, 125.0, 250.0, 500.0,
                 1_000.0, 2_000.0, 4_000.0, 8_000.0, 16_000.0)


def best_lag(a: np.ndarray, b: np.ndarray, sr: int,
             search_ms: float = 5.0) -> int:
    """Find integer-sample lag k such that a[k:] aligns with b[:N-k] best
    (or vice versa for negative k).  Search range +-search_ms.
    Uses normalised cross-correlation on a centred subset for speed.
    Returns k (b is shifted by k samples relative to a).
    """
    n = min(len(a), len(b))
    if n < 1024:
        return 0
    # Use a window in the middle to avoid transient ends.
    half = min(n // 4, sr)  # up to 1 s
    mid = n // 2
    a_w = a[mid - half: mid + half].astype(np.float64)
    b_w = b[mid - half: mid + half].astype(np.float64)
    a_w -= a_w.mean()
    b_w -= b_w.mean()
    if a_w.std() < 1e-12 or b_w.std() < 1e-12:
        return 0
    max_lag = int(round(search_ms * 1e-3 * sr))
    best_k = 0
    best_c = -math.inf
    # Brute-force for small max_lag (~240 samples) is fine.
    for k in range(-max_lag, max_lag + 1):
        if k >= 0:
            c = float(np.dot(a_w[k:], b_w[:len(a_w) - k]))
        else:
            c = float(np.dot(a_w[:len(a_w) + k], b_w[-k:]))
        if c > best_c:
            best_c = c
            best_k = k
    return best_k


def scalar_fit_residual(a: np.ndarray, b: np.ndarray, sr: int
                        ) -> tuple[float, float, int]:
    """Best LS scalar fit a ~= s * b_shifted; return (s, residual_rms, lag).
    `a` is the reference (public), `b` is the variant being fit.
    """
    lag = best_lag(a, b, sr)
    n = min(len(a), len(b))
    if lag >= 0:
        a_al = a[lag:n].astype(np.float64)
        b_al = b[:n - lag].astype(np.float64)
    else:
        a_al = a[:n + lag].astype(np.float64)
        b_al = b[-lag:n].astype(np.float64)
    bb = float(np.dot(b_al, b_al))
    if bb < 1e-30:
        return 0.0, float(np.sqrt(np.mean(a_al * a_al))), lag
    s = float(np.dot(a_al, b_al)) / bb
    resid = a_al - s * b_al
    rms = float(np.sqrt(np.mean(resid * resid)))
    return s, rms, lag


def goertzel_amp(x: np.ndarray, sr: int, freq: float) -> float:
    """Single-bin DFT magnitude at `freq` (no windowing) using full x.
    Returns amplitude (peak), i.e. 2*|X[k]|/N for non-DC/non-Nyquist.
    """
    n = len(x)
    if n == 0 or freq <= 0 or freq >= sr / 2:
        return 0.0
    t = np.arange(n, dtype=np.float64) / sr
    c = np.cos(2.0 * math.pi * freq * t)
    s = np.sin(2.0 * math.pi * freq * t)
    re = float(np.dot(x.astype(np.float64), c))
    im = -float(np.dot(x.astype(np.float64), s))
    mag = math.hypot(re, im) / n
    return 2.0 * mag


def stft_power_ratio_db(a: np.ndarray, b: np.ndarray, sr: int,
                        bin_freqs=OCTAVE_BIN_HZ) -> list[float]:
    """For each frequency in bin_freqs, return 10*log10(|B(f)|^2 / |A(f)|^2)
    over the full signal using single-bin DFT magnitudes.  Returns one dB
    value per bin (so this is really a per-bin level ratio, not STFT, but
    captures broadband linear-tonal divergence on a sweep just as well).
    """
    out: list[float] = []
    for f in bin_freqs:
        if f >= sr / 2:
            out.append(float("nan"))
            continue
        amp_a = goertzel_amp(a, sr, f)
        amp_b = goertzel_amp(b, sr, f)
        if amp_a < 1e-12 or amp_b < 1e-12:
            out.append(float("nan"))
            continue
        out.append(20.0 * math.log10(amp_b / amp_a))
    return out


def analyse_sine(label: str, sr: int, samples: np.ndarray, f0: float,
                 out_dir: Path) -> tuple[float, dict[str, float]]:
    """Per-variant sine-tone analysis at f0.  Returns (worst_resid_db,
    per_variant_resid_db_dict).  `samples` is the trimmed (NUM_CHANNELS, N)
    array.
    """
    # Reference (public) for residual / lag baseline -- use T4 public for
    # T4 variants and T5 public for T5 variants.
    public_t4 = samples[8]
    public_t5 = samples[9]
    pub_t4_peak = float(np.max(np.abs(public_t4)))
    pub_t5_peak = float(np.max(np.abs(public_t5)))

    print(f"[{label}] post-tube sine analysis @ {f0:.1f} Hz:")
    print(f"    public T4 peak={pub_t4_peak:.4e}  T5 peak={pub_t5_peak:.4e}")

    rows: list[tuple[str, float, float, float, float, float, float, float,
                     float, float, int]] = []
    # Columns: variant, tube, peak, rms, h1, h2, h3, h5, thd_pct, resid_db,
    # lag.
    worst_resid_db = -math.inf
    resid_table: dict[str, float] = {}
    for var, t4_idx, t5_idx in POST_TUBE_VARIANTS:
        for tube_label, ch_idx, ref, ref_peak in (
            ("T4", t4_idx, public_t4, pub_t4_peak),
            ("T5", t5_idx, public_t5, pub_t5_peak),
        ):
            x = samples[ch_idx]
            peak = float(np.max(np.abs(x)))
            rms = float(np.sqrt(np.mean(x.astype(np.float64) ** 2)))
            h1 = goertzel_amp(x, sr, f0)
            h2 = goertzel_amp(x, sr, 2 * f0)
            h3 = goertzel_amp(x, sr, 3 * f0)
            h5 = goertzel_amp(x, sr, 5 * f0)
            thd = (math.sqrt(h2 * h2 + h3 * h3 + h5 * h5) / h1
                   if h1 > 0 else 0.0)
            if var == "public":
                s, resid_rms, lag = 1.0, 0.0, 0
                resid_db = -math.inf
            else:
                s, resid_rms, lag = scalar_fit_residual(ref, x, sr)
                resid_db = (20.0 * math.log10(resid_rms / ref_peak)
                            if resid_rms > 0 and ref_peak > 0 else -math.inf)
                if resid_db > worst_resid_db:
                    worst_resid_db = resid_db
            key = f"{var}_{tube_label}"
            resid_table[key] = resid_db
            rows.append((f"{var}_{tube_label}", peak, rms, h1, h2, h3, h5,
                         thd * 100.0, s, resid_db, lag))

    # Print table.
    print(f"    {'tap':<14} {'peak':>10} {'rms':>10} "
          f"{'H1':>10} {'H2':>10} {'H3':>10} {'H5':>10} "
          f"{'THD%':>7} {'fit s':>8} {'resid_dB':>10} {'lag':>5}")
    for name, peak, rms, h1, h2, h3, h5, thd_p, s, rdb, lag in rows:
        rdb_s = f"{rdb:+.2f}" if math.isfinite(rdb) else "  -inf"
        print(f"    {name:<14} {peak:10.4e} {rms:10.4e} "
              f"{h1:10.4e} {h2:10.4e} {h3:10.4e} {h5:10.4e} "
              f"{thd_p:7.3f} {s:8.5f} {rdb_s:>10} {lag:5d}")

    return worst_resid_db, resid_table


def analyse_sweep(label: str, sr: int, samples: np.ndarray,
                  out_dir: Path) -> tuple[float, list[tuple[str, list[float]]]]:
    """Per-variant sweep analysis: scalar-fit residual + per-bin level
    ratio dB vs public.  Returns (worst_resid_db, [(variant_tube,
    ratios_db_per_bin), ...]).
    """
    public_t4 = samples[8]
    public_t5 = samples[9]
    pub_t4_peak = float(np.max(np.abs(public_t4)))
    pub_t5_peak = float(np.max(np.abs(public_t5)))

    print(f"[{label}] post-tube sweep analysis:")
    print(f"    public T4 peak={pub_t4_peak:.4e}  T5 peak={pub_t5_peak:.4e}")

    # Scalar-fit residual table.
    print(f"    {'tap':<14} {'peak':>10} {'rms':>10} "
          f"{'fit s':>8} {'resid_dB':>10} {'lag':>5}")
    worst_resid_db = -math.inf
    for var, t4_idx, t5_idx in POST_TUBE_VARIANTS:
        for tube_label, ch_idx, ref, ref_peak in (
            ("T4", t4_idx, public_t4, pub_t4_peak),
            ("T5", t5_idx, public_t5, pub_t5_peak),
        ):
            x = samples[ch_idx]
            peak = float(np.max(np.abs(x)))
            rms = float(np.sqrt(np.mean(x.astype(np.float64) ** 2)))
            if var == "public":
                s, resid_rms, lag = 1.0, 0.0, 0
                resid_db = -math.inf
            else:
                s, resid_rms, lag = scalar_fit_residual(ref, x, sr)
                resid_db = (20.0 * math.log10(resid_rms / ref_peak)
                            if resid_rms > 0 and ref_peak > 0 else -math.inf)
                if resid_db > worst_resid_db:
                    worst_resid_db = resid_db
            rdb_s = f"{rdb:+.2f}" if math.isfinite(rdb := resid_db) else "  -inf"
            print(f"    {var}_{tube_label:<11} {peak:10.4e} {rms:10.4e} "
                  f"{s:8.5f} {rdb_s:>10} {lag:5d}")

    # Per-bin level ratio table (variant / public).
    print(f"    Level ratio dB (variant - public), per octave bin:")
    header = "    " + " " * 14 + "".join(f"{f:>9.0f}" for f in OCTAVE_BIN_HZ)
    print(header)
    ratios: list[tuple[str, list[float]]] = []
    for var, t4_idx, t5_idx in POST_TUBE_VARIANTS:
        if var == "public":
            continue
        for tube_label, ch_idx, ref in (
            ("T4", t4_idx, public_t4),
            ("T5", t5_idx, public_t5),
        ):
            x = samples[ch_idx]
            r = stft_power_ratio_db(ref, x, sr)
            ratios.append((f"{var}_{tube_label}", r))
            cells = "".join(
                (f"{v:+9.2f}" if math.isfinite(v) else "      nan")
                for v in r
            )
            print(f"    {var}_{tube_label:<11}{cells}")

    return worst_resid_db, ratios


def smooth_ratios(ratios: list[tuple[str, list[float]]],
                  variation_db_thresh: float = 1.0) -> bool:
    """Heuristic: ratios are 'smooth' if the within-row max-min is small
    (i.e. roughly flat across band) OR the row is monotone with small
    second differences.  We just check max-min span here.
    """
    for _, row in ratios:
        finite = [v for v in row if math.isfinite(v)]
        if len(finite) < 2:
            continue
        span = max(finite) - min(finite)
        if span > variation_db_thresh * 5:
            return False
    return True


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
    ap.add_argument("--linear-threshold-db", type=float, default=-60.0,
                    help="If worst post-tube scalar-fit residual is "
                         "<= this (dB vs public peak), classify as linear "
                         "divergence (default -60).")
    args = ap.parse_args()

    if not args.probe_bin.exists():
        print(f"error: probe bin not found: {args.probe_bin}", file=sys.stderr)
        print("hint: cargo build --release --bin nilamp_drive_probe_render",
              file=sys.stderr)
        return 2

    sr = args.sample_rate
    pre_ok = True
    worst_resid_db = -math.inf
    sweep_ratios: list[tuple[str, list[float]]] = []

    if not args.skip_sine:
        sine = gen_sine(sr, args.sine_freq, args.sine_dur, args.sine_amp)
        ok, samples = run_signal("sine440", sine, sr, args.out_dir, args.gain,
                                 args.probe_bin, args.max_abs_rel, args.rms_db)
        pre_ok = pre_ok and ok
        rdb, _ = analyse_sine("sine440", sr, samples, args.sine_freq,
                              args.out_dir)
        if rdb > worst_resid_db:
            worst_resid_db = rdb

    if not args.skip_sweep:
        sweep = gen_log_sweep(sr, args.sweep_dur, amp=args.sweep_amp)
        ok, samples = run_signal("sweep5s", sweep, sr, args.out_dir, args.gain,
                                 args.probe_bin, args.max_abs_rel, args.rms_db)
        pre_ok = pre_ok and ok
        rdb, ratios = analyse_sweep("sweep5s", sr, samples, args.out_dir)
        if rdb > worst_resid_db:
            worst_resid_db = rdb
        sweep_ratios = ratios

    print(f"\nverdict (pre-tube oracle): "
          f"{'PASS' if pre_ok else 'FAIL'} "
          f"(thresholds: max_abs_rel <= {args.max_abs_rel:.1e}, "
          f"rms_db <= {args.rms_db:+.1f})")

    print(f"\npost-tube divergence summary:")
    if math.isfinite(worst_resid_db):
        print(f"  worst scalar-fit residual vs public: "
              f"{worst_resid_db:+.2f} dB")
    else:
        print(f"  worst scalar-fit residual vs public: -inf (all variants "
              "match public exactly)")

    is_linear = worst_resid_db <= args.linear_threshold_db
    smooth = smooth_ratios(sweep_ratios) if sweep_ratios else True
    if is_linear and smooth:
        verdict = "LINEAR divergence"
        next_step = ("next probe: small-signal v0..v12 in "
                     "nilamp_t5_balance_render (post-power-chain / "
                     "branch-mix / denominator topology error).")
    else:
        verdict = "NONLINEAR divergence"
        next_step = ("next probe: t4_dia / t5_dia taps to localise "
                     "tube-state offender (peak-detector params, kfb).")
    print(f"  ratios smooth across band: {smooth}")
    print(f"  classification: {verdict}")
    print(f"  {next_step}")
    return 0 if pre_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
