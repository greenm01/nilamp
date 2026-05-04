// SPDX-License-Identifier: MIT
// Diagnostic top-level: emit pre-tube drive signals (and the T3 outputs they
// derive from) for the public path and selected rejected backend candidates.
// Used by tools/compare_drive_taps.py to verify that the linear pre-chains
// going *into* T4 / T5 match a Python oracle to <= 1e-6.  This isolates
// whether the public-vs-rejected ABX gap lives upstream (filter-coefficient
// or state interaction) or downstream (tube state / PSS timing) of the tubes.
//
// PSS feedback intentionally remains on the public T4-only sag loop, matching
// dsp/nilamp.dsp; we are inspecting drive into the tubes only.
//
// Outputs (8 channels):
//   0. res4_v_public         T3 plate, public path (input to public T4)
//   1. res4_vk_public        T3 cathode, public path (input to public T5)
//   2. res4_backend_v        T3 plate with hp(0.41) pre-T3 (v6 source)
//   3. res4_backend_vk       T3 cathode with hp(0.41) pre-T3 (v6 source)
//   4. t4_in_public_drive    public T4 drive = res4_v (raw, no extra filter)
//   5. t5_in_public_drive    public T5 drive = res4_vk * k2 -> hp(hp4) -> peq -> hs
//   6. t4_in_v6_drive        v6 T4 drive = res4_backend_v * k1 -> hp(hp3) -> peq -> hs
//   7. t4_in_v10_drive       v10 T4 drive = res4_v * k1 -> hp(hp3) -> peq -> hs
//
// Channel 4 == channel 0 by construction; kept as a sanity slot.
import("stdfaust.lib");
tube = library("hk_tube.lib");
flt = library("hk_filters.lib");
tables = library("5e3_tables.lib");
c = library("5e3_constants.lib");

gain1 = hslider("Input Gain [unit:dB]", 0, -12, 12, 0.1) : ba.db2linear : si.smoo;
volume = hslider("Volume [%]", 50, 0, 100, 1) / 100.0 : si.smoo;
bass = hslider("Bass [%]", 50, 0, 100, 1) / 100.0 : si.smoo;
mid = hslider("Mid [%]", 50, 0, 100, 1) / 100.0 : si.smoo;
treble = hslider("Treble [%]", 50, 0, 100, 1) / 100.0 : si.smoo;
sag = hslider("Sag [%]", 50, 0, 100, 1) / 100.0 : si.smoo;

r_pss = sag * 22000.0;
tau_pss = 0.05;

TBL_SIZE = 13503;
XMAX = 15.0;
DX = 0.02;

t1_table = tables.t1_12ax7_table;
t2_table = tables.t2_12ax7_table;
t3_table = tables.t3_cd_table;
t4_table = tables.t4_6v6_table;

// Mode-0 power-chain constants (mirror dsp/diagnostics/nilamp_t5_balance.dsp).
k1_mode0 = 0.797;
k2_mode0 = 0.940;
hp3_hz = 5.8;
hp4_hz = 6.4;
kp1 = 1.1220184543;
fp_hz = 80.0;
qp1 = 2.6685237666;
ks1 = 1.4125375446;
fs1_hz = 2098.1359672;

