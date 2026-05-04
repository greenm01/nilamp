// SPDX-License-Identifier: MIT
// Test harness: mode-0 T4/T5 push-pull branch with synthetic T3 taps.
//
// Inputs:
//   1. t3_v  - synthetic T3 plate output
//   2. t3_vk - synthetic T3 cathode output
//
// Outputs:
//   1. T4 v_out
//   2. T5 v_out
//   3. post push-pull value (T4 - T5)
//   4. summed dia (T4 + T5)
import("stdfaust.lib");
tube = library("hk_tube.lib");
flt = library("hk_filters.lib");
tables = library("5e3_tables.lib");
c = library("5e3_constants.lib");

k1_mode0 = 0.797;
k2_mode0 = 0.940;
hp3_hz = 5.8;
hp4_hz = 6.4;
kp1 = 1.1220184543;
fp_hz = 80.0;
qp1 = 2.6685237666;
ks1 = 1.4125375446;
fs1_hz = 2098.1359672;

process(t3_v, t3_vk) = t4_v, t5_v, post_pp, total_dia
with {
    t4_in = t3_v
        : *(k1_mode0)
        : flt.flt_ii1_hp(hp3_hz)
        : flt.flt_sv2_peq(kp1, fp_hz, qp1, 1, 1)
        : flt.flt_sv1_hs(ks1, fs1_hz, 1);
    t5_in = t3_vk
        : *(k2_mode0)
        : flt.flt_ii1_hp(hp4_hz)
        : flt.flt_sv2_peq(kp1, fp_hz, qp1, 1, 1)
        : flt.flt_sv1_hs(ks1, fs1_hz, 1);

    res_t4 = tube.tube_ck_simple(
        13503, tables.t4_6v6_table, 15.0, 0.02,
        c.t4_kpre, c.t4_isat, c.t4_rl, c.t4_kpk,
        c.t4_kspre, c.t4_kspost, c.t4_ksva, c.t4_ksib, c.t4_kfb,
        c.t4_pk_xth, c.t4_pk_xdiode, c.t4_pk_k1, c.t4_pk_k2,
        c.t4_avg_f,
        t4_in, 0);
    t4_v = res_t4 : _ , !;
    t4_dia = res_t4 : ! , _;

    res_t5 = tube.tube_ck_simple(
        13503, tables.t5_6v6_table, 15.0, 0.02,
        c.t5_kpre, c.t5_isat, c.t5_rl, c.t5_kpk,
        c.t5_kspre, c.t5_kspost, c.t5_ksva, c.t5_ksib, c.t5_kfb,
        c.t5_pk_xth, c.t5_pk_xdiode, c.t5_pk_k1, c.t5_pk_k2,
        c.t5_avg_f,
        t5_in, 0);
    t5_v = res_t5 : _ , !;
    t5_dia = res_t5 : ! , _;

    post_pp = t4_v - t5_v;
    total_dia = t4_dia + t5_dia;
};
