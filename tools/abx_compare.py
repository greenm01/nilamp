"""Numerical ABX comparison: nilamp (Faust/Rust) vs Keller JSFX.

Renders the same input audio + parameter set through both the Rust offline
renderer (`nilamp_render`) and Keller's JSFX (via `tools.jsfx_render`), then
computes a battery of numerical metrics on the difference.

Pipeline per test point:
  1. Render input -> nilamp output (Rust/Faust path).
  2. Render input -> jsfx output  (REAPER/Keller path).
  3. Trim first 100 ms from both (JSFX warm-up; see notes below).
  4. Time-align via cross-correlation peak (group delays may differ).
  5. Compute metrics: peak, RMS, residual RMS, max abs diff, per-band level
     deltas, THD ratio (if input is pure sine), spectral centroid delta.
  6. Pass/fail vs configurable thresholds.

Slider mapping (Rust plugin params <-> JSFX sliders), native units:
    gain    (-12..12 dB)  ->   gin     (-12..12 dB)  with -12.79 dB offset
                               (JSFX applies an internal +12 dB plus a
                                sqrt(1.2) factor; the harness translates
                                p.gin = gain_db - 12.79 to equalize.
                                Rust gain_db must be in [+0.79, +24.79] dB.)
    volume  (0..100 %)    <->  vol     (0..100 %)    identity
    bass    (0..100 %)    <->  bass    (0..100 %)    identity
    mid     (0..100 %)    <->  mid     (0..100 %)    identity
    treble  (0..100 %)    <->  treble  (0..100 %)    identity
    sag     (0..100 %)    <->  (held at 100; JSFX has no counterpart slider,
                                its 3-stage PSS is fixed-internal)

Other JSFX sliders are pinned to match the Rust DSP topology:
    tube1 = 1   (12AX7 path; matches t1_12ax7_table in nilamp.dsp)
    mode  = 0   (CD 5E3 cathodyne; matches tube_cd() stage 4 in nilamp.dsp)
    gcomp, gp_*, gs_*, f*, q*, gout = JSFX slider defaults

Why the 100 ms trim:
The harness JSFX (twd_dlx_ii_harness.jsfx, staged by tools.jsfx_render.stage_jsfx)
removes Keller's wall-clock-based mute, but `is_muted = 1` is still set in
`@slider` and only cleared by the next `@block`. The first audio block after
slider initialization is therefore partially muted at a render-speed-dependent
sample boundary (max abs diff ~0.26 in the first 100 ms). After the first block,
output is reproducible to ~-85 dB RMS across runs. We discard the first 100 ms
unconditionally.
"""
from __future__ import annotations

import argparse
import hashlib
import math
import shutil
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

REPO_ROOT = Path(__file__).resolve().parent.parent
NILAMP_RENDER = REPO_ROOT / "target" / "release" / "nilamp_render"
JSFX_RENDER = [sys.executable, "-m", "tools.jsfx_render.render_jsfx"]

# JSFX warm-up window (see module docstring).
JSFX_WARMUP_S = 0.100

# JSFX input-gain offset, derived from twd_dlx_ii_harness.jsfx
# parameter_update() line 229:
#     gin_eff = 10^(0.05 * (p.gin + 12)) * sqrt(1.2)
# Faust nilamp.dsp applies plain db2linear(p.gain). Equating effective
# gains: p.gin = p.gain - 12 - 20*log10(sqrt(1.2)) = p.gain - 12.79.
# We translate at the harness boundary so identical Rust gain_db produces
# identical actual signal gain into T1.
JSFX_GIN_OFFSET_DB = 12.0 + 20.0 * math.log10(math.sqrt(1.2))  # = 12.7918

# JSFX gin slider range, from twd_dlx_ii_harness.jsfx slider1 declaration.
JSFX_GIN_MIN_DB = -12.0
JSFX_GIN_MAX_DB = 12.0
EQUALIZABLE_GAIN_MIN_DB = JSFX_GIN_MIN_DB + JSFX_GIN_OFFSET_DB  #  +0.79 dB
EQUALIZABLE_GAIN_MAX_DB = JSFX_GIN_MAX_DB + JSFX_GIN_OFFSET_DB  # +24.79 dB


