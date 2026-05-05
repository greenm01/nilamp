# Next session - root cause of T4 divergence: DC bias stripped by hp3/hp4

## TL;DR

The v6 / v10 backend variants strip a **−20.4 V DC operating-point offset**
from the T4 drive that the public path preserves. `hp3 = 5.8 Hz` (and
`hp4 = 6.4 Hz` on the v6 T5 chain) sit *below* `t4_avg_f ≈ 23.58 Hz`, so
they remove exactly the sub-audio band that T4's averager + `kfb` loop
uses to set grid bias. Result: variant T4 grid bias settles to a
−4 V averager-feedback DC vs public's +0.31 V, which through `t4_kfb =
0.18144` shifts the T4 operating point ~24 V deeper, completely
reshaping the harmonic distribution (H2 collapses 14 → 5-7).

The fix is structural: redesign the variant pre-T4 chain so it does
NOT high-pass below ~30 Hz, OR explicitly preserve the public path's
DC offset across the variant chain.

## State of the tree

Public `dsp/nilamp.dsp` includes the ABX-safe T5 subtractive audio
branch plus the JSFX post-power backend filter subset (peq3/hs3/hp5/lp2
into T4+T5 output denominator). PSS dia feedback intentionally remains
on the existing T4-only path.

ABX baseline at `gain=+6, defaults` (unchanged):

| Test | Public baseline |
|---|---:|
| 440 Hz sine RMS residual | -16.0 dB |
| 5 s log-sweep RMS residual | -11.2 dB |
| Sweep peak A / B | 0.4195 / 0.3698 |

## Diagnostic rig (this session)

`dsp/diagnostics/nilamp_drive_taps.dsp` extended from 14 → 20 channels.
All extra T4 / T5 instances feed off the same `old_dvs` and have their
dia outputs discarded at PSS-aggregation time, so the comparison
isolates "tube response to altered drive at fixed PSS sag" from
PSS-topology differences.

| Channel | Tap |
|---|---|
| 0 | `res4_v_public` (T3 plate, public) |
| 1 | `res4_vk_public` (T3 cathode, public) |
| 2 | `res4_backend_v` (T3 plate with `hp(0.41)` pre-T3, v6 source) |
| 3 | `res4_backend_vk` (T3 cathode with `hp(0.41)` pre-T3) |
| 4 | `t4_in_public_drive` (== ch0 sanity slot) |
| 5 | `t5_in_public_drive` (`*k2 -> hp(hp4) -> peq -> hs`) |
| 6 | `t4_in_v6_drive` (`*k1 -> hp(hp3) -> peq -> hs` of ch2) |
| 7 | `t4_in_v10_drive` (`*k1 -> hp(hp3) -> peq -> hs` of ch0) |
| 8 | `t4_v_public` |
| 9 | `t5_v_public` |
| 10 | `t4_v_v6` |
| 11 | `t5_v_v6` |
| 12 | `t4_v_v10` |
| 13 | `t5_v_v10` (== ch9 sanity) |
| 14 | `t4_dia_public` |
| 15 | `t4_dia_v6` |
| 16 | `t4_dia_v10` |
| 17 | `t4_advk_public` (averager-feedback proxy: `lp(t4_avg_f, t4_v - dvs) * t4_kfb`) |
| 18 | `t4_advk_v6` |
| 19 | `t4_advk_v10` |

The `advk` proxy reconstructs in Faust what `tube_ck` computes
internally as `next_advk = flt_ii1_lp(avg_f, v_out - dvs) * kfb`.
Each variant's proxy is fed by that variant's post-tube voltage so it
reflects the bias-loop state the variant tube *would* settle to under
identical params -- a one-step proxy, not closed-loop, but sufficient
to detect steady-state DC differences.

## Probe results

### Pre-tube oracle: still PASS

ch5 sine -129.6 dB / sweep -117.4 dB; ch6 -126.8 / -124.2 dB; ch7
-128.3 / -124.3 dB. The linear pre-tube chain rebuild against
`keller_oracle.flt_*_block` remains a regression guard.

### Post-tube T4 (440 Hz sine, gain=+6)

| Tap | H1 | H2 | H3 | H5 | H2/H1 |
|---|---:|---:|---:|---:|---:|
| public_T4 | 163.2 | 14.27 | 52.40 | 29.99 | 0.0874 |
| v6_T4 | 181.5 | 5.26 | 58.92 | 33.91 | 0.0290 |
| v10_T4 | 180.8 | 7.66 | 58.42 | 33.33 | 0.0424 |

H2 collapses to ~1/3 the public value despite similar total THD.

### Level-match probe verdict: STRUCTURAL

`tools/probe_t4_level.py` reduced gain by `delta_db = +0.903 dB`
(average T4 H1 ratio variants vs public). Level-matched
`B.public_T4_atten H2 = 14.09` matches `A.public_T4 H2 = 14.27`, NOT
`A.v6_T4 H2 = 5.26`. Sweep residuals barely move (-17.14 → -17.62 dB)
and the +5 dB 8 kHz hump survives level-matching. **Drive level is
not the cause.**

### dia / advk probe verdict: BIAS-FEEDBACK

`tools/probe_t4_dia.py` on a 440 Hz sine at gain=+6:

| variant | dia.DC | dia.RMS | t4.DC | advk.DC | advk.RMS |
|---|---:|---:|---:|---:|---:|
| public | +0.0001 | 4.19e-2 | -27.29 | **+0.314** | 1.17 |
| v6 | +0.0080 | 4.71e-2 | -51.07 | **-3.998** | 4.19 |
| v10 | +0.0083 | 4.70e-2 | -51.92 | **-4.151** | 4.34 |

Max |advk DC delta| = **4.46 V** (cutoff at 0.05 V). dia DC matches
within 8 mV. The variants' grid-bias loop settles to a totally
different operating point under identical PSS sag.

### Drive-band probe verdict: ROOT CAUSE FOUND

`tools/probe_t4_drive_band.py` on the same sine, no Faust rebuild:

| variant | drive.DC | drive.RMS | lp(drive,t4_avg_f).DC |
|---|---:|---:|---:|
| public | **-20.41** | 42.6 | -20.35 |
| v6 | -0.04 | 30.8 | -0.05 |
| v10 | -0.03 | 30.7 | -0.04 |

The public T4 drive carries a **-20.4 V DC operating-point offset**
(T3's plate quiescent voltage). The v6 / v10 drives have ~0 V DC
because `flt_ii1_hp(hp3 = 5.8 Hz)` (and `hp(0.41)` for v6's pre-T3
stage) high-pass the signal, removing exactly that DC.

`t4_avg_f = 1 / (2π · 0.00675) ≈ 23.58 Hz`. Both `hp3 = 5.8` and `hp4
= 6.4` sit below it, so they cut the band the bias loop integrates.

Numerical sanity: `kfb · 20.4 ≈ 0.1814 · 20.4 ≈ 3.7 V`, very close to
the observed 4 V advk DC shift. The remaining ~0.3 V comes from the
variant tube's own loop drift around the new operating point.

## Cause-and-effect summary

1. Public T3 plate output sits at ~-20 V DC quiescent (normal tube
   plate operating point).
2. Public T4 chain feeds T3 plate directly into T4 grid; `tube_ck`'s
   averager LP (~23.58 Hz) catches that -20 V DC and the `kfb` loop
   biases T4's grid around it.
3. v6 / v10 chains apply `hp(5.8) → peq → hs` before T4. The hp at
   5.8 Hz removes the DC operating point.
4. Variant T4 averager sees ~0 V DC instead of -20 V; through
   `kfb=0.18144` that shifts grid voltage by ~3.7 V.
5. Shifted grid voltage moves T4 deep into a different region of the
   tube curve where curvature differs.
6. Result: H2 collapses, H3 / H5 grow, post-tube DC drops -24 V,
   sweep envelope acquires a ~+5 dB 8 kHz hump.

## Gates to keep

- Public ABX gate unchanged: sine >= -16.0 dB and sweep >= -11.2 dB.
- T5 dia / PSS feedback stays on the existing T4-only path.
- JSFX render cache in `tools/abx_compare.py` is still valid (no
  harness changes).

## Next steps

The path forward is to design new variants (v13+) for
`dsp/diagnostics/nilamp_drive_taps.dsp` that preserve the public T3
plate's DC operating point through the EQ chain. Two natural
candidates:

1. **v13: peq + hs only on `res4_v`, no hp.** Removes hp3 entirely.
   The `peq(80 Hz, Q=2.67, +1 dB shelf)` and `hs(2098 Hz, +3 dB)`
   are themselves unity-DC linear filters (single-pole shelves and
   biquads have well-defined DC gain), so the -20.4 V DC should
   propagate. Expected: variant T4 advk DC ≈ public, post-tube
   matches.

2. **v14: peq + hs only on `res4_backend_v` (hp(0.41) pre-T3
   retained, hp3 removed).** Tests whether the pre-T3 `hp(0.41)` is
   benign (its cutoff is sub-`t4_avg_f` so it ALSO strips DC; if so
   v14 would still mismatch and v13 alone should be the winner).

   Note: `hp(0.41)` is below `t4_avg_f` too. If v6 was failing on
   *both* the pre-T3 hp and the pre-T4 hp, then v14 will not match
   either. Likely v13 is the answer.

3. **Validation cycle:** after building v13, re-run probe_t4_dia to
   verify advk DC for v13 ≈ public, then re-run compare_drive_taps
   post-tube analysis (scalar fit residual should drop to -50+ dB).
   Then run public ABX gate on v13 promoted into `dsp/nilamp.dsp` to
   check sine residual >= -16 and sweep >= -11.2.

Implementation cost: ~1 line added to `nilamp_drive_taps.dsp` for each
variant, +3 channels per variant (drive, t4_v, optionally dia / advk),
~10 min Faust + Rust rebuild, ~2 min probe runs.

A simpler alternative worth considering: **raise hp3 cutoff above
t4_avg_f** (e.g. 30 Hz) and accept the small low-end loss. This
preserves the pre-T4 EQ chain shape but moves the high-pass out of the
bias-loop band. May or may not be enough -- the issue is that the
public path has NO high-pass at all between T3 and T4, so even a
30-Hz hp would still subtract some DC.

## Files added or modified this session

- `dsp/diagnostics/nilamp_drive_taps.dsp` -- 14 → 20 channels (added
  t4_dia per variant + averager-feedback proxy per variant).
- `src/bin/nilamp_drive_probe_render.rs` -- `NUM_CHANNELS` 14 → 20,
  doc updated.
- `tools/compare_drive_taps.py` -- channel-name table extended to 20.
- `tools/probe_t4_dia.py` (new) -- T4 dia + advk diagnostic, decision
  rule (bias-feedback / peak-detector / instantaneous).
- `tools/probe_t4_drive_band.py` (new) -- pure-Python LP analysis of
  pre-T4 drive signals to localize WHICH band differs.

## Files to read first next session

- `dsp/diagnostics/nilamp_drive_taps.dsp` -- 20-channel layout, where
  to add v13 / v14.
- `tools/probe_t4_dia.py` -- diagnostic to re-run after new variants
  exist (read advk DC delta).
- `tools/probe_t4_drive_band.py` -- diagnostic to confirm drive DC is
  preserved.
- `dsp/hk_tube.lib` -- `tube_ck_simple` averager `next_advk = lp(avg_f,
  v_out - dvs) * kfb` at line ~30.
- `dsp/5e3_constants.lib` -- T4 6V6 `t4_kfb = 0.18144`, `t4_avg_f =
  1 / (2π · 0.00675)`.
- `~/.config/REAPER/Effects/nilamp_abx/twd_dlx_ii_harness.jsfx` --
  JSFX reference (post-power chain ~lines 395-434).
