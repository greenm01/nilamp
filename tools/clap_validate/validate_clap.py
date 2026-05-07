#!/usr/bin/env python3
"""Validate nilamp's native CLAP shell without launching a DAW."""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
EXE = ".exe" if sys.platform == "win32" else ""
TEST_CLAP_LOAD = REPO_ROOT / "native" / "bin" / f"test_clap_load{EXE}"


def run_clap_loader(plugin: Path) -> None:
    subprocess.run([str(TEST_CLAP_LOAD), str(plugin)], check=True)


def run_optional_clap_validator(plugin: Path) -> None:
    validator = os.environ.get("CLAP_VALIDATOR") or shutil.which("clap-validator")
    if validator is None and sys.platform == "win32":
        local_validator = Path("C:/src/clap-validator/target/release/clap-validator.exe")
        if local_validator.is_file():
            validator = str(local_validator)
    if validator is None:
        print("clap-validator not found; skipping external CLAP validation")
        return

    proc = subprocess.run(
        [validator, "validate", str(plugin)],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.returncode != 0:
        raise RuntimeError(f"clap-validator failed with exit code {proc.returncode}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plugin", default=REPO_ROOT / "native/bin/nilamp-twd-mkii.clap", type=Path)
    parser.add_argument("--skip-clap-validator", action="store_true")
    args = parser.parse_args(argv)

    plugin = args.plugin.resolve()
    if not plugin.exists():
        raise FileNotFoundError(f"plugin not found: {plugin}")
    if not TEST_CLAP_LOAD.is_file():
        raise FileNotFoundError(f"native CLAP loader not found: {TEST_CLAP_LOAD}")

    run_clap_loader(plugin)
    if not args.skip_clap_validator:
        run_optional_clap_validator(plugin)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
