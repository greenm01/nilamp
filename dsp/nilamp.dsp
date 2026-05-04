import("stdfaust.lib");
tube = library("hk_tube.lib");
adnl = library("hk_adnl.lib");
pkd = library("hk_pkd.lib");
flt = library("hk_filters.lib");
tables = library("5e3_tables.lib");

// --- 5E3 Parameters (Partial) ---
gain1 = hslider("Input Gain [unit:dB]", 0, -12, 12, 0.1) : ba.db2linear : si.smoo;
volume = hslider("Volume [%]", 50, 0, 100, 1) / 100.0 : si.smoo;
bass = hslider("Bass [%]", 50, 0, 100, 1) / 100.0 : si.smoo;
mid = hslider("Mid [%]", 50, 0, 100, 1) / 100.0 : si.smoo;
treble = hslider("Treble [%]", 50, 0, 100, 1) / 100.0 : si.smoo;

// --- 5E3 Processing ---
process = _ : *(gain1) : stage1 : stage2 : stage3 : stage4 : stage5 : *(0.1);

// Stage 1: T1 (12AY7)
stage1(v) = tube.tube_ck_simple(tables.t1_12ay7_table, 15, 0.02, 0.16, 0.0022, 100000, 0, 0, 0, 0.2, 0, 1.0, 0, 0.25, 0.1, 0.1, 10, v, 0);

// Stage 2: Tonestack + Gain
stage2 = flt.flt_ii1_hp(10) : *(volume*volume) : flt.flt_sv2_tst(bass*bass, mid*mid, treble*treble, 500, 0.5, 1, 1) : flt.flt_ii1_lp(8800);

// Stage 3: T2 (12AX7)
stage3(v) = tube.tube_ck_simple(tables.t2_12ax7_table, 15, 0.02, 0.398, 0.00155, 100000, 0, 0, 0, 0.38, 0, 1.0, 0, 0.25, 0.1, 0.1, 10, v, 0);

// Stage 4: T3 (Cathodyne) - Simplified
stage4(v) = tube.tube_cd(tables.t3_cd_table, 15, 0.02, 0.1, 0.0016, 56000, 57500, 0, 0, 0, 0.4, 0.4, 0, 0, 0, 0.1, 0.1, 1, 0, 0, 0, 0, v, 0) : _ , !;

// Stage 5: T4 (Power Tube 6V6)
stage5(v) = tube.tube_ck_simple(tables.t4_6v6_table, 15, 0.02, 0.1, 0.11, 3000, 0, 0, 0, 0.1, 0, 1.0, 0, 0.25, 0.1, 0.1, 10, v, 0);
