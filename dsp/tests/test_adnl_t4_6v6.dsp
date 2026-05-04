// Test harness: ADNL waveshaper bound to t4_6v6_table (power triode).
// Note: 6V6 in 5E3 prototype is wired as triode-mode (b=2, see
// tools/gen_5e3_tables.py); push-pull / pentode is deferred to 5e3-v2.
// See test_adnl_t1_12ax7.dsp for harness shape and convention notes.
import("stdfaust.lib");
adnl = library("hk_adnl.lib");
tables = library("5e3_tables.lib");

process(x) = adnl.adnl_process(13503, tables.t4_6v6_table, 15.0, 0.02, x);
