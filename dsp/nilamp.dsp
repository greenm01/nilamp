import("stdfaust.lib");
tube = library("hk_tube.lib");
adnl = library("hk_adnl.lib");
pkd = library("hk_pkd.lib");
flt = library("hk_filters.lib");
tables = library("5e3_tables.lib");

// --- 5E3 Parameters ---
gain1 = hslider("Input Gain [unit:dB]", 0, -12, 12, 0.1) : ba.db2linear : si.smoo;
volume = hslider("Volume [%]", 50, 0, 100, 1) / 100.0 : si.smoo;
bass = hslider("Bass [%]", 50, 0, 100, 1) / 100.0 : si.smoo;
mid = hslider("Mid [%]", 50, 0, 100, 1) / 100.0 : si.smoo;
treble = hslider("Treble [%]", 50, 0, 100, 1) / 100.0 : si.smoo;
sag = hslider("Sag [%]", 50, 0, 100, 1) / 100.0 : si.smoo;

// PSU parameters
r_pss = sag * 1000.0; // PSU internal resistance
tau_pss = 0.05; // 50ms time constant

// --- 5E3 Processing with Global dvs Loop ---
process = _ : *(gain1) : global_loop
with {
    global_loop(vin) = loop_block(vin)
    with {
        loop_block(v_in_ext) = loop_core(v_in_ext) ~ _
        with {
            loop_core(v_in_ext, old_dvs) = next_dvs, v_out
            with {
                // Stage 1: T1 (12AX7 v1)
                res1 = tables.t1_12ax7_table : tube.tube_ck_simple(15, 0.02, 0.16, 0.0022, 100000, 0, 0, 0, 0.2, 0, 1.0, 0, 0.25, 0.1, 0.1, 10, v_in_ext, old_dvs);
                res1_v = res1 : _ , !;
                res1_dia = res1 : ! , _;

                // Stage 2: Tonestack + Volume
                v2 = res1_v : flt.flt_ii1_hp(10) : *(volume*volume) : flt.flt_sv2_tst(bass*bass, mid*mid, treble*treble, 500, 0.5, 1, 1) : flt.flt_ii1_lp(8800);

                // Stage 3: T2 (12AX7)
                res3 = tables.t2_12ax7_table : tube.tube_ck_simple(15, 0.02, 0.398, 0.00155, 100000, 0, 0, 0, 0.38, 0, 1.0, 0, 0.25, 0.1, 0.1, 10, v2, old_dvs);
                res3_v = res3 : _ , !;
                res3_dia = res3 : ! , _;

                // Stage 4: T3 (Cathodyne)
                res4 = tables.t3_cd_table : tube.tube_cd(15, 0.02, 0.1, 0.0016, 56000, 57500, 0, 0, 0, 0.4, 0.4, 0, 0, 0, 0.1, 0.1, 1, 0, 0, 0, 0, res3_v, old_dvs);
                res4_v = res4 : _ , ! , !;
                res4_dia = res4 : ! , ! , _;

                // Stage 5: T4 (Power Tube 6V6)
                res5 = tables.t4_6v6_table : tube.tube_ck_simple(15, 0.02, 0.1, 0.11, 3000, 0, 0, 0, 0.1, 0, 1.0, 0, 0.25, 0.1, 0.1, 10, res4_v, old_dvs);
                res5_v = res5 : _ , !;
                res5_dia = res5 : ! , _;

                total_dia = res1_dia + res3_dia + res4_dia + res5_dia;
                
                // PSU Stage
                res_pss = total_dia : tube.tube_pss(r_pss, tau_pss, 0, 0);
                next_dvs = res_pss : _ , !;
                
                v_out = res5_v * 0.1;
            };
        };
    };
};
