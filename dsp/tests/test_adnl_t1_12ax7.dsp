// SPDX-License-Identifier: MIT
// Test harness: ADNL waveshaper bound to t1_12ax7_table.
// Single input (audio sample x), single output (waveshaped + ADAA).
//
// xmax/dx must match the values used to generate the table in
// tools/gen_5e3_tables.py (defaults: 15.0 and 0.02).  The table size
// passed to adnl_process is the literal cell count of the waveform
// (segments * 9 + 3 metadata cells = 1500 * 9 + 3 = 13503).
import("stdfaust.lib");
adnl = library("hk_adnl.lib");
tables = library("5e3_tables.lib");

process(x) = adnl.adnl_process(13503, tables.t1_12ax7_table, 15.0, 0.02, x);
