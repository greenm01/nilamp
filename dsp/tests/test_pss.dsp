// Test harness: power-supply sag stage (tube_pss).
//
// Pinned against tools.keller_oracle.TubePss.  Standalone harness
// driven by an externally generated dia signal; snext and dvs_in are
// hard-coded to 0 (single PSS stage in isolation, no upstream feed
// from another PSS lump).  Uses sag=1.0 -> r=22000 (TWD-DLX-II's p3
// resistor at full sag), tau=50ms (canonical PSU smoothing constant).
//
// Output 1: dvs_out (modulated supply voltage).
// Output 2: s       (smoothed dia, used as snext for upstream PSS).
import("stdfaust.lib");
tube = library("hk_tube.lib");

R = 22000.0;
TAU = 0.05;

process(dia) = tube.tube_pss(R, TAU, 0, dia, 0);
