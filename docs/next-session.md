# Next session - regression is downstream of the tubes

## State of the tree

Public `dsp/nilamp.dsp` includes the ABX-safe T5 subtractive audio branch plus
the JSFX post-power backend filter subset. The public output path is:
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

## What was added this session

A pre-tube drive-signal probe: `dsp/diagnostics/nilamp_drive_taps.dsp` plus
`src/bin/nilamp_drive_probe_render.rs`.  The diagnostic emits an 8-channel
WAV per render so downstream tools can see what is going *into* T4 / T5,
without changing `dsp/nilamp.dsp`.

| Channel | Tap | Source |
|---|---|---|
| 0 | `res4_v_public` | Public path T3 plate |
| 1 | `res4_vk_public` | Public path T3 cathode |
| 2 | `res4_backend_v` | T3 plate with `hp(0.41)` pre-T3 (v6 source) |
| 3 | `res4_backend_vk` | T3 cathode with `hp(0.41)` pre-T3 (v6 source) |
| 4 | `t4_in_public_drive` | Public T4 drive (== ch0 by construction) |
| 5 | `t5_in_public_drive` | Public T5 drive: `*k2 -> hp(hp4) -> peq -> hs` |
| 6 | `t4_in_v6_drive` | v6 T4 drive: `*k1 -> hp(hp3) -> peq -> hs` of ch2 |
| 7 | `t4_in_v10_drive` | v10 T4 drive: `*k1 -> hp(hp3) -> peq -> hs` of ch0 |

`tools/compare_drive_taps.py` runs 440 Hz sine and a 5 s log sweep through
the probe at `gain=+6, defaults`, then reconstructs channels 4-7 from the
rendered T3 channels (0-3) using `keller_oracle.flt_*_block`, applying the
same 100 ms warm-up trim used by `tools/abx_compare.py`.

## Result: drive signals match the oracle to f32 precision

| Tap | sine 440 Hz max\|d\|/peak | sine RMS dB | sweep max\|d\|/peak | sweep RMS dB |
|---|---:|---:|---:|---:|
| ch4 t4_in_public  | 0 | -inf | 0 | -inf |
| ch5 t5_in_public  | 1.44e-6 | -129.6 | 9.11e-6 | -117.4 |
| ch6 t4_in_v6_drive | 2.58e-6 | -126.8 | 2.73e-6 | -124.2 |
| ch7 t4_in_v10_drive | 2.23e-6 | -128.3 | 2.63e-6 | -124.3 |

Relative max errors are at the float32 precision floor (~1e-6).  Conclusion:

- The pre-tube drive signals for the public path, the v6 candidate, and
  the v10 candidate are computed correctly in Faust.  They match the
  Python oracle (which already matches REAPER/JSFX to <= 1.2e-7) to
  within float32 precision.
- The public-vs-backend ABX regression is therefore *not* caused by a
  filter-coefficient bug, smoothing artefact, or block-boundary effect
  in the linear pre-chain.  Every linear stage upstream of T4 / T5 is
  pinned three ways: Faust, JSFX, and the Python oracle.

## Next steps - look downstream of the tubes

The remaining candidates are nonlinear / stateful interactions that the
drive-signal probe cannot see:

1. **Tube operating point under altered drive.**  v6 / v10 push slightly
   different DC content and sub-audio energy into T4 / T5.  Even with
   identical pre-chain math, the tube models can land on different
   bias points or different ADNL regions.  Probe: emit `t4_v` and
   `t5_v` (post-tube voltages) for the public, v6, and v10 cases on the
   same input, then compare *ratios* / spectral envelopes rather than
   sample-wise residuals to abstract over align/scale.
2. **PSS feedback timing.**  All variants currently share the public
   T4-only PSS loop (per the doc rule).  Confirm by tapping
   `next_dvs_current` for public vs. a v6-driven PSS where
   `total_dia` includes the v6 T4 dia term, and measure the loop's
   step response to a synthetic dia burst.
3. **Branch mix / denominator under altered drive.**  Re-run the v0..v12
   variants with a "linearity probe" input (small-signal sine well below
   bias clip) and confirm whether the regression is bias-dependent or
   structural.  If small-signal residuals also fail, the bug is purely
   linear and lives in the post-power chain (peq3 / hs3 / hp5 / lp2)
   ordering or in the denominator scaling.
4. **`total_dia` composition.**  The public chain feeds T4-only dia into
   PSS.  Once T5 reliably tracks the JSFX backend, audit whether the
   JSFX harness folds T5 dia into PSS and at what gain.  Do not change
   `total_dia` in nilamp during this audit; only document the JSFX
   reference behaviour.

## Gates to keep

- Public ABX gate unchanged: sine >= -16.0 dB and sweep >= -11.2 dB.
- T5 dia / PSS feedback stays on the existing T4-only path; do not add
  T5 dia to `total_dia` during backend-EQ work.
- JSFX-render cache in `tools/abx_compare.py` is still valid; only
  invalidate (`--no-jsfx-cache`) when harness slider settings change.

## Files added or modified this session

- `dsp/diagnostics/nilamp_drive_taps.dsp` (new) - 8-channel pre-tube
  drive-signal diagnostic.
- `src/bin/nilamp_drive_probe_render.rs` (new) - offline renderer for
  the diagnostic, writes 8-ch float32 WAV.
- `tools/compare_drive_taps.py` (new) - runs the probe and compares
  channels 4-7 to a Python oracle reconstruction, with 100 ms warm-up
  trim matching ABX.
- `Cargo.toml` - register `nilamp_drive_probe_render` bin.

## Files to read first next session

- `dsp/diagnostics/nilamp_drive_taps.dsp` - drive-tap layout.
- `tools/compare_drive_taps.py` - oracle reconstruction logic.
- `dsp/diagnostics/nilamp_t5_balance.dsp` - existing post-tube variants
  (v0..v12), the obvious starting point for a *post-tube* probe.
- `dsp/nilamp.dsp` - public audio path (T4-only PSS, branch mix).
- `~/.config/REAPER/Effects/nilamp_abx/twd_dlx_ii_harness.jsfx` - JSFX
  reference, especially the post-power chain around lines 395-434 and
  any `dia` / PSS-equivalent logic.
