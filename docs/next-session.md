# Next session - native C/Lua parity and legacy purge

## SESSION LOG (most recent first)

### Session: JSFX/native tap diagnostics -> SCALE-LIKE MISMATCH LOCALIZED

**Edit summary.**

- Added a staged `twd_dlx_ii_taps.jsfx` harness variant that emits the same
  nine diagnostic taps as `native/bin/nilamp_taps_render`.
- Extended the JSFX render wrapper to support multichannel renders and to open
  a temporary project with `-newinst`, matching the unattended host-test
  pattern.
- Added `tools/compare_taps.py` for per-tap JSFX/native comparison.
- No DSP code was changed; the tap results show high correlation and mostly
  scale-like mismatch, not a clear topology break.

**Verification.**

- `python3 -m py_compile tools/abx_compare.py tools/jsfx_render/render_jsfx.py tools/jsfx_render/stage_jsfx.py tools/compare_taps.py` passes.
- `make native-test` passes.
- `make native-host-test` passes with `clap-validator`: 21 tests run, 16
  passed, 0 failed, 5 skipped, 0 warnings.
- Tap sine comparison: first suspect remains `v_out`; per-stage correlations
  are `>= 0.9996`, with best-fit gain mostly around `+0.70 dB` through the
  middle taps.
- Tap sweep comparison: correlations are lower but still mostly shape-aligned
  (`0.9735` to `0.9954`); best-fit gain is about `+0.9 dB` through signal
  taps and `+1.6 dB` through PSS taps.
- ABX sine: `-16.5 dB` residual below native peak, threshold `-16.0 dB` -> PASS.
  Correlation `0.999330`; best native-to-JSFX gain `0.805335` (`-1.88 dB`).
- ABX sweep: `-16.8 dB` residual below native peak, threshold `-11.2 dB` -> PASS.
  Correlation `0.978765`; best native-to-JSFX gain `0.732585` (`-2.70 dB`).

**Next work.**

1. Do not add arbitrary output trim. The tap harness should be used to test
   one source-backed gain hypothesis at a time.
2. Leading candidates are gain/smoothing initialization around Keller `g1/g2/g3`
   and PSS/current scaling, because taps remain highly correlated while scale
   differs.

### Session: C CLAP shell lands -> REAPER HOST TEST PASS

**Edit summary.**

- Vendored official CLAP C headers under `third_party/clap/`.
- Added `native/src/nilamp_clap.c`, a no-GUI CLAP audio effect exposing the
  native DSP as one stereo input/output pair.
- Exposed six automatable host parameters: gain, volume, bass, mid, treble,
  and sag.
- Added simple binary CLAP state save/load for those six parameter values.
- Added `native/tests/test_clap_load.c`, a minimal loader smoke test that
  scans the plugin, activates it, processes stereo audio, and applies one gain
  automation event.
- Updated `make native` to build `native/bin/nilamp.clap`; updated
  `make native-test` to run the CLAP smoke test.
- Added `make native-host-test`, a REAPER-dependent CLAP host validation that
  uses temporary `CLAP_PATH` discovery, verifies host-visible parameters, and
  renders a short test WAV.
- The host test opens and saves a temporary `/tmp` project before quitting so
  REAPER does not prompt to save the validation project.

**Verification.**

- `make native-test` passes.
- `make native-host-test` passes. `clap-validator` was not found on PATH, so
  external CLAP validation was skipped; REAPER rendered finite non-silent
  output with peak `1.136228e+05`.

**Next work.**

1. Continue JSFX parity work using `native/bin/nilamp_render`; the plugin shell
   should stay thin until the DSP/parity surface stabilizes.
2. For GUI work, follow `docs/notes/gui-dev.md`: Pugl for native plugin
   windowing/embed, Sokol headers for lightweight runtime support, and NanoVG
   for immediate-mode 2D drawing.

### Session: ABX harness gain mapping fix -> PUBLIC GATES PASS

**Edit summary.**

- Confirmed `TrackFX_SetParam` writes raw JSFX slider values by logging
  `TrackFX_GetParam` readbacks after every harness slider set.
- Moved REAPER project/render sample-rate setup before media insertion and
  JSFX instantiation so Keller's `srate`-derived coefficients initialize at
  the intended render rate.
- Fixed the ABX input gain mapping to compensate only Keller's explicit
  `+12 dB` `p.gin` lift. The old `sqrt(1.2)` compensation made the native path
  too hot and caused the sine gate miss.
- Bumped the JSFX cache key and included the Lua render driver plus staged
  JSFX source in the key so harness changes cannot reuse stale reference WAVs.

**Verification.**

- `python3 -m py_compile tools/abx_compare.py tools/jsfx_render/render_jsfx.py`
  passes.
- `make native` and `make native-test` pass.
- ABX sine: `-16.5 dB` residual below native peak, threshold `-16.0 dB` -> PASS.
  Correlation `0.999330`; best native-to-JSFX gain `0.805336` (`-1.88 dB`).
- ABX sweep: `-16.8 dB` residual below native peak, threshold `-11.2 dB` -> PASS.
  Correlation `0.978765`; best native-to-JSFX gain `0.732585` (`-2.70 dB`).

**Next work.**

1. Keep the ABX harness gain mapping at `p.gin = gain_db - 12`.
2. Remaining parity work should target shape/phase residuals, not global output
   gain; the public gates now pass.

### Session: Source-backed JSFX topology fixes -> SINE STILL SCALE-LIMITED

