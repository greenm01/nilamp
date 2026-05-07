#!/usr/bin/env python3
"""Run a focused Keller JSFX parity matrix for native nilamp.

This is broader than the public `make native-jsfx-test` gate but still small
enough for a local pre-commit check after DSP or parameter-mapping changes.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from abx_compare import Metrics, Params, make_preset_wav, run_one


@dataclass(frozen=True)
class MatrixCase:
    name: str
    preset: str
    params: Params
    threshold_db: float


def cases() -> list[MatrixCase]:
    base = Params()
    return [
        MatrixCase("noon_ltp1_sine", "sine", base, -16.0),
        MatrixCase("noon_ltp1_sweep", "sweep", base, -11.2),
        MatrixCase("tube1_12ay7_ltp1", "sine", Params(tube1=0), -16.0),
        MatrixCase("splitter_cd_5e3", "sine", Params(splitter=0), -16.0),
        MatrixCase("splitter_cd_bal", "sine", Params(splitter=1), -16.0),
        MatrixCase("splitter_ltp1", "sine", Params(splitter=2), -16.0),
        MatrixCase("splitter_ltp2", "sine", Params(splitter=3), -16.0),
        MatrixCase("splitter_ltp3", "sine", Params(splitter=4), -16.0),
        MatrixCase("ltp3_gcomp_off", "sine", Params(splitter=4, gain_comp=0), -16.0),
        MatrixCase("ltp3_gcomp_tube1", "sine", Params(splitter=4, gain_comp=1), -16.0),
        MatrixCase("ltp3_gcomp_splitter", "sine", Params(splitter=4, gain_comp=2), -16.0),
        MatrixCase("ltp3_gcomp_both", "sine", Params(splitter=4, gain_comp=3), -16.0),
        MatrixCase("output_gain_plus3", "sine", Params(output_gain_db=3.0), -16.0),
        MatrixCase(
            "tone_options_probe",
            "sine",
            Params(tone_fmid_dbhz=62.0, tone_qmid_db=-3.0),
            -16.0,
        ),
        MatrixCase(
            "speaker_options_probe",
            "sine",
            Params(
                spk_res_gain1_db=4.0,
                spk_res_gain2_db=5.0,
                spk_res_fres_dbhz=42.0,
                spk_res_qts_db=9.0,
                spk_ind_gain1_db=6.0,
                spk_ind_gain2_db=4.0,
                spk_ind_find_dbhz=66.0,
            ),
            -16.0,
        ),
    ]


def verdict(case: MatrixCase, metrics: Metrics) -> tuple[str, bool]:
    if metrics.passed(case.threshold_db):
        return "PASS", True
    return "FAIL", False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path, default=Path("/tmp/nilamp_parity_matrix"))
    parser.add_argument("--jsfx-timeout", type=float, default=60.0)
    parser.add_argument("--no-jsfx-cache", action="store_true")
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    input_by_preset = {
        preset: make_preset_wav(preset, args.out_dir)
        for preset in sorted({case.preset for case in cases()})
    }

    results: list[tuple[MatrixCase, str, Metrics | None]] = []
    for case in cases():
        print(f"\n== {case.name} ==", flush=True)
        try:
            metrics = run_one(
                input_by_preset[case.preset],
                case.params,
                args.out_dir,
                case.name,
                jsfx_timeout_s=args.jsfx_timeout,
                use_jsfx_cache=not args.no_jsfx_cache,
            )
        except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
            print(f"ERROR: {exc}", flush=True)
            results.append((case, "ERROR", None))
            continue
        status, _ = verdict(case, metrics)
        print(metrics.report())
        print(f"verdict: {status} (threshold {case.threshold_db:+.1f} dB)")
        results.append((case, status, metrics))

    print("\nsummary:")
    unexpected_failures = 0
    for case, status, metrics in results:
        resid = "n/a" if metrics is None else f"{metrics.rms_residual_db:+.1f} dB"
        print(f"  {status:<11} {case.name:<32} {resid}")
        if status != "PASS":
            unexpected_failures += 1

    return 1 if unexpected_failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
