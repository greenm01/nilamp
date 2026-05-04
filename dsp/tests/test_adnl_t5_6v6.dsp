// SPDX-License-Identifier: MIT
// Test harness: ADNL waveshaper bound to t5_6v6_table.
// T5 is the second mode-0 6V6 used by the push-pull aux branch.
import("stdfaust.lib");
adnl = library("hk_adnl.lib");
tables = library("5e3_tables.lib");

process(x) = adnl.adnl_process(13503, tables.t5_6v6_table, 15.0, 0.02, x);