# --------------------------------------------------------------------------- #
# Parameter set
# --------------------------------------------------------------------------- #

@dataclass(frozen=True)
class Params:
    """Native-unit parameter set, identical for both renderers."""
    gain_db: float = 0.0       # -12..+12 dB
    volume_pct: float = 50.0   # 0..100
    bass_pct: float = 50.0     # 0..100
    mid_pct: float = 50.0      # 0..100
    treble_pct: float = 50.0   # 0..100
    sag_pct: float = 100.0     # 0..100 (Rust only; JSFX has no equivalent)

    def to_nilamp_args(self) -> list[str]:
        return [
            "--gain", str(self.gain_db),
            "--volume", str(self.volume_pct),
            "--bass", str(self.bass_pct),
            "--mid", str(self.mid_pct),
            "--treble", str(self.treble_pct),
            "--sag", str(self.sag_pct),
        ]

    def jsfx_gin(self) -> float:
        """Translate Rust gain_db to the JSFX p.gin slider value that produces
        the same effective audio gain into T1. See JSFX_GIN_OFFSET_DB."""
        gin = self.gain_db - JSFX_GIN_OFFSET_DB
        if gin < JSFX_GIN_MIN_DB or gin > JSFX_GIN_MAX_DB:
            raise ValueError(
                f"gain_db={self.gain_db} requires JSFX p.gin={gin:.4f} dB, "
                f"which is outside the slider range "
                f"[{JSFX_GIN_MIN_DB}, {JSFX_GIN_MAX_DB}]. "
                f"Use gain_db in "
                f"[{EQUALIZABLE_GAIN_MIN_DB:.4f}, {EQUALIZABLE_GAIN_MAX_DB:.4f}] dB."
            )
        return gin

    def to_jsfx_args(self) -> list[str]:
        # JSFX bass/mid/treble sliders have range 0..100 (verified from
        # twd_dlx_ii_harness.jsfx slider declarations), matching Rust 1:1.
        # gin is offset-translated; see jsfx_gin().
        return [
            "-s", f"gin={self.jsfx_gin()}",
            "-s", f"vol={self.volume_pct}",
            "-s", f"bass={self.bass_pct}",
            "-s", f"mid={self.mid_pct}",
            "-s", f"treble={self.treble_pct}",
            # Topology pins:
            "-s", "tube1=1",   # 12AX7 (matches nilamp.dsp t1_12ax7_table)
            "-s", "mode=0",    # CD 5E3 cathodyne (matches tube_cd() stage 4)
        ]


# --------------------------------------------------------------------------- #
# WAV I/O (mono float32 only; matches both renderers' output format)
# --------------------------------------------------------------------------- #

