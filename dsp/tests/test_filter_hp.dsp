// Test harness: 1st-order impulse-invariant high-pass at 10 Hz.
// Matches the DC-blocker nilamp.dsp drops in front of the volume control
// (dsp/nilamp.dsp:37).
import("stdfaust.lib");
flt = library("hk_filters.lib");

process(x) = flt.flt_ii1_hp(10, x);
