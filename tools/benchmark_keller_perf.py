#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Benchmark nilamp native plugin runtime against Keller JSFX under ysfx.

The headline comparison is steady-state in-memory audio processing after the
effect/plugin has already been loaded and warmed. Lifecycle and reload phases
are reported separately so ysfx JIT/startup cost stays visible without being
mixed into runtime processing numbers.
"""
from __future__ import annotations

import argparse
import json
import math
import statistics
import struct
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

REPO_ROOT = Path(__file__).resolve().parent.parent
NATIVE_BIN = REPO_ROOT / "native" / "bin"
DEFAULT_OUT_DIR = Path("/tmp/nilamp_perf")
DEFAULT_JSFX_ROOT = REPO_ROOT / "native" / "build" / "jsfx"
DEFAULT_IMPORT_ROOT = DEFAULT_JSFX_ROOT / "Effects"
DEFAULT_JSFX = DEFAULT_IMPORT_ROOT / "nilamp_abx" / "twd_dlx_ii_harness.jsfx"
DEFAULT_CLAP = NATIVE_BIN / "nilamp-twd-mkii.clap"
DEFAULT_VST3 = NATIVE_BIN / "nilamp-twd-mkii.vst3"

SURFACE_DRIVERS = {
    "keller_ysfx": NATIVE_BIN / "bench_ysfx_perf",
    "nilamp_clap": NATIVE_BIN / "bench_clap_perf",
    "nilamp_vst3": NATIVE_BIN / "bench_vst3_perf",
}

DEFAULT_SURFACES = ("keller_ysfx", "nilamp_clap", "nilamp_vst3")
DEFAULT_PHASES = ("steady_plugin_process", "plugin_lifecycle", "reload")
DEFAULT_INPUTS = ("sine", "sweep", "silence", "di_like")
DEFAULT_BLOCKS = (32, 64, 128, 512)


@dataclass(frozen=True)
class Case:
    name: str
    params: dict[str, float | int] = field(default_factory=dict)
    input_scale: float = 1.0


DEFAULT_CASES = (
    Case("keller_default"),
    Case("low_input_linear", input_scale=1e-3),
    Case("hot_input_nonlinear", input_scale=2.0),
    Case("splitter_cd_5e3", params={"splitter": 0}),
)


def split_csv(value: str) -> list[str]:
    return [part.strip() for part in value.split(",") if part.strip()]


def parse_blocks(value: str) -> list[int]:
    blocks = [int(part) for part in split_csv(value)]
    if not blocks or any(block <= 0 for block in blocks):
        raise argparse.ArgumentTypeError("blocks must be positive integers")
    return blocks


def ensure_stage() -> None:
    if not DEFAULT_JSFX.is_file():
        subprocess.run([sys.executable, "-m", "tools.jsfx_render.stage_jsfx"],
                       check=True, cwd=REPO_ROOT)


def require_file(path: Path, label: str) -> None:
    if not path.exists():
        raise FileNotFoundError(f"{label} not found: {path}")


def params_to_args(case: Case) -> list[str]:
    args = ["--input-scale", str(case.input_scale)]
    name_map = {
        "gain": "--gain",
        "volume": "--volume",
        "bass": "--bass",
        "mid": "--mid",
        "treble": "--treble",
        "sag": "--sag",
        "output_gain": "--output-gain",
        "fmid": "--fmid",
        "qmid": "--qmid",
        "res_gain1": "--res-gain1",
        "res_gain2": "--res-gain2",
        "res_fres": "--res-fres",
        "res_qts": "--res-qts",
        "ind_gain1": "--ind-gain1",
        "ind_gain2": "--ind-gain2",
        "ind_find": "--ind-find",
        "gcomp": "--gcomp",
        "tube1": "--tube1",
        "splitter": "--splitter",
    }
    for key, value in case.params.items():
        args.extend([name_map[key], str(value)])
    return args


def surface_args(surface: str, args: argparse.Namespace) -> list[str]:
    if surface == "keller_ysfx":
        return [
            "--effect", str(args.jsfx_source),
            "--import-root", str(args.import_root),
            "--ysfx-input-gain", str(args.ysfx_input_gain),
        ]
    if surface == "nilamp_clap":
        return ["--plugin", str(args.clap_plugin)]
    if surface == "nilamp_vst3":
        return ["--plugin", str(args.vst3_plugin)]
    raise ValueError(f"unknown surface: {surface}")


def run_driver(cmd: list[str]) -> list[dict[str, object]]:
    proc = subprocess.run(cmd, cwd=REPO_ROOT, text=True, capture_output=True)
    if proc.returncode != 0:
        if proc.stdout:
            print(proc.stdout, file=sys.stderr, end="")
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        raise subprocess.CalledProcessError(proc.returncode, cmd)
    records: list[dict[str, object]] = []
    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError as exc:
            raise RuntimeError(f"driver emitted non-JSON line: {line}") from exc
    return records


def read_raw_f32(path: Path) -> list[float]:
    data = path.read_bytes()
    if len(data) % 4 != 0:
        raise ValueError(f"raw float file size is not divisible by 4: {path}")
    return list(struct.unpack(f"<{len(data) // 4}f", data))


def rms(values: Iterable[float]) -> float:
    values_list = list(values)
    if not values_list:
        return 0.0
    return math.sqrt(sum(value * value for value in values_list) / len(values_list))


def residual_vs_reference(reference_raw: Path, candidate_raw: Path,
                          sample_rate: int, warmup_s: float = 0.100) -> dict[str, float]:
    ref = read_raw_f32(reference_raw)
    cand = read_raw_f32(candidate_raw)
    trim = min(int(round(sample_rate * warmup_s)), len(ref), len(cand))
    ref = ref[trim:]
    cand = cand[trim:]
    n = min(len(ref), len(cand))
    if n == 0:
        return {"residual_rms": 0.0, "residual_db_vs_keller_peak": 0.0}
    ref = ref[:n]
    cand = cand[:n]
    diff_rms = rms(ref[i] - cand[i] for i in range(n))
    ref_peak = max((abs(x) for x in ref), default=0.0)
    residual_db = -math.inf if diff_rms == 0.0 else (
        20.0 * math.log10(diff_rms / ref_peak) if ref_peak > 0.0 else 0.0
    )
    return {
        "residual_rms": diff_rms,
        "residual_db_vs_keller_peak": residual_db,
    }


def mean(records: list[dict[str, object]], key: str) -> float:
    values = [float(record[key]) for record in records if key in record]
    return statistics.fmean(values) if values else 0.0


def pformat(value: float, digits: int = 2) -> str:
    if math.isinf(value):
        return "-inf" if value < 0 else "+inf"
    return f"{value:.{digits}f}"


def summarize(records: list[dict[str, object]], phases: list[str]) -> None:
    print()
    print("Headline: steady-state plugin processing")
    print("surface       input    case                 block  realtime  ns/frame  cpu%    residual dB")
    print("------------  -------  -------------------  -----  --------  --------  ------  -----------")
    groups: dict[tuple[str, str, str, int], list[dict[str, object]]] = {}
    for record in records:
        if record.get("phase") != "steady_plugin_process":
            continue
        key = (
            str(record["surface"]),
            str(record["input"]),
            str(record["case"]),
            int(record["block"]),
        )
        groups.setdefault(key, []).append(record)
    for key in sorted(groups):
        surface, input_name, case_name, block = key
        rows = groups[key]
        residual_values = [
            float(row["residual_db_vs_keller_peak"])
            for row in rows
            if "residual_db_vs_keller_peak" in row
        ]
        residual = statistics.fmean(residual_values) if residual_values else math.nan
        residual_text = "" if math.isnan(residual) else pformat(residual, 1)
        print(f"{surface:<12}  {input_name:<7}  {case_name:<19}  {block:>5}  "
              f"{mean(rows, 'realtime_factor'):>8.2f}  {mean(rows, 'ns_per_frame'):>8.1f}  "
              f"{mean(rows, 'cpu_pct'):>6.1f}  {residual_text:>11}")

    for phase in phases:
        if phase == "steady_plugin_process":
            continue
        phase_rows = [record for record in records if record.get("phase") == phase]
        if not phase_rows:
            continue
        print()
        print(phase.replace("_", " ").title())
        print("surface       input    case                 block  wall ms   cpu%    max RSS KB")
        print("------------  -------  -------------------  -----  --------  ------  ----------")
        phase_groups: dict[tuple[str, str, str, int], list[dict[str, object]]] = {}
        for record in phase_rows:
            key = (
                str(record["surface"]),
                str(record["input"]),
                str(record["case"]),
                int(record["block"]),
            )
            phase_groups.setdefault(key, []).append(record)
        for key in sorted(phase_groups):
            surface, input_name, case_name, block = key
            rows = phase_groups[key]
            print(f"{surface:<12}  {input_name:<7}  {case_name:<19}  {block:>5}  "
                  f"{mean(rows, 'wall_s') * 1000.0:>8.2f}  "
                  f"{mean(rows, 'cpu_pct'):>6.1f}  {mean(rows, 'max_rss_kb'):>10.0f}")


def selected_cases(quick: bool) -> tuple[Case, ...]:
    if quick:
        return (DEFAULT_CASES[0],)
    return DEFAULT_CASES


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--warmups", type=int, default=1)
    ap.add_argument("--duration", type=float, default=8.0)
    ap.add_argument("--sample-rate", type=int, default=48000)
    ap.add_argument("--blocks", type=parse_blocks,
                    default=list(DEFAULT_BLOCKS),
                    help="comma-separated block sizes")
    ap.add_argument("--inputs", default=",".join(DEFAULT_INPUTS),
                    help="comma-separated inputs")
    ap.add_argument("--phases", default=",".join(DEFAULT_PHASES),
                    help="comma-separated phases")
    ap.add_argument("--surfaces", default=",".join(DEFAULT_SURFACES),
                    help="comma-separated surfaces")
    ap.add_argument("--quick", action="store_true",
                    help="run only sine/default/block=64 for smoke testing")
    ap.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    ap.add_argument("--json-out", type=Path)
    ap.add_argument("--jsfx-source", type=Path, default=DEFAULT_JSFX)
    ap.add_argument("--import-root", type=Path, default=DEFAULT_IMPORT_ROOT)
    ap.add_argument("--ysfx-input-gain", type=float, default=0.5)
    ap.add_argument("--clap-plugin", type=Path, default=DEFAULT_CLAP)
    ap.add_argument("--vst3-plugin", type=Path, default=DEFAULT_VST3)
    args = ap.parse_args()

    if args.runs <= 0:
        ap.error("--runs must be positive")
    if args.warmups < 0:
        ap.error("--warmups must be non-negative")
    if args.duration <= 0.0:
        ap.error("--duration must be positive")

    phases = split_csv(args.phases)
    surfaces = split_csv(args.surfaces)
    inputs = ["sine"] if args.quick else split_csv(args.inputs)
    blocks = [64] if args.quick else args.blocks
    cases = selected_cases(args.quick)

    unknown_surfaces = set(surfaces) - set(SURFACE_DRIVERS)
    if unknown_surfaces:
        ap.error(f"unknown surfaces: {sorted(unknown_surfaces)}")
    unknown_phases = set(phases) - set(DEFAULT_PHASES)
    if unknown_phases:
        ap.error(f"unknown phases: {sorted(unknown_phases)}")

    ensure_stage()
    require_file(args.jsfx_source, "staged Keller JSFX")
    if "keller_ysfx" in surfaces:
        require_file(SURFACE_DRIVERS["keller_ysfx"], "ysfx benchmark driver")
    if "nilamp_clap" in surfaces:
        require_file(SURFACE_DRIVERS["nilamp_clap"], "CLAP benchmark driver")
        require_file(args.clap_plugin, "CLAP plugin")
    if "nilamp_vst3" in surfaces:
        require_file(SURFACE_DRIVERS["nilamp_vst3"], "VST3 benchmark driver")
        require_file(args.vst3_plugin, "VST3 plugin")

    args.out_dir.mkdir(parents=True, exist_ok=True)
    raw_dir = args.out_dir / "raw"
    raw_dir.mkdir(parents=True, exist_ok=True)

    all_records: list[dict[str, object]] = []
    total = len(phases) * len(inputs) * len(blocks) * len(cases) * len(surfaces)
    done = 0

    for phase in phases:
        for input_name in inputs:
            for block in blocks:
                for case in cases:
                    raw_paths: dict[str, Path] = {}
                    for surface in surfaces:
                        done += 1
                        driver = SURFACE_DRIVERS[surface]
                        raw_path = raw_dir / (
                            f"{phase}_{surface}_{input_name}_{case.name}_b{block}.f32"
                        )
                        if phase != "steady_plugin_process":
                            raw_path = Path()
                        cmd = [
                            str(driver),
                            "--phase", phase,
                            "--input-kind", input_name,
                            "--sample-rate", str(args.sample_rate),
                            "--duration", str(args.duration),
                            "--block", str(block),
                            "--runs", str(args.runs),
                            "--warmups", str(args.warmups),
                            *surface_args(surface, args),
                            *params_to_args(case),
                        ]
                        if raw_path:
                            cmd.extend(["--output-raw", str(raw_path)])
                        print(f"[{done}/{total}] {surface} {phase} "
                              f"{input_name}/{case.name}/b{block}",
                              file=sys.stderr, flush=True)
                        records = run_driver(cmd)
                        for record in records:
                            record["case"] = case.name
                            record["input_scale"] = case.input_scale
                            record["driver"] = str(driver)
                        all_records.extend(records)
                        if raw_path:
                            raw_paths[surface] = raw_path

                    if phase == "steady_plugin_process" and "keller_ysfx" in raw_paths:
                        reference = raw_paths["keller_ysfx"]
                        for surface, raw_path in raw_paths.items():
                            if surface == "keller_ysfx":
                                continue
                            residual = residual_vs_reference(reference, raw_path,
                                                             args.sample_rate)
                            for record in all_records:
                                if (record.get("phase") == phase and
                                        record.get("surface") == surface and
                                        record.get("input") == input_name and
                                        record.get("case") == case.name and
                                        int(record.get("block", -1)) == block):
                                    record.update(residual)

    summarize(all_records, phases)

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps({
            "schema": "nilamp-keller-perf-v1",
            "records": all_records,
        }, indent=2, sort_keys=True) + "\n")
        print(f"\nwrote {args.json_out}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