**Edit summary.**

- Added JSFX mode-0 `hp2` (`0.41 Hz`) between T2 and the cathodyne in the
  native graph.
- Added JSFX T4/T5 `advk` averaging at the top of each native sample.
- Updated the Python tap oracle and regenerated tap fixtures for the new graph.
- Made ABX JSFX slider pins explicit and added correlation / best-fit gain
  diagnostics to `tools/abx_compare.py`.

**Verification.**

- `python3 tools/gen_fixtures.py` regenerated fixtures successfully.
- `make clean-native`, `make native-test`, and `make native` pass.
- ABX sine: `-15.1 dB` residual below native peak, threshold `-16.0 dB` -> FAIL.
  Correlation `0.998704`; best native-to-JSFX gain `0.776263` (`-2.20 dB`).
- ABX sweep: `-15.0 dB` residual below native peak, threshold `-11.2 dB` -> PASS.
  Correlation `0.979004`; best native-to-JSFX gain `0.686693` (`-3.26 dB`).

**Next work.**

1. Do not add arbitrary output gain compensation. The sine miss is now
   scale-dominated, but the next change should be tied to a JSFX source line or
   verified REAPER slider/value behavior.
2. Confirm whether `TrackFX_SetParam` is applying raw JSFX slider values by
   logging `TrackFX_GetParam`/formatted values after each set in a temporary
   render probe or harness update.
3. If slider values are correct, inspect remaining gain path differences:
   `g1/g2/g3` smoothing/init, `gcomp` mode-0 behavior, and post-power `gout`.

### Session: Native fixture parity and ABX presets -> PARTIAL SUCCESS

**Edit summary.**

- Expanded native test-only C wrappers for PKD, ADNL, SVF filters, CK/CD tube
  stages, power-pair diagnostics, PSS, and full 5E3 taps.
- Regenerated nilamp tap fixtures to match the native tap renderer order:
  `v_out, res1_v, res3_v, res4_v, drive_t4, res5_v, res_t5_v, dvs2, dvs3`.
- Added deterministic `tools/abx_compare.py --preset sine|sweep` input
  generation so ABX does not depend on tracked WAV files.

**Verification.**

- `python3 tools/gen_fixtures.py` regenerated fixtures successfully.
- `make clean-native`, `make native-test`, and `make native` pass.
- `native/bin/nilamp_taps_render` on the generated sweep produced finite
  9-channel output for all taps through the old `[0.5, 3.0]` s collapse window.
- ABX sine: `-15.2 dB` residual below native peak, threshold `-16.0 dB` -> FAIL.
- ABX sweep: `-14.6 dB` residual below native peak, threshold `-11.2 dB` -> PASS.

**Next work.**

1. Investigate the remaining sine ABX miss. First compare native/JSFX peak and
   RMS scale after alignment; sine peak is currently native `0.2488`, JSFX
   `0.1937`, which suggests a remaining gain/topology mismatch rather than
   collapse.
2. Keep `make native-test` as the regression gate before touching ABX-facing
   DSP.
3. Rerun:

```bash
python3 tools/abx_compare.py --preset sine --rms-threshold-db -16 --out-dir /tmp/nilamp_abx_native --label native_sine
python3 tools/abx_compare.py --preset sweep --rms-threshold-db -11.2 --jsfx-timeout 120 --out-dir /tmp/nilamp_abx_native --label native_sweep
```

### Session: Native engine replaces feedback-loop investigation → READY FOR C PARITY WORK

**Decision.**  Stop investing in the old graph/toolchain and continue only on
the native C/Lua path.

**Current architecture.**

- C owns realtime DSP and offline rendering.
- Lua is allowed only for build-time config/codegen.
- Python remains the numeric oracle, table generator, fixture generator, and
  ABX analysis layer.
- Keller's JSFX source remains canonical.

**Important DSP finding carried forward.**  The old implementation collapsed
because the PSS/tube call order put `dvs2`/`dvs3` one sample late entering the
tubes. JSFX computes PSS first from previous-sample tube currents, then feeds
current-sample `dvs2`/`dvs3` into the tube stages. The native C engine must keep
that exact order.

**Native state.**

- `native/src/nilamp_dsp.c` contains the C engine and JSFX-order PSS loop.
- `native/bin/nilamp_render` is the canonical offline renderer.
- `native/bin/nilamp_taps_render` emits 9 taps:
  `v_out, res1_v, res3_v, res4_v, drive_t4, res5_v, res_t5_v, dvs2, dvs3`.
- `native/generated/nilamp_tables.{c,h}` are generated by
  `tools/gen_5e3_tables.py`.
- `make native-test` currently covers the first native primitive fixtures.

**Next work.**

1. Expand native fixture tests to cover PKD, ADNL, SVF tone/PEQ/shelf,
   `tube_ck`, `tube_cd`, power-pair taps, and full 5E3 taps.
2. Render the cached ABX sweep through `native/bin/nilamp_taps_render` and
   verify the old [0.5, 3.0] s output collapse is gone.
3. Run `python3 tools/abx_compare.py ...` with the native renderer and compare
   against the public gates:
   - sine residual >= -16 dB
   - sweep residual >= -11.2 dB
4. Record native ABX/oracle numbers here before any C CLAP plugin work.

**Commands.**

```bash
python3 tools/gen_5e3_tables.py
make clean-native
make native
make native-test
python3 tools/abx_compare.py input.wav
```
