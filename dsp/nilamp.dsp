// SPDX-License-Identifier: MIT
// 5E3 Tweed Deluxe top-level Faust port.
//
// TODO(5e3-v2): the prototype defers four pieces of the canonical
// TWD-DLX-II patch.  Grep for `5e3-v2` to find the individual call
// sites; the high-level summary lives here:
//   1. Multi-PSS chain.  TWD chains three PSS lumps (p1/p2/p3 at
//      lines 189-191 of the JSFX); we collapse them into one.
//   2. Push-pull power section.  T4 + T5 6V6 pair with advk averaging
//      and dia summation (TWD lines 376-379) -> we run T4 alone.
//   3. ADNL post-EQ.  flt_df2_set_adnl_eq populates a per-stage
//      DF2 biquad after the ADNL nonlinearity; currently bypassed
//      with identity coefficients (1,0,0,0,0).
//   4. Top-level compile.  faust hangs on this file (SIGALRM); gated
//      behind NILAMP_BUILD_TOPLEVEL=1 in build.rs until that's sorted
//      (likely needs -mem / -double / split-table investigation).
import("stdfaust.lib");
tube = library("hk_tube.lib");
adnl = library("hk_adnl.lib");
pkd = library("hk_pkd.lib");
flt = library("hk_filters.lib");
tables = library("5e3_tables.lib");
c = library("5e3_constants.lib");

// --- 5E3 Parameters ---
gain1 = hslider("Input Gain [unit:dB]", 0, -12, 12, 0.1) : ba.db2linear : si.smoo;
volume = hslider("Volume [%]", 50, 0, 100, 1) / 100.0 : si.smoo;
bass = hslider("Bass [%]", 50, 0, 100, 1) / 100.0 : si.smoo;
mid = hslider("Mid [%]", 50, 0, 100, 1) / 100.0 : si.smoo;
treble = hslider("Treble [%]", 50, 0, 100, 1) / 100.0 : si.smoo;
sag = hslider("Sag [%]", 50, 0, 100, 1) / 100.0 : si.smoo;

// PSU parameters.  The full TWD-DLX patch chains three PSS stages
// (p1/p2/p3, lines 189-191); the current Faust port collapses them
// into a single lump for the 5E3 prototype.  TODO(5e3-v2): split.
r_pss = sag * 22000.0;          // matches p3 (final PSS) at sag=1.0
tau_pss = 0.05;                 // 50 ms time constant

// 5E3 stage table size — all four tables share the same xmax/dx grid,
// so they all have 13503 cells (= 2 * xmax / dx + 3, see gen_tables.py).
TBL_SIZE = 13503;
XMAX = 15.0;
DX = 0.02;

// --- Tube curve selection ---------------------------------------------------
// Two parallel families of tube tables are emitted by tools/gen_5e3_tables.py:
//
//   <stage>_table     — Keller's behavioral GLF (b/type fitted by ear).
//                       What the canonical Keller JSFX patch ships; all 15
//                       regression tests are pinned against this path.
//   <stage>_table_dz  — Dempwolf-Zölzer ECC83 + per-stage DC load-line.
//                       Asymmetric grid-current / cutoff behaviour matching
//                       a real 12AX7.  See docs/notes/dsp-project-notes.md §T2.2.
//
// To switch a stage to the DZ curve, change its ``_table`` alias below to
// the ``_table_dz`` variant.  Faust resolves these aliases at compile time
// (waveforms are not first-class values, hence the manual swap rather than
// an ``if``-on-flag).  T4 (6V6) is a pentode and only has a GLF table.
//
// The 5E3 ECC83/12AX7 stages (T1 in the 12AX7-mod variant, T2, T3) all have
// DZ counterparts.  T1's default 12AY7 voicing has no DZ params and stays GLF.
t1_table = tables.t1_12ax7_table;
t2_table = tables.t2_12ax7_table;
t3_table = tables.t3_cd_table;
t4_table = tables.t4_6v6_table;

