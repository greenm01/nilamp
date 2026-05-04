// Test harness: 1st-order impulse-invariant low-pass at 8.8 kHz.
// Matches the LP that nilamp.dsp drops on the chain output (dsp/nilamp.dsp:37).
import("stdfaust.lib");
flt = library("hk_filters.lib");

process(x) = flt.flt_ii1_lp(8800, x);
