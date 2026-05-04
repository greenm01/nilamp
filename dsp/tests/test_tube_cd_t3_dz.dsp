// SPDX-License-Identifier: MIT
// Test harness: full tube_cd pipeline with T3 (cathodyne) parameters,
// using the Dempwolf-Zölzer load-line ADNL table (t3_cd_table_dz).
//
// Companion to dsp/tests/test_tube_cd.dsp.  Same topology + runtime
// constants; only the ADNL waveform differs.  Pins the DZ cathodyne
// load-line solver + table generator + Faust wiring against the
// oracle's adnl_set_dz_cd.
//
// dvs hard-coded to 0; NEQ pinned to identity.  See test_tube_cd.dsp.
import("stdfaust.lib");
tube = library("hk_tube.lib");
tables = library("5e3_tables.lib");
c = library("5e3_constants.lib");

process(v) = tube.tube_cd(
    13503, tables.t3_cd_table_dz, 15.0, 0.02,
    c.t3_kpre, c.t3_isat, c.t3_rl, c.t3_rkl, c.t3_kpk,
    c.t3_kspre, c.t3_kspost, c.t3_ksva, c.t3_ksvk, c.t3_ksib,
    c.t3_pk_xth, c.t3_pk_xdiode, c.t3_pk_k1, c.t3_pk_k2,
    1, 0, 0, 0, 0,
    v, 0);
