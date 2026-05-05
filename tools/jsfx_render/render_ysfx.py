"""Render audio through Keller's TWD DLX II JSFX via ysfx.

This is the default headless JSFX renderer for nilamp parity work. It loads the
staged harness JSFX directly with ysfx and writes 32-bit float WAV output.
"""
from __future__ import annotations

import os
import subprocess
from pathlib import Path
from typing import Mapping

from tools.jsfx_render.render_jsfx import SliderInfo, slider_info

REPO_ROOT = Path(__file__).resolve().parents[2]
YSFX_RENDER = REPO_ROOT / "native" / "bin" / "ysfx_render"
DEFAULT_JSFX_ROOT = REPO_ROOT / "native" / "build" / "jsfx"
DEFAULT_IMPORT_ROOT = DEFAULT_JSFX_ROOT / "Effects"
DEFAULT_JSFX_SOURCE = DEFAULT_IMPORT_ROOT / "nilamp_abx" / "twd_dlx_ii_harness.jsfx"
DEFAULT_EFFECT_NAME = "nilamp_abx/twd_dlx_ii_harness"
REAPER_MONO_INPUT_GAIN = 0.5


def _default_source_for_effect(effect_name: str) -> Path:
    rel = effect_name
    if rel.startswith("JS:"):
        rel = rel[3:].strip()
    if rel.endswith(".jsfx"):
        return DEFAULT_IMPORT_ROOT / rel
    return DEFAULT_IMPORT_ROOT / f"{rel}.jsfx"


def _import_root_for_source(jsfx_source: Path) -> Path:
    source = jsfx_source.resolve()
    if source.parent.parent.name == "Effects":
        return source.parent.parent
    return source.parent


def _ensure_default_stage(jsfx_source: Path) -> None:
    default_root = DEFAULT_JSFX_ROOT.resolve()
    try:
        source_is_default = jsfx_source.resolve().is_relative_to(default_root)
    except FileNotFoundError:
        source_is_default = default_root in jsfx_source.resolve().parents
    if source_is_default and not jsfx_source.exists():
        from tools.jsfx_render.stage_jsfx import stage

        stage(DEFAULT_IMPORT_ROOT / "nilamp_abx")


def render(
    *,
    input_wav: str | os.PathLike[str],
    output_wav: str | os.PathLike[str],
    sliders: Mapping[str, float] | None = None,
    sample_rate: int = 48000,
    timeout_s: float = 60.0,
    effect_name: str = DEFAULT_EFFECT_NAME,
    jsfx_source: str | os.PathLike[str] | None = None,
    channels: int = 1,
    ysfx_render_bin: str | os.PathLike[str] = YSFX_RENDER,
    input_gain: float = REAPER_MONO_INPUT_GAIN,
) -> Path:
    """Render `input_wav` through a staged JSFX harness."""
    del sample_rate
    input_path = Path(input_wav).resolve()
    output_path = Path(output_wav).resolve()
    source_path = Path(jsfx_source).resolve() if jsfx_source is not None else _default_source_for_effect(effect_name)
    render_bin = Path(ysfx_render_bin).resolve()

    if not render_bin.is_file():
        raise FileNotFoundError(f"ysfx renderer not found: {render_bin}. Run: make native/bin/ysfx_render")
    if not input_path.is_file():
        raise FileNotFoundError(f"input wav not found: {input_path}")
    if channels < 1:
        raise ValueError("channels must be >= 1")
    _ensure_default_stage(source_path)
    if not source_path.is_file():
        raise FileNotFoundError(f"JSFX source not found at {source_path}")
    output_path.parent.mkdir(parents=True, exist_ok=True)

    info: dict[str, SliderInfo] = slider_info(source_path)
    requested = dict(sliders or {})
    unknown = set(requested) - set(info)
    if unknown:
        raise ValueError(f"Unknown slider(s): {sorted(unknown)}. Known: {sorted(info)}")

    pairs: list[tuple[int, float]] = []
    for name, value in requested.items():
        si = info[name]
        value_f = float(value)
        if not (si.minimum <= value_f <= si.maximum):
            raise ValueError(f"slider '{name}' = {value} out of range [{si.minimum}, {si.maximum}]")
        pairs.append((si.index, value_f))
    pairs.sort()

    try:
        output_path.unlink()
    except FileNotFoundError:
        pass

    cmd = [
        str(render_bin),
        "--input",
        str(input_path),
        "--output",
        str(output_path),
        "--effect",
        str(source_path),
        "--import-root",
        str(_import_root_for_source(source_path)),
        "--channels",
        str(channels),
        "--input-gain",
        str(input_gain),
    ]
    for idx, value in pairs:
        cmd.extend(["--slider", f"{idx}={value}"])
    subprocess.run(cmd, check=True, capture_output=True, timeout=timeout_s, cwd=REPO_ROOT)
    if not output_path.exists() or output_path.stat().st_size == 0:
        raise RuntimeError(f"ysfx render produced no output at {output_path}")
    return output_path


def _cli() -> int:
    import argparse

    ap = argparse.ArgumentParser(description="Render audio through Keller TWD DLX II JSFX using ysfx.")
    ap.add_argument("input_wav", nargs="?")
    ap.add_argument("output_wav", nargs="?")
    ap.add_argument("--slider", "-s", action="append", default=[], help="NAME=VALUE; repeatable.")
    ap.add_argument("--sample-rate", type=int, default=48000)
    ap.add_argument("--timeout", type=float, default=60.0)
    ap.add_argument("--effect-name", default=DEFAULT_EFFECT_NAME)
    ap.add_argument("--jsfx-source", type=Path)
    ap.add_argument("--channels", type=int, default=1)
    ap.add_argument("--ysfx-render-bin", type=Path, default=YSFX_RENDER)
    ap.add_argument("--input-gain", type=float, default=REAPER_MONO_INPUT_GAIN,
                    help="Input gain before JSFX processing (default: 0.5, matching the old REAPER mono harness).")
    ap.add_argument("--list-sliders", action="store_true")
    args = ap.parse_args()

    jsfx_source = args.jsfx_source or _default_source_for_effect(args.effect_name)
    _ensure_default_stage(jsfx_source)
    if args.list_sliders:
        for name, si in sorted(slider_info(jsfx_source).items(), key=lambda kv: kv[1].index):
            print(f"  {si.index:>2}  {name:<10} default={si.default:<6} range=[{si.minimum}, {si.maximum}]  {si.label}")
        return 0

    if not args.input_wav or not args.output_wav:
        ap.error("input_wav and output_wav are required (unless --list-sliders)")

    sliders: dict[str, float] = {}
    for spec in args.slider:
        if "=" not in spec:
            raise SystemExit(f"bad --slider spec (need NAME=VALUE): {spec!r}")
        name, value = spec.split("=", 1)
        sliders[name.strip()] = float(value)

    out = render(
        input_wav=args.input_wav,
        output_wav=args.output_wav,
        sliders=sliders,
        sample_rate=args.sample_rate,
        timeout_s=args.timeout,
        effect_name=args.effect_name,
        jsfx_source=jsfx_source,
        channels=args.channels,
        ysfx_render_bin=args.ysfx_render_bin,
        input_gain=args.input_gain,
    )
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(_cli())
