// SPDX-License-Identifier: MIT
// Diagnostic top-level: render T5 branch-balance variants without changing
// dsp/nilamp.dsp's public audio path.
//
// Outputs:
//   0. v0_current                  current T4-only path + half denominator
//   1. v1_raw_t4_filtered_t5       raw T4 - filtered T5 + full denominator
//   2. v2_filtered_t4_filtered_t5  filtered T4 - filtered T5 + full denominator
//   3. v3_sign_add                 filtered T4 + filtered T5 + full denominator
//   4. v4_half_denom_control       filtered T4 - filtered T5 + half denominator
//   5. v5_post_backend_current_sag post-power PEQ/HS/HP/LP on current T5 mix
//   6. v6_full_backend_current_sag pre/post backend around T4/T5
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

// Mode-0 power-chain constants from twd_dlx_ii_harness.jsfx.
k1_mode0 = 0.797;
k2_mode0 = 0.940;
hp3_hz = 5.8;
hp4_hz = 6.4;
kp1 = 1.1220184543;
kp2 = 1.2589254118;
fp_hz = 80.0;
qp1 = 2.6685237666;
qp2 = 2.2440931043;
ks1 = 1.4125375446;
ks2 = 1.4125375446;
fs1_hz = 2098.1359672;
fs2_hz = 1485.8089753;

half_gout = 0.5 / (c.t4_rl * c.t4_isat);
full_gout = 0.5 / (c.t4_rl * c.t4_isat + c.t5_rl * c.t5_isat);

