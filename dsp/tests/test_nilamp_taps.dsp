// SPDX-License-Identifier: MIT
// Test harness: frozen-default top-level 5E3 taps around T3/T4 and the
// diagnostic T5 branch.  This does not change dsp/nilamp.dsp's public audio
// path; it exposes internal signals for oracle comparison before another
// top-level T5 wiring attempt.
import("stdfaust.lib");
tube = library("hk_tube.lib");
flt = library("hk_filters.lib");
tables = library("5e3_tables.lib");
c = library("5e3_constants.lib");

TBL_SIZE = 13503;
XMAX = 15.0;
DX = 0.02;

// ABX defaults, frozen so this harness isolates chain math instead of UI
// smoothing.  nilamp.dsp defaults: gain=0 dB; volume/bass/mid/treble/sag=50%.
gain1 = 1.0;
volume2 = 0.25;
bass2 = 0.25;
mid2 = 0.25;
treble2 = 0.25;
r_pss = 11000.0;
tau_pss = 0.05;

// Mode-0 T5 aux branch constants.
k2_mode0 = 0.940;
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
        // First output is fed back as old_dvs next sample.  The remaining
        // outputs are diagnostic taps:
        //   old_dvs, t3_v, t3_vk, t4_v, t5_v_diag, post_pp_diag,
        //   total_dia_current, total_dia_with_t5, next_dvs_current.
        loop_block(v_in_ext) = (loop_core(v_in_ext) ~ _) : !, _, _, _, _, _, _, _, _, _
        with {
            loop_core(v_in_ext, old_dvs) =
                next_dvs_current,
                old_dvs,
                res4_v,
                res4_vk,
                res5_v,
                res_t5_v,
                post_pp,
                total_dia_current,
                total_dia_with_t5,
                next_dvs_current
            with {
                res1 = tube.tube_ck_simple(
                    TBL_SIZE, tables.t1_12ax7_table, XMAX, DX,
                    c.t1_kpre, c.t1_isat, c.t1_rl, c.t1_kpk,
                    c.t1_kspre, c.t1_kspost, c.t1_ksva, c.t1_ksib, c.t1_kfb,
                    c.t1_pk_xth, c.t1_pk_xdiode, c.t1_pk_k1, c.t1_pk_k2,
                    c.t1_avg_f,
                    v_in_ext, old_dvs);
                res1_v = res1 : _ , !;
                res1_dia = res1 : ! , _;

                v2 = res1_v
                    : flt.flt_ii1_hp(10)
                    : *(volume2)
                    : flt.flt_sv2_tst(bass2, mid2, treble2, 630, 0.5, 1, 1)
                    : flt.flt_ii1_lp(8800);

                res3 = tube.tube_ck_simple(
                    TBL_SIZE, tables.t2_12ax7_table, XMAX, DX,
                    c.t2_kpre, c.t2_isat, c.t2_rl, c.t2_kpk,
                    c.t2_kspre, c.t2_kspost, c.t2_ksva, c.t2_ksib, c.t2_kfb,
                    c.t2_pk_xth, c.t2_pk_xdiode, c.t2_pk_k1, c.t2_pk_k2,
                    c.t2_avg_f,
                    v2, old_dvs);
                res3_v = res3 : _ , !;
                res3_dia = res3 : ! , _;

                res4 = tube.tube_cd(
                    TBL_SIZE, tables.t3_cd_table, XMAX, DX,
                    c.t3_kpre, c.t3_isat, c.t3_rl, c.t3_rkl, c.t3_kpk,
                    c.t3_kspre, c.t3_kspost, c.t3_ksva, c.t3_ksvk, c.t3_ksib,
                    c.t3_pk_xth, c.t3_pk_xdiode, c.t3_pk_k1, c.t3_pk_k2,
                    1, 0, 0, 0, 0,
                    res3_v, old_dvs);
                res4_v = res4 : _ , ! , !;
                res4_vk = res4 : ! , _ , !;
                res4_dia = res4 : ! , ! , _;

                res5 = tube.tube_ck_simple(
                    TBL_SIZE, tables.t4_6v6_table, XMAX, DX,
                    c.t4_kpre, c.t4_isat, c.t4_rl, c.t4_kpk,
                    c.t4_kspre, c.t4_kspost, c.t4_ksva, c.t4_ksib, c.t4_kfb,
                    c.t4_pk_xth, c.t4_pk_xdiode, c.t4_pk_k1, c.t4_pk_k2,
                    c.t4_avg_f,
                    res4_v, old_dvs);
                res5_v = res5 : _ , !;
                res5_dia = res5 : ! , _;

                t5_in = res4_vk
                    : *(k2_mode0)
                    : flt.flt_ii1_hp(hp4_hz)
                    : flt.flt_sv2_peq(kp1, fp_hz, qp1, 1, 1)
                    : flt.flt_sv1_hs(ks1, fs1_hz, 1);

                res_t5 = tube.tube_ck_simple(
                    TBL_SIZE, tables.t5_6v6_table, XMAX, DX,
                    c.t5_kpre, c.t5_isat, c.t5_rl, c.t5_kpk,
                    c.t5_kspre, c.t5_kspost, c.t5_ksva, c.t5_ksib, c.t5_kfb,
                    c.t5_pk_xth, c.t5_pk_xdiode, c.t5_pk_k1, c.t5_pk_k2,
                    c.t5_avg_f,
                    t5_in, old_dvs);
                res_t5_v = res_t5 : _ , !;
                res_t5_dia = res_t5 : ! , _;

                post_pp = res5_v - res_t5_v;
                total_dia_current = res1_dia + res3_dia + res4_dia + res5_dia;
                total_dia_with_t5 = total_dia_current + res_t5_dia;

                res_pss_current = tube.tube_pss(
                    r_pss, tau_pss, 0, total_dia_current, old_dvs);
                next_dvs_current = res_pss_current : _ , !;
            };
        };
    };
};
