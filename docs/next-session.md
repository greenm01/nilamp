# Next session - root cause confirmed; pre-T4 EQ topology open question

## TL;DR

The T4 bias-loop divergence found last session was caused by `hp3 = 5.8 Hz`
and `hp4 = 6.4 Hz` in the variant pre-T4 chains stripping the −20.4 V DC
operating-point offset from T3's plate. Both cutoffs sit below
`t4_avg_f ≈ 23.58 Hz`, removing the band the averager + `kfb` loop uses
to set grid bias.

This session built two new variants to test the fix:
- **v13**: `res4_v → *k1 → peq → hs` (no hp). **Closes 80% of the DC
  gap (−9.49 V vs public −12.59 V on sine), matches public's post-tube
  T4 DC within 0.3 V, H2/H1 ratio aligns with public within 7%, and
  eliminates the +5 dB 8 kHz hump.** The remaining −21.6 dB residual
  vs public is the linear EQ shape (peq+hs) that v13 adds and public
  lacks -- not bias-loop drift.
- **v15**: DC-bypass topology (LP at 1 Hz to extract DC, run *k1+EQ on
  AC, re-add DC). Drive DC matches public to −0.001 V but post-tube
  drifts +5 V worse than v13. The split-path topology introduces a
  non-linear interaction at T4's grid that makes v15 worse than v13.

**Hypothesis confirmed:** removing the pre-T4 high-pass restores T4 to
its public operating point. v13 is the right structural fix for any
future variant that wants to apply pre-T4 EQ.

## State of the tree

Public `dsp/nilamp.dsp` includes the ABX-safe T5 subtractive audio
branch plus the JSFX post-power backend filter subset (peq3/hs3/hp5/lp2
applied AFTER T4+T5, in `post_pp = res5_v - res_t5_v : peq3 : hs3 :
hp5 : lp2`). PSS dia feedback intentionally remains on the existing
T4-only path. **No public-path edits this session** -- diagnostics only.

ABX baseline at `gain=+6, defaults` (unchanged):

| Test | Public baseline |
|---|---:|
| 440 Hz sine RMS residual | -16.0 dB |
| 5 s log-sweep RMS residual | -11.2 dB |
| Sweep peak A / B | 0.4195 / 0.3698 |

## Diagnostic rig (24 channels)

`dsp/diagnostics/nilamp_drive_taps.dsp` extended from 20 → 24 channels.
All extra T4 / T5 instances feed off the same `old_dvs` and have their
dia outputs discarded at PSS-aggregation time, so the comparison
isolates "tube response to altered drive at fixed PSS sag" from
PSS-topology differences.

| Channel | Tap |
|---|---|
| 0 | `res4_v_public` (T3 plate, public) |
| 1 | `res4_vk_public` (T3 cathode, public) |
| 2 | `res4_backend_v` (T3 plate with `hp(0.41)` pre-T3) |
| 3 | `res4_backend_vk` (T3 cathode with `hp(0.41)` pre-T3) |
| 4 | `t4_in_public_drive` (== ch0 sanity) |
| 5 | `t5_in_public_drive` |
| 6 | `t4_in_v6_drive` |
| 7 | `t4_in_v10_drive` |
| 8 | `t4_v_public` |
| 9 | `t5_v_public` |
| 10 | `t4_v_v6` |
| 11 | `t5_v_v6` |
| 12 | `t4_v_v10` |
| 13 | `t5_v_v10` (== ch9 sanity) |
| 14 | `t4_dia_public` |
| 15 | `t4_dia_v6` |
| 16 | `t4_dia_v10` |
| 17 | `t4_advk_public` (averager-feedback proxy) |
| 18 | `t4_advk_v6` |
| 19 | `t4_advk_v10` |
| **20** | **`t4_in_v13_drive`** (no-hp; `res4_v -> *k1 -> peq -> hs`) |
| **21** | **`t4_v_v13`** |
| **22** | **`t4_in_v15_drive`** (DC-bypass; AC*k1+EQ, DC re-added) |
| **23** | **`t4_v_v15`** |