process = _ : *(gain1) : global_loop
with {
    global_loop(vin) = loop_block(vin)
    with {
        loop_block(v_in_ext) = (loop_core(v_in_ext) ~ _) : !, _, _, _, _, _, _, _, _
        with {
            loop_core(v_in_ext, old_dvs) =
                next_dvs_current,
                ch0_res4_v_public,
                ch1_res4_vk_public,
                ch2_res4_backend_v,
                ch3_res4_backend_vk,
                ch4_t4_in_public_drive,
                ch5_t5_in_public_drive,
                ch6_t4_in_v6_drive,
                ch7_t4_in_v10_drive
            with {
                res1 = tube.tube_ck_simple(
                    TBL_SIZE, t1_table, XMAX, DX,
                    c.t1_kpre, c.t1_isat, c.t1_rl, c.t1_kpk,
                    c.t1_kspre, c.t1_kspost, c.t1_ksva, c.t1_ksib, c.t1_kfb,
                    c.t1_pk_xth, c.t1_pk_xdiode, c.t1_pk_k1, c.t1_pk_k2,
                    c.t1_avg_f,
                    v_in_ext, old_dvs);
                res1_v = res1 : _ , !;
                res1_dia = res1 : ! , _;

                v2 = res1_v
                    : flt.flt_ii1_hp(10)
                    : *(volume * volume)
                    : flt.flt_sv2_tst(bass * bass, mid * mid, treble * treble,
                                      630, 0.5, 1, 1)
                    : flt.flt_ii1_lp(8800);

                res3 = tube.tube_ck_simple(
                    TBL_SIZE, t2_table, XMAX, DX,
                    c.t2_kpre, c.t2_isat, c.t2_rl, c.t2_kpk,
                    c.t2_kspre, c.t2_kspost, c.t2_ksva, c.t2_ksib, c.t2_kfb,
                    c.t2_pk_xth, c.t2_pk_xdiode, c.t2_pk_k1, c.t2_pk_k2,
                    c.t2_avg_f,
                    v2, old_dvs);
                res3_v = res3 : _ , !;
                res3_dia = res3 : ! , _;

                // Public T3 (no pre-filter into T3 grid).
                res4 = tube.tube_cd(
                    TBL_SIZE, t3_table, XMAX, DX,
                    c.t3_kpre, c.t3_isat, c.t3_rl, c.t3_rkl, c.t3_kpk,
                    c.t3_kspre, c.t3_kspost, c.t3_ksva, c.t3_ksvk, c.t3_ksib,
                    c.t3_pk_xth, c.t3_pk_xdiode, c.t3_pk_k1, c.t3_pk_k2,
                    1, 0, 0, 0, 0,
                    res3_v, old_dvs);
                res4_v = res4 : _ , ! , !;
                res4_vk = res4 : ! , _ , !;
                res4_dia = res4 : ! , ! , _;

                // v6 T3 (hp(0.41) inserted before T3 grid).
                res4_backend = tube.tube_cd(
                    TBL_SIZE, t3_table, XMAX, DX,
                    c.t3_kpre, c.t3_isat, c.t3_rl, c.t3_rkl, c.t3_kpk,
                    c.t3_kspre, c.t3_kspost, c.t3_ksva, c.t3_ksvk, c.t3_ksib,
                    c.t3_pk_xth, c.t3_pk_xdiode, c.t3_pk_k1, c.t3_pk_k2,
                    1, 0, 0, 0, 0,
                    res3_v : flt.flt_ii1_hp(0.41), old_dvs);
                res4_backend_v_local = res4_backend : _ , ! , !;
                res4_backend_vk_local = res4_backend : ! , _ , !;

                // Public T4 (raw drive = res4_v) -- needed only for dia/PSS.
                res_t4_raw = tube.tube_ck_simple(
                    TBL_SIZE, t4_table, XMAX, DX,
                    c.t4_kpre, c.t4_isat, c.t4_rl, c.t4_kpk,
                    c.t4_kspre, c.t4_kspost, c.t4_ksva, c.t4_ksib, c.t4_kfb,
                    c.t4_pk_xth, c.t4_pk_xdiode, c.t4_pk_k1, c.t4_pk_k2,
                    c.t4_avg_f,
                    res4_v, old_dvs);
                t4_raw_dia = res_t4_raw : ! , _;

                // PSS -- public T4-only sag loop (matches dsp/nilamp.dsp).
                total_dia_current = res1_dia + res3_dia + res4_dia + t4_raw_dia;
                res_pss_current = tube.tube_pss(
                    r_pss, tau_pss, 0, total_dia_current, old_dvs);
                next_dvs_current = res_pss_current : _ , !;

                // Drive-tap channels.
                ch0_res4_v_public = res4_v;
                ch1_res4_vk_public = res4_vk;
                ch2_res4_backend_v = res4_backend_v_local;
                ch3_res4_backend_vk = res4_backend_vk_local;
                ch4_t4_in_public_drive = res4_v;
                ch5_t5_in_public_drive = res4_vk
                    : *(k2_mode0)
                    : flt.flt_ii1_hp(hp4_hz)
                    : flt.flt_sv2_peq(kp1, fp_hz, qp1, 1, 1)
                    : flt.flt_sv1_hs(ks1, fs1_hz, 1);
                ch6_t4_in_v6_drive = res4_backend_v_local
                    : *(k1_mode0)
                    : flt.flt_ii1_hp(hp3_hz)
                    : flt.flt_sv2_peq(kp1, fp_hz, qp1, 1, 1)
                    : flt.flt_sv1_hs(ks1, fs1_hz, 1);
                ch7_t4_in_v10_drive = res4_v
                    : *(k1_mode0)
                    : flt.flt_ii1_hp(hp3_hz)
                    : flt.flt_sv2_peq(kp1, fp_hz, qp1, 1, 1)
                    : flt.flt_sv1_hs(ks1, fs1_hz, 1);
            };
        };
    };
};