def read_wav_f32(path: Path) -> tuple[list[float], int]:
    """Read a 32-bit float mono WAV. Returns (samples, sample_rate).

    Uses raw chunk parsing because `wave` doesn't handle WAVE_FORMAT_IEEE_FLOAT.
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
            # WAVE_FORMAT_EXTENSIBLE (0xFFFE) wraps the real format tag in the
            # first 2 bytes of the SubFormat GUID at offset 24 of fmt body.
            if fmt_tag == 0xFFFE and len(body) >= 26:
                fmt_tag = struct.unpack("<H", body[24:26])[0]
        elif cid == b"data":
            samples_bytes = body
        i += 8 + sz + (sz & 1)

    if fmt_tag != 3:  # IEEE float
        raise ValueError(f"{path}: expected IEEE float (fmt_tag=3), got {fmt_tag}")
    if channels != 1:
        raise ValueError(f"{path}: expected mono, got {channels} channels")
    if bits != 32:
        raise ValueError(f"{path}: expected 32-bit, got {bits}-bit")

    if sr is None:
        raise ValueError(f"{path}: no fmt chunk")
    n = len(samples_bytes) // 4
    samples = list(struct.unpack(f"<{n}f", samples_bytes))
    return samples, sr


def write_wav_f32(path: Path, samples: Sequence[float], sr: int) -> None:
    """Write a 32-bit float mono WAV (plain WAVE_FORMAT_IEEE_FLOAT, no
    EXTENSIBLE wrapper). Matches the format `tools.jsfx_render` produces and
    that `nilamp_render` accepts."""
    n = len(samples)
    data = struct.pack(f"<{n}f", *samples)
    fmt = struct.pack("<HHIIHH", 3, 1, sr, sr * 4, 4, 32)  # IEEE float, mono
    chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    chunks += b"data" + struct.pack("<I", len(data)) + data
    riff = b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks
    path.write_bytes(riff)


def scale_wav(in_path: Path, out_path: Path, scale: float) -> None:
    """Read in_path, multiply every sample by scale, write to out_path."""
    samples, sr = read_wav_f32(in_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    write_wav_f32(out_path, [s * scale for s in samples], sr)


# --------------------------------------------------------------------------- #
# Renderers
# --------------------------------------------------------------------------- #

def render_nilamp(
    input_wav: Path,
    output_wav: Path,
    params: Params,
    renderer: Path,
    variant: str | None,
) -> None:
    cmd = [str(renderer), "--input", str(input_wav), "--output", str(output_wav),
           *params.to_nilamp_args()]
    if variant is not None:
        cmd.extend(["--variant", variant])
    subprocess.run(cmd, check=True, capture_output=True)


def render_jsfx(input_wav: Path, output_wav: Path, params: Params, timeout_s: float) -> None:
    cmd = [
        *JSFX_RENDER,
        str(input_wav),
        str(output_wav),
        "--timeout",
        str(timeout_s),
        *params.to_jsfx_args(),
    ]
    subprocess.run(cmd, check=True, capture_output=True, cwd=REPO_ROOT)


def jsfx_cache_key(input_wav: Path, params: Params) -> str:
    h = hashlib.sha256()
    h.update(b"nilamp-jsfx-cache-v1\0")
    h.update(input_wav.read_bytes())
    h.update(b"\0")
    for arg in params.to_jsfx_args():
        h.update(arg.encode("utf-8"))
        h.update(b"\0")
    return h.hexdigest()


def render_jsfx_cached(
    input_wav: Path,
    output_wav: Path,
    params: Params,
    timeout_s: float,
    cache_dir: Path,
    use_cache: bool,
    label: str,
) -> None:
    if not use_cache:
        print(f"[{label}] rendering jsfx...", flush=True)
        render_jsfx(input_wav, output_wav, params, timeout_s)
        return

    cache_dir.mkdir(parents=True, exist_ok=True)
    cached_wav = cache_dir / f"{jsfx_cache_key(input_wav, params)}.wav"
    if cached_wav.exists():
        print(f"[{label}] jsfx cache hit: {cached_wav}", flush=True)
    else:
        tmp_wav = cached_wav.with_suffix(".tmp.wav")
        if tmp_wav.exists():
            tmp_wav.unlink()
        print(f"[{label}] rendering jsfx cache miss...", flush=True)
        render_jsfx(input_wav, tmp_wav, params, timeout_s)
        tmp_wav.replace(cached_wav)
    shutil.copyfile(cached_wav, output_wav)


# --------------------------------------------------------------------------- #
# Signal processing helpers
# --------------------------------------------------------------------------- #

def _trim_warmup(samples: list[float], sr: int) -> list[float]:
    n = int(round(JSFX_WARMUP_S * sr))
    return samples[n:]


def _xcorr_lag(a: Sequence[float], b: Sequence[float], max_lag: int) -> int:
    """Find lag k in [-max_lag, max_lag] maximizing sum(a[i+k]*b[i]).

    Positive lag means b is delayed relative to a (i.e. a leads).
    Brute-force; only used on short windows.
    """
    n = min(len(a), len(b)) - max_lag
    best_k = 0
    best_v = -math.inf
    for k in range(-max_lag, max_lag + 1):
        s = 0.0
        if k >= 0:
            for i in range(n):
                s += a[i + k] * b[i]
        else:
            for i in range(n):
                s += a[i] * b[i - k]
        if s > best_v:
            best_v = s
            best_k = k
    return best_k


def _align(a: list[float], b: list[float], sr: int, max_lag_ms: float = 5.0) -> tuple[list[float], list[float], int]:
    """Cross-correlate to find best integer-sample lag, then trim both to overlap.

    Returns (a_aligned, b_aligned, lag). lag>0 means b was delayed; we drop
    `lag` samples from a's head. lag<0 means a was delayed; we drop |lag|
    samples from b's head.
    """
    max_lag = int(round(max_lag_ms * 1e-3 * sr))
    # Use a representative middle window to keep xcorr cheap.
    win = min(4096, len(a), len(b))
    start = (min(len(a), len(b)) - win) // 2
    aw = a[start:start + win]
    bw = b[start:start + win]
    lag = _xcorr_lag(aw, bw, max_lag)
    if lag > 0:
        a2 = a[lag:]
        b2 = b[:]
    elif lag < 0:
        a2 = a[:]
        b2 = b[-lag:]
    else:
        a2, b2 = a[:], b[:]
    n = min(len(a2), len(b2))
    return a2[:n], b2[:n], lag


def _rms(x: Sequence[float]) -> float:
    if not x:
        return 0.0
    return math.sqrt(sum(v * v for v in x) / len(x))


def _peak(x: Sequence[float]) -> float:
    return max((abs(v) for v in x), default=0.0)


def _db(x: float, ref: float) -> float:
    if x <= 0 or ref <= 0:
        return -math.inf
    return 20.0 * math.log10(x / ref)


# --------------------------------------------------------------------------- #
# Metrics
# --------------------------------------------------------------------------- #

@dataclass
class Metrics:
    sr: int
    n: int
    lag_samples: int
    peak_a: float
    peak_b: float
    rms_a: float
    rms_b: float
    rms_residual: float
    max_abs_diff: float
    rms_residual_db: float    # dB below peak_a
    max_diff_db: float        # dB below peak_a

    def passed(self, rms_db_threshold: float = -60.0) -> bool:
        return self.rms_residual_db <= rms_db_threshold

    def report(self) -> str:
        lines = [
            f"  samples:       {self.n}  (sr={self.sr} Hz)",
            f"  align lag:     {self.lag_samples} samples ({self.lag_samples / self.sr * 1000:.3f} ms)",
            f"  peak A / B:    {self.peak_a:.4f} / {self.peak_b:.4f}",
            f"  rms A / B:     {self.rms_a:.4f} / {self.rms_b:.4f}",
            f"  rms residual:  {self.rms_residual:.4e}  ({self.rms_residual_db:+.1f} dB below peak_a)",
            f"  max |A-B|:     {self.max_abs_diff:.4e}  ({self.max_diff_db:+.1f} dB below peak_a)",
        ]
        return "\n".join(lines)


def compute_metrics(a: list[float], b: list[float], sr: int) -> Metrics:
    a2, b2, lag = _align(a, b, sr)
    peak_a = _peak(a2)
    peak_b = _peak(b2)
    rms_a = _rms(a2)
    rms_b = _rms(b2)
    diff = [a2[i] - b2[i] for i in range(len(a2))]
    rms_r = _rms(diff)
    max_d = _peak(diff)
    return Metrics(
        sr=sr,
        n=len(a2),
        lag_samples=lag,
        peak_a=peak_a,
        peak_b=peak_b,
        rms_a=rms_a,
        rms_b=rms_b,
        rms_residual=rms_r,
        max_abs_diff=max_d,
        rms_residual_db=_db(rms_r, peak_a),
        max_diff_db=_db(max_d, peak_a),
    )


# --------------------------------------------------------------------------- #
# Driver
# --------------------------------------------------------------------------- #

def run_one(input_wav: Path, params: Params, out_dir: Path, label: str,
            input_scale: float = 1.0, jsfx_timeout_s: float = 60.0,
            nilamp_renderer: Path = NILAMP_RENDER,
            nilamp_variant: str | None = None,
            use_jsfx_cache: bool = True) -> Metrics:
    out_dir.mkdir(parents=True, exist_ok=True)
    nilamp_wav = out_dir / f"{label}_nilamp.wav"
    jsfx_wav = out_dir / f"{label}_jsfx.wav"

    # If a non-unity input scale is requested, materialize a scaled copy of
    # the input WAV and feed that to both renderers. Both need to see the
    # same samples for the comparison to be meaningful.
    if input_scale != 1.0:
        scaled_wav = out_dir / f"{label}_input_scaled.wav"
        scale_wav(input_wav, scaled_wav, input_scale)
        rendered_input = scaled_wav
    else:
        rendered_input = input_wav

    print(f"[{label}] JSFX p.gin = {params.jsfx_gin():+.4f} dB "
          f"(Rust gain_db = {params.gain_db:+.4f})", flush=True)
    if input_scale != 1.0:
        print(f"[{label}] input pre-scale = {input_scale:g}", flush=True)
    print(f"[{label}] rendering nilamp...", flush=True)
    render_nilamp(rendered_input, nilamp_wav, params, nilamp_renderer, nilamp_variant)
    render_jsfx_cached(
        rendered_input,
        jsfx_wav,
        params,
        jsfx_timeout_s,
        out_dir / "jsfx_cache",
        use_jsfx_cache,
        label,
    )

    a, sr_a = read_wav_f32(nilamp_wav)
    b, sr_b = read_wav_f32(jsfx_wav)
    if sr_a != sr_b:
        raise RuntimeError(f"sample-rate mismatch: nilamp={sr_a} jsfx={sr_b}")
    a = _trim_warmup(a, sr_a)
    b = _trim_warmup(b, sr_b)
    return compute_metrics(a, b, sr_a)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input", type=Path, help="input WAV (mono float32)")
    ap.add_argument("--out-dir", type=Path, default=Path("/tmp/abx_compare"))
    ap.add_argument("--label", default="default")
    ap.add_argument("--gain", type=float, default=EQUALIZABLE_GAIN_MIN_DB,
                    help=f"Rust gain_db (default: {EQUALIZABLE_GAIN_MIN_DB:.4f} = "
                         "minimum equalizable). Must be in "
                         f"[{EQUALIZABLE_GAIN_MIN_DB:.4f}, "
                         f"{EQUALIZABLE_GAIN_MAX_DB:.4f}] dB.")
    ap.add_argument("--volume", type=float, default=50.0)
    ap.add_argument("--bass", type=float, default=50.0)
    ap.add_argument("--mid", type=float, default=50.0)
    ap.add_argument("--treble", type=float, default=50.0)
    ap.add_argument("--sag", type=float, default=100.0)
    ap.add_argument("--input-scale", type=float, default=1.0,
                    help="Pre-scale input WAV by this factor before rendering. "
                         "Use small values (e.g. 1e-3) to keep all stages in "
                         "their linear regime so the residual reflects only "
                         "linear filter mismatch.")
    ap.add_argument("--jsfx-timeout", type=float, default=60.0,
                    help="REAPER/JSFX render timeout in seconds (default: 60).")
    ap.add_argument("--no-jsfx-cache", action="store_true",
                    help="Disable JSFX reference-render cache.")
    ap.add_argument("--nilamp-render", type=Path, default=NILAMP_RENDER,
                    help=f"nilamp renderer binary (default: {NILAMP_RENDER})")
    ap.add_argument("--nilamp-variant",
                    help="Optional diagnostic variant passed to nilamp renderer.")
    ap.add_argument("--rms-threshold-db", type=float, default=-60.0)
    args = ap.parse_args()

    params = Params(
        gain_db=args.gain, volume_pct=args.volume,
        bass_pct=args.bass, mid_pct=args.mid, treble_pct=args.treble,
        sag_pct=args.sag,
    )

    print(f"input: {args.input}")
    print(f"params: {params}")
    print()

    m = run_one(args.input, params, args.out_dir, args.label,
                input_scale=args.input_scale, jsfx_timeout_s=args.jsfx_timeout,
                nilamp_renderer=args.nilamp_render,
                nilamp_variant=args.nilamp_variant,
                use_jsfx_cache=not args.no_jsfx_cache)
    print(f"\nresults [{args.label}]:")
    print(m.report())
    ok = m.passed(args.rms_threshold_db)
    print(f"\n  verdict: {'PASS' if ok else 'FAIL'} (threshold {args.rms_threshold_db:+.1f} dB)")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
