# SPDX-License-Identifier: MIT
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
from gen_tables import gen_adnl_table, gen_adnl_table_dz_ck, gen_adnl_table_dz_cd  # noqa: E402

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


def gen_adnl(cfg, input_buf: np.ndarray) -> np.ndarray:
    """Reference output for an ADNL test harness bound to `cfg`'s GLF table.

    Builds the same table that gen_5e3_tables.py emits (gen_adnl_table with
    kbias, b, type_b, kloop), then runs Keller's ADAA-corrected waveshaper
    on the input.  Works for any CkConfig or CdConfig.
    """
    table = gen_adnl_table(cfg.kbias, cfg.b, cfg.type_b, cfg.kloop)
    proc = ko.AdnlProcessor(table)
    return proc.process_block(input_buf.copy().astype(np.float32))


def _ck_oracle(cfg) -> ko.TubeCk:
    """Build a TubeCk oracle whose params line up with c.t<N>_* in
    dsp/5e3_constants.lib.  Keep this in sync with _ck_lines() in
    gen_5e3_tables.py: same scalars, same NEQ-bypass coefficients
    (b0=1, all others 0)."""
    table = gen_adnl_table(cfg.kbias, cfg.b, cfg.type_b, cfg.kloop)
    pk_k1 = 1.0 - np.exp(-1.0 / (cfg.tattack * SAMPLE_RATE))
    pk_k2 = np.exp(-1.0 / (cfg.trelease * SAMPLE_RATE))
    avg_f = 1.0 / (2.0 * np.pi * cfg.tck)
    return ko.TubeCk(
        kpre=cfg.kpre, isat=cfg.isat, rl=cfg.rl, kpk=cfg.kpk,
        kspre=cfg.kspre, kspost=cfg.kspost, ksva=cfg.ksva,
        ksib=cfg.ksib, kfb=cfg.kfb,
        pk_xth=cfg.pk_xth, pk_xdiode=cfg.pk_xdrop,
        pk_k1=pk_k1, pk_k2=pk_k2, avg_f=avg_f,
        neq_b0=1.0, neq_b1=0.0, neq_b2=0.0, neq_a1=0.0, neq_a2=0.0,
        sr=SAMPLE_RATE, adnl_table=table,
    )


def _ck_oracle_dz(cfg) -> ko.TubeCk:
    """DZ-table variant of :func:`_ck_oracle`.  Same params, only the
    ADNL waveform changes (Dempwolf-Zölzer ECC83 + per-stage CK load
    line, with the same saturation clipping the Faust-side DZ table
    uses)."""
    table = gen_adnl_table_dz_ck(
        vs=cfg.vs, ra=cfg.ra, rl=cfg.rl, rk=cfg.rk,
        isat=cfg.isat, ibias=cfg.ibias, kpre=cfg.kpre,
    )
    pk_k1 = 1.0 - np.exp(-1.0 / (cfg.tattack * SAMPLE_RATE))
    pk_k2 = np.exp(-1.0 / (cfg.trelease * SAMPLE_RATE))
    avg_f = 1.0 / (2.0 * np.pi * cfg.tck)
    return ko.TubeCk(
        kpre=cfg.kpre, isat=cfg.isat, rl=cfg.rl, kpk=cfg.kpk,
        kspre=cfg.kspre, kspost=cfg.kspost, ksva=cfg.ksva,
        ksib=cfg.ksib, kfb=cfg.kfb,
        pk_xth=cfg.pk_xth, pk_xdiode=cfg.pk_xdrop,
        pk_k1=pk_k1, pk_k2=pk_k2, avg_f=avg_f,
        neq_b0=1.0, neq_b1=0.0, neq_b2=0.0, neq_a1=0.0, neq_a2=0.0,
        sr=SAMPLE_RATE, adnl_table=table,
    )


