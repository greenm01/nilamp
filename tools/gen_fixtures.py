"""Generate float32 .bin fixtures for tests/regression.rs.

All fixtures are raw little-endian float32 buffers (no header), produced by
the Python oracle in tools/keller_oracle.py.  Inputs and reference outputs are
both committed under tests/fixtures/ so cargo test does not require Python at
test time (only at fixture-regeneration time).

Run:
    python3 tools/gen_fixtures.py
"""

from pathlib import Path
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
import keller_oracle as ko  # noqa: E402
import gen_5e3_tables as t5e3  # noqa: E402
from gen_tables import gen_adnl_table  # noqa: E402

FIXTURES_DIR = Path("tests/fixtures")
SAMPLE_RATE = 48_000
DURATION = 0.1  # seconds — 4800 samples @ 48 kHz
N = int(SAMPLE_RATE * DURATION)


def write(path: Path, data: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data.astype(np.float32).tofile(path)
    print(f"  wrote {path}  ({len(data)} samples, {path.stat().st_size} bytes)")


def gen_input_sine(freq: float = 1000.0, amp: float = 0.5) -> np.ndarray:
    t = np.arange(N) / SAMPLE_RATE
    return (amp * np.sin(2 * np.pi * freq * t)).astype(np.float32)


def gen_pkd(input_buf: np.ndarray) -> np.ndarray:
    """Reference output for dsp/tests/test_pkd.dsp at the canonical parameter set."""
    xth = 0.0
    xdiode = 0.001
    tau_attack = 1e-3
    tau_release = 50e-3
    k1 = ko.pkd_k1(tau_attack, SAMPLE_RATE)
    k2 = ko.pkd_k2(tau_release, SAMPLE_RATE)
    return ko.pkd_process_block(xth, xdiode, k1, k2, input_buf.copy())


def gen_adnl_t1_12ax7(input_buf: np.ndarray) -> np.ndarray:
    """Reference output for dsp/tests/test_adnl_t1_12ax7.dsp.

    Builds the same GLF table that gen_5e3_tables.py emits for T1, then runs
    Keller's ADAA-corrected waveshaper on the input.
    """
    cfg = t5e3.T1_12AX7
    table = gen_adnl_table(cfg.kbias, cfg.b, cfg.type_b, cfg.kloop)
    proc = ko.AdnlProcessor(table)
    return proc.process_block(input_buf.copy().astype(np.float32))


def main() -> None:
    print("Generating fixtures…")

    sine = gen_input_sine()
    write(FIXTURES_DIR / "sine_1k_amp05_48k_4800.f32", sine)

    # PKD reference
    write(FIXTURES_DIR / "pkd_baseline_48k.f32", gen_pkd(sine))

    # ADNL t1_12ax7 — small-signal (sine well inside [-xmax, xmax])
    write(
        FIXTURES_DIR / "adnl_t1_12ax7_sine05_48k.f32",
        gen_adnl_t1_12ax7(sine),
    )

    # ADNL t1_12ax7 — large-signal sine (peak amplitude 20, exceeds xmax=15)
    # to exercise the ymin/ymax saturation arms in hk_adnl.lib.
    big_sine = gen_input_sine(freq=200.0, amp=20.0)
    write(FIXTURES_DIR / "sine_200_amp20_48k_4800.f32", big_sine)
    write(
        FIXTURES_DIR / "adnl_t1_12ax7_sine20_48k.f32",
        gen_adnl_t1_12ax7(big_sine),
    )


if __name__ == "__main__":
    main()
