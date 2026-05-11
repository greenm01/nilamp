#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Sample REAPER process CPU while the nilamp REAPER perf Lua script runs."""
from __future__ import annotations

import argparse
import csv
import json
import os
import statistics
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def default_marker_path() -> Path:
    temp = os.environ.get("TMPDIR") or os.environ.get("TEMP") or os.environ.get("TMP") or "/tmp"
    return Path(temp) / "nilamp_reaper_perf_markers.jsonl"


def run_ps(args: list[str]) -> str:
    return subprocess.check_output(["ps", *args], text=True, stderr=subprocess.DEVNULL)


def process_exists(pid: int) -> bool:
    try:
        run_ps(["-p", str(pid), "-o", "pid="])
        return True
    except subprocess.CalledProcessError:
        return False


def resolve_process(process_name: str, pid: int) -> int:
    if pid > 0:
        if not process_exists(pid):
            raise RuntimeError(f"process {pid} is not running")
        return pid

    needle = process_name.lower()
    matches: list[tuple[int, str]] = []
    for line in run_ps(["-axo", "pid=,comm="]).splitlines():
        parts = line.strip().split(None, 1)
        if len(parts) != 2:
            continue
        try:
            candidate_pid = int(parts[0])
        except ValueError:
            continue
        command = parts[1]
        basename = Path(command).name.lower()
        if basename == needle or needle in command.lower():
            matches.append((candidate_pid, command))

    if not matches:
        raise RuntimeError(f"no process matching {process_name!r} is running")
    if len(matches) > 1:
        found = ", ".join(f"{p}:{c}" for p, c in matches)
        raise RuntimeError(f"multiple processes match {process_name!r}: {found}; pass --pid")
    return matches[0][0]


def sample_process(pid: int, logical_processors: int) -> dict[str, Any]:
    out = run_ps(["-p", str(pid), "-o", "pid=,pcpu=,rss="]).strip()
    if not out:
        raise RuntimeError(f"process {pid} exited")
    parts = out.split()
    if len(parts) < 3:
        raise RuntimeError(f"could not parse ps output: {out!r}")
    one_core = float(parts[1])
    rss_kb = int(parts[2])
    return {
        "cpu_pct_one_core": one_core,
        "cpu_pct_total_capacity": one_core / max(1, logical_processors),
        "working_set_bytes": rss_kb * 1024,
    }


def read_new_markers(path: Path, line_count: int) -> tuple[list[dict[str, Any]], int]:
    if not path.exists():
        return [], line_count
    lines = path.read_text(encoding="utf-8").splitlines()
    markers: list[dict[str, Any]] = []
    for line in lines[line_count:]:
        if not line.strip():
            continue
        try:
            markers.append(json.loads(line))
        except json.JSONDecodeError:
            print(f"warning: ignoring malformed marker line: {line}", file=sys.stderr)
    return markers, len(lines)