def _cd_oracle(cfg) -> ko.TubeCd:
    """Build a TubeCd oracle matching dsp/tests/test_tube_cd.dsp.
    No tck/avg_f for cathodyne (no advk path)."""
    table = gen_adnl_table(cfg.kbias, cfg.b, cfg.type_b, cfg.kloop)
    pk_k1 = 1.0 - np.exp(-1.0 / (cfg.tattack * SAMPLE_RATE))
    pk_k2 = np.exp(-1.0 / (cfg.trelease * SAMPLE_RATE))
    return ko.TubeCd(
        kpre=cfg.kpre, isat=cfg.isat, rl=cfg.rl, rkl=(cfg.rk + cfg.rl),
        kpk=cfg.kpk, kspre=cfg.kspre, kspost=cfg.kspost,
        ksva=cfg.ksva, ksvk=cfg.ksvk, ksib=cfg.ksib,
        pk_xth=cfg.pk_xth, pk_xdiode=cfg.pk_xdrop,
        pk_k1=pk_k1, pk_k2=pk_k2,
        neq_b0=1.0, neq_b1=0.0, neq_b2=0.0, neq_a1=0.0, neq_a2=0.0,
        sr=SAMPLE_RATE, adnl_table=table,
    )


def _cd_oracle_dz(cfg) -> ko.TubeCd:
    """DZ-table variant of :func:`_cd_oracle`."""
    table = gen_adnl_table_dz_cd(
        vs=cfg.vs, ra=cfg.ra, rl=cfg.rl, rk=cfg.rk,
        isat=cfg.isat, ibias=cfg.ibias, kpre=cfg.kpre,
    )
    pk_k1 = 1.0 - np.exp(-1.0 / (cfg.tattack * SAMPLE_RATE))
    pk_k2 = np.exp(-1.0 / (cfg.trelease * SAMPLE_RATE))
    return ko.TubeCd(
        kpre=cfg.kpre, isat=cfg.isat, rl=cfg.rl, rkl=(cfg.rk + cfg.rl),
        kpk=cfg.kpk, kspre=cfg.kspre, kspost=cfg.kspost,
        ksva=cfg.ksva, ksvk=cfg.ksvk, ksib=cfg.ksib,
        pk_xth=cfg.pk_xth, pk_xdiode=cfg.pk_xdrop,
        pk_k1=pk_k1, pk_k2=pk_k2,
        neq_b0=1.0, neq_b1=0.0, neq_b2=0.0, neq_a1=0.0, neq_a2=0.0,
        sr=SAMPLE_RATE, adnl_table=table,
    )


# Stages we have per-stage test DSPs for.  Keep the short names in sync with
# dsp/tests/test_adnl_<short>.dsp and the test names in tests/regression.rs.
ADNL_STAGES = [
    ("t1_12ax7", t5e3.T1_12AX7),
    ("t2_12ax7", t5e3.T2_12AX7),
    ("t3_cd",    t5e3.T3_CD),
    ("t4_6v6",   t5e3.T4_6V6),
    ("t5_6v6",   t5e3.T5_6V6),
]


def gen_power_pair_inputs() -> tuple[np.ndarray, np.ndarray]:
    """Synthetic T3 plate/cathode taps for the T4/T5 branch diagnostic."""
    t = np.arange(N) / SAMPLE_RATE
    t3_v = (10.0 * np.sin(2 * np.pi * 1000.0 * t)).astype(np.float32)
    t3_vk = (-9.0 * np.sin(2 * np.pi * 1000.0 * t)).astype(np.float32)
    return t3_v, t3_vk


