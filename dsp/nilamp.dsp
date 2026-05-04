// SPDX-License-Identifier: MIT
// 5E3 Tweed Deluxe top-level Faust port.
//
// TODO(5e3-v2): the prototype defers four pieces of the canonical
// TWD-DLX-II patch.  Grep for `5e3-v2` to find the individual call
// sites; the high-level summary lives here:
//   1. Multi-PSS chain.  TWD chains three PSS lumps (p1/p2/p3 at
//      lines 189-191 of the JSFX); we collapse them into one.
//   2. Push-pull sag feedback.  T4 + T5 6V6 audio mixing is ported, but
//      PSS dia feedback intentionally stays on the current T4-only path
//      until the T5 dia/PSS interaction has its own ABX-safe diagnostic.
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
t5_table = tables.t5_6v6_table;

// Mode-0 T5 aux branch constants from twd_dlx_ii_harness.jsfx.
k2_mode0 = 0.940;
hp4_hz = 6.4;
kp1 = 1.1220184543;
kp2 = 1.2589254118;
fp_hz = 80.0;
qp1 = 2.6685237666;
qp2 = 2.2440931043;
ks1 = 1.4125375446;
ks2 = 1.4125375446;
fs1_hz = 2098.1359672;
fs2_hz = 1485.8089753;

// --- 5E3 Processing with Global dvs Loop ---
process = _ : *(gain1) : global_loop
with {
    global_loop(vin) = loop_block(vin)
    with {
        // loop_core has 2 outputs (next_dvs, v_out).  In Faust, `A ~ B`
        // does NOT reduce A's output count — it only routes B's outputs
        // back into A's first inputs.  So `loop_core ~ _` still emits
        // both signals; we cull next_dvs with `(! , _)` to leave just
        // v_out for the outer chain.
        loop_block(v_in_ext) = (loop_core(v_in_ext) ~ _) : ! , _
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
                //
                // Tone-stack center frequency must match JSFX
                // tst1.flt_sv2_set_tst(... fm ...) where
                //   fm = iso266(p.fm) and p.fm defaults to 56 dBHz.
                // iso266(56) quantizes 10^2.8 ≈ 630.957 to the nearest
                // multiple of 10 → 630 Hz.  Q = iso266(p.qm) =
                // iso266(-6) = 0.5.  Verified by tracing
                // HK_LIB_TOOLS.jsfx-inc:26-49.
                //
                // pwf=1, pwQ=1: bilinear pre-warping enabled, matching
                // JSFX line 240: tst1.flt_sv2_set_tst(... 1, 1, 1).
                // (The third trailing 1 in JSFX is `sm` smoothing,
                // not represented in Faust — smoothing happens upstream
                // via si.smoo on the slider readouts.)
                v2 = res1_v
                    : flt.flt_ii1_hp(10)
                    : *(volume * volume)
                    : flt.flt_sv2_tst(bass * bass, mid * mid, treble * treble,
                                      630, 0.5, 1, 1)
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

                // Stage 5: T4/T5 6V6 push-pull output.  The T5 branch
                // mirrors twd_dlx_ii_harness.jsfx:419-428 and subtracts
                // the cathode-driven aux tube from the current T4 plate
                // path.  Keep total_dia below on the current T4-only
                // path; adding T5 dia to PSS regressed ABX and needs a
                // separate sag-feedback diagnostic.
                res5 = tube.tube_ck_simple(
                    TBL_SIZE, t4_table, XMAX, DX,
                    c.t4_kpre, c.t4_isat, c.t4_rl, c.t4_kpk,
                    c.t4_kspre, c.t4_kspost, c.t4_ksva, c.t4_ksib, c.t4_kfb,
                    c.t4_pk_xth, c.t4_pk_xdiode, c.t4_pk_k1, c.t4_pk_k2,
                    c.t4_avg_f,
                    res4_v, old_dvs);
                res5_v   = res5 : _ , !;
                res5_dia = res5 : ! , _;

                aux_in = res4_vk
                    : *(k2_mode0)
                    : flt.flt_ii1_hp(hp4_hz)
                    : flt.flt_sv2_peq(kp1, fp_hz, qp1, 1, 1)
                    : flt.flt_sv1_hs(ks1, fs1_hz, 1);

                res_t5 = tube.tube_ck_simple(
                    TBL_SIZE, t5_table, XMAX, DX,
                    c.t5_kpre, c.t5_isat, c.t5_rl, c.t5_kpk,
                    c.t5_kspre, c.t5_kspost, c.t5_ksva, c.t5_ksib, c.t5_kfb,
                    c.t5_pk_xth, c.t5_pk_xdiode, c.t5_pk_k1, c.t5_pk_k2,
                    c.t5_avg_f,
                    aux_in, old_dvs);
                res_t5_v = res_t5 : _ , !;

                post_pp = res5_v - res_t5_v;

                total_dia = res1_dia + res3_dia + res4_dia + res5_dia;

                // PSU Stage (single lump; see TODO above).
                // tube_pss signature: (r, tau, snext, dia, dvs_in) -> (dvs_out, s).
                // snext=0 (no downstream PSS), dia=total_dia (sum of stage
                // plate currents), dvs_in=old_dvs (1-cycle-delayed sag from
                // the global feedback `~`).
                res_pss = tube.tube_pss(r_pss, tau_pss, 0, total_dia, old_dvs);
                next_dvs = res_pss : _ , !;

                // Output normalization.  Mirrors TWD-DLX-II:356:
                //   gout = 0.5 / (t4.rl*t4.isat + t5.rl*t5.isat) at 0 dB.
                // Post-power backend mirrors the ABX-safe diagnostic subset:
                // peq3, hs3, hp5, lp2, then gout.  The pre-T4 backend chain
                // remains diagnostic-only until its sweep regression is
                // isolated.
                v_out = post_pp
                    : flt.flt_sv2_peq(kp2, fp_hz, qp2, 1, 1)
                    : flt.flt_sv1_hs(ks2, fs2_hz, 1)
                    : flt.flt_ii1_hp(40)
                    : flt.flt_df2_lp(10000, sqrt(0.5), 1, 0)
                    : *(0.5 / (c.t4_rl * c.t4_isat + c.t5_rl * c.t5_isat));
            };
        };
    };
};