process = _ : *(gain1) : global_loop
with {
    global_loop(vin) = loop_block(vin)
    with {
        loop_block(v_in_ext) = (loop_core(v_in_ext) ~ _) : !, _, _, _, _, _, _, _
        with {
            loop_core(v_in_ext, old_dvs) =
                next_dvs_current,
                v0_current,
                v1_raw_t4_filtered_t5,
                v2_filtered_t4_filtered_t5,
                v3_sign_add,
                v4_half_denom_control,
                v5_post_backend_current_sag,
                v6_full_backend_current_sag
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

                res4_backend = tube.tube_cd(
                    TBL_SIZE, t3_table, XMAX, DX,
                    c.t3_kpre, c.t3_isat, c.t3_rl, c.t3_rkl, c.t3_kpk,
                    c.t3_kspre, c.t3_kspost, c.t3_ksva, c.t3_ksvk, c.t3_ksib,
                    c.t3_pk_xth, c.t3_pk_xdiode, c.t3_pk_k1, c.t3_pk_k2,
                    1, 0, 0, 0, 0,
                    res3_v : flt.flt_ii1_hp(0.41), old_dvs);
                res4_backend_v = res4_backend : _ , ! , !;
                res4_backend_vk = res4_backend : ! , _ , !;

                res_t4_raw = tube.tube_ck_simple(
                    TBL_SIZE, t4_table, XMAX, DX,
                    c.t4_kpre, c.t4_isat, c.t4_rl, c.t4_kpk,
                    c.t4_kspre, c.t4_kspost, c.t4_ksva, c.t4_ksib, c.t4_kfb,
                    c.t4_pk_xth, c.t4_pk_xdiode, c.t4_pk_k1, c.t4_pk_k2,
                    c.t4_avg_f,
                    res4_v, old_dvs);
                t4_raw_v = res_t4_raw : _ , !;
                t4_raw_dia = res_t4_raw : ! , _;

                t4_filtered_in = res4_v
                    : *(k1_mode0)
                    : flt.flt_ii1_hp(hp3_hz)
                    : flt.flt_sv2_peq(kp1, fp_hz, qp1, 1, 1)
                    : flt.flt_sv1_hs(ks1, fs1_hz, 1);

                res_t4_filtered = tube.tube_ck_simple(
                    TBL_SIZE, t4_table, XMAX, DX,
                    c.t4_kpre, c.t4_isat, c.t4_rl, c.t4_kpk,
                    c.t4_kspre, c.t4_kspost, c.t4_ksva, c.t4_ksib, c.t4_kfb,
                    c.t4_pk_xth, c.t4_pk_xdiode, c.t4_pk_k1, c.t4_pk_k2,
                    c.t4_avg_f,
                    t4_filtered_in, old_dvs);
                t4_filtered_v = res_t4_filtered : _ , !;

                t4_backend_in = res4_backend_v
                    : *(k1_mode0)
                    : flt.flt_ii1_hp(hp3_hz)
                    : flt.flt_sv2_peq(kp1, fp_hz, qp1, 1, 1)
                    : flt.flt_sv1_hs(ks1, fs1_hz, 1);

                res_t4_backend = tube.tube_ck_simple(
                    TBL_SIZE, t4_table, XMAX, DX,
                    c.t4_kpre, c.t4_isat, c.t4_rl, c.t4_kpk,
                    c.t4_kspre, c.t4_kspost, c.t4_ksva, c.t4_ksib, c.t4_kfb,
                    c.t4_pk_xth, c.t4_pk_xdiode, c.t4_pk_k1, c.t4_pk_k2,
                    c.t4_avg_f,
                    t4_backend_in, old_dvs);
                t4_backend_v = res_t4_backend : _ , !;

                t5_in = res4_vk
                    : *(k2_mode0)
                    : flt.flt_ii1_hp(hp4_hz)
                    : flt.flt_sv2_peq(kp1, fp_hz, qp1, 1, 1)
                    : flt.flt_sv1_hs(ks1, fs1_hz, 1);

                res_t5 = tube.tube_ck_simple(
                    TBL_SIZE, t5_table, XMAX, DX,
                    c.t5_kpre, c.t5_isat, c.t5_rl, c.t5_kpk,
                    c.t5_kspre, c.t5_kspost, c.t5_ksva, c.t5_ksib, c.t5_kfb,
                    c.t5_pk_xth, c.t5_pk_xdiode, c.t5_pk_k1, c.t5_pk_k2,
                    c.t5_avg_f,
                    t5_in, old_dvs);
                t5_v = res_t5 : _ , !;

                t5_backend_in = res4_backend_vk
                    : *(k2_mode0)
                    : flt.flt_ii1_hp(hp4_hz)
                    : flt.flt_sv2_peq(kp1, fp_hz, qp1, 1, 1)
                    : flt.flt_sv1_hs(ks1, fs1_hz, 1);

                res_t5_backend = tube.tube_ck_simple(
                    TBL_SIZE, t5_table, XMAX, DX,
                    c.t5_kpre, c.t5_isat, c.t5_rl, c.t5_kpk,
                    c.t5_kspre, c.t5_kspost, c.t5_ksva, c.t5_ksib, c.t5_kfb,
                    c.t5_pk_xth, c.t5_pk_xdiode, c.t5_pk_k1, c.t5_pk_k2,
                    c.t5_avg_f,
                    t5_backend_in, old_dvs);
                t5_backend_v = res_t5_backend : _ , !;

                // Keep all variants on the current T4-only sag loop.  This
                // isolates branch mix/gain/phase from PSS topology changes.
                total_dia_current = res1_dia + res3_dia + res4_dia + t4_raw_dia;
                res_pss_current = tube.tube_pss(
                    r_pss, tau_pss, 0, total_dia_current, old_dvs);
                next_dvs_current = res_pss_current : _ , !;

                v0_current = t4_raw_v
                    : *(half_gout)
                    : flt.flt_ii1_hp(40);
                v1_raw_t4_filtered_t5 = (t4_raw_v - t5_v)
                    : *(full_gout)
                    : flt.flt_ii1_hp(40);
                v2_filtered_t4_filtered_t5 = (t4_filtered_v - t5_v)
                    : *(full_gout)
                    : flt.flt_ii1_hp(40);
                v3_sign_add = (t4_filtered_v + t5_v)
                    : *(full_gout)
                    : flt.flt_ii1_hp(40);
                v4_half_denom_control = (t4_filtered_v - t5_v)
                    : *(half_gout)
                    : flt.flt_ii1_hp(40);
                v5_post_backend_current_sag = (t4_raw_v - t5_v)
                    : flt.flt_sv2_peq(kp2, fp_hz, qp2, 1, 1)
                    : flt.flt_sv1_hs(ks2, fs2_hz, 1)
                    : flt.flt_ii1_hp(40)
                    : flt.flt_df2_lp(10000, sqrt(0.5), 1, 0)
                    : *(full_gout);
                v6_full_backend_current_sag = (t4_backend_v - t5_backend_v)
                    : flt.flt_sv2_peq(kp2, fp_hz, qp2, 1, 1)
                    : flt.flt_sv1_hs(ks2, fs2_hz, 1)
                    : flt.flt_ii1_hp(40)
                    : flt.flt_df2_lp(10000, sqrt(0.5), 1, 0)
                    : *(full_gout);
            };
        };
    };
};
