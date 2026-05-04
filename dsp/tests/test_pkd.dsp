// Test harness: PKD (peak detector) — single-input, single-output
// driven by Keller's xth/xdiode/k1/k2 parameters wired as Faust controls.
import("stdfaust.lib");
pkd = library("hk_pkd.lib");

// Externalize parameters as horizontal sliders so the Rust harness can
// program them via set_param().  Indices (left-to-right of declaration):
//   0: xth
//   1: xdiode
//   2: k1   (precomputed attack coefficient)
//   3: k2   (precomputed release coefficient)
xth    = hslider("xth",    0.0, -10.0, 10.0,  0.0001);
xdiode = hslider("xdiode", 0.001, 1e-6,  1.0,   1e-6);
k1     = hslider("k1",     0.5,   0.0,   1.0,   1e-7);
k2     = hslider("k2",     0.99,  0.0,   1.0,   1e-7);

process = pkd.pkd_process(xth, xdiode, k1, k2);
