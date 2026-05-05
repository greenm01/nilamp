"""Stage Keller's JSFX bundle into REAPER's Effects directory for harness use.

Copies vendor/keller-jsfx into ~/.config/REAPER/Effects/nilamp_abx/, with two
variants of the main amp:

  - twd_dlx_ii.jsfx           : verbatim vendor file (for manual GUI use).
  - twd_dlx_ii_harness.jsfx   : with wall-clock mute disabled for deterministic
                                offline rendering. Used by the ABX harness.
  - twd_dlx_ii_taps.jsfx      : harness variant that emits internal stage taps
                                as nine output channels for parity debugging.
  - twd_dlx_ii_tap_select.jsfx: harness variant that renders one selected tap
                                as mono output through the public ABX path.

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
TAPS_AMP = "twd_dlx_ii_taps.jsfx"
TAP_SELECT_AMP = "twd_dlx_ii_tap_select.jsfx"
PROBE_SRC = REPO_ROOT / "tools" / "jsfx_render" / "filter_semantics_probe.jsfx"
PROBE_AMP = "filter_semantics_probe.jsfx"


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
    (dest / TAPS_AMP).write_text(_apply_tap_patches(patched))
    (dest / TAP_SELECT_AMP).write_text(_apply_tap_select_patches(patched))

    shutil.copy2(PROBE_SRC, dest / PROBE_AMP)

    # Leave a marker so we (and the renderer wrapper) can detect a stale stage.
    (dest / ".staged-by-nilamp").write_text("\n".join([
        f"vendor: {VENDOR_DIR}",
        f"amp_orig: {STAGED_AMP}",
        f"amp_harness: {HARNESS_AMP}",
        f"amp_taps: {TAPS_AMP}",
        f"amp_tap_select: {TAP_SELECT_AMP}",
        f"filter_probe: {PROBE_AMP}",
        "harness_patches: remove-wallclock-mute, tap-outputs, tap-select",
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


def _apply_tap_patches(src: str) -> str:
    """Emit the stage taps that native/bin/nilamp_taps_render writes."""
    out_pin_needle = "out_pin:mono output\n//out_pin:right output"
    out_pin_replacement = "\n".join([
        "out_pin:v_out",
        "out_pin:res1_v",
        "out_pin:res3_v",
        "out_pin:res4_v",
        "out_pin:drive_t4",
        "out_pin:res5_v",
        "out_pin:res_t5_v",
        "out_pin:dvs2",
        "out_pin:dvs3",
    ])
    if out_pin_needle not in src:
        raise RuntimeError("expected JSFX mono output pin block not found")
    src = src.replace(out_pin_needle, out_pin_replacement, 1)

    replacements = [
        (
            "dvs3 = p3.tube_pss_process(dvs2, 0, \tdia3);",
            "dvs3 = p3.tube_pss_process(dvs2, 0, \tdia3);\n"
            "tap_dvs2 = dvs2;\n"
            "tap_dvs3 = dvs3;",
        ),
        (
            "spl0 = t1.tube_ck_process(spl0, dvs3);",
            "spl0 = t1.tube_ck_process(spl0, dvs3);\n"
            "tap_res1_v = spl0;",
        ),
        (
            "spl0 = t2.tube_ck_process(spl0, dvs3);\nspl0 = hp2.flt_ii1_process_hp(spl0);",
            "spl0 = t2.tube_ck_process(spl0, dvs3);\n"
            "tap_res3_v = spl0;\n"
            "spl0 = hp2.flt_ii1_process_hp(spl0);",
        ),
        (
            "spl0 = t3.tube_cd_process(spl0, dvs3);",
            "spl0 = t3.tube_cd_process(spl0, dvs3);\n"
            "tap_res4_v = spl0;",
        ),
        (
            "spl0 = hs1.flt_sv1_process(spl0);\nspl0 = t4.tube_ck_process(spl0, dvs2);",
            "spl0 = hs1.flt_sv1_process(spl0);\n"
            "tap_drive_t4 = spl0;\n"
            "spl0 = t4.tube_ck_process(spl0, dvs2);",
        ),
        (
            "spl0 = t4.tube_ck_process(spl0, dvs2);",
            "spl0 = t4.tube_ck_process(spl0, dvs2);\n"
            "tap_res5_v = spl0;",
        ),
        (
            "aux = t5.tube_ck_process(aux, dvs2);\nspl0 -= aux;",
            "aux = t5.tube_ck_process(aux, dvs2);\n"
            "tap_res_t5_v = aux;\n"
            "spl0 -= aux;",
        ),
        (
            "spl0 = g3.gain_process(spl0);",
            "spl0 = g3.gain_process(spl0);\n"
            "tap_v_out = spl0;",
        ),
        (
            "is_muted ==1 ?\n(\n\tspl0 = 0;\n\t//spl1 = 0\n);",
            "is_muted ==1 ?\n(\n\tspl0 = 0;\n\t//spl1 = 0\n);\n\n"
            "spl0 = tap_v_out;\n"
            "spl1 = tap_res1_v;\n"
            "spl2 = tap_res3_v;\n"
            "spl3 = tap_res4_v;\n"
            "spl4 = tap_drive_t4;\n"
            "spl5 = tap_res5_v;\n"
            "spl6 = tap_res_t5_v;\n"
            "spl7 = tap_dvs2;\n"
            "spl8 = tap_dvs3;",
        ),
    ]
    for needle, replacement in replacements:
        if needle not in src:
            raise RuntimeError(f"expected JSFX tap patch site not found: {needle!r}")
        src = src.replace(needle, replacement, 1)
    return src


def _apply_tap_select_patches(src: str) -> str:
    """Emit one selected stage tap as mono output for public-path checks."""
    slider_needle = (
        "slider18:p.gcomp=\t\t2\t\t<0, \t3,  1{Off, Tube 1, Splitter, Both}>-   "
        "Gain Compensation"
    )
    slider_replacement = "\n".join([
        slider_needle,
        "",
        "slider19:p.tap=        0       <0, 8, 1{v_out, res1_v, res3_v, res4_v, drive_t4, res5_v, res_t5_v, dvs2, dvs3}>-   Tap",
    ])
    if slider_needle not in src:
        raise RuntimeError("expected JSFX gain-comp slider declaration not found")
    src = src.replace(slider_needle, slider_replacement, 1)

    replacements = [
        (
            "dvs3 = p3.tube_pss_process(dvs2, 0, \tdia3);",
            "dvs3 = p3.tube_pss_process(dvs2, 0, \tdia3);\n"
            "tap_dvs2 = dvs2;\n"
            "tap_dvs3 = dvs3;",
        ),
        (
            "spl0 = t1.tube_ck_process(spl0, dvs3);",
            "spl0 = t1.tube_ck_process(spl0, dvs3);\n"
            "tap_res1_v = spl0;",
        ),
        (
            "spl0 = t2.tube_ck_process(spl0, dvs3);\nspl0 = hp2.flt_ii1_process_hp(spl0);",
            "spl0 = t2.tube_ck_process(spl0, dvs3);\n"
            "tap_res3_v = spl0;\n"
            "spl0 = hp2.flt_ii1_process_hp(spl0);",
        ),
        (
            "spl0 = t3.tube_cd_process(spl0, dvs3);",
            "spl0 = t3.tube_cd_process(spl0, dvs3);\n"
            "tap_res4_v = spl0;",
        ),
        (
            "spl0 = hs1.flt_sv1_process(spl0);\nspl0 = t4.tube_ck_process(spl0, dvs2);",
            "spl0 = hs1.flt_sv1_process(spl0);\n"
            "tap_drive_t4 = spl0;\n"
            "spl0 = t4.tube_ck_process(spl0, dvs2);",
        ),
        (
            "spl0 = t4.tube_ck_process(spl0, dvs2);",
            "spl0 = t4.tube_ck_process(spl0, dvs2);\n"
            "tap_res5_v = spl0;",
        ),
        (
            "aux = t5.tube_ck_process(aux, dvs2);\nspl0 -= aux;",
            "aux = t5.tube_ck_process(aux, dvs2);\n"
            "tap_res_t5_v = aux;\n"
            "spl0 -= aux;",
        ),
        (
            "spl0 = g3.gain_process(spl0);",
            "spl0 = g3.gain_process(spl0);\n"
            "tap_v_out = spl0;",
        ),
        (
            "is_muted ==1 ?\n(\n\tspl0 = 0;\n\t//spl1 = 0\n);",
            "is_muted ==1 ?\n(\n\tspl0 = 0;\n\t//spl1 = 0\n);\n\n"
            "tap_selected = tap_v_out;\n"
            "p.tap == 1 ? tap_selected = tap_res1_v;\n"
            "p.tap == 2 ? tap_selected = tap_res3_v;\n"
            "p.tap == 3 ? tap_selected = tap_res4_v;\n"
            "p.tap == 4 ? tap_selected = tap_drive_t4;\n"
            "p.tap == 5 ? tap_selected = tap_res5_v;\n"
            "p.tap == 6 ? tap_selected = tap_res_t5_v;\n"
            "p.tap == 7 ? tap_selected = tap_dvs2;\n"
            "p.tap == 8 ? tap_selected = tap_dvs3;\n"
            "spl0 = tap_selected;",
        ),
    ]
    for needle, replacement in replacements:
        if needle not in src:
            raise RuntimeError(f"expected JSFX tap-select patch site not found: {needle!r}")
        src = src.replace(needle, replacement, 1)
    return src


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dest", type=Path, default=DEFAULT_DEST)
    args = ap.parse_args()
    stage(args.dest)
    print(f"staged to {args.dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
