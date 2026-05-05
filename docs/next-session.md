# Next session - divergence localized at T4 (nonlinear)

## State of the tree

Public `dsp/nilamp.dsp` includes the ABX-safe T5 subtractive audio branch
plus the JSFX post-power backend filter subset. The public output path is:
`post_pp = res5_v - res_t5_v`, `peq3 -> hs3 -> hp5 -> lp2`, then full T4+T5
output denominator. PSS dia feedback intentionally remains on the existing
T4-only path.

Current normal-renderer ABX baseline at `gain=+6, defaults`:

| Test | Current public baseline |
|---|---:|
| 440 Hz sine RMS residual | -16.0 dB |
| 5 s log-sweep RMS residual | -11.2 dB |
| Sweep peak A / B | 0.4195 / 0.3698 |
| Sweep align lag | 1 sample |

## What was added / extended this session

The pre-tube drive-signal probe was extended to 14 channels covering
post-tube voltages too: `dsp/diagnostics/nilamp_drive_taps.dsp` plus
`src/bin/nilamp_drive_probe_render.rs`.  All extra T4 / T5 instances feed
off the same `old_dvs` and have their dia outputs discarded -- they do not
perturb PSS, so the comparison isolates "tube response to altered drive at
fixed PSS" from PSS-topology divergence.

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
| 8 | `t4_v_public` (T4 fed by ch4) |
| 9 | `t5_v_public` (T5 fed by ch5) |
| 10 | `t4_v_v6` (T4 fed by ch6) |
| 11 | `t5_v_v6` (T5 fed by v6 T5 drive `res4_backend_vk -> *k2/hp/peq/hs`) |
| 12 | `t4_v_v10` (T4 fed by ch7) |
| 13 | `t5_v_v10` (== ch9 sanity slot; v10 keeps public T5 path) |

`tools/compare_drive_taps.py` now also runs:
- single-bin DFT levels at `f0 / 2f0 / 3f0 / 5f0` for the 440 Hz sine, with
  per-tap THD;
- per-octave-bin level-ratio dB on the sweep (variant - public);
- best least-squares scalar-fit residual after lag alignment (post-tube
  vs the public counterpart).

## Pre-tube oracle: still PASS (regression guard intact)

| Tap | sine max\|d\|/peak | sine RMS dB | sweep max\|d\|/peak | sweep RMS dB |
|---|---:|---:|---:|---:|
| ch4 t4_in_public | 0 | -inf | 0 | -inf |
| ch5 t5_in_public | 1.44e-6 | -129.6 | 9.11e-6 | -117.4 |
| ch6 t4_in_v6_drive | 2.58e-6 | -126.8 | 2.73e-6 | -124.2 |
| ch7 t4_in_v10_drive | 2.23e-6 | -128.3 | 2.63e-6 | -124.3 |

## Post-tube divergence: nonlinear at T4

Sanity slots verify the test rig: `v10_T5 == public_T5` produces a scalar
fit `s=1.00000` and `-inf` residual exactly, and ch4==ch0 has zero error.

Sine 440 Hz, `gain=+6, defaults` (residual dB vs public peak):

| Tap | peak | H1 | H2 | H3 | H5 | THD% | scalar s | resid_dB |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| public_T4 | 188.9 | 163.2 | 14.27 | 52.40 | 29.99 | 38.0 | 1.000 | -inf |
| v6_T4 | 203.5 | 181.5 | 5.26 | 58.92 | 33.91 | 37.6 | 0.851 | -17.85 |
| v10_T4 | 203.2 | 180.8 | 7.66 | 58.42 | 33.33 | 37.4 | 0.848 | -16.85 |
| public_T5 | 230.7 | 204.2 | 23.06 | 63.82 | 33.13 | 37.0 | 1.000 | -inf |
| v6_T5 | 230.8 | 204.6 | 19.44 | 65.00 | 34.97 | 37.3 | 0.995 | -30.50 |
| v10_T5 | 230.7 | 204.2 | 23.06 | 63.82 | 33.13 | 37.0 | 1.000 | -inf |

Sweep 5 s, post-tube level ratio dB (variant - public, per octave bin):

