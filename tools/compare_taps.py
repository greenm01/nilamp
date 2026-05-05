#!/usr/bin/env python3
"""Compare native 5E3 stage taps against Keller JSFX tap renders."""
from __future__ import annotations

import argparse
import math
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

from abx_compare import (
    EQUALIZABLE_GAIN_MIN_DB,
    Params,
    _align,
    _db,
    _peak,
    _rms,
    _trim_warmup,
    compute_metrics,
    make_preset_wav,
    read_wav_f32,
    render_jsfx as render_public_jsfx,
    scale_wav,
)

REPO_ROOT = Path(__file__).resolve().parent.parent
NATIVE_TAPS_RENDER = REPO_ROOT / "native" / "bin" / "nilamp_taps_render"
JSFX_RENDER = [sys.executable, "-m", "tools.jsfx_render.render_jsfx"]
JSFX_TAP_SELECT_SOURCE = (
    Path.home() / ".config" / "REAPER" / "Effects" / "nilamp_abx" / "twd_dlx_ii_tap_select.jsfx"
)
JSFX_TAP_SELECT_EFFECT = "nilamp_abx/twd_dlx_ii_tap_select"

TAP_NAMES = [
    "v_out",
    "res1_v",
    "res3_v",
    "res4_v",
    "drive_t4",
    "res5_v",
    "res_t5_v",
    "dvs2",
    "dvs3",
    "p2_s",
    "p3_s",
    "drive_t5",
    "post_pp",
    "post_peq3",
    "post_hs3",
    "post_hp5",
]


@dataclass
class TapMetrics:
    name: str
    lag_samples: int
    peak_native: float
    peak_jsfx: float
    rms_residual_db: float
    correlation: float
    gain_native_to_jsfx: float

    def report(self) -> str:
        gain_db = 20.0 * math.log10(abs(self.gain_native_to_jsfx)) if self.gain_native_to_jsfx else -math.inf
        return (
            f"{self.name:<10} lag={self.lag_samples:>4} "
            f"peak native/jsfx={self.peak_native:.4e}/{self.peak_jsfx:.4e} "
            f"resid={self.rms_residual_db:+6.1f} dB "
            f"corr={self.correlation:.6f} "
            f"gain={self.gain_native_to_jsfx:.6f} ({gain_db:+.2f} dB)"
        )


def read_wav_f32_channels(path: Path) -> tuple[list[list[float]], int]:
    data = path.read_bytes()
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"not a WAV: {path}")

    fmt_tag = None
    channels = None
    sr = None
    bits = None
    samples_bytes = b""
    pos = 12
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt_tag = struct.unpack_from("<H", body, 0)[0]
            channels = struct.unpack_from("<H", body, 2)[0]
            sr = struct.unpack_from("<I", body, 4)[0]
            bits = struct.unpack_from("<H", body, 14)[0]
            if fmt_tag == 0xFFFE and len(body) >= 26:
                fmt_tag = struct.unpack_from("<H", body, 24)[0]
        elif cid == b"data":
            samples_bytes = body
        pos += 8 + size + (size & 1)

    if fmt_tag != 3 or bits != 32 or channels is None or channels == 0 or sr is None:
        raise ValueError(
            f"{path}: expected 32-bit float WAV, got tag={fmt_tag} "
            f"channels={channels} bits={bits}"
        )
    frame_count = len(samples_bytes) // (4 * channels)
    floats = struct.unpack(f"<{frame_count * channels}f", samples_bytes[:frame_count * channels * 4])
    out = [[] for _ in range(channels)]
    for frame in range(frame_count):
        base = frame * channels
        for ch in range(channels):
            out[ch].append(floats[base + ch])
    return out, sr


