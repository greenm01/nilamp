#!/usr/bin/env python3
"""Validate nilamp's native VST3 shell without launching a DAW."""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
TEST_VST3_LOAD = REPO_ROOT / "native" / "bin" / (
    "test_vst3_load.exe" if sys.platform == "win32" else "test_vst3_load"
)


def run_vst3_loader(plugin: Path) -> None:
    subprocess.run([str(TEST_VST3_LOAD), str(plugin)], check=True)


def run_optional_validator(plugin: Path) -> None:
    validator = shutil.which("validator") or shutil.which("vst3validator")
    if validator is None:
        print("VST3 validator not found; skipping external VST3 validation")
        return

    commands = [
        [validator, str(plugin)],
        [validator, "validate", str(plugin)],
    ]
    last_proc: subprocess.CompletedProcess[str] | None = None
    for command in commands:
        proc = subprocess.run(
            command,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        last_proc = proc
        if proc.returncode == 0:
            if proc.stdout:
                print(proc.stdout, end="")
            return
    assert last_proc is not None
    if last_proc.stdout:
        print(last_proc.stdout, end="")
    raise RuntimeError(f"VST3 validator failed with exit code {last_proc.returncode}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plugin", default=REPO_ROOT / "native/bin/nilamp-twd-mkii.vst3", type=Path)
    parser.add_argument("--skip-validator", action="store_true")
    args = parser.parse_args(argv)

    plugin = args.plugin.resolve()
    if not plugin.is_dir():
        raise FileNotFoundError(f"VST3 bundle not found: {plugin}")
    if not TEST_VST3_LOAD.is_file():
        raise FileNotFoundError(f"native VST3 loader not found: {TEST_VST3_LOAD}")

    run_vst3_loader(plugin)
    if not args.skip_validator:
        run_optional_validator(plugin)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