def gen_power_pair_diag(t3_v: np.ndarray, t3_vk: np.ndarray) -> tuple[np.ndarray, ...]:
    """Reference for dsp/tests/test_power_pair_t5.dsp.

    Mirrors the mode-0 T4/T5 branch only: pre-power PEQ/HS on both T3 taps,
    T4/T5 tube_ck stages with dvs=0, subtractive push-pull mix, and summed
    dia.  It intentionally excludes post-push-pull PEQ/HS/HP/LP and PSS.
    """
    k1 = 0.797
    k2 = 0.940
    hp3_hz = 5.8
    hp4_hz = 6.4
    kp1 = 1.1220184543
    fp_hz = 80.0
    qp1 = 2.6685237666
    ks1 = 1.4125375446
    fs1_hz = 2098.1359672

    t4_in = ko.flt_sv1_hs_block(
        ks1, fs1_hz, 1, SAMPLE_RATE,
        ko.flt_sv2_peq_block(
            kp1, fp_hz, qp1, 1, 1, SAMPLE_RATE,
            ko.flt_ii1_hp_block(hp3_hz, SAMPLE_RATE, (t3_v * k1).astype(np.float32)),
        ),
    )
    t5_in = ko.flt_sv1_hs_block(
        ks1, fs1_hz, 1, SAMPLE_RATE,
        ko.flt_sv2_peq_block(
            kp1, fp_hz, qp1, 1, 1, SAMPLE_RATE,
            ko.flt_ii1_hp_block(hp4_hz, SAMPLE_RATE, (t3_vk * k2).astype(np.float32)),
        ),
    )

    dvs_zero = np.zeros(N, dtype=np.float32)
    t4 = _ck_oracle(t5e3.T4_6V6)
    t5 = _ck_oracle(t5e3.T5_6V6)
    t4_v, t4_dia = t4.process_block(t4_in, dvs_zero)
    t5_v, t5_dia = t5.process_block(t5_in, dvs_zero)
    post_pp = (t4_v - t5_v).astype(np.float32)
    total_dia = (t4_dia + t5_dia).astype(np.float32)
    return t4_v, t5_v, post_pp, total_dia