def render_native_taps(input_wav: Path, output_wav: Path, params: Params,
                       renderer: Path) -> None:
    cmd = [
        str(renderer),
        "--input", str(input_wav),
        "--output", str(output_wav),
        "--gain", str(params.gain_db),
        "--volume", str(params.volume_pct),
        "--bass", str(params.bass_pct),
        "--mid", str(params.mid_pct),
        "--treble", str(params.treble_pct),
        "--sag", str(params.sag_pct),
    ]
    subprocess.run(cmd, check=True, capture_output=True)


def render_jsfx_tap(input_wav: Path, output_wav: Path, params: Params,
                    timeout_s: float, tap_index: int) -> None:
    if not JSFX_TAP_SELECT_SOURCE.is_file():
        raise FileNotFoundError(
            f"staged JSFX tap-select harness not found: {JSFX_TAP_SELECT_SOURCE}\n"
            "Run: python3 -m tools.jsfx_render.stage_jsfx"
        )
    if tap_index < 0 or tap_index >= len(TAP_NAMES):
        raise ValueError(f"tap_index out of range: {tap_index}")
    cmd = [
        *JSFX_RENDER,
        str(input_wav),
        str(output_wav),
        "--timeout", str(timeout_s),
        "--effect-name", JSFX_TAP_SELECT_EFFECT,
        "--jsfx-source", str(JSFX_TAP_SELECT_SOURCE),
        "--channels", "1",
        *params.to_jsfx_args(),
        "-s", f"tap={tap_index}",
    ]
    subprocess.run(cmd, check=True, capture_output=True, cwd=REPO_ROOT)


def check_selected_vout_matches_public(input_wav: Path, out_dir: Path, label: str,
                                       params: Params, timeout_s: float,
                                       tap0_wav: Path) -> None:
    public_wav = out_dir / f"{label}_jsfx_public.wav"
    render_public_jsfx(input_wav, public_wav, params, timeout_s)
    render_jsfx_tap(input_wav, tap0_wav, params, timeout_s, 0)

    public, public_sr = read_wav_f32(public_wav)
    tap0, tap0_sr = read_wav_f32(tap0_wav)
    if public_sr != tap0_sr:
        raise RuntimeError(f"sample-rate mismatch public={public_sr} tap0={tap0_sr}")

    metrics = compute_metrics(_trim_warmup(public, public_sr), _trim_warmup(tap0, tap0_sr), public_sr)
    print(
        "tap/public v_out check: "
        f"resid={metrics.rms_residual_db:+.1f} dB "
        f"corr={metrics.correlation:.9f} "
        f"gain={metrics.gain_a_to_b:.9f} ({metrics.gain_a_to_b_db:+.3f} dB)"
    )
    if metrics.correlation < 0.999999 or metrics.rms_residual_db > -80.0:
        raise RuntimeError(
            "tap-select v_out does not match the public JSFX render; "
            "fix the diagnostic harness before trusting per-stage results"
        )


def compute_tap_metrics(name: str, native: Sequence[float], jsfx: Sequence[float], sr: int) -> TapMetrics:
    native_aligned, jsfx_aligned, lag = _align(list(native), list(jsfx), sr)
    diff = [native_aligned[i] - jsfx_aligned[i] for i in range(len(native_aligned))]
    peak_native = _peak(native_aligned)
    peak_jsfx = _peak(jsfx_aligned)
    dot_ab = sum(native_aligned[i] * jsfx_aligned[i] for i in range(len(native_aligned)))
    dot_aa = sum(x * x for x in native_aligned)
    dot_bb = sum(x * x for x in jsfx_aligned)
    corr = dot_ab / math.sqrt(dot_aa * dot_bb) if dot_aa > 0.0 and dot_bb > 0.0 else 0.0
    gain = dot_ab / dot_aa if dot_aa > 0.0 else 0.0
    return TapMetrics(
        name=name,
        lag_samples=lag,
        peak_native=peak_native,
        peak_jsfx=peak_jsfx,
        rms_residual_db=_db(_rms(diff), peak_native),
        correlation=corr,
        gain_native_to_jsfx=gain,
    )


