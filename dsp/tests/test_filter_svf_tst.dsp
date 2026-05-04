// Test harness: 2nd-order TPT SVF wired as a tonestack.
// Frozen at the same operating point nilamp.dsp uses for its mid-section
// tonestack (dsp/nilamp.dsp:37): bass=mid=treble=0.5, so b = m = t = 0.25
// after squaring; f = 500 Hz, Q = 0.5, both prewarp flags on.
import("stdfaust.lib");
flt = library("hk_filters.lib");

process(x) = flt.flt_sv2_tst(0.25, 0.25, 0.25, 500, 0.5, 1, 1, x);