def main() -> None:
    print("Generating fixtures…")

    sine = gen_input_sine()
    write(FIXTURES_DIR / "sine_1k_amp05_48k_4800.f32", sine)

    big_sine = gen_input_sine(freq=200.0, amp=20.0)
    write(FIXTURES_DIR / "sine_200_amp20_48k_4800.f32", big_sine)

    # PKD reference
    write(FIXTURES_DIR / "pkd_baseline_48k.f32", gen_pkd(sine))

    # ADNL — small-signal (sine well inside [-xmax, xmax]) and large-signal
    # (peak amplitude 20, exceeds xmax=15) for each table.  The large-signal
    # case exercises the ymin/ymax saturation arms in hk_adnl.lib.
    for short, cfg in ADNL_STAGES:
        write(
            FIXTURES_DIR / f"adnl_{short}_sine05_48k.f32",
            gen_adnl(cfg, sine),
        )
        write(
            FIXTURES_DIR / f"adnl_{short}_sine20_48k.f32",
            gen_adnl(cfg, big_sine),
        )

    # Filters — same operating points the 5E3 top-level (dsp/nilamp.dsp:37)
    # uses, against the 1 kHz/amp 0.5 sine.  Lets us pin the Faust port of
    # hk_filters.lib (flt_ii1_lp / flt_ii1_hp / flt_sv2_tst) to the JSFX
    # implementations without depending on scipy.signal at test time.
    write(
        FIXTURES_DIR / "filter_lp_8800_sine05_48k.f32",
        ko.flt_ii1_lp_block(8800.0, SAMPLE_RATE, sine),
    )
    write(
        FIXTURES_DIR / "filter_hp_10_sine05_48k.f32",
        ko.flt_ii1_hp_block(10.0, SAMPLE_RATE, sine),
    )
    write(
        FIXTURES_DIR / "filter_svf_tst_sine05_48k.f32",
        ko.flt_sv2_tst_block(0.25, 0.25, 0.25, 500.0, 0.5, 1, 1, SAMPLE_RATE, sine),
    )

    # Composite tube stage — full pipeline (ADNL + PKD + advk feedback +
    # state) with dvs hard-coded to 0.  T2 (12AX7 v2) for the CK harness
    # because it has kpk>0 and a non-trivial advk path; T3 for cathodyne.
    # See dsp/tests/test_tube_ck.dsp / test_tube_cd.dsp.
    dvs_zero = np.zeros(N, dtype=np.float32)

    ck = _ck_oracle(t5e3.T2_12AX7)
    ck_v, ck_dia = ck.process_block(sine.copy(), dvs_zero)
    write(FIXTURES_DIR / "tube_ck_t2_v_sine05_48k.f32", ck_v)
    write(FIXTURES_DIR / "tube_ck_t2_dia_sine05_48k.f32", ck_dia)

    ck_t5 = _ck_oracle(t5e3.T5_6V6)
    ck_t5_v, ck_t5_dia = ck_t5.process_block(sine.copy(), dvs_zero)
    write(FIXTURES_DIR / "tube_ck_t5_v_sine05_48k.f32", ck_t5_v)
    write(FIXTURES_DIR / "tube_ck_t5_dia_sine05_48k.f32", ck_t5_dia)

    cd = _cd_oracle(t5e3.T3_CD)
    # Drive the cathodyne with the small-signal preamp output from the
    # CK oracle so we exercise it inside its actual operating range.
    # Scale by isat path -> v_out is ~ -rl * isat * adnl(...) ~ \u00b1tens of V,
    # which the cathodyne wants on its grid.  Use sine amp 0.5 directly
    # for now; the closed-loop end-to-end test will exercise the realistic
    # cascade voltages.
    cd_v, cd_vk, cd_dia = cd.process_block(sine.copy(), dvs_zero)
    write(FIXTURES_DIR / "tube_cd_t3_v_sine05_48k.f32",  cd_v)
    write(FIXTURES_DIR / "tube_cd_t3_vk_sine05_48k.f32", cd_vk)
    write(FIXTURES_DIR / "tube_cd_t3_dia_sine05_48k.f32", cd_dia)

    # DZ variants — same harness shape, swap the ADNL table for the
    # Dempwolf-Zölzer load-line curve.  See dsp/tests/test_tube_ck_t2_dz.dsp
    # / test_tube_cd_t3_dz.dsp.
    ck_dz = _ck_oracle_dz(t5e3.T2_12AX7)
    ck_dz_v, ck_dz_dia = ck_dz.process_block(sine.copy(), dvs_zero)
    write(FIXTURES_DIR / "tube_ck_t2_dz_v_sine05_48k.f32", ck_dz_v)
    write(FIXTURES_DIR / "tube_ck_t2_dz_dia_sine05_48k.f32", ck_dz_dia)

    cd_dz = _cd_oracle_dz(t5e3.T3_CD)
    cd_dz_v, cd_dz_vk, cd_dz_dia = cd_dz.process_block(sine.copy(), dvs_zero)
    write(FIXTURES_DIR / "tube_cd_t3_dz_v_sine05_48k.f32",  cd_dz_v)
    write(FIXTURES_DIR / "tube_cd_t3_dz_vk_sine05_48k.f32", cd_dz_vk)
    write(FIXTURES_DIR / "tube_cd_t3_dz_dia_sine05_48k.f32", cd_dz_dia)

    # PSS \u2014 standalone, snext=dvs_in=0.  dia values in a real amp are
    # ~mA-range, but tube_pss is linear in dia so any scale exercises
    # the same code path.  Use the 1 kHz sine directly.
    pss = ko.TubePss(r=22000.0, tau=0.05, sr=SAMPLE_RATE)
    pss_dvs, pss_s = pss.process_block(sine.copy(), dvs_zero, dvs_zero)
    write(FIXTURES_DIR / "pss_dvs_sine05_48k.f32", pss_dvs)
    write(FIXTURES_DIR / "pss_s_sine05_48k.f32",   pss_s)

    # T4/T5 mode-0 branch diagnostic.
    t3_v, t3_vk = gen_power_pair_inputs()
    write(FIXTURES_DIR / "power_pair_t3_v_48k.f32", t3_v)
    write(FIXTURES_DIR / "power_pair_t3_vk_48k.f32", t3_vk)
    pp_t4_v, pp_t5_v, pp_post, pp_dia = gen_power_pair_diag(t3_v, t3_vk)
    write(FIXTURES_DIR / "power_pair_t4_v_48k.f32", pp_t4_v)
    write(FIXTURES_DIR / "power_pair_t5_v_48k.f32", pp_t5_v)
    write(FIXTURES_DIR / "power_pair_post_pp_48k.f32", pp_post)
    write(FIXTURES_DIR / "power_pair_total_dia_48k.f32", pp_dia)


if __name__ == "__main__":
    main()
