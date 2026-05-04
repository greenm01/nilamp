// SPDX-License-Identifier: MIT
// Test harness: ADNL waveshaper bound to t3_cd_table (cathodyne).
// The ADNL block itself produces a single output regardless of the
// downstream tube_cd 2-output structure; we test only the table+ADAA path
// here.  See test_adnl_t1_12ax7.dsp for harness shape and convention notes.
import("stdfaust.lib");
adnl = library("hk_adnl.lib");
tables = library("5e3_tables.lib");

process(x) = adnl.adnl_process(13503, tables.t3_cd_table, 15.0, 0.02, x);
