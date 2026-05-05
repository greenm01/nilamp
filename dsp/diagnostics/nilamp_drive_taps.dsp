// SPDX-License-Identifier: MIT
// Diagnostic top-level: emit pre-tube drive signals, the T3 outputs they
// derive from, and post-tube voltages for the public path and the v6 / v10
// rejected backend candidates.  Used by tools/compare_drive_taps.py to:
//   1. verify the linear pre-chains going *into* T4 / T5 match a Python
//      oracle to <= 1e-6 (regression guard for filter coefficients), and
//   2. compare post-tube voltages between candidates with PSS held identical,
//      isolating tube-state divergence from PSS-topology divergence.
//
// PSS feedback intentionally remains on the public T4-only sag loop, matching
// dsp/nilamp.dsp.  All extra T4/T5 instances feed off the same `old_dvs` and
// have their dia outputs discarded -- they do not perturb PSS.
//
// Outputs (20 channels):
//   T3 outputs:
//     0. res4_v_public         T3 plate, public path
//     1. res4_vk_public        T3 cathode, public path
//     2. res4_backend_v        T3 plate with hp(0.41) pre-T3 (v6 source)
//     3. res4_backend_vk       T3 cathode with hp(0.41) pre-T3 (v6 source)
//   Pre-tube drive signals:
//     4. t4_in_public_drive    public T4 drive = res4_v (raw, no extra filter)
//     5. t5_in_public_drive    public T5 drive = res4_vk * k2 -> hp(hp4) -> peq -> hs
//     6. t4_in_v6_drive        v6 T4 drive = res4_backend_v * k1 -> hp(hp3) -> peq -> hs
//     7. t4_in_v10_drive       v10 T4 drive = res4_v * k1 -> hp(hp3) -> peq -> hs
//   Post-tube voltages (PSS shared across all):
//     8. t4_v_public           T4 fed by ch4 (== public path raw T4)
//     9. t5_v_public           T5 fed by ch5
//    10. t4_v_v6                T4 fed by ch6
//    11. t5_v_v6                T5 fed by v6 T5 drive (res4_backend_vk -> k2/hp/peq/hs)
//    12. t4_v_v10               T4 fed by ch7
//    13. t5_v_v10               T5 fed by ch5 (== ch9 by construction; v10 keeps public T5)
//   T4 dia (post-tube cathode current, fed into PSS for public only):
//    14. t4_dia_public          T4 dia, public drive
//    15. t4_dia_v6              T4 dia, v6 drive (not fed to PSS)
//    16. t4_dia_v10             T4 dia, v10 drive (not fed to PSS)
//   T4 averager-feedback proxy (mirrors `tube_ck` internal `next_advk`):
//     proxy = lp(t4_avg_f, t4_v - dvs) * t4_kfb
//    17. t4_advk_public         averager-feedback proxy, public
//    18. t4_advk_v6             averager-feedback proxy, v6
//    19. t4_advk_v10            averager-feedback proxy, v10
//
// Channel 4 == channel 0 by construction; channel 13 == channel 9 by
// construction.  Both are kept as sanity slots.
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
t5_table = tables.t5_6v6_table;

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
        loop_block(v_in_ext) = (loop_core(v_in_ext) ~ _) : !, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _
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
                ch7_t4_in_v10_drive,
                ch8_t4_v_public,
                ch9_t5_v_public,
                ch10_t4_v_v6,
                ch11_t5_v_v6,
                ch12_t4_v_v10,
                ch13_t5_v_v10,
                ch14_t4_dia_public,
                ch15_t4_dia_v6,
                ch16_t4_dia_v10,
                ch17_t4_advk_public,
                ch18_t4_advk_v6,
                ch19_t4_advk_v10
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

                // Pre-tube drive signals (linear pre-chains into T4 / T5).
                drive_t4_public = res4_v;
                drive_t5_public = res4_vk
                    : *(k2_mode0)
                    : flt.flt_ii1_hp(hp4_hz)
                    : flt.flt_sv2_peq(kp1, fp_hz, qp1, 1, 1)
                    : flt.flt_sv1_hs(ks1, fs1_hz, 1);
                drive_t4_v6 = res4_backend_v_local
                    : *(k1_mode0)
                    : flt.flt_ii1_hp(hp3_hz)
                    : flt.flt_sv2_peq(kp1, fp_hz, qp1, 1, 1)
                    : flt.flt_sv1_hs(ks1, fs1_hz, 1);
                drive_t4_v10 = res4_v
                    : *(k1_mode0)
                    : flt.flt_ii1_hp(hp3_hz)
                    : flt.flt_sv2_peq(kp1, fp_hz, qp1, 1, 1)
                    : flt.flt_sv1_hs(ks1, fs1_hz, 1);
                drive_t5_v6 = res4_backend_vk_local
                    : *(k2_mode0)
                    : flt.flt_ii1_hp(hp4_hz)
                    : flt.flt_sv2_peq(kp1, fp_hz, qp1, 1, 1)
                    : flt.flt_sv1_hs(ks1, fs1_hz, 1);

                // Public T4 (drive = res4_v).  Provides dia for PSS and ch8.
                res_t4_public = tube.tube_ck_simple(
                    TBL_SIZE, t4_table, XMAX, DX,
                    c.t4_kpre, c.t4_isat, c.t4_rl, c.t4_kpk,
                    c.t4_kspre, c.t4_kspost, c.t4_ksva, c.t4_ksib, c.t4_kfb,
                    c.t4_pk_xth, c.t4_pk_xdiode, c.t4_pk_k1, c.t4_pk_k2,
                    c.t4_avg_f,
                    drive_t4_public, old_dvs);
                t4_v_public_local = res_t4_public : _ , !;
                t4_dia_public = res_t4_public : ! , _;

                // Public T5 (drive = drive_t5_public).  dia discarded.
                res_t5_public = tube.tube_ck_simple(
                    TBL_SIZE, t5_table, XMAX, DX,
                    c.t5_kpre, c.t5_isat, c.t5_rl, c.t5_kpk,
                    c.t5_kspre, c.t5_kspost, c.t5_ksva, c.t5_ksib, c.t5_kfb,
                    c.t5_pk_xth, c.t5_pk_xdiode, c.t5_pk_k1, c.t5_pk_k2,
                    c.t5_avg_f,
                    drive_t5_public, old_dvs);
                t5_v_public_local = res_t5_public : _ , !;

                // v6 T4 (drive = drive_t4_v6).  dia exposed for ch15.
                res_t4_v6 = tube.tube_ck_simple(
                    TBL_SIZE, t4_table, XMAX, DX,
                    c.t4_kpre, c.t4_isat, c.t4_rl, c.t4_kpk,
                    c.t4_kspre, c.t4_kspost, c.t4_ksva, c.t4_ksib, c.t4_kfb,
                    c.t4_pk_xth, c.t4_pk_xdiode, c.t4_pk_k1, c.t4_pk_k2,
                    c.t4_avg_f,
                    drive_t4_v6, old_dvs);
                t4_v_v6_local = res_t4_v6 : _ , !;
                t4_dia_v6_local = res_t4_v6 : ! , _;

                // v6 T5 (drive = drive_t5_v6).  dia discarded.
                res_t5_v6 = tube.tube_ck_simple(
                    TBL_SIZE, t5_table, XMAX, DX,
                    c.t5_kpre, c.t5_isat, c.t5_rl, c.t5_kpk,
                    c.t5_kspre, c.t5_kspost, c.t5_ksva, c.t5_ksib, c.t5_kfb,
                    c.t5_pk_xth, c.t5_pk_xdiode, c.t5_pk_k1, c.t5_pk_k2,
                    c.t5_avg_f,
                    drive_t5_v6, old_dvs);
                t5_v_v6_local = res_t5_v6 : _ , !;

                // v10 T4 (drive = drive_t4_v10).  dia exposed for ch16.
                res_t4_v10 = tube.tube_ck_simple(
                    TBL_SIZE, t4_table, XMAX, DX,
                    c.t4_kpre, c.t4_isat, c.t4_rl, c.t4_kpk,
                    c.t4_kspre, c.t4_kspost, c.t4_ksva, c.t4_ksib, c.t4_kfb,
                    c.t4_pk_xth, c.t4_pk_xdiode, c.t4_pk_k1, c.t4_pk_k2,
                    c.t4_avg_f,
                    drive_t4_v10, old_dvs);
                t4_v_v10_local = res_t4_v10 : _ , !;
                t4_dia_v10_local = res_t4_v10 : ! , _;

                // Averager-feedback proxy: lp(t4_avg_f, t4_v - dvs) * t4_kfb.
                // This mirrors `tube_ck` internal `next_advk` exactly given
                // identical pk/avg parameters; it is *not* the actual fed-back
                // signal inside the variant tubes (those keep their own
                // independent state), but it lets us see whether the variants'
                // post-tube voltages would, on this same time step, drive the
                // tube's grid-bias loop differently from public.
                t4_advk_public_local = (t4_v_public_local - old_dvs)
                    : flt.flt_ii1_lp(c.t4_avg_f) : *(c.t4_kfb);
                t4_advk_v6_local = (t4_v_v6_local - old_dvs)
                    : flt.flt_ii1_lp(c.t4_avg_f) : *(c.t4_kfb);
                t4_advk_v10_local = (t4_v_v10_local - old_dvs)
                    : flt.flt_ii1_lp(c.t4_avg_f) : *(c.t4_kfb);

                // PSS -- public T4-only sag loop (matches dsp/nilamp.dsp).
                total_dia_current = res1_dia + res3_dia + res4_dia + t4_dia_public;
                res_pss_current = tube.tube_pss(
                    r_pss, tau_pss, 0, total_dia_current, old_dvs);
                next_dvs_current = res_pss_current : _ , !;

                // Channel taps.
                ch0_res4_v_public = res4_v;
                ch1_res4_vk_public = res4_vk;
                ch2_res4_backend_v = res4_backend_v_local;
                ch3_res4_backend_vk = res4_backend_vk_local;
                ch4_t4_in_public_drive = drive_t4_public;
                ch5_t5_in_public_drive = drive_t5_public;
                ch6_t4_in_v6_drive = drive_t4_v6;
                ch7_t4_in_v10_drive = drive_t4_v10;
                ch8_t4_v_public = t4_v_public_local;
                ch9_t5_v_public = t5_v_public_local;
                ch10_t4_v_v6 = t4_v_v6_local;
                ch11_t5_v_v6 = t5_v_v6_local;
                ch12_t4_v_v10 = t4_v_v10_local;
                ch13_t5_v_v10 = t5_v_public_local;
                ch14_t4_dia_public = t4_dia_public;
                ch15_t4_dia_v6 = t4_dia_v6_local;
                ch16_t4_dia_v10 = t4_dia_v10_local;
                ch17_t4_advk_public = t4_advk_public_local;
                ch18_t4_advk_v6 = t4_advk_v6_local;
                ch19_t4_advk_v10 = t4_advk_v10_local;
            };
        };
    };
};
