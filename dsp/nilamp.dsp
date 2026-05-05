// SPDX-License-Identifier: MIT
// 5E3 Tweed Deluxe top-level Faust port.
//
// TODO(5e3-v2): the prototype defers three pieces of the canonical
// TWD-DLX-II patch.  Grep for `5e3-v2` to find the individual call
// sites; the high-level summary lives here:
//   1. Push-pull sag feedback.  T4 + T5 6V6 audio mixing is ported, but
//      PSS dia feedback intentionally stays on the current T4-only path
//      until the T5 dia/PSS interaction has its own ABX-safe diagnostic.
//   2. ADNL post-EQ.  flt_df2_set_adnl_eq populates a per-stage
//      DF2 biquad after the ADNL nonlinearity; currently bypassed
//      with identity coefficients (1,0,0,0,0).
//   3. Top-level compile.  faust hangs on this file (SIGALRM); gated
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

// PSU parameters.  Three cascaded PSS lumps matching JSFX
// twd_dlx_ii.jsfx:189-191.  The sag slider scales p3's resistor
// (the long-time-constant final lump that produces audible droop);
// p1 and p2 are fixed per JSFX.
//
//   JSFX:  p1.tube_pss_set(125,   0.008)
//          p2.tube_pss_set(5100,  0.0816)
//          p3.tube_pss_set(22000, 0.352)
//          kgrid = 0.025
//          dia1 = t4.dia + t5.dia            (power tubes)
//          dig  = kgrid * dia1                (grid current to p2)
//          dia3 = t1.dia + t2.dia + t3.dia   (preamp tubes)
//          dvs1 = p1(0,    p2.s, dia1)
//          dvs2 = p2(dvs1, p3.s, dig)        // power tubes see dvs2
//          dvs3 = p3(dvs2, 0,    dia3)       // preamp tubes see dvs3
r_p1 = 125.0;
tau_p1 = 0.008;
r_p2 = 5100.0;
tau_p2 = 0.0816;
r_p3 = sag * 22000.0;           // sag slider only scales p3 (final lump)
tau_p3 = 0.352;
kgrid = 0.025;

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

// Mode-0 T4 / T5 aux branch constants from twd_dlx_ii_harness.jsfx.
k1_mode0 = 0.797;
k2_mode0 = 0.940;
hp3_hz = 5.8;
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
        // loop_core has 5 outputs: (next_dvs2, next_dvs3, next_s2,
        // next_s3, v_out).  In Faust, `A ~ B` does NOT reduce A's
        // output count — it only routes B's outputs back into A's
        // first inputs.  So `loop_core ~ si.bus(4)` still emits all
        // five signals; we cull the four feedback values with
        // (!,!,!,!,_) to leave just v_out for the outer chain.
        loop_block(v_in_ext) = (loop_core(v_in_ext) ~ si.bus(4))
                             : !,!,!,!,_
        with {
            loop_core(v_in_ext, old_dvs2, old_dvs3, old_s2, old_s3) =
                next_dvs2, next_dvs3, next_s2, next_s3, v_out
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
                    v_in_ext, old_dvs3);
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
                    v2, old_dvs3);
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
                    res3_v, old_dvs3);
                res4_v  = res4 : _ , ! , !;
                res4_vk = res4 : ! , _ , !;
                res4_dia = res4 : ! , ! , _;

                // Stage 5: T4/T5 6V6 push-pull output.  Both halves see
                // dvs2 (the second PSS lump's output, fed by power-tube
                // plate currents through p1).  The T4 input gets the
                // canonical *k1 → hp3 → peq1 → hs1 chain (loudspeaker
                // pre-shaping; JSFX twd_dlx_ii.jsfx:411-415).  The T5
                // branch mirrors twd_dlx_ii.jsfx:419-428 and subtracts
                // the cathode-driven aux tube from the T4 plate path.
                drive_t4 = res4_v
                    : *(k1_mode0)
                    : flt.flt_ii1_hp(hp3_hz)
                    : flt.flt_sv2_peq(kp1, fp_hz, qp1, 1, 1)
                    : flt.flt_sv1_hs(ks1, fs1_hz, 1);

                res5 = tube.tube_ck_simple(
                    TBL_SIZE, t4_table, XMAX, DX,
                    c.t4_kpre, c.t4_isat, c.t4_rl, c.t4_kpk,
                    c.t4_kspre, c.t4_kspost, c.t4_ksva, c.t4_ksib, c.t4_kfb,
                    c.t4_pk_xth, c.t4_pk_xdiode, c.t4_pk_k1, c.t4_pk_k2,
                    c.t4_avg_f,
                    drive_t4, old_dvs2);
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
                    aux_in, old_dvs2);
                res_t5_v   = res_t5 : _ , !;
                res_t5_dia = res_t5 : ! , _;

                post_pp = res5_v - res_t5_v;

                // PSS Stage — three cascaded lumps matching JSFX
                // twd_dlx_ii.jsfx:379-390.  Power-tube plate currents
                // (dia1) drive p1; kgrid-scaled grid current (dig)
                // drives p2; preamp plate currents (dia3) drive p3.
                // Power tubes (T4/T5) consume dvs2; preamp tubes
                // (T1/T2/T3) consume dvs3.  s2 and s3 (the smoothed
                // currents that p1 reads as p2.s and p2 reads as
                // p3.s) are 1-sample-delayed via the global feedback
                // loop, matching JSFX's sequential-call instance(s)
                // semantics.
                dia1 = res5_dia + res_t5_dia;
                dig  = kgrid * dia1;
                dia3 = res1_dia + res3_dia + res4_dia;
                res_pss1 = tube.tube_pss(r_p1, tau_p1, old_s2, dia1, 0);
                next_dvs1 = res_pss1 : _ , !;
                res_pss2 = tube.tube_pss(r_p2, tau_p2, old_s3, dig, next_dvs1);
                next_dvs2 = res_pss2 : _ , !;
                next_s2   = res_pss2 : ! , _;
                res_pss3 = tube.tube_pss(r_p3, tau_p3, 0, dia3, next_dvs2);
                next_dvs3 = res_pss3 : _ , !;
                next_s3   = res_pss3 : ! , _;

                // Output normalization.  Mirrors TWD-DLX-II:356:
                //   gout = 0.5 / (t4.rl*t4.isat + t5.rl*t5.isat) at 0 dB.
                // Post-power backend matches JSFX twd_dlx_ii.jsfx:428-432:
                // peq3 → hs3 → hp5 → lp2 → gout.
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
