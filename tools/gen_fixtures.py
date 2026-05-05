# SPDX-License-Identifier: MIT
"""Generate float32 .bin fixtures for native regression tests.

All fixtures are raw little-endian float32 buffers (no header), produced by
the Python oracle in tools/keller_oracle.py.  Inputs and reference outputs are
both committed under tests/fixtures/ so native tests do not require Python at
test time.

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
    """Reference output for the PKD canonical parameter set."""
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
    """Build a TubeCk oracle using the native C engine's stage constants."""
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
    line, with the same saturation clipping the native DZ table uses)."""
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
    """Build a TubeCd oracle. No tck/avg_f for cathodyne (no advk path)."""
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


# Stages with per-stage oracle fixtures. Keep the short names stable because
# fixture filenames and native test names refer to them.
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


def gen_backend_filter_diag(input_buf: np.ndarray) -> tuple[np.ndarray, ...]:
    """Reference for the backend filter diagnostic."""
    k1 = 0.797
    k2 = 0.940
    hp3_hz = 5.8
    hp4_hz = 6.4
    kp1 = 1.1220184543
    fp_hz = 80.0
    qp1 = 2.6685237666
    ks1 = 1.4125375446
    fs1_hz = 2098.1359672

    hp3 = ko.flt_ii1_hp_block(hp3_hz, SAMPLE_RATE, input_buf)
    hp4 = ko.flt_ii1_hp_block(hp4_hz, SAMPLE_RATE, input_buf)
    peq1 = ko.flt_sv2_peq_block(kp1, fp_hz, qp1, 1, 1, SAMPLE_RATE, input_buf)
    hs1 = ko.flt_sv1_hs_block(ks1, fs1_hz, 1, SAMPLE_RATE, input_buf)
    peq1_hs1 = ko.flt_sv1_hs_block(
        ks1, fs1_hz, 1, SAMPLE_RATE,
        ko.flt_sv2_peq_block(kp1, fp_hz, qp1, 1, 1, SAMPLE_RATE, input_buf),
    )
    t4_pre = ko.flt_sv1_hs_block(
        ks1, fs1_hz, 1, SAMPLE_RATE,
        ko.flt_sv2_peq_block(
            kp1, fp_hz, qp1, 1, 1, SAMPLE_RATE,
            ko.flt_ii1_hp_block(hp3_hz, SAMPLE_RATE, (input_buf * k1).astype(np.float32)),
        ),
    )
    t5_pre = ko.flt_sv1_hs_block(
        ks1, fs1_hz, 1, SAMPLE_RATE,
        ko.flt_sv2_peq_block(
            kp1, fp_hz, qp1, 1, 1, SAMPLE_RATE,
            ko.flt_ii1_hp_block(hp4_hz, SAMPLE_RATE, (input_buf * k2).astype(np.float32)),
        ),
    )
    return hp3, hp4, peq1, hs1, peq1_hs1, t4_pre, t5_pre


def gen_power_pair_diag(t3_v: np.ndarray, t3_vk: np.ndarray) -> tuple[np.ndarray, ...]:
    """Reference for the T4/T5 power-pair diagnostic.

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


