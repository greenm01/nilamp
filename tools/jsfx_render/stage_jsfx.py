"""Stage Keller's JSFX bundle into REAPER's Effects directory for harness use.

Copies vendor/keller-jsfx into ~/.config/REAPER/Effects/nilamp_abx/, with two
variants of the main amp:

  - twd_dlx_ii.jsfx           : verbatim vendor file (for manual GUI use).
  - twd_dlx_ii_harness.jsfx   : with wall-clock mute disabled for deterministic
                                offline rendering. Used by the ABX harness.

The harness variant patches one line:

    time_precise() > t_unmute ? is_muted = 0;
  ->
    is_muted = 0;  // harness: removed wall-clock dependency

Rationale: the original JSFX gates its output for ~100 ms after any slider
change, using `time_precise()` to schedule the un-mute. In offline render the
wall-clock advances at variable speed relative to the audio, so the un-mute
fires at nondeterministic sample positions, which (combined with the amp's
nonlinear PSS feedback) produces non-bit-exact output across runs of the same
project. Removing the mute makes renders deterministic without altering the
DSP path itself.

Usage:
    python -m tools.jsfx_render.stage_jsfx [--dest DIR]
"""
from __future__ import annotations

import argparse
import shutil
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
VENDOR_DIR = REPO_ROOT / "vendor" / "keller-jsfx"
DEFAULT_DEST = Path.home() / ".config" / "REAPER" / "Effects" / "nilamp_abx"

# Vendor source filename for the main amp.
VENDOR_AMP = "TWD DLX  II.jsfx"   # note: two spaces, as in upstream
STAGED_AMP = "twd_dlx_ii.jsfx"
HARNESS_AMP = "twd_dlx_ii_harness.jsfx"


def stage(dest: Path) -> None:
    if not VENDOR_DIR.is_dir():
        raise FileNotFoundError(f"vendor dir not found: {VENDOR_DIR}")
    dest.mkdir(parents=True, exist_ok=True)

    # Copy all vendor files, flattening Libs/ alongside the amp so JSFX
    # `import` statements (which use bare filenames) resolve.
    for p in VENDOR_DIR.rglob("*"):
        if not p.is_file():
            continue
        if p.suffix.lower() not in {".jsfx", ".jsfx-inc"}:
            continue
        shutil.copy2(p, dest / p.name)

    # Rename the amp to a sane filename and create the harness variant.
    src_amp = dest / VENDOR_AMP
    if not src_amp.is_file():
        raise FileNotFoundError(f"expected staged amp at {src_amp}")
    src_amp.replace(dest / STAGED_AMP)

    text = (dest / STAGED_AMP).read_text()
    patched = _apply_harness_patches(text)
    (dest / HARNESS_AMP).write_text(patched)

    # Leave a marker so we (and the renderer wrapper) can detect a stale stage.
    (dest / ".staged-by-nilamp").write_text("\n".join([
        f"vendor: {VENDOR_DIR}",
        f"amp_orig: {STAGED_AMP}",
        f"amp_harness: {HARNESS_AMP}",
        "harness_patches: remove-wallclock-mute",
        "",
    ]))


def _apply_harness_patches(src: str) -> str:
    """Replace wall-clock mute with no-op so renders are deterministic."""
    needle = "time_precise() > t_unmute ? is_muted = 0;"
    if needle not in src:
        raise RuntimeError(
            "expected wall-clock mute line not found; vendor JSFX may have changed.\n"
            f"looking for: {needle!r}"
        )
    replacement = "is_muted = 0; // harness: removed wall-clock dependency"
    return src.replace(needle, replacement, 1)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dest", type=Path, default=DEFAULT_DEST)
    args = ap.parse_args()
    stage(args.dest)
    print(f"staged to {args.dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