def run(args: argparse.Namespace) -> list[TapMetrics]:
    out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    params = Params(
        gain_db=args.gain,
        volume_pct=args.volume,
        bass_pct=args.bass,
        mid_pct=args.mid,
        treble_pct=args.treble,
        sag_pct=args.sag,
    )

    input_wav = make_preset_wav(args.preset, out_dir) if args.input is None else args.input
    rendered_input = input_wav
    if args.input_scale != 1.0:
        rendered_input = out_dir / f"{args.label}_input_scaled.wav"
        scale_wav(input_wav, rendered_input, args.input_scale)

    native_wav = out_dir / f"{args.label}_native_taps.wav"
    jsfx_tap_wavs = [
        out_dir / f"{args.label}_jsfx_tap_{idx}_{name}.wav"
        for idx, name in enumerate(TAP_NAMES)
    ]
    if not args.keep_outputs:
        for path in (native_wav, out_dir / f"{args.label}_jsfx_public.wav", *jsfx_tap_wavs):
            try:
                path.unlink()
            except FileNotFoundError:
                pass

    render_native_taps(rendered_input, native_wav, params, args.native_taps_render)
    if not args.skip_vout_check:
        check_selected_vout_matches_public(
            rendered_input,
            out_dir,
            args.label,
            params,
            args.jsfx_timeout,
            jsfx_tap_wavs[0],
        )

    native_channels, native_sr = read_wav_f32_channels(native_wav)
    if len(native_channels) < len(TAP_NAMES):
        raise RuntimeError(
            f"tap channel mismatch native={len(native_channels)} expected={len(TAP_NAMES)}"
        )

    results = []
    for idx, name in enumerate(TAP_NAMES):
        if idx != 0 or args.skip_vout_check:
            render_jsfx_tap(rendered_input, jsfx_tap_wavs[idx], params, args.jsfx_timeout, idx)
        jsfx, jsfx_sr = read_wav_f32(jsfx_tap_wavs[idx])
        if native_sr != jsfx_sr:
            raise RuntimeError(f"sample-rate mismatch native={native_sr} jsfx={jsfx_sr}")
        native = _trim_warmup(native_channels[idx], native_sr)
        jsfx = _trim_warmup(jsfx, jsfx_sr)
        results.append(compute_tap_metrics(name, native, jsfx, native_sr))
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, nargs="?")
    parser.add_argument("--preset", choices=["sine", "sweep"])
    parser.add_argument("--out-dir", type=Path, default=Path("/tmp/nilamp_tap_compare"))
    parser.add_argument("--label", default="tap_compare")
    parser.add_argument("--gain", type=float, default=EQUALIZABLE_GAIN_MIN_DB)
    parser.add_argument("--volume", type=float, default=50.0)
    parser.add_argument("--bass", type=float, default=50.0)
    parser.add_argument("--mid", type=float, default=50.0)
    parser.add_argument("--treble", type=float, default=50.0)
    parser.add_argument("--sag", type=float, default=100.0)
    parser.add_argument("--input-scale", type=float, default=1.0)
    parser.add_argument("--jsfx-timeout", type=float, default=60.0)
    parser.add_argument("--native-taps-render", type=Path, default=NATIVE_TAPS_RENDER)
    parser.add_argument("--keep-outputs", action="store_true")
    parser.add_argument("--skip-vout-check", action="store_true",
                        help="Skip the public-JSFX vs selected-v_out harness guard.")
    args = parser.parse_args()

    if args.input is not None and args.preset is not None:
        parser.error("provide either an input WAV or --preset, not both")
    if args.input is None and args.preset is None:
        args.preset = "sine"
    results = run(args)
    print("tap comparison:")
    for result in results:
        print("  " + result.report())

    first_bad = next((r for r in results if r.correlation < 0.99 or r.rms_residual_db > -40.0), None)
    if first_bad:
        print(f"\nfirst suspect tap: {first_bad.name}")
        return 0
    print("\nall taps within diagnostic threshold")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
