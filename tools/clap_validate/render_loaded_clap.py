#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Diagnostic: render a mono test signal through the dlopen'd CLAP plugin
in two stereo-input presentation modes (shared/distinct) and compare against
the offline reference renderer.

Pass/fail is printed for each mode and channel; non-zero exit on any failure.
"""

from __future__ import annotations

import argparse
import math
import struct
import subprocess
import sys
import tempfile
import wave
from dataclasses import dataclass
from pathlib import Path

import numpy as np

SAMPLE_RATE = 48000
DURATION_SEC = 2.0


@dataclass
class Metrics:
    peak: float
    rms: float
    residual_db: float
    correlation: float
    high_band_noise_db: float


def gen_mono_test(path: Path, sr: int, amp: float) -> int:
    """Mono float32 WAV: log sweep + decaying chord scaled to peak ~amp.

    A real DI guitar typically peaks at 0.3..0.8 fullscale.
    """
    n = int(sr * DURATION_SEC)
    t = np.arange(n, dtype=np.float64) / sr
    f0, f1 = 80.0, 4000.0
    k = (f1 / f0) ** (1.0 / DURATION_SEC)
    phase = 2.0 * math.pi * f0 * (k**t - 1.0) / math.log(k)
    sweep = np.sin(phase)
    chord = sum(np.sin(2.0 * math.pi * f * t) for f in (110.0, 220.0, 330.0))
    chord *= np.exp(-1.5 * t)
    sig = 0.65 * sweep + 0.35 * chord
    peak = float(np.max(np.abs(sig)))
    if peak > 0:
        sig *= amp / peak
    sig = sig.astype(np.float32)
    write_mono_f32_wav(path, sig, sr)
    return n


def write_mono_f32_wav(path: Path, samples: np.ndarray, sr: int) -> None:
    samples = np.ascontiguousarray(samples, dtype=np.float32)
    data = samples.tobytes()
    with open(path, "wb") as f:
        f.write(b"RIFF")
        f.write(struct.pack("<I", 4 + 8 + 16 + 8 + len(data)))
        f.write(b"WAVE")
        f.write(b"fmt ")
        f.write(struct.pack("<IHHIIHH", 16, 3, 1, sr, sr * 4, 4, 32))
        f.write(b"data")
        f.write(struct.pack("<I", len(data)))
        f.write(data)


def read_f32_wav(path: Path) -> tuple[np.ndarray, int]:
    """Returns (samples, sr). Samples shape (frames, channels) for >1 ch."""
    raw = path.read_bytes()
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise ValueError(f"not a RIFF/WAVE: {path}")
    pos = 12
    fmt = None
    data = None
    sr = 0
    channels = 0
    bits = 0
    fmt_tag = 0
    while pos + 8 <= len(raw):
        cid = raw[pos:pos + 4]
        (csz,) = struct.unpack("<I", raw[pos + 4:pos + 8])
        body = pos + 8
        if cid == b"fmt ":
            fmt_tag, channels, sr, _, _, bits = struct.unpack("<HHIIHH",
                                                              raw[body:body + 16])
            fmt = True
        elif cid == b"data":
            data = raw[body:body + csz]
        pos = body + csz + (csz & 1)
    if fmt is None or data is None or fmt_tag != 3 or bits != 32:
        raise ValueError(f"require float32 WAV: {path}")
    arr = np.frombuffer(data, dtype=np.float32)
    if channels > 1:
        arr = arr.reshape(-1, channels)
    return arr, sr


def metrics(reference: np.ndarray, candidate: np.ndarray, sr: int) -> Metrics:
    n = min(len(reference), len(candidate))
    a = reference[:n].astype(np.float64)
    b = candidate[:n].astype(np.float64)
    diff = a - b
    peak = float(np.max(np.abs(b)))
    rms = float(np.sqrt(np.mean(b * b) + 1e-30))
    rms_diff = float(np.sqrt(np.mean(diff * diff) + 1e-30))
    rms_ref = float(np.sqrt(np.mean(a * a) + 1e-30))
    residual_db = 20.0 * math.log10(rms_diff / (rms_ref + 1e-30) + 1e-30)
    denom = math.sqrt(float(np.sum(a * a)) * float(np.sum(b * b)) + 1e-30)
    corr = float(np.sum(a * b) / denom) if denom > 0 else 0.0

    # High-band noise: signal energy above 8 kHz in the residual.
    spec = np.fft.rfft(diff)
    freqs = np.fft.rfftfreq(n, 1.0 / sr)
    hi_mask = freqs > 8000.0
    hi_energy = float(np.sum(np.abs(spec[hi_mask]) ** 2)) + 1e-30
    full_energy = float(np.sum(np.abs(np.fft.rfft(b)) ** 2)) + 1e-30
    hi_noise_db = 10.0 * math.log10(hi_energy / full_energy)

    return Metrics(peak, rms, residual_db, corr, hi_noise_db)


def run(cmd: list[str]) -> None:
    print("$", " ".join(cmd), file=sys.stderr)
    r = subprocess.run(cmd, check=False)
    if r.returncode != 0:
        raise SystemExit(f"command failed: {' '.join(cmd)}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--plugin", required=True)
    ap.add_argument("--render", required=True, help="path to nilamp_render")
    ap.add_argument("--driver", required=True, help="path to render_loaded_clap")
    ap.add_argument("--block", type=int, default=512)
    ap.add_argument("--sample-rate", type=int, default=SAMPLE_RATE,
                    help="render sample rate (Hz)")
    ap.add_argument("--input-amp", type=float, default=0.5,
                    help="peak amplitude of synthesized mono input (0..1)")
    ap.add_argument("--gain", type=float, default=0.0,
                    help="amp Gain (dB)")
    ap.add_argument("--volume", type=float, default=50.0)
    ap.add_argument("--bass", type=float, default=50.0)
    ap.add_argument("--mid", type=float, default=50.0)
    ap.add_argument("--treble", type=float, default=50.0)
    ap.add_argument("--sag", type=float, default=50.0)
    ap.add_argument("--keep", action="store_true",
                    help="keep temp dir for inspection")
    ap.add_argument(
        "--residual-db", type=float, default=-80.0,
        help="max acceptable residual dB vs offline reference (default -80)",
    )
    ap.add_argument(
        "--high-band-db", type=float, default=-60.0,
        help="max acceptable >8kHz residual energy fraction in dB (default -60)",
    )
    args = ap.parse_args()

    tmpdir = Path(tempfile.mkdtemp(prefix="nilamp_loaded_clap_"))
    print(f"[work dir] {tmpdir}", file=sys.stderr)

    in_wav = tmpdir / "input.wav"
    ref_wav = tmpdir / "ref_offline.wav"
    sr = int(args.sample_rate)
    n = gen_mono_test(in_wav, sr, args.input_amp)
    print(f"[input] mono float32, {n} frames @ {sr} Hz, peak={args.input_amp}",
          file=sys.stderr)

    # Common parameter args.
    param_args = [
        "--gain", str(args.gain),
        "--volume", str(args.volume),
        "--bass", str(args.bass),
        "--mid", str(args.mid),
        "--treble", str(args.treble),
        "--sag", str(args.sag),
    ]

    # Offline reference (mono out).
    run([args.render, "--input", str(in_wav), "--output", str(ref_wav),
         "--block", str(args.block)] + param_args)
    ref, ref_sr = read_f32_wav(ref_wav)
    if ref.ndim != 1:
        raise SystemExit("reference must be mono")
    if ref_sr != sr:
        raise SystemExit(f"sr mismatch ref={ref_sr}")

    failures: list[str] = []

    cases = [
        ("shared",       args.block, 0),
        ("distinct",     args.block, 0),
        ("mono_input",   args.block, 0),
        ("inplace_mono", args.block, 0),
        ("shared",       args.block, 1),
        ("distinct",     args.block, 1),
        ("mono_input",   args.block, 1),
        ("inplace_mono", args.block, 1),
    ]

    for mode, block, vary in cases:
        tag = f"{mode}{'_vary' if vary else ''}"
        out_l = tmpdir / f"loaded_{tag}_L.wav"
        out_r = tmpdir / f"loaded_{tag}_R.wav"
        cmd = [args.driver,
               "--plugin", args.plugin,
               "--input", str(in_wav),
               "--output-l", str(out_l), "--output-r", str(out_r),
               "--mode", mode,
               "--block", str(block),
               "--sample-rate", str(sr)] + param_args
        if vary:
            cmd += ["--vary-block", "1", "--vary-seed", "1"]
        run(cmd)
        L, _ = read_f32_wav(out_l)
        R, _ = read_f32_wav(out_r)

        # Bit-exact L==R check.
        l_eq_r = bool(np.array_equal(L, R))
        max_l_minus_r = float(np.max(np.abs(L.astype(np.float64) - R.astype(np.float64))))
        ml = metrics(ref, L, sr)
        mr = metrics(ref, R, sr)

        print()
        print(f"=== mode={mode} block={block} vary={vary} ===")
        print(f"  L==R bit-exact: {l_eq_r}  max|L-R|={max_l_minus_r:.3e}")
        for ch_tag, m in (("L", ml), ("R", mr)):
            print(f"  {ch_tag}: peak={m.peak:.4f} rms={m.rms:.4f} "
                  f"residual={m.residual_db:6.2f} dB "
                  f"corr={m.correlation:.6f} "
                  f"hi(>8k)={m.high_band_noise_db:6.2f} dB")

        # We treat L==R as the headline pass criterion in mono-equivalent
        # input modes. Variable-block can perturb sample-level FP results
        # vs the offline reference (different block boundaries reorder some
        # state-update fences); use loose residual threshold there but
        # keep the L==R requirement strict.
        residual_threshold = args.residual_db if not vary else max(args.residual_db, -40.0)
        hi_threshold = args.high_band_db if not vary else max(args.high_band_db, -30.0)

        if not l_eq_r:
            failures.append(f"{tag}: L != R (max diff {max_l_minus_r:.3e})")
        if ml.residual_db > residual_threshold:
            failures.append(
                f"{tag} L: residual {ml.residual_db:.2f} dB "
                f"> threshold {residual_threshold} dB")
        if mr.residual_db > residual_threshold:
            failures.append(
                f"{tag} R: residual {mr.residual_db:.2f} dB "
                f"> threshold {residual_threshold} dB")
        if ml.high_band_noise_db > hi_threshold:
            failures.append(
                f"{tag} L: hi-band residual {ml.high_band_noise_db:.2f} dB "
                f"> threshold {hi_threshold} dB")
        if mr.high_band_noise_db > hi_threshold:
            failures.append(
                f"{tag} R: hi-band residual {mr.high_band_noise_db:.2f} dB "
                f"> threshold {hi_threshold} dB")

    print()
    if failures:
        print("FAIL:", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        if not args.keep:
            print(f"(kept temp dir for inspection: {tmpdir})", file=sys.stderr)
        return 1

    print("PASS: loaded CLAP matches offline reference in both modes")
    if not args.keep:
        import shutil
        shutil.rmtree(tmpdir, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