def percentile(values: list[float], pct: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    rank = (pct / 100.0) * (len(ordered) - 1)
    lo = int(rank)
    hi = min(lo + 1, len(ordered) - 1)
    weight = rank - lo
    return ordered[lo] * (1.0 - weight) + ordered[hi] * weight


def mean(values: list[float]) -> float | None:
    return statistics.fmean(values) if values else None


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    fieldnames: list[str] = []
    for row in rows:
        for key in row:
            if key not in fieldnames:
                fieldnames.append(key)
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def summarize(samples: list[dict[str, Any]], markers: list[dict[str, Any]],
              warmup_trim_s: float, cooldown_trim_s: float) -> list[dict[str, Any]]:
    starts: dict[tuple[str, int], dict[str, Any]] = {}
    intervals: list[dict[str, Any]] = []
    for marker in markers:
        key = (str(marker.get("scenario", "")), int(marker.get("run") or 0))
        if marker.get("event") == "start":
            starts[key] = marker
        elif marker.get("event") == "end" and key in starts:
            start = starts.pop(key)
            intervals.append({
                "scenario": marker.get("scenario", ""),
                "surface": marker.get("surface", ""),
                "editor": marker.get("editor", ""),
                "run": int(marker.get("run") or 0),
                "fx_name": marker.get("fx_name", ""),
                "start_elapsed_s": start["observed_elapsed_s"],
                "end_elapsed_s": marker["observed_elapsed_s"],
            })

    rows: list[dict[str, Any]] = []
    aggregate_values: dict[str, dict[str, Any]] = {}
    for interval in intervals:
        start_s = float(interval["start_elapsed_s"]) + warmup_trim_s
        end_s = float(interval["end_elapsed_s"]) - cooldown_trim_s
        window = [s for s in samples if start_s <= float(s["elapsed_s"]) <= end_s]
        total_values = [float(s["cpu_pct_total_capacity"]) for s in window]
        one_core_values = [float(s["cpu_pct_one_core"]) for s in window]
        row = {
            "row_type": "run",
            **interval,
            "samples": len(window),
            "warmup_trim_s": warmup_trim_s,
            "cooldown_trim_s": cooldown_trim_s,
            "mean_cpu_pct_total_capacity": mean(total_values),
            "median_cpu_pct_total_capacity": percentile(total_values, 50),
            "p95_cpu_pct_total_capacity": percentile(total_values, 95),
            "max_cpu_pct_total_capacity": max(total_values) if total_values else None,
            "mean_cpu_pct_one_core": mean(one_core_values),
            "median_cpu_pct_one_core": percentile(one_core_values, 50),
            "p95_cpu_pct_one_core": percentile(one_core_values, 95),
            "max_cpu_pct_one_core": max(one_core_values) if one_core_values else None,
        }
        rows.append(row)

        scenario = str(interval["scenario"])
        agg = aggregate_values.setdefault(scenario, {
            "surface": interval["surface"],
            "editor": interval["editor"],
            "fx_name": interval["fx_name"],
            "values": [],
            "one_core_values": [],
        })
        agg["values"].extend(total_values)
        agg["one_core_values"].extend(one_core_values)

    for scenario in sorted(aggregate_values):
        agg = aggregate_values[scenario]
        values = agg["values"]
        one_core_values = agg["one_core_values"]
        rows.append({
            "row_type": "aggregate",
            "scenario": scenario,
            "surface": agg["surface"],
            "editor": agg["editor"],
            "run": 0,
            "fx_name": agg["fx_name"],
            "samples": len(values),
            "start_elapsed_s": "",
            "end_elapsed_s": "",
            "warmup_trim_s": warmup_trim_s,
            "cooldown_trim_s": cooldown_trim_s,
            "mean_cpu_pct_total_capacity": mean(values),
            "median_cpu_pct_total_capacity": percentile(values, 50),
            "p95_cpu_pct_total_capacity": percentile(values, 95),
            "max_cpu_pct_total_capacity": max(values) if values else None,
            "mean_cpu_pct_one_core": mean(one_core_values),
            "median_cpu_pct_one_core": percentile(one_core_values, 50),
            "p95_cpu_pct_one_core": percentile(one_core_values, 95),
            "max_cpu_pct_one_core": max(one_core_values) if one_core_values else None,
        })
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--process-name", default="REAPER")
    ap.add_argument("--pid", type=int, default=0)
    ap.add_argument("--marker-path", type=Path, default=default_marker_path())
    ap.add_argument("--out-dir", type=Path, default=Path("dist/reaper-perf"))
    ap.add_argument("--poll-ms", type=int, default=250)
    ap.add_argument("--timeout-seconds", type=float, default=900.0)
    ap.add_argument("--warmup-trim-seconds", type=float, default=2.0)
    ap.add_argument("--cooldown-trim-seconds", type=float, default=1.0)
    ap.add_argument("--keep-existing-markers", action="store_true")
    args = ap.parse_args()

    if args.poll_ms <= 0:
        ap.error("--poll-ms must be positive")
    args.out_dir.mkdir(parents=True, exist_ok=True)
    if not args.keep_existing_markers:
        args.marker_path.unlink(missing_ok=True)
    args.marker_path.parent.mkdir(parents=True, exist_ok=True)
    args.marker_path.touch(exist_ok=True)

    pid = resolve_process(args.process_name, args.pid)
    logical_processors = os.cpu_count() or 1
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    raw_csv = args.out_dir / f"reaper-cpu-samples-{stamp}.csv"
    raw_json = args.out_dir / f"reaper-cpu-samples-{stamp}.json"
    marker_json = args.out_dir / f"reaper-markers-{stamp}.json"
    summary_csv = args.out_dir / f"reaper-cpu-summary-{stamp}.csv"
    summary_json = args.out_dir / f"reaper-cpu-summary-{stamp}.json"

    print(f"Sampling REAPER process {pid} every {args.poll_ms} ms.")
    print(f"Marker file: {args.marker_path}")
    print("Run tools/reaper_perf/run_reaper_perf_scenarios.lua inside REAPER now.")

    samples: list[dict[str, Any]] = []
    markers: list[dict[str, Any]] = []
    start_mono = time.monotonic()
    line_count = 0
    done = False
    while not done:
        time.sleep(args.poll_ms / 1000.0)
        elapsed_s = time.monotonic() - start_mono
        if elapsed_s > args.timeout_seconds:
            raise RuntimeError(
                f"timed out after {args.timeout_seconds:g} seconds waiting for done marker"
            )

        sample = sample_process(pid, logical_processors)
        sample.update({
            "time_utc": datetime.now(timezone.utc).isoformat(),
            "elapsed_s": elapsed_s,
            "process_id": pid,
            "logical_processors": logical_processors,
        })
        samples.append(sample)

        new_markers, line_count = read_new_markers(args.marker_path, line_count)
        for marker in new_markers:
            marker["observed_utc"] = sample["time_utc"]
            marker["observed_elapsed_s"] = elapsed_s
            markers.append(marker)
            if marker.get("event") == "done":
                done = True

    summary = summarize(samples, markers, args.warmup_trim_seconds, args.cooldown_trim_seconds)
    write_csv(raw_csv, samples)
    raw_json.write_text(json.dumps(samples, indent=2), encoding="utf-8")
    marker_json.write_text(json.dumps(markers, indent=2), encoding="utf-8")
    write_csv(summary_csv, summary)
    summary_json.write_text(json.dumps(summary, indent=2), encoding="utf-8")

    print()
    print("Aggregate scenario summary, CPU normalized to total logical CPU capacity:")
    for row in summary:
        if row["row_type"] != "aggregate":
            continue
        median = row["median_cpu_pct_total_capacity"]
        p95 = row["p95_cpu_pct_total_capacity"]
        median_s = f"{median:.3f}%" if median is not None else "n/a"
        p95_s = f"{p95:.3f}%" if p95 is not None else "n/a"
        print(f"{row['scenario']:<28} samples={row['samples']:<4} "
              f"median={median_s} p95={p95_s}")
    print("Wrote:")
    for path in (summary_csv, summary_json, raw_csv, raw_json, marker_json):
        print(f"  {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
