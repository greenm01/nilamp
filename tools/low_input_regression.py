"""Low-input numerical-stability regression tests.

The native amp engine has been observed to be numerically unstable for
input magnitudes between roughly 1e-9 and 1e-3, while the Keller JSFX
reference is well-behaved across the same range. The public ABX gate at
sine amp 0.15 / sweep amp 0.08 does not exercise this region. This test
locks in the regression so a future DSP fix can be verified.

Test cases (all 1 channel @ 48 kHz, default plugin params):

  zero     - 1.0 s of exact zeros.
             Native peak must be < 1e-10 (engine stays at rest).

  noise_lo - 0.5 s of gaussian noise, RMS 1e-6.
             Native peak must be < 1e-3 (no overflow / runaway).

  noise_floor - 1.0 s of gaussian noise, RMS 1e-4 (mimics a real audio
             interface noise floor as observed in REAPER).
             Native peak must be < 10x the JSFX peak on the same input.

The JSFX comparison is optional. If `native/bin/ysfx_render` and the
staged JSFX harness are not present, that case is skipped with a notice
rather than a failure, so this script can run in `make native-test`
without requiring ysfx.

Usage:
    python3 tools/low_input_regression.py [--require-jsfx]
"""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

REPO_ROOT = Path(__file__).resolve().parent.parent
NILAMP_RENDER = REPO_ROOT / "native" / "bin" / "nilamp_render"
YSFX_RENDER = REPO_ROOT / "native" / "bin" / "ysfx_render"
JSFX_HARNESS = (
    REPO_ROOT
    / "native"
    / "build"
    / "jsfx"
    / "Effects"
    / "nilamp_abx"
    / "twd_dlx_ii_harness.jsfx"
)
JSFX_IMPORT_ROOT = REPO_ROOT / "native" / "build" / "jsfx" / "Effects"


def write_wav_f32_mono(path: Path, samples: np.ndarray, sr: int) -> None:
    data = samples.astype(np.float32, copy=False).tobytes()
    with open(path, "wb") as f:
        f.write(b"RIFF")
        f.write(struct.pack("<I", 4 + 8 + 16 + 8 + len(data)))
        f.write(b"WAVE")
        f.write(b"fmt ")
        f.write(struct.pack("<IHHIIHH", 16, 3, 1, sr, sr * 4, 4, 32))
        f.write(b"data")
        f.write(struct.pack("<I", len(data)))
        f.write(data)


def read_wav_f32(path: Path) -> np.ndarray:
    raw = path.read_bytes()
    pos = 12
    while pos + 8 <= len(raw):
        cid = raw[pos : pos + 4]
        csz = struct.unpack("<I", raw[pos + 4 : pos + 8])[0]
        body = pos + 8
        if cid == b"data":
            return np.frombuffer(raw[body : body + csz], dtype=np.float32)
        pos = body + csz + (csz & 1)
    raise RuntimeError(f"no data chunk in {path}")


def render_native(in_wav: Path, out_wav: Path) -> None:
    subprocess.run(
        [
            str(NILAMP_RENDER),
            "--input",
            str(in_wav),
            "--output",
            str(out_wav),
            "--block",
            "64",
        ],
        check=True,
        capture_output=True,
    )


def render_jsfx(in_wav: Path, out_wav: Path) -> None:
    subprocess.run(
        [
            str(YSFX_RENDER),
            "--input",
            str(in_wav),
            "--output",
            str(out_wav),
            "--effect",
            str(JSFX_HARNESS),
            "--import-root",
            str(JSFX_IMPORT_ROOT),
            "--block",
            "64",
            "--channels",
            "1",
        ],
        check=True,
        capture_output=True,
    )


def have_jsfx() -> bool:
    return YSFX_RENDER.exists() and JSFX_HARNESS.exists()


def make_zero(sr: int, dur_s: float) -> np.ndarray:
    return np.zeros(int(sr * dur_s), dtype=np.float32)