| Tap | 32 | 63 | 125 | 250 | 500 | 1k | 2k | 4k | 8k | 16k |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| v6_T4 | -1.86 | -0.33 | +0.97 | +0.86 | -0.12 | +1.21 | +1.27 | +1.45 | +4.03 | -2.10 |
| v6_T5 | -0.01 | +0.05 | +0.07 | -0.02 | -0.28 | -0.04 | +0.06 | -0.14 | -0.41 | -0.45 |
| v10_T4 | -1.84 | -0.44 | +0.87 | +0.88 | +0.03 | +1.22 | +1.33 | +1.52 | +4.98 | -1.48 |
| v10_T5 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |

Worst scalar-fit residual: -16.60 dB (v10_T4 sweep). Classification:
**NONLINEAR divergence**, located primarily at T4.

Observations:

- T4 v6 and v10 both fit best with `s ~ 0.85` -- the variant tubes are
  ~1.5 dB louder than public, with non-flat envelope tilt across band.
- **H2 changes substantially**: `public_T4 H2=14.27` vs `v6_T4 H2=5.26`
  and `v10_T4 H2=7.66`.  THD totals are similar (37-38%) but the
  *distribution* of harmonic energy is different.  This is a clean
  signature of a different operating point in the tube nonlinearity
  rather than a level-only or linear-EQ difference.
- T4 v6 vs T4 v10 are nearly identical post-tube (residual within ~1 dB
  of each other), even though their pre-T4 drives come from different T3
  sources (`res4_backend_v` vs `res4_v`).  This means the divergence is
  driven mostly by the post-T3-but-pre-T4 path (the `*k1 / hp(hp3) /
  peq / hs` chain itself), not by the T3 source variant.
- T5 v6 is only -30 dB different from T5 public (much smaller); T5 v10
  matches public exactly by construction.  T5 is **not** the dominant
  offender.

## Next steps - probe T4 dia / peak detector

Localizing nonlinear divergence at T4 narrows the suspects to:

1. **Tube peak-detector state** (`tube_ck_simple` `pk_xth`, `pk_xdiode`,
   `pk_k1`, `pk_k2`, `kfb`) -- shared `old_dvs` should make this state
   identical across variants, but the tube also carries internal averager
   state (`avg_f`).  Probe: emit `t4_dia_public`, `t4_dia_v6`,
   `t4_dia_v10`, and the internal peak-detector output (a synthetic
   `t4_pk_avg` exposed via a one-off diagnostic) for all three variants
   on the same input.  Compare RMS-vs-peak ratios; an excursion in pk_avg
   between variants would explain the operating-point shift.
2. **T4 instance independence vs PSS-shared state.**  The current rig
   uses three independent `tube_ck_simple` calls all consuming the same
   `old_dvs`.  Confirm that each tube does indeed maintain its own
   internal state (averager, peak detector); a Faust state-collision bug
   would manifest as identical post-tube outputs across variants, which
   is *not* what we see, so this is unlikely but worth confirming via a
   re-render of public alone vs public-in-the-multi-tube-block.
3. **kfb feedback path.**  `kfb` couples plate to grid; under a
   different drive spectrum the steady-state operating point shifts.
   This is the most likely structural cause of the H2 change.
4. **Drive level scaling.**  Both v6 and v10 produce ~+1.5 dB more H1 at
   T4 plate than public; under a 6V6 with mild grid-current onset that
   alone perturbs H2/H3 ratios.  Test by feeding public a `*0.851`
   pre-attenuated drive and checking whether public_T4 then matches
   v6_T4 in H2 distribution.

The cleanest first step is option 4: it's a no-build-required probe
(write a Python script that renders public-only and public-attenuated
through the existing 14-channel rig with `--gain` reduced to compensate
the 1.5 dB gap, then compare H2 distributions).

If option 4 does *not* explain the H2 shift, escalate to a t4-dia
diagnostic (option 1) and consider a 16- or 18-channel extension of
`nilamp_drive_taps.dsp`.

## Level-match probe result: STRUCTURAL (option 4 ruled out)

`tools/probe_t4_level.py` ran two probe passes: A at `--gain +6.0`, then
B at `--gain (6.0 - delta_db)` where `delta_db = +0.903 dB` is the
average T4 plate H1 ratio (variants vs public).  At 440 Hz the
level-matched public reproduces public's H2 almost exactly, **not** the
variant H2:

| Tap | H1 | H2 | H3 | H5 | H2/H1 |
|---|---:|---:|---:|---:|---:|
| A.public_T4 | 163.2 | 14.27 | 52.40 | 29.99 | 0.08744 |
| A.v6_T4 | 181.5 | 5.26 | 58.92 | 33.91 | 0.02900 |
| A.v10_T4 | 180.8 | 7.66 | 58.42 | 33.33 | 0.04236 |
| B.public_T4_atten | 162.8 | 14.09 | 52.11 | 29.63 | 0.08655 |

