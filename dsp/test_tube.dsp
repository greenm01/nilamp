import("stdfaust.lib");
tube = library("hk_tube.lib");

// Trivial dummy table for compilation test
dummy_table = waveform {0,0,0,0,0,0,0,0,0};

process = tube.tube_ck_simple(dummy_table, 15, 0.02, 1, 0.001, 100000, 0, 0, 0, 1, 0, 0.01, 0.1, 0.1, 0.1, 0.1, 10);
