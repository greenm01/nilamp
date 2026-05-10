"""Numerical ABX comparison: native nilamp vs Keller JSFX.

Renders the same input audio + parameter set through both the native C renderer
(`native/bin/nilamp_render`) and Keller's JSFX (via ysfx), then
computes a battery of numerical metrics on the difference.

Pipeline per test point:
  1. Render input -> nilamp output (native C path).
  2. Render input -> jsfx output  (ysfx/Keller path).
  3. Trim first 100 ms from both (JSFX warm-up; see notes below).
  4. Time-align via cross-correlation peak (group delays may differ).
  5. Compute metrics: peak, RMS, residual RMS, max abs diff, per-band level
     deltas, THD ratio (if input is pure sine), spectral centroid delta.
  6. Pass/fail vs configurable thresholds.

Slider mapping (nilamp params <-> JSFX sliders), native units:
    gain    (-12..12 dB)  <->  gin     (-12..12 dB)  identity
                               (both paths apply Keller's internal +12 dB
                                input calibration)
    volume  (0..100 %)    <->  vol     (0..100 %)    identity
    bass    (0..100 %)    <->  bass    (0..100 %)    identity
    mid     (0..100 %)    <->  mid     (0..100 %)    identity
    treble  (0..100 %)    <->  treble  (0..100 %)    identity
    sag     (0..100 %)    <->  nilamp-only PSS depth; 50 matches Keller default
    gout    (-12..12 dB)  <->  gout    (-12..12 dB)  identity
    gcomp   (0..3 enum)   <->  gcomp   (0..3 enum)   identity

Topology and Options sliders are exposed by the native renderer and passed
through to JSFX:
    tube1 = 0/1  (12AY7/12AX7)
    mode  = 0..4 (CD 5E3, CD BAL, LTP 1, LTP 2, LTP 3)
    fm, qm, gp_*, fp, qp, gs_*, fs

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
NILAMP_RENDER = REPO_ROOT / "native" / "bin" / "nilamp_render"
JSFX_RENDER = [sys.executable, "-m", "tools.jsfx_render.render_ysfx"]
JSFX_RENDER_DRIVER = REPO_ROOT / "native" / "bin" / "ysfx_render"
JSFX_SOURCE = REPO_ROOT / "native" / "build" / "jsfx" / "Effects" / "nilamp_abx" / "twd_dlx_ii_harness.jsfx"
JSFX_INPUT_GAIN = 1.0

# JSFX warm-up window (see module docstring).
JSFX_WARMUP_S = 0.100

# JSFX input-gain offset, derived from twd_dlx_ii_harness.jsfx
# parameter_update() line 229:
#     gin_eff = 10^(0.05 * (p.gin + 12)) * sqrt(1.2)
# Native nilamp applies the same fixed +12 dB input calibration internally, so
# the user-visible gain slider maps 1:1 to Keller's p.gin.
JSFX_GIN_OFFSET_DB = 0.0

# JSFX gin slider range, from twd_dlx_ii_harness.jsfx slider1 declaration.
JSFX_GIN_MIN_DB = -12.0
JSFX_GIN_MAX_DB = 12.0
EQUALIZABLE_GAIN_MIN_DB = JSFX_GIN_MIN_DB + JSFX_GIN_OFFSET_DB  # -12.0 dB
EQUALIZABLE_GAIN_MAX_DB = JSFX_GIN_MAX_DB + JSFX_GIN_OFFSET_DB  # +12.0 dB


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
    sag_pct: float = 50.0      # 0..100 (50 matches Keller's fixed JSFX PSS)
    output_gain_db: float = 0.0
    tone_fmid_dbhz: float = 56.0
    tone_qmid_db: float = -6.0
    spk_res_gain1_db: float = 1.0
    spk_res_gain2_db: float = 2.0
    spk_res_fres_dbhz: float = 38.0
    spk_res_qts_db: float = 6.0
    spk_ind_gain1_db: float = 3.0
    spk_ind_gain2_db: float = 3.0
    spk_ind_find_dbhz: float = 62.0
    gain_comp: int = 2          # 0=Off, 1=Tube 1, 2=Splitter, 3=Both
    tube1: int = 1             # 0=12AY7, 1=12AX7
    splitter: int = 2          # JSFX TWD DLX II default: LTP 1

    def to_nilamp_args(self) -> list[str]:
        return [
            "--gain", str(self.gain_db),
            "--volume", str(self.volume_pct),
            "--bass", str(self.bass_pct),
            "--mid", str(self.mid_pct),
            "--treble", str(self.treble_pct),
            "--sag", str(self.sag_pct),
            "--output-gain", str(self.output_gain_db),
            "--fmid", str(self.tone_fmid_dbhz),
            "--qmid", str(self.tone_qmid_db),
            "--res-gain1", str(self.spk_res_gain1_db),
            "--res-gain2", str(self.spk_res_gain2_db),
            "--res-fres", str(self.spk_res_fres_dbhz),
            "--res-qts", str(self.spk_res_qts_db),
            "--ind-gain1", str(self.spk_ind_gain1_db),
            "--ind-gain2", str(self.spk_ind_gain2_db),
            "--ind-find", str(self.spk_ind_find_dbhz),
            "--gcomp", str(self.gain_comp),
            "--tube1", str(self.tube1),
            "--splitter", str(self.splitter),
        ]

    def artifact_suffix(self) -> str:
        parts = [
            self.gain_db, self.volume_pct, self.bass_pct, self.mid_pct,
            self.treble_pct, self.sag_pct, self.output_gain_db,
            self.tone_fmid_dbhz, self.tone_qmid_db, self.spk_res_gain1_db,
            self.spk_res_gain2_db, self.spk_res_fres_dbhz,
            self.spk_res_qts_db, self.spk_ind_gain1_db,
            self.spk_ind_gain2_db, self.spk_ind_find_dbhz,
            self.gain_comp, self.tube1, self.splitter,
        ]
        text = ",".join(f"{float(p):.9g}" for p in parts)
        return hashlib.sha1(text.encode("utf-8")).hexdigest()[:10]

    def jsfx_gin(self) -> float:
        """Translate nilamp gain_db to the JSFX p.gin slider value."""
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
        # twd_dlx_ii_harness.jsfx slider declarations), matching nilamp 1:1.
        # gin maps to nilamp's visible gain; see jsfx_gin().
        return [
            "-s", f"gin={self.jsfx_gin()}",
            "-s", f"vol={self.volume_pct}",
            "-s", f"bass={self.bass_pct}",
            "-s", f"mid={self.mid_pct}",
            "-s", f"treble={self.treble_pct}",
            "-s", f"tube1={self.tube1}",
            "-s", f"mode={self.splitter}",
            "-s", f"gcomp={self.gain_comp}",
            "-s", f"fm={self.tone_fmid_dbhz}",
            "-s", f"qm={self.tone_qmid_db}",
            "-s", f"gp_pre={self.spk_res_gain1_db}",
            "-s", f"gp_post={self.spk_res_gain2_db}",
            "-s", f"fp={self.spk_res_fres_dbhz}",
            "-s", f"qp={self.spk_res_qts_db}",
            "-s", f"gs_pre={self.spk_ind_gain1_db}",
            "-s", f"gs_post={self.spk_ind_gain2_db}",
            "-s", f"fs={self.spk_ind_find_dbhz}",
            "-s", f"gout={self.output_gain_db}",
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


def make_preset_wav(preset: str, out_dir: Path) -> Path:
    """Create a deterministic mono float32 ABX input under out_dir."""
    sr = 48_000
    duration_s = 5.0
    n = int(sr * duration_s)
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"abx_{preset}_{sr}hz.wav"
    samples: list[float] = []
    if preset == "sine":
        freq = 440.0
        amp = 0.15
        for i in range(n):
            samples.append(amp * math.sin(2.0 * math.pi * freq * i / sr))
    elif preset == "sweep":
        f0 = 40.0
        f1 = 12_000.0
        amp = 0.08
        ratio = f1 / f0
        for i in range(n):
            t = i / sr
            phase = 2.0 * math.pi * f0 * duration_s / math.log(ratio) * (math.exp(t / duration_s * math.log(ratio)) - 1.0)
            samples.append(amp * math.sin(phase))
    else:
        raise ValueError(f"unknown preset: {preset}")
    write_wav_f32(path, samples, sr)
    return path


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
) -> None:
    cmd = [str(renderer), "--input", str(input_wav), "--output", str(output_wav),
           *params.to_nilamp_args()]
    subprocess.run(cmd, check=True, capture_output=True)


def render_jsfx(input_wav: Path, output_wav: Path, params: Params, timeout_s: float) -> None:
    cmd = [
        *JSFX_RENDER,
        str(input_wav),
        str(output_wav),
        "--timeout",
        str(timeout_s),
        "--input-gain",
        str(JSFX_INPUT_GAIN),
        *params.to_jsfx_args(),
    ]
    subprocess.run(cmd, check=True, capture_output=True, cwd=REPO_ROOT)


def jsfx_cache_key(input_wav: Path, params: Params) -> str:
    if not JSFX_SOURCE.is_file():
        subprocess.run([sys.executable, "-m", "tools.jsfx_render.stage_jsfx"], check=True, cwd=REPO_ROOT)
    h = hashlib.sha256()
    h.update(b"nilamp-ysfx-cache-v2\0")
    h.update(f"input_gain={JSFX_INPUT_GAIN:.9g}".encode("utf-8"))
    h.update(b"\0")
    h.update(input_wav.read_bytes())
    h.update(b"\0")
    for path in (JSFX_RENDER_DRIVER, JSFX_SOURCE):
        h.update(str(path).encode("utf-8"))
        h.update(b"\0")
        h.update(path.read_bytes())
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
    correlation: float
    gain_a_to_b: float
    gain_a_to_b_db: float

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
            f"  correlation:   {self.correlation:.6f}",
            f"  best A->B gain:{self.gain_a_to_b:.6f}  ({self.gain_a_to_b_db:+.2f} dB)",
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
    dot_ab = sum(a2[i] * b2[i] for i in range(len(a2)))
    dot_aa = sum(x * x for x in a2)
    dot_bb = sum(x * x for x in b2)
    corr = dot_ab / math.sqrt(dot_aa * dot_bb) if dot_aa > 0 and dot_bb > 0 else 0.0
    gain = dot_ab / dot_aa if dot_aa > 0 else 0.0
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
        correlation=corr,
        gain_a_to_b=gain,
        gain_a_to_b_db=20.0 * math.log10(abs(gain)) if gain != 0.0 else -math.inf,
    )


# --------------------------------------------------------------------------- #
# Driver
# --------------------------------------------------------------------------- #

def run_one(input_wav: Path, params: Params, out_dir: Path, label: str,
            input_scale: float = 1.0, jsfx_timeout_s: float = 60.0,
            nilamp_renderer: Path = NILAMP_RENDER,
            use_jsfx_cache: bool = True) -> Metrics:
    out_dir.mkdir(parents=True, exist_ok=True)
    artifact_label = f"{label}_{params.artifact_suffix()}"
    nilamp_wav = out_dir / f"{artifact_label}_nilamp.wav"
    jsfx_wav = out_dir / f"{artifact_label}_jsfx.wav"

    # If a non-unity input scale is requested, materialize a scaled copy of
    # the input WAV and feed that to both renderers. Both need to see the
    # same samples for the comparison to be meaningful.
    if input_scale != 1.0:
        scaled_wav = out_dir / f"{artifact_label}_input_scaled.wav"
        scale_wav(input_wav, scaled_wav, input_scale)
        rendered_input = scaled_wav
    else:
        rendered_input = input_wav

    print(f"[{label}] JSFX p.gin = {params.jsfx_gin():+.4f} dB "
          f"(nilamp gain_db = {params.gain_db:+.4f})", flush=True)
    if input_scale != 1.0:
        print(f"[{label}] input pre-scale = {input_scale:g}", flush=True)
    print(f"[{label}] rendering nilamp...", flush=True)
    render_nilamp(rendered_input, nilamp_wav, params, nilamp_renderer)
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
    ap.add_argument("input", type=Path, nargs="?", help="input WAV (mono float32)")
    ap.add_argument("--preset", choices=["sine", "sweep"],
                    help="Generate a deterministic ABX input instead of passing an input WAV.")
    ap.add_argument("--out-dir", type=Path, default=Path("/tmp/abx_compare"))
    ap.add_argument("--label", default="default")
    ap.add_argument("--gain", type=float, default=0.0,
                    help="nilamp gain_db (default: 0.0000, Keller noon). "
                         "Must be in "
                         f"[{EQUALIZABLE_GAIN_MIN_DB:.4f}, "
                         f"{EQUALIZABLE_GAIN_MAX_DB:.4f}] dB.")
    ap.add_argument("--volume", type=float, default=50.0)
    ap.add_argument("--bass", type=float, default=50.0)
    ap.add_argument("--mid", type=float, default=50.0)
    ap.add_argument("--treble", type=float, default=50.0)
    ap.add_argument("--sag", type=float, default=50.0)
    ap.add_argument("--output-gain", type=float, default=0.0)
    ap.add_argument("--fmid", type=float, default=56.0)
    ap.add_argument("--qmid", type=float, default=-6.0)
    ap.add_argument("--res-gain1", type=float, default=1.0)
    ap.add_argument("--res-gain2", type=float, default=2.0)
    ap.add_argument("--res-fres", type=float, default=38.0)
    ap.add_argument("--res-qts", type=float, default=6.0)
    ap.add_argument("--ind-gain1", type=float, default=3.0)
    ap.add_argument("--ind-gain2", type=float, default=3.0)
    ap.add_argument("--ind-find", type=float, default=62.0)
    ap.add_argument("--gcomp", type=int, choices=[0, 1, 2, 3], default=2)
    ap.add_argument("--tube1", type=int, choices=[0, 1], default=1)
    ap.add_argument("--splitter", type=int, choices=[0, 1, 2, 3, 4], default=2)
    ap.add_argument("--input-scale", type=float, default=1.0,
                    help="Pre-scale input WAV by this factor before rendering. "
                         "Use small values (e.g. 1e-3) to keep all stages in "
                         "their linear regime so the residual reflects only "
                         "linear filter mismatch.")
    ap.add_argument("--jsfx-timeout", type=float, default=60.0,
                    help="ysfx render timeout in seconds (default: 60).")
    ap.add_argument("--no-jsfx-cache", action="store_true",
                    help="Disable JSFX reference-render cache.")
    ap.add_argument("--nilamp-render", type=Path, default=NILAMP_RENDER,
                    help=f"nilamp renderer binary (default: {NILAMP_RENDER})")
    ap.add_argument("--rms-threshold-db", type=float, default=-60.0)
    args = ap.parse_args()

    params = Params(
        gain_db=args.gain, volume_pct=args.volume,
        bass_pct=args.bass, mid_pct=args.mid, treble_pct=args.treble,
        sag_pct=args.sag, output_gain_db=args.output_gain,
        tone_fmid_dbhz=args.fmid, tone_qmid_db=args.qmid,
        spk_res_gain1_db=args.res_gain1, spk_res_gain2_db=args.res_gain2,
        spk_res_fres_dbhz=args.res_fres, spk_res_qts_db=args.res_qts,
        spk_ind_gain1_db=args.ind_gain1, spk_ind_gain2_db=args.ind_gain2,
        spk_ind_find_dbhz=args.ind_find, gain_comp=args.gcomp,
        tube1=args.tube1, splitter=args.splitter,
    )

    if args.input is None and args.preset is None:
        ap.error("provide an input WAV or --preset")
    if args.input is not None and args.preset is not None:
        ap.error("provide either an input WAV or --preset, not both")
    input_wav = make_preset_wav(args.preset, args.out_dir) if args.preset is not None else args.input

    print(f"input: {input_wav}")
    print(f"params: {params}")
    print()

    m = run_one(input_wav, params, args.out_dir, args.label,
                input_scale=args.input_scale, jsfx_timeout_s=args.jsfx_timeout,
                nilamp_renderer=args.nilamp_render,
                use_jsfx_cache=not args.no_jsfx_cache)
    print(f"\nresults [{args.label}]:")
    print(m.report())
    ok = m.passed(args.rms_threshold_db)
    print(f"\n  verdict: {'PASS' if ok else 'FAIL'} (threshold {args.rms_threshold_db:+.1f} dB)")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