`B.public_T4_atten` H2 (14.09) matches `A.public_T4` (14.27) -- as
expected for a small linear gain trim -- and is ~2.7x bigger than
`A.v6_T4` (5.26) and ~1.8x bigger than `A.v10_T4` (7.66).  Level-matched
sweep residuals barely move:

| Comparison | v6 resid_dB | v10 resid_dB |
|---|---:|---:|
| variant vs A.public | -17.14 | -16.60 |
| variant vs B.public_atten | -17.62 | -17.02 |

And the +5 dB 8 kHz hump in the per-octave level-ratio survives
level-matching (4.93 dB worst residual bin, tolerance was 0.5 dB).

Conclusion: the divergence is **structural** -- the variant spectrum
hitting T4 changes the tube operating point in a way no scalar level
trim can compensate.  The cause must lie in:

- **`kfb` feedback** redistributing grid bias under altered drive
  spectrum;
- **`tube_ck_simple` averager / peak-detector state** drifting
  differently across variants because each tube maintains its own
  state (separate Faust instances), even though `old_dvs` is shared;
- **harmonic phase relationships** between fundamental and 2nd
  harmonic content surviving the linear pre-chain reshaping (h(0.41)
  on T3 grid, hp(hp3), peq, hs) and presenting the tube with a
  drive whose *envelope shape* differs from public, even after H1
  is matched.

## Next steps - escalate to dia / state probe

1. Extend `dsp/diagnostics/nilamp_drive_taps.dsp` to expose:
   - `t4_dia_public`, `t4_dia_v6`, `t4_dia_v10` (already computed
     internally; just route as new channels);
   - the per-tube internal peak-detector / averager scalar (requires a
     small instrumentation pass in `dsp/hk_tube.lib` -- add an optional
     diagnostic output, or compute a parallel averager in the probe
     using the `c.t4_avg_f` time constant on the post-tube voltage to
     proxy what the tube sees internally).
2. Compare dia RMS-vs-peak and dia spectrum across the three variants.
   A divergence in the LF / sub-audio dia content would point to bias
   bias-feedback ringing under the altered drive; a divergence at the
   harmonic frequencies would point to peak-detector / averager state.
3. If dia is identical, the offender is the tube model itself reacting
   to a phase-/envelope-shifted drive; remediation is a pre-T4
   *spectrum-flattening* tweak in the v6/v10 path or accepting the
   nonlinear cost as the structural diff between public and JSFX
   reference.

## Gates to keep

- Public ABX gate unchanged: sine >= -16.0 dB and sweep >= -11.2 dB.
- T5 dia / PSS feedback stays on the existing T4-only path; do not add
  T5 dia to `total_dia` during backend-EQ work.
- JSFX-render cache in `tools/abx_compare.py` is still valid; only
  invalidate (`--no-jsfx-cache`) when harness slider settings change.

## Files added or modified this session

- `dsp/diagnostics/nilamp_drive_taps.dsp` - extended from 8 to 14
  channels (added post-tube T4/T5 voltages for public, v6, v10).
- `src/bin/nilamp_drive_probe_render.rs` - `NUM_CHANNELS` 8 -> 14, doc.
- `tools/compare_drive_taps.py` - added per-variant sine harmonic table,
  scalar-fit residual, sweep level-ratio table, decision rule.
- `tools/probe_t4_level.py` (new) - level-match probe ruling out
  drive-level as the cause of T4 divergence.

## Files to read first next session

- `dsp/diagnostics/nilamp_drive_taps.dsp` - 14-channel layout.
- `tools/compare_drive_taps.py` - post-tube analysis logic.
- `dsp/hk_tube.lib` - `tube_ck_simple` peak-detector / averager state.
- `dsp/5e3_constants.lib` - T4 6V6 `kfb`, `pk_xth`, `pk_xdiode`, `pk_k1`,
  `pk_k2`, `avg_f`.
- `dsp/diagnostics/nilamp_t5_balance.dsp` - if Phase 5a small-signal
  branch becomes relevant later (currently NOT next, given nonlinear
  classification).
- `~/.config/REAPER/Effects/nilamp_abx/twd_dlx_ii_harness.jsfx` - JSFX
  reference (post-power chain ~lines 395-434).
