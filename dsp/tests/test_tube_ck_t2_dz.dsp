// SPDX-License-Identifier: MIT
// Test harness: full tube_ck pipeline with T2 (12AX7 v2) parameters,
// using the Dempwolf-Zölzer load-line ADNL table (t2_12ax7_table_dz).
//
// Companion to dsp/tests/test_tube_ck.dsp: same call, identical
// topology + runtime constants, only the waveshaping table differs.
// The Python oracle builds the table via tools.gen_tables.gen_adnl_table_dz_ck
// (which calls keller_oracle.adnl_set_dz_ck) so a successful match here
// pins the entire DZ generator + Faust wiring path against the oracle.
//
// dvs hard-coded to 0 (open-loop), see test_tube_ck.dsp.
import("stdfaust.lib");
tube = library("hk_tube.lib");
tables = library("5e3_tables.lib");
c = library("5e3_constants.lib");

process(v) = tube.tube_ck_simple(
    13503, tables.t2_12ax7_table_dz, 15.0, 0.02,
    c.t2_kpre, c.t2_isat, c.t2_rl, c.t2_kpk,
    c.t2_kspre, c.t2_kspost, c.t2_ksva, c.t2_ksib, c.t2_kfb,
    c.t2_pk_xth, c.t2_pk_xdiode, c.t2_pk_k1, c.t2_pk_k2,
    c.t2_avg_f,
    v, 0);