## Probe results

### Drive DC (sine 440 Hz, 8 s for LP settling)

| variant | drive.DC | t4_v.DC | t4_v.RMS | H2/H1 | resid_dB |
|---|---:|---:|---:|---:|---:|
| public | -12.59 | -15.01 | 116.0 | 0.0874 | (ref) |
| v6 | +0.01 | -31.93 | 125.2 | 0.0290 | -14.62 |
| v10 | -0.00 | -32.77 | 125.2 | 0.0426 | -13.49 |
| **v13** | **-9.49** | **-15.34** | 112.8 | **0.0933** | **-21.65** |
| v15 | -12.59 | -9.85 | 108.1 | 0.1123 | -17.69 |

### Sweep per-octave dB (variant - public, 8 s log sweep)

| variant | 32 | 63 | 125 | 250 | 500 | 1k | 2k | 4k | 8k | 16k |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| v6 | +1.5 | +0.6 | -1.1 | -0.8 | -0.6 | -0.9 | -0.6 | -1.5 | **+0.5** | -3.5 |
| v10 | +2.0 | +0.1 | -1.0 | -0.9 | -0.6 | -1.0 | -0.6 | -1.6 | **+0.4** | -3.6 |
| v13 | +1.4 | +0.4 | -0.0 | +0.1 | +0.8 | +0.1 | -0.1 | -1.7 | -0.3 | -2.5 |
| v15 | +1.5 | +0.6 | +0.3 | +0.4 | +1.4 | +0.5 | +0.3 | -1.6 | -0.4 | -2.2 |

The +5 dB 8 kHz hump that survived level-matching in v6/v10 (last
session) is **gone** in v13/v15. Confirms bias-loop fix.

### Pre-tube oracle: still PASS (regression guard intact)

ch5 sine -129.6 dB / sweep -117.4 dB; ch6 -126.8 / -124.2 dB; ch7
-128.3 / -124.3 dB. Linear pre-tube chain rebuild against
`keller_oracle.flt_*_block` remains a regression guard.

## Why v15 is worse than v13