// --- 5E3 Processing with Global dvs Loop ---
process = _ : *(gain1) : global_loop
with {
    global_loop(vin) = loop_block(vin)
    with {
        loop_block(v_in_ext) = loop_core(v_in_ext) ~ _
        with {
            loop_core(v_in_ext, old_dvs) = next_dvs, v_out
            with {
                // Stage 1: T1 (12AX7 v1) — clean preamp.  All scalars
                // sourced from c.t1_* (see tools/gen_5e3_tables.py for
                // their derivations).
                res1 = tube.tube_ck_simple(
                    TBL_SIZE, t1_table, XMAX, DX,
                    c.t1_kpre, c.t1_isat, c.t1_rl, c.t1_kpk,
                    c.t1_kspre, c.t1_kspost, c.t1_ksva, c.t1_ksib, c.t1_kfb,
                    c.t1_pk_xth, c.t1_pk_xdiode, c.t1_pk_k1, c.t1_pk_k2,
                    c.t1_avg_f,
                    v_in_ext, old_dvs);
                res1_v   = res1 : _ , !;
                res1_dia = res1 : ! , _;

                // Stage 2: Tonestack + Volume.
                v2 = res1_v
                    : flt.flt_ii1_hp(10)
                    : *(volume * volume)
                    : flt.flt_sv2_tst(bass * bass, mid * mid, treble * treble,
                                      500, 0.5, 1, 1)
                    : flt.flt_ii1_lp(8800);

                // Stage 3: T2 (12AX7 v2) — overdrive preamp.
                res3 = tube.tube_ck_simple(
                    TBL_SIZE, t2_table, XMAX, DX,
                    c.t2_kpre, c.t2_isat, c.t2_rl, c.t2_kpk,
                    c.t2_kspre, c.t2_kspost, c.t2_ksva, c.t2_ksib, c.t2_kfb,
                    c.t2_pk_xth, c.t2_pk_xdiode, c.t2_pk_k1, c.t2_pk_k2,
                    c.t2_avg_f,
                    v2, old_dvs);
                res3_v   = res3 : _ , !;
                res3_dia = res3 : ! , _;

                // Stage 4: T3 (cathodyne / phase splitter).  No avg_f
                // arg — tube_cd has no advk averaging path.  No NEQ
                // here either; the trailing 1,0,0,0,0 are identity
                // neq_b0/b1/b2/a1/a2 transfer-function coefficients.
                // TODO(5e3-v2): port flt_df2_set_adnl_eq and pull the
                // real coefficients from c.t3_neq_*.
                res4 = tube.tube_cd(
                    TBL_SIZE, t3_table, XMAX, DX,
                    c.t3_kpre, c.t3_isat, c.t3_rl, c.t3_rkl, c.t3_kpk,
                    c.t3_kspre, c.t3_kspost, c.t3_ksva, c.t3_ksvk, c.t3_ksib,
                    c.t3_pk_xth, c.t3_pk_xdiode, c.t3_pk_k1, c.t3_pk_k2,
                    1, 0, 0, 0, 0,
                    res3_v, old_dvs);
                res4_v  = res4 : _ , ! , !;
                res4_vk = res4 : ! , _ , !;
                res4_dia = res4 : ! , ! , _;

                // Stage 5: T4 (6V6, single triode-mode power tube).
                // The full 5E3 has a push-pull pair (T4 + T5); we
                // collapse it to one for the prototype.
                // TODO(5e3-v2): re-introduce T5 and the push-pull
                // mixing (see TWD-DLX-II:376-379 for advk averaging
                // and dia summation).
                res5 = tube.tube_ck_simple(
                    TBL_SIZE, t4_table, XMAX, DX,
                    c.t4_kpre, c.t4_isat, c.t4_rl, c.t4_kpk,
                    c.t4_kspre, c.t4_kspost, c.t4_ksva, c.t4_ksib, c.t4_kfb,
                    c.t4_pk_xth, c.t4_pk_xdiode, c.t4_pk_k1, c.t4_pk_k2,
                    c.t4_avg_f,
                    res4_v, old_dvs);
                res5_v   = res5 : _ , !;
                res5_dia = res5 : ! , _;

                total_dia = res1_dia + res3_dia + res4_dia + res5_dia;

                // PSU Stage (single lump; see TODO above).
                res_pss = total_dia : tube.tube_pss(r_pss, tau_pss, 0, 0);
                next_dvs = res_pss : _ , !;

                v_out = res5_v * 0.1;
            };
        };
    };
};