def gen_nilamp_taps(input_buf: np.ndarray) -> tuple[np.ndarray, ...]:
    """Reference for the nilamp tap-render diagnostic.

    Mirrors the current native top-level cascade at default params:
    gain=0 dB; volume/bass/mid/treble/sag=50%.
    """
    class Df2Lp:
        def __init__(self, f: float, q: float, sr: int):
            pi_t = np.pi / sr
            k0 = f * pi_t
            aux1 = np.sqrt(1.0 + 4.0 * q * q)
            aux2 = (k0 / np.sin(2.0 * k0)) * np.log((aux1 + 1.0) / (aux1 - 1.0))
            kq0 = np.exp(aux2) - np.exp(-aux2)
            k = np.tan(f * pi_t)
            kq = k * kq0
            ksqr = k * k
            kdiv = 1.0 / (1.0 + kq + ksqr)
            self.a1 = (-2.0 + 2.0 * ksqr) * kdiv
            self.a2 = (1.0 - kq + ksqr) * kdiv
            self.b0 = ksqr * kdiv
            self.b1 = 2.0 * ksqr * kdiv
            self.b2 = self.b0
            self.x1 = 0.0
            self.x2 = 0.0
            self.y1 = 0.0
            self.y2 = 0.0

        def process_sample(self, x: float) -> float:
            y = self.b0 * x + self.b1 * self.x1 + self.b2 * self.x2 - self.a1 * self.y1 - self.a2 * self.y2
            self.x2 = self.x1
            self.x1 = x
            self.y2 = self.y1
            self.y1 = y
            return y

    hp10 = ko.FltIi1Hp(10.0, SAMPLE_RATE)
    tone = ko.FltSv2Tst(0.25, 0.25, 0.25, 630.0, 0.5, 1, 1, SAMPLE_RATE)
    lp8800 = ko.FltIi1Lp(8800.0, SAMPLE_RATE)
    hp3 = ko.FltIi1Hp(5.8, SAMPLE_RATE)
    peq1_t4 = ko.FltSv2Peq(1.1220184543, 80.0, 2.6685237666, 1, 1, SAMPLE_RATE)
    hs1_t4 = ko.FltSv1Hs(1.4125375446, 2098.1359672, 1, SAMPLE_RATE)
    hp4 = ko.FltIi1Hp(6.4, SAMPLE_RATE)
    peq1_t5 = ko.FltSv2Peq(1.1220184543, 80.0, 2.6685237666, 1, 1, SAMPLE_RATE)
    hs1_t5 = ko.FltSv1Hs(1.4125375446, 2098.1359672, 1, SAMPLE_RATE)
    peq3 = ko.FltSv2Peq(1.2589254118, 80.0, 2.2440931043, 1, 1, SAMPLE_RATE)
    hs3 = ko.FltSv1Hs(1.4125375446, 1485.8089753, 1, SAMPLE_RATE)
    hp5 = ko.FltIi1Hp(40.0, SAMPLE_RATE)
    lp2 = Df2Lp(10000.0, np.sqrt(0.5), SAMPLE_RATE)

    t1 = _ck_oracle(t5e3.T1_12AX7)
    t2 = _ck_oracle(t5e3.T2_12AX7)
    t3 = _cd_oracle(t5e3.T3_CD)
    t4 = _ck_oracle(t5e3.T4_6V6)
    t5 = _ck_oracle(t5e3.T5_6V6)
    p1 = ko.TubePss(r=125.0, tau=0.008, sr=SAMPLE_RATE)
    p2 = ko.TubePss(r=5100.0, tau=0.0816, sr=SAMPLE_RATE)
    p3 = ko.TubePss(r=11000.0, tau=0.352, sr=SAMPLE_RATE)

    n = len(input_buf)
    v_out_buf = np.empty(n, dtype=np.float32)
    res1_v_buf = np.empty(n, dtype=np.float32)
    res3_v_buf = np.empty(n, dtype=np.float32)
    res4_v_buf = np.empty(n, dtype=np.float32)
    drive_t4_buf = np.empty(n, dtype=np.float32)
    res5_v_buf = np.empty(n, dtype=np.float32)
    res_t5_v_buf = np.empty(n, dtype=np.float32)
    dvs2_buf = np.empty(n, dtype=np.float32)
    dvs3_buf = np.empty(n, dtype=np.float32)

    prev_dia1 = 0.0
    prev_dig = 0.0
    prev_dia3 = 0.0
    for i, vin in enumerate(input_buf):
        old_s2 = p2.s
        old_s3 = p3.s
        dvs1, _ = p1.process_sample(prev_dia1, old_s2, 0.0)
        dvs2, _ = p2.process_sample(prev_dig, old_s3, dvs1)
        dvs3, _ = p3.process_sample(prev_dia3, 0.0, dvs2)

        res1_v, res1_dia = t1.process_sample(vin, dvs3)
        v2 = hp10.process_sample(res1_v)
        v2 *= 0.25
        v2 = tone.process_sample(v2)
        v2 = lp8800.process_sample(v2)

        res3_v, res3_dia = t2.process_sample(v2, dvs3)
        res4_v, res4_vk, res4_dia = t3.process_sample(res3_v, dvs3)

        drive_t4 = hp3.process_sample(res4_v * 0.797)
        drive_t4 = peq1_t4.process_sample(drive_t4)
        drive_t4 = hs1_t4.process_sample(drive_t4)
        res5_v, res5_dia = t4.process_sample(drive_t4, dvs2)

        aux = hp4.process_sample(res4_vk * 0.940)
        aux = peq1_t5.process_sample(aux)
        aux = hs1_t5.process_sample(aux)
        res_t5_v, res_t5_dia = t5.process_sample(aux, dvs2)

        v_out = res5_v - res_t5_v
        v_out = peq3.process_sample(v_out)
        v_out = hs3.process_sample(v_out)
        v_out = hp5.process_sample(v_out)
        v_out = lp2.process_sample(v_out)
        v_out *= 0.5 / (t5e3.T4_6V6.rl * t5e3.T4_6V6.isat + t5e3.T5_6V6.rl * t5e3.T5_6V6.isat)

        v_out_buf[i] = v_out
        res1_v_buf[i] = res1_v
        res3_v_buf[i] = res3_v
        res4_v_buf[i] = res4_v
        drive_t4_buf[i] = drive_t4
        res5_v_buf[i] = res5_v
        res_t5_v_buf[i] = res_t5_v
        dvs2_buf[i] = dvs2
        dvs3_buf[i] = dvs3

        prev_dia1 = res5_dia + res_t5_dia
        prev_dig = 0.025 * prev_dia1
        prev_dia3 = res1_dia + res3_dia + res4_dia

    return (
        v_out_buf,
        res1_v_buf,
        res3_v_buf,
        res4_v_buf,
        drive_t4_buf,
        res5_v_buf,
        res_t5_v_buf,
        dvs2_buf,
        dvs3_buf,
    )


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
    # case exercises the ymin/ymax saturation arms in the ADNL runtime.
    for short, cfg in ADNL_STAGES:
        write(
            FIXTURES_DIR / f"adnl_{short}_sine05_48k.f32",
            gen_adnl(cfg, sine),
        )
        write(
            FIXTURES_DIR / f"adnl_{short}_sine20_48k.f32",
            gen_adnl(cfg, big_sine),
        )

    # Filters: same operating points the native 5E3 top-level uses, against
    # the 1 kHz/amp 0.5 sine. This pins the filter math to Keller's JSFX
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
    backend_names = [
        "hp3",
        "hp4",
        "peq1",
        "hs1",
        "peq1_hs1",
        "t4_pre_chain",
        "t5_pre_chain",
    ]
    for name, data in zip(backend_names, gen_backend_filter_diag(sine), strict=True):
        write(FIXTURES_DIR / f"filter_backend_{name}_sine05_48k.f32", data)

    # Composite tube stage — full pipeline (ADNL + PKD + advk feedback +
    # state) with dvs hard-coded to 0.  T2 (12AX7 v2) for the CK harness
    # because it has kpk>0 and a non-trivial advk path; T3 for cathodyne.
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
    # Dempwolf-Zölzer load-line curve.
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

    # Frozen-default top-level tap diagnostic.
    (
        tap_v_out,
        tap_res1_v,
        tap_res3_v,
        tap_res4_v,
        tap_drive_t4,
        tap_res5_v,
        tap_res_t5_v,
        tap_dvs2,
        tap_dvs3,
    ) = gen_nilamp_taps(sine.copy())
    write(FIXTURES_DIR / "nilamp_taps_v_out_48k.f32", tap_v_out)
    write(FIXTURES_DIR / "nilamp_taps_res1_v_48k.f32", tap_res1_v)
    write(FIXTURES_DIR / "nilamp_taps_res3_v_48k.f32", tap_res3_v)
    write(FIXTURES_DIR / "nilamp_taps_res4_v_48k.f32", tap_res4_v)
    write(FIXTURES_DIR / "nilamp_taps_drive_t4_48k.f32", tap_drive_t4)
    write(FIXTURES_DIR / "nilamp_taps_res5_v_48k.f32", tap_res5_v)
    write(FIXTURES_DIR / "nilamp_taps_res_t5_v_48k.f32", tap_res_t5_v)
    write(FIXTURES_DIR / "nilamp_taps_dvs2_48k.f32", tap_dvs2)
    write(FIXTURES_DIR / "nilamp_taps_dvs3_48k.f32", tap_dvs3)


if __name__ == "__main__":
    main()
