// Test harness: ADNL waveshaper bound to t2_12ax7_table.
// See test_adnl_t1_12ax7.dsp for harness shape and convention notes.
import("stdfaust.lib");
adnl = library("hk_adnl.lib");
tables = library("5e3_tables.lib");

process(x) = adnl.adnl_process(13503, tables.t2_12ax7_table, 15.0, 0.02, x);