def make_noise(sr: int, dur_s: float, rms: float, seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    return (rms * rng.standard_normal(int(sr * dur_s))).astype(np.float32)


def case_zero(tmp: Path) -> tuple[bool, str]:
    sr = 48000
    sig = make_zero(sr, 1.0)
    in_wav = tmp / "zero_in.wav"
    out_wav = tmp / "zero_out_native.wav"
    write_wav_f32_mono(in_wav, sig, sr)
    render_native(in_wav, out_wav)
    out = read_wav_f32(out_wav)
    peak = float(np.max(np.abs(out)))
    threshold = 1e-10
    ok = peak < threshold
    return ok, (
        f"zero: native peak={peak:.3e} threshold<{threshold:.0e} "
        f"-> {'PASS' if ok else 'FAIL'}"
    )


def case_noise_low(tmp: Path) -> tuple[bool, str]:
    sr = 48000
    sig = make_noise(sr, 0.5, 1e-6, seed=1)
    in_wav = tmp / "noise_lo_in.wav"
    out_wav = tmp / "noise_lo_out_native.wav"
    write_wav_f32_mono(in_wav, sig, sr)
    render_native(in_wav, out_wav)
    out = read_wav_f32(out_wav)
    peak = float(np.max(np.abs(out)))
    threshold = 1e-3
    finite = bool(np.all(np.isfinite(out)))
    ok = bool(peak < threshold) and finite
    return ok, (
        f"noise_lo (rms=1e-6): native peak={peak:.3e} threshold<{threshold:.0e} "
        f"finite={finite} "
        f"-> {'PASS' if ok else 'FAIL'}"
    )


def case_noise_floor(tmp: Path, require_jsfx: bool) -> tuple[bool, str]:
    sr = 48000
    sig = make_noise(sr, 1.0, 1e-4, seed=7)
    in_wav = tmp / "noise_floor_in.wav"
    out_native = tmp / "noise_floor_out_native.wav"
    write_wav_f32_mono(in_wav, sig, sr)
    render_native(in_wav, out_native)
    a = read_wav_f32(out_native)
    peak_n = float(np.max(np.abs(a)))

    if not have_jsfx():
        msg = (
            f"noise_floor (rms=1e-4): native peak={peak_n:.3e} "
            f"jsfx unavailable -> SKIP"
        )
        if require_jsfx:
            return False, msg.replace("SKIP", "FAIL (--require-jsfx set)")
        return True, msg

    out_jsfx = tmp / "noise_floor_out_jsfx.wav"
    render_jsfx(in_wav, out_jsfx)
    b = read_wav_f32(out_jsfx)
    peak_j = float(np.max(np.abs(b)))
    if peak_j <= 0.0:
        return False, (
            f"noise_floor: jsfx peak is zero ({peak_j!r}); cannot ratio."
        )
    ratio = peak_n / peak_j
    threshold = 10.0
    finite = bool(np.all(np.isfinite(a)))
    ok = bool(ratio < threshold) and finite
    return ok, (
        f"noise_floor (rms=1e-4): native peak={peak_n:.3e} "
        f"jsfx peak={peak_j:.3e} ratio={ratio:.2f}x threshold<{threshold:.0f}x "
        f"-> {'PASS' if ok else 'FAIL'}"
    )


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--require-jsfx",
        action="store_true",
        help="Treat missing ysfx_render or JSFX harness as a failure.",
    )
    args = parser.parse_args(argv)

    if not NILAMP_RENDER.exists():
        print(f"missing: {NILAMP_RENDER} (run `make native` first)", file=sys.stderr)
        return 2

    print("low-input regression tests")
    print(f"  nilamp_render: {NILAMP_RENDER}")
    print(f"  jsfx available: {have_jsfx()}")
    print()

    failures: list[str] = []
    with tempfile.TemporaryDirectory(prefix="nilamp-low-input-") as td:
        tmp = Path(td)
        for name, fn in [
            ("zero", lambda: case_zero(tmp)),
            ("noise_lo", lambda: case_noise_low(tmp)),
            ("noise_floor", lambda: case_noise_floor(tmp, args.require_jsfx)),
        ]:
            ok, msg = fn()
            print(f"  {msg}")
            if not ok:
                failures.append(name)

    print()
    if failures:
        print(f"FAIL: {len(failures)} case(s): {', '.join(failures)}")
        return 1
    print("PASS: all cases")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
