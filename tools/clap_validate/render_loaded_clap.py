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


def gen_mono_test(path: Path) -> int:
    """Mono float32 WAV: log sweep + decaying chord, peak ~0.15.

    Defaults make the amp gentle, so output stays well below clip and the
    sanitizer in the CLAP wrapper is a no-op.
    """
    n = int(SAMPLE_RATE * DURATION_SEC)
    t = np.arange(n, dtype=np.float64) / SAMPLE_RATE
    f0, f1 = 80.0, 4000.0
    k = (f1 / f0) ** (1.0 / DURATION_SEC)
    phase = 2.0 * math.pi * f0 * (k**t - 1.0) / math.log(k)
    sweep = 0.10 * np.sin(phase)
    chord = sum(np.sin(2.0 * math.pi * f * t) for f in (110.0, 220.0, 330.0))
    chord *= 0.05 * np.exp(-1.5 * t)
    sig = (sweep + chord).astype(np.float32)

    write_mono_f32_wav(path, sig, SAMPLE_RATE)
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
    n = gen_mono_test(in_wav)
    print(f"[input] mono float32, {n} frames @ {SAMPLE_RATE} Hz", file=sys.stderr)

    # Offline reference (mono out).
    run([args.render, "--input", str(in_wav), "--output", str(ref_wav),
         "--block", str(args.block)])
    ref, ref_sr = read_f32_wav(ref_wav)
    if ref.ndim != 1:
        raise SystemExit("reference must be mono")
    if ref_sr != SAMPLE_RATE:
        raise SystemExit(f"sr mismatch ref={ref_sr}")

    failures: list[str] = []

    for mode in ("shared", "distinct"):
        out_l = tmpdir / f"loaded_{mode}_L.wav"
        out_r = tmpdir / f"loaded_{mode}_R.wav"
        run([args.driver,
             "--plugin", args.plugin,
             "--input", str(in_wav),
             "--output-l", str(out_l), "--output-r", str(out_r),
             "--mode", mode,
             "--block", str(args.block),
             "--sample-rate", str(SAMPLE_RATE)])
        L, _ = read_f32_wav(out_l)
        R, _ = read_f32_wav(out_r)

        # Bit-exact check.
        l_eq_r = bool(np.array_equal(L, R))
        max_l_minus_r = float(np.max(np.abs(L.astype(np.float64) - R.astype(np.float64))))
        ml = metrics(ref, L, SAMPLE_RATE)
        mr = metrics(ref, R, SAMPLE_RATE)

        print()
        print(f"=== mode={mode} block={args.block} ===")
        print(f"  L==R bit-exact: {l_eq_r}  max|L-R|={max_l_minus_r:.3e}")
        for tag, m in (("L", ml), ("R", mr)):
            print(f"  {tag}: peak={m.peak:.4f} rms={m.rms:.4f} "
                  f"residual={m.residual_db:6.2f} dB "
                  f"corr={m.correlation:.6f} "
                  f"hi(>8k)={m.high_band_noise_db:6.2f} dB")

        if mode == "shared" and not l_eq_r:
            failures.append(f"{mode}: L != R (max diff {max_l_minus_r:.3e})")
        if ml.residual_db > args.residual_db:
            failures.append(
                f"{mode} L: residual {ml.residual_db:.2f} dB "
                f"> threshold {args.residual_db} dB")
        if mr.residual_db > args.residual_db:
            failures.append(
                f"{mode} R: residual {mr.residual_db:.2f} dB "
                f"> threshold {args.residual_db} dB")
        if ml.high_band_noise_db > args.high_band_db:
            failures.append(
                f"{mode} L: hi-band residual {ml.high_band_noise_db:.2f} dB "
                f"> threshold {args.high_band_db} dB")
        if mr.high_band_noise_db > args.high_band_db:
            failures.append(
                f"{mode} R: hi-band residual {mr.high_band_noise_db:.2f} dB "
                f"> threshold {args.high_band_db} dB")

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