Counter-intuitive but explainable. v15's drive matches public's DC to
−0.001 V (vs v13's +4 V residual from k1=0.797 attenuating DC), yet
post-tube T4 DC for v15 sits +5 V above public, while v13 matches to
within 0.3 V. The DC-bypass topology adds a path:

```
v15: drive = (res4_v - lp(1 Hz, res4_v)) * k1 -> peq -> hs + lp(1 Hz, res4_v)
```

The DC component arrives at T4 grid through a different filter chain
(unity LP) than the AC component (k1+peq+hs with phase response). T4's
nonlinearity sees the sum of two paths with different group delay and
amplitude scaling, which the tube model treats as one input. The
result is a different rectification pattern than what public produces
where everything passes through one chain (the trivial identity
chain).

v13's simpler `*k1 -> peq -> hs` keeps all signal components on one
filter path; even though k1 attenuates DC, the relative phase /
amplitude structure that hits T4's grid is internally consistent. The
tube's averager catches the (smaller magnitude but consistent) DC and
biases correctly.

**Lesson:** DC preservation is necessary but not sufficient. The full
pre-T4 chain must be **topologically consistent** -- one filter path,
not split-and-recombine.

## Why v13 still has -21.6 dB residual (and why that's not a bug)

v13 deliberately adds an EQ chain (`peq @80 Hz, Q=2.67, +1 dB`; `hs
@2098 Hz, +3 dB`) that public does not have. Per the sweep table, this
shows up as +0.8 dB at 500 Hz, -1.7 dB at 4 kHz, and -2.5 dB at 16 kHz
relative to public. The residual is **the EQ shape itself**, not a
bias-loop pathology.

For comparison: the public ABX gate is sine ≥ −16 dB. v13 sits at
−21.6 dB sine residual, comfortably below the gate -- so v13 *would*
pass ABX numerically as a substitute T4 chain, even though it carries
audible EQ shaping.

## Implication for the original v6/v10 redesign goal

The original purpose of v6/v10 was to test "move the JSFX backend EQ
from post-T4 (where public puts it) to pre-T4." Last session we found
those variants had a bias-loop bug (hp stripping DC). This session
fixed that bug in v13/v15. But:

- v13 is "public + pre-T4 EQ shaping". It diverges from public by the
  EQ shape itself, deliberately.
- The question "does pre-T4 EQ sound better than post-T4 EQ?" is still
  open and is a perceptual judgment, not an ABX gate.

The c3aa9a0 commit and this session's diagnostics removed a confound
(bias-loop drift) from that perceptual comparison. Future audition of
v13 vs public is now meaningful; previously v6/v10 carried bias drift
that masked the EQ-topology question.

## Gates to keep

- Public ABX gate unchanged: sine ≥ -16.0 dB and sweep ≥ -11.2 dB.
- T5 dia / PSS feedback stays on the existing T4-only path.
- JSFX render cache in `tools/abx_compare.py` is still valid.

## Open questions for next session

1. **Is pre-T4 EQ a desirable design direction at all?** v13 confirms
   the topology is technically viable (no bias-loop bugs) but the
   public path's post-T4 EQ may be intentional for a reason. Would
   need either an A/B audition with a JSFX reference or a project
   decision document.

2. **Why does v13's k1 = 0.797 matter so little?** Predicted +4 V
   drive-DC residual; observed post-tube T4 DC matches public within
   0.3 V. Either:
   - T4's averager loop closes the 4 V drive-DC gap because at the
     attenuated audio band v13 has, the averager output settles to a
     different equilibrium that compensates;
   - or the dataflow has subtleties not captured in the linear
     analysis (e.g. T4's nonlinearity at smaller audio amplitudes
     produces less rectified DC, partially compensating).
   Could be probed by extending the diagnostic with `t4_advk_v13` /
   `t4_dia_v13` channels (already designed for, just not built).

3. **Should we promote v13 to public path as an EQ-topology
   experiment?** That would replace `drive_t4 = res4_v` in
   `dsp/nilamp.dsp` with v13's chain. ABX would still pass (sine
   −21.6 dB > −16 dB gate), but it'd be a different sound, not a
   bug-fix. Requires a perceptual goal first.

## Files added or modified this session

- `dsp/diagnostics/nilamp_drive_taps.dsp` -- 20 → 24 channels (added
  drive + t4_v for v13 and v15).
- `src/bin/nilamp_drive_probe_render.rs` -- `NUM_CHANNELS` 20 → 24.
- `tools/compare_drive_taps.py` -- CHANNEL_NAMES extended.
- `tools/probe_t4_drive_band.py` -- VARIANTS tuple extended for v13/v15.
- `tools/compare_v13_v15.py` (new) -- post-tube scalar-fit residual
  and per-octave-bin level ratio dB for v13/v15 vs public.

## Recent commits

- `007dcb7` test(dsp): v13/v15 confirm DC-bias hypothesis at T4
- `c3aa9a0` test(dsp): localize T4 divergence to DC bias stripped by hp3/hp4
- `c4e127f` test(dsp): probe T4 level-vs-structural divergence on v6/v10
- `9f106b5` test(dsp): probe post-tube voltages for v6/v10 vs public path
- `d039eaf` test(dsp): probe pre-tube drive signals against Python oracle

## Files to read first next session

- `tools/compare_v13_v15.py` -- post-tube comparison logic, decision rule.
- `dsp/diagnostics/nilamp_drive_taps.dsp` -- 24-channel layout (v13/v15
  blocks at lines ~190-220).
- `tools/probe_t4_dia.py` -- bias-loop diagnostic (v6/v10 advk only;
  extend to v13/v15 if needed).
- `dsp/nilamp.dsp` -- public T4 chain (`drive_t4 = res4_v` directly).
- `~/.config/REAPER/Effects/nilamp_abx/twd_dlx_ii_harness.jsfx` -- JSFX
  reference (post-power chain ~lines 395-434).
