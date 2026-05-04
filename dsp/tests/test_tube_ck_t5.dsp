// SPDX-License-Identifier: MIT
// Test harness: full tube_ck pipeline with T5 (mode-0 6V6) parameters.
//
// Output 1: v_out (plate voltage relative to bias).
// Output 2: dia   (anode-current delta, used downstream for PSS sag).
import("stdfaust.lib");
tube = library("hk_tube.lib");
tables = library("5e3_tables.lib");
c = library("5e3_constants.lib");

process(v) = tube.tube_ck_simple(
    13503, tables.t5_6v6_table, 15.0, 0.02,
    c.t5_kpre, c.t5_isat, c.t5_rl, c.t5_kpk,
    c.t5_kspre, c.t5_kspost, c.t5_ksva, c.t5_ksib, c.t5_kfb,
    c.t5_pk_xth, c.t5_pk_xdiode, c.t5_pk_k1, c.t5_pk_k2,
    c.t5_avg_f,
    v, 0);
