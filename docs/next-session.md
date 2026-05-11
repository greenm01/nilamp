# Next session - native C/KDL parity and legacy purge

## SESSION LOG (most recent first)

### Session: Windows v1.0.1 release package deploy

**Context.** The GitHub `v1.0.1` release had macOS and Linux packages plus
checksums uploaded, but no Windows download package.

**Edit summary.**

- Confirmed current `main` is one docs-only commit after tag `v1.0.1`; package
  code matches the tagged source.
- Downloaded the current release `SHA256SUMS` into
  `dist/release-v1.0.1-current` so the macOS and Linux hashes were preserved.
- Ran `nmake /nologo /f Makefile.msvc RELEASE_VERSION=1.0.1
  package-windows-release` after `clean-native`, generating the Windows x64
  ZIP, ZIP signature, refreshed `SHA256SUMS`, and `SHA256SUMS.asc`.
- Uploaded the Windows ZIP, ZIP signature, `SHA256SUMS`, and `SHA256SUMS.asc`
  to GitHub release `v1.0.1` with `gh release upload --clobber`.
- Updated the `v1.0.1` GitHub release notes so the downloads and install
  instructions list Windows, macOS, and Linux packages.

**Verification.**

- ZIP contents include `nilamp-twd-mkii.clap`,
  `nilamp-twd-mkii.vst3/Contents/x86_64-win/nilamp-twd-mkii.vst3`,
  `install.cmd`, `README-Windows.txt`, and `LICENSE`.
- Windows ZIP SHA-256 is
  `9047dce67bcc76a29e85b22dc9234a7ebd393ebd4dd11082f5c12ac5c1d5c4da`.
- GPG verification reports good signatures for the Windows ZIP and
  `SHA256SUMS`; GPG also notes the sandbox trustdb is not writable.
- `nmake /nologo /f Makefile.msvc native-host-test` passes. CLAP validator
  reports 21 tests run, 15 passed, 0 failed, 6 skipped. Steinberg VST3
  validator reports 47 tests passed, 0 failed.
- `gh release view v1.0.1` confirms the uploaded Windows ZIP is 1,281,613
  bytes and has digest
  `sha256:9047dce67bcc76a29e85b22dc9234a7ebd393ebd4dd11082f5c12ac5c1d5c4da`.

### Session: Linux v1.0.1 release package redeploy

**Context.** The GitHub `v1.0.1` release had macOS assets and checksum files
uploaded, but the Linux download package was missing.

**Edit summary.**

- Confirmed `HEAD` matches tag `v1.0.1` at `2fd380f`.
- Downloaded the current release `SHA256SUMS` into
  `dist/release-v1.0.1-linux` so the existing macOS hash was preserved.
- Ran `make package-linux-release DIST_DIR=dist/release-v1.0.1-linux`,
  generating the Linux x86_64 tarball, tarball signature, refreshed
  `SHA256SUMS`, and `SHA256SUMS.asc`.
- Uploaded the Linux tarball, tarball signature, `SHA256SUMS`, and
  `SHA256SUMS.asc` to GitHub release `v1.0.1` with `gh release upload
  --clobber`.

**Verification.**

- Package contents include `nilamp-twd-mkii.clap`,
  `nilamp-twd-mkii.vst3/Contents/x86_64-linux/nilamp-twd-mkii.so`,
  `install.sh`, `README-Linux.txt`, and `LICENSE`.
- `sha256sum -c SHA256SUMS` passes after downloading the existing macOS ZIP
  locally for a complete check.
- GPG verification passes for the Linux tarball and `SHA256SUMS`.
- `make native-test` passes.
- `gh release view v1.0.1` confirms the uploaded Linux tarball is 1,602,653
  bytes and has digest
  `sha256:7b3682d23667e14ea6acbba64b453519690b2387bc17013408d64f822e8b63c4`.

### Session: Unique host parameter names and VST3 smooth automation

**Context.** REAPER showed two host parameters named `Gain`, and envelope-lane
controls appeared to pull back toward center in read/default automation states.
The duplicate names came from shared GUI/host labels; VST3 also quantized
continuous host-normalized values to display steps before applying automation.

**Edit summary.**

- Added generated `host_name` metadata to controls so CLAP/VST3 can expose
  unique DAW names while the custom GUI keeps compact labels.
- Renamed host-visible ambiguous parameters to `Input Gain`, `Output Gain`,
  `Speaker Resonance Gain 1/2`, and `Speaker Inductor Gain 1/2`.
- Kept parameter IDs, state version, DSP defaults, and GUI labels unchanged.
- Changed VST3 normalized-to-plain conversion so continuous parameters remain
  smooth; enum/list parameters still resolve to discrete choices.
- Added CLAP and VST3 loader checks for the new host names and VST3 continuous
  conversion behavior.

**Verification.**

- `make native-test` passes.
- `make native-host-test` passes; VST3 validator reports the unique parameter
  titles and `47 tests passed, 0 tests failed`.
- `make native-jsfx-test` passes with sine residual `-84.3 dB`, peak
  native/JSFX `0.3422 / 0.3422`, and best gain `+0.00 dB`.
- `python3 -m py_compile tools/gen_amp_models.py` passes.

### Session: Keller/YSFX unity input calibration

**Context.** Keller reported nilamp VST3 as roughly `6 dB` lower than his
plugin. The old headless YSFX parity harness applied a `0.5` input feed before
JSFX processing, but DAW-hosted YSFX receives unity input from the host.

**Edit summary.**

- Changed the Keller TWD DLX II model input feed from `0.5` to `1.0`, with no
  state migration or backwards compatibility because existing projects do not
  need preservation for this calibration.
- Updated the YSFX render wrapper, performance harness, and ABX cache key to
  use unity input gain by default.
- Regenerated nilamp tap fixtures for the new unity-input reference.
- Kept CLAP/VST3 state schema and parameter IDs unchanged.

**Verification.**

- `make native-test` passes.
- `make native-host-test` passes with Steinberg VST3 validator installed; VST3
  validator result is `47 tests passed, 0 tests failed`.
- `make native-jsfx-test` passes with sine residual `-84.3 dB`, peak
  native/JSFX `0.3422 / 0.3422`, and best gain `+0.00 dB`.
- `make native-perf-bench PERF_BENCH_ARGS="--json-out /tmp/nilamp_perf/results_unity_input_full.json"` passes; median matched speedups are `2.08x` for CLAP and `2.06x` for VST3 versus Keller/YSFX.
- `python3 -m py_compile tools/benchmark_keller_perf.py tools/abx_compare.py tools/jsfx_render/render_ysfx.py` passes.

### Session: Dirty-driven editor redraw

**Context.** Keller's Gig Performer report may include editor/UI overhead in
host CPU metering. The native DSP benchmark is faster than Keller/ysfx, but
nilamp's editor was still pumping at roughly 30 Hz while visible.

**Edit summary.**

- Changed the shared GUI timer path to refresh parameters without
  unconditionally requesting a redraw.
- Kept redraws event/dirty-driven: expose, resize, direct user input, host
  parameter changes, or already-dirty model state still repaint promptly.
- Kept the CLAP and VST3 editor pump timers at 33 ms for responsive embedded
  event dispatch, but removed the continuous 30 Hz redraw when the GUI is
  static.
- Did not change DSP, parameter IDs, state format, or the perf benchmark
  harness.

**Verification.**

- `make native-test` passes.
- `make native-host-test` passes; external VST3 validator is not installed and
  is skipped.
- `make native-perf-bench PERF_BENCH_ARGS="--quick --runs 1 --warmups 0 --duration 0.25 --json-out /tmp/nilamp_perf/dirty_gui_smoke.json"` passes.

### Session: CLAP editor callback fallback hardening

**Context.** The shared GUI fixes apply to CLAP, but CLAP host-side editor
refresh depends on either host timer support or `on_main_thread()` callback
pumping. Hosts without timer registration only received one callback when the
GUI was shown.

**Edit summary.**

- Added a CLAP GUI callback fallback helper that re-requests
  `host->request_callback()` from GUI/main-thread paths while the editor is
  visible and no host timer is registered.
- Kept callback requests out of `process()` and active `params.flush()`; those
  paths continue to update atomic parameter state only.
- Refreshed CLAP GUI values from state load, inactive `params.flush()`, GUI
  show, timer ticks, and callback fallback ticks.
- Added `NILAMP_GUI_LOG` messages for CLAP timer ticks, callback fallback
  reasons, inactive host parameter flushes, and state-load refreshes.
- Updated the Reaper/Carla validation note to test CLAP and VST3 separately.

**Verification.**

- `make native-test` passes.
- `make native-host-test` passes; external VST3 validator is not installed and
  is skipped.
- `make native-jsfx-test` passes with sine residual `-81.7 dB`.
- `make native-perf-bench PERF_BENCH_ARGS="--quick --runs 1 --warmups 0 --duration 0.25 --json-out /tmp/nilamp_perf/clap_editor_fallback_smoke.json"` passes.
- `python3 -m py_compile tools/benchmark_keller_perf.py tools/abx_compare.py tools/jsfx_render/render_ysfx.py` passes.
- `git diff --check` passes.

### Session: Keller host-feedback editor fixes

**Context.** Helmut Keller reported three host-visible issues from VST3 testing:
mouse wheel did not change GUI parameters, DAW-side parameter edits did not
refresh the GUI until focus returned, and a possible `6 dB` input-level mismatch.
Local validation is through REAPER and Carla rather than Gig Performer.

**Edit summary.**

- Added Pugl scroll handling to the custom GUI and applied wheel deltas to
  hovered knobs, value boxes, toggles, and enum selectors using existing
  parameter step sizes.
- Added a public GUI refresh hook and made the VST3 editor refresh from host
  parameter changes immediately, with a guard to avoid reentrant refreshes for
  editor-originated edits.
- Documented a REAPER/Carla host-validation workflow for input calibration,
  mouse wheel behavior, host parameter refresh, and editor-open CPU trends.
- Clarified that the synthetic perf harness excludes DAW editor and host CPU
  metering behavior, and that VST3 cold lifecycle timing needs real-host or
  subprocess validation.
- Did not change input calibration yet; the current ysfx parity gate still
  matches, and the reported `6 dB` delta should be confirmed in REAPER before a
  DSP calibration change.

**Verification.**

- `make native-test` passes.
- `make native-host-test` passes; external VST3 validator is not installed and
  is skipped.
- `make native-jsfx-test` passes with sine residual `-81.7 dB`.
- `make native-perf-bench PERF_BENCH_ARGS="--quick --runs 1 --warmups 0 --duration 0.25 --json-out /tmp/nilamp_perf/host_feedback_smoke.json"` passes.
- Installed CLAP to `~/.clap/nilamp-twd-mkii.clap` and VST3 to
  `~/.vst3/nilamp-twd-mkii.vst3`; installed loader smokes pass.
- `python3 -m py_compile tools/benchmark_keller_perf.py tools/abx_compare.py tools/jsfx_render/render_ysfx.py` passes.
- `git diff --check` passes.

### Session: Keller/ysfx performance benchmark harness

**Context.** Need a repeatable benchmark that compares nilamp native plugin
runtime against Keller's JSFX implementation under ysfx, while keeping ysfx
load/compile/JIT cost visible but separate from steady-state plugin processing.

**Edit summary.**

- Added native in-memory benchmark drivers for Keller/ysfx, loaded CLAP, and
  loaded VST3.
- Added `tools/benchmark_keller_perf.py` to run the extended matrix, summarize
  steady-state runtime as the headline, report lifecycle/reload phases
  separately, and optionally emit JSON.
- Added `make native-perf-bench` with `PERF_BENCH_ARGS` passthrough.
- Documented the benchmark in README and `docs/notes/perf-benchmark.md`.

**Verification.**

- `make native-perf-bench PERF_BENCH_ARGS="--quick --runs 1 --warmups 0 --duration 0.25 --json-out /tmp/nilamp_perf/smoke.json"` passes.
- Smoke run reports steady-state plugin processing for Keller/ysfx, CLAP, and
  VST3, plus separate lifecycle and reload sections.
- Smoke residual sanity after JSFX warm-up trim is about `-73.1 dB` for CLAP
  and VST3 versus Keller/ysfx on the short sine case.
- `make native-test` passes.
- `make native-host-test` passes; external VST3 validator is not installed and
  is skipped.
- `make native-jsfx-test` passes with sine residual `-81.7 dB`.
- `git diff --check` passes.

### Session: Windows CLAP/VST3 release package upload

**Context.** The Windows release asset on GitHub still contained the older
CLAP-only ZIP after the Windows VST3 build landed.

**Edit summary.**

- Pushed `0354bdc` (`Add Windows VST3 build and validation`) to `origin/main`.
- Downloaded the current v1.0.0 release `SHA256SUMS` into
  `dist\release-v1.0.0-current`.
- Ran `nmake /nologo /f Makefile.msvc package-windows-release`, generating a
  Windows x64 ZIP that contains both `nilamp-twd-mkii.clap` and
  `nilamp-twd-mkii.vst3`.
- Uploaded the regenerated Windows ZIP, ZIP signature, `SHA256SUMS`, and
  `SHA256SUMS.asc` to GitHub release `v1.0.0` with `gh release upload
  --clobber`.
- Updated the v1.0.0 GitHub release notes so all platform install instructions
  describe CLAP and VST3.

**Verification.**

- ZIP contents include `nilamp-twd-mkii.clap`,
  `nilamp-twd-mkii.vst3\Contents\x86_64-win\nilamp-twd-mkii.vst3`,
  `install.cmd`, `README-Windows.txt`, and `LICENSE`.
- Local `SHA256SUMS` preserves the macOS and Linux hashes and updates the
  Windows hash to
  `b3d318e8c296bc2e467bbeb454bc634fae8dfea5d98bb4a21b9aa5fac19957eb`.
- GPG verification reports good signatures for the Windows ZIP and
  `SHA256SUMS`; the sandbox returned a nonzero status because the trustdb was
  not writable.
- `gh release view v1.0.0` confirms the uploaded Windows ZIP is 1,276,742
  bytes and has digest
  `sha256:b3d318e8c296bc2e467bbeb454bc634fae8dfea5d98bb4a21b9aa5fac19957eb`.

### Session: Windows MSVC VST3 build and install

**Context.** The Windows MSVC path built and installed the updated CLAP, but
VST3 was only wired into the POSIX Makefile. Windows needed a native VST3 bundle
so REAPER can test CLAP and VST3 side by side.

**Edit summary.**

- Added Windows VST3 SDK object builds, bundle layout, loader smoke, host
  validation, per-user install, and release packaging to `Makefile.msvc`.
- Updated the VST3 editor shell to advertise `HWND` on Windows and create the
  Pugl GUI through `NILAMP_GUI_API_WIN32`.
- Made `test_vst3_load` use `LoadLibraryA`, `InitDll`, `ExitDll`, and the
  Windows bundle path.
- Made shared host parameter text parsing use the existing Windows-safe
  `nilamp_stricmp` shim.
- Updated Windows package/install docs and the Windows package script so ZIPs
  include and install both CLAP and VST3.

**Verification.**

- `nmake /nologo /f Makefile.msvc native-test` passes and builds
  `native\bin\nilamp-twd-mkii.vst3\Contents\x86_64-win\nilamp-twd-mkii.vst3`.
- Installed Steinberg's VST3 SDK validator from the official SDK source:
  cloned `https://github.com/steinbergmedia/vst3sdk.git` to `C:\src\vst3sdk`,
  built `validator` in `C:\src\vst3sdk-build`, added
  `C:\src\vst3sdk-build\bin\Release` to the user PATH, and added a Scoop shim
  at `C:\Users\mag\scoop\shims\validator.exe`.
- `validator -version` reports `VST 3.8.0 Plug-in Validator`.
- `nmake /nologo /f Makefile.msvc native-host-test` passes. `clap-validator`
  reports 21 tests run, 15 passed, 0 failed, 6 skipped. Steinberg VST3
  `validator` reports 47 tests passed, 0 failed.
- Installed CLAP to
  `%LOCALAPPDATA%\Programs\Common\CLAP\nilamp-twd-mkii.clap` and VST3 to
  `%LOCALAPPDATA%\Programs\Common\VST3\nilamp-twd-mkii.vst3`.
- Installed CLAP validation passes with 21 tests run, 15 passed, 0 failed, 6
  skipped.
- Installed VST3 validation passes with 47 tests passed, 0 failed. Validator
  confirms 1 audio input bus and 1 audio output bus, and both mono and stereo
  arrangements pass. The native smoke tests also assert default mono
  input/output metadata, matching the Linux/macOS JSFX-aligned behavior.

### Session: Public release and parity docs

**Context.** The README still described nilamp as CLAP-only and carried stale
release-package wording after the VST3 export and mono I/O parity work.

**Edit summary.**

- Updated the README to describe nilamp as a native CLAP/VST3 guitar amp plugin
  with Windows, macOS, and Linux release packages.
- Added the current ysfx-verified input-to-output parity claim:
  `-80.7 dB` full-chain sweep residual against Keller TWD DLX II JSFX defaults.
- Refreshed ABX notes and the older DSP project notes so the public docs no
  longer present the current plugin path as CLAP-only.

**Verification.**

- Docs-only change; build not rerun.
- `git diff --check` passes.

### Session: Default mono CLAP/VST3 I/O

**Context.** REAPER listening showed nilamp was closest to Keller when the
input/output channel layouts were matched manually. Keller `TWD DLX II` declares
mono input and mono output, while nilamp had been declaring stereo by default.

**Edit summary.**

- Changed CLAP's default audio port declaration to mono input / mono output.
- Added CLAP audio-port configurations for `Mono` and `Stereo`, plus config
  info support so hosts can inspect and select stereo explicitly while the
  plugin is deactivated.
- Changed VST3's default bus arrangement to mono while preserving the existing
  mono/stereo `setBusArrangements()` support.
- Extended CLAP and VST3 smoke tests to cover default mono metadata and mono
  processing, while retaining stereo processing coverage.
- Reinstalled the rebuilt CLAP and VST3 bundles for REAPER.

**Verification.**

- `make native-test` passes.
- `make native-host-test` passes with installed `clap-validator` and Steinberg
  `validator`: CLAP reports 21 tests run, 16 passed, 0 failed, 5 skipped; VST3
  reports 47 tests passed, 0 failed.
- `make native-jsfx-test` passes: default sine residual `-81.7 dB`; tap
  diagnostics and low-input regression pass.
- `make native-jsfx-matrix-test` passes; all matrix cases pass including
  `splitter_ltp2` at `-71.5 dB`.
- Installed CLAP at
  `~/Library/Audio/Plug-Ins/CLAP/nilamp-twd-mkii.clap` and VST3 at
  `~/Library/Audio/Plug-Ins/VST3/nilamp-twd-mkii.vst3`; installed loader smokes
  pass and installed VST3 passes `validator -q`.

### Session: LTP 2 JSFX render startup fix

**Context.** The new parity matrix initially had to mark `LTP 2` as a known
Keller/ysfx reference-render issue because `mode=3` produced silence.

**Edit summary.**

- Fixed `native/bin/ysfx_render` to apply requested JSFX slider values before
  `ysfx_init()`.
- Root cause: Keller's `changed_init()` seeds `old = this + 1`. With the old
  renderer lifecycle, setting `mode` from default `2` to `3` after init made
  `old == this`, so Keller's `p.mode.changed()` did not run and the LTP power
  stage setup stayed uninitialized.
- Removed the `LTP 2` known-issue escape from the parity matrix; it is now a
  normal required passing case.

**Verification.**

- Direct Keller/ysfx render for `mode=3` now produces nonzero output.
- `abx_compare.py --preset sine --tube1 1 --splitter 3 --rms-threshold-db -16`
  passes with `LTP 2` residual `-71.5 dB`.
- `compare_taps.py --preset sine --tube1 1 --splitter 3` passes all tap
  diagnostics for `LTP 2`.
- `make native-jsfx-matrix-test` passes with `splitter_ltp2` residual
  `-71.5 dB`; no known-issue cases remain.
- `make native-jsfx-test` passes.

### Session: Keller option parity and gain-comp matrix

**Context.** Follow-up audit for remaining Keller parity gaps and hardcoded
parameters after the noon gain/brightness fixes.

**Edit summary.**

- Fixed Keller gain compensation ordering: nilamp now squares the Volume knob
  first, then applies Tube 1 and Splitter compensation once, matching Keller's
  JSFX `vol = (p.vol / 100)^2` path.
- Moved Keller's gain-comp constants for 12AX7 and LTP compensation into the
  KDL-generated model data instead of keeping them hardcoded in the DSP loop.
- Exposed the remaining Keller option sliders in `nilamp_render`,
  `abx_compare.py`, and `compare_taps.py`: output gain, tone-stack `Fmid/Qmid`,
  speaker resonance, speaker inductor, and gain compensation.
- Added `tools/parity_matrix.py` plus `make native-jsfx-matrix-test` to cover
  default sine/sweep parity, Tube 1, splitter modes, LTP3 gain-comp modes, and
  option-slider probes.

**Verification.**

- `make native-test` passes.
- `make native-jsfx-test` passes: default sine residual `-81.7 dB`, tap
  diagnostics pass, and low-input regression passes.
- `make native-jsfx-matrix-test` passes for all supported Keller render cases:
  default sine `-81.7 dB`, default sweep `-80.7 dB`, CD 5E3 `-80.5 dB`, CD BAL
  `-80.3 dB`, LTP3 `-81.7 dB`, LTP3 gain-comp modes `-78.3..-81.7 dB`, output
  gain `-81.7 dB`, tone options `-81.7 dB`, and speaker options `-79.5 dB`.
- Matrix records `LTP 2` as a known Keller/ysfx reference-render issue because
  direct JSFX renders for `mode=3` are silent in this harness while nilamp
  produces signal.
- `make native-host-test` passes with installed `clap-validator` and Steinberg
  `validator`: CLAP reports 21 tests run, 16 passed, 0 failed, 5 skipped; VST3
  reports 47 tests passed, 0 failed.
- Reinstalled the macOS VST3 at
  `~/Library/Audio/Plug-Ins/VST3/nilamp-twd-mkii.vst3`; installed bundle passes
  `test_vst3_load` and `validator -q`.
- `git diff --check` passes.

### Session: Keller final lowpass brightness parity

**Context.** REAPER listening after the noon gain calibration still found
Keller's JSFX a little noisier/brighter and nilamp a little darker at noon with
`LTP 1`.

**Edit summary.**

- Fixed nilamp's final TWD DLX II lowpass to match Keller
  `flt_df2_set_lp(10000, sqrt(0.5), 1, 0)`: frequency prewarp on, Q prewarp
  off.
- Added a focused native fixture/API check for that Keller DF2 lowpass mode.
- Updated the Python fixture generator and regenerated affected top-level
  fixture buffers.
- Reinstalled the corrected macOS VST3 bundle for REAPER.

**Verification.**

- `make native-test` passes.
- `make native-host-test` passes with installed `clap-validator` and Steinberg
  `validator`.
- `make native-jsfx-test` passes: sine ABX residual `-81.7 dB`, correlation
  `1.000000`, all taps within diagnostic threshold, and low-input regression
  passes.
- Sweep ABX at noon/LTP1 passes with residual `-80.7 dB`; JSFX-vs-nilamp band
  deltas through 12 kHz are effectively `0.000 dB` after being about `+2` to
  `+3 dB` in the high band before the fix.
- Installed VST3 at `~/Library/Audio/Plug-Ins/VST3/nilamp-twd-mkii.vst3`
  passes `test_vst3_load` and `validator -q`.

### Session: Keller noon input gain calibration

**Context.** REAPER listening against Keller's original TWD DLX II JSFX showed
nilamp had less internal gain at visible noon settings with `LTP 1`.

**Edit summary.**

- Added explicit KDL/generated model data for Keller's fixed `+12 dB` input
  calibration.
- Applied that offset inside native DSP while keeping the visible input Gain
  parameter default at `0 dB`.
- Updated ABX/tap comparison tooling so nilamp `Gain=0` maps to JSFX `gin=0`.
- Regenerated native model data and fixture buffers for the hotter default.

**Verification.**

- `make native-test` passes.
- `make native-host-test` passes with installed `clap-validator` and Steinberg
  `validator`.
- `make native-jsfx-test` passes: sine ABX residual `-37.1 dB`, correlation
  `0.999849`, best gain `+0.02 dB`; tap diagnostics and low-input regression
  pass.

### Session: macOS VST3 export

**Context.** Added a macOS-first VST3 export path alongside the existing CLAP
plugin while keeping realtime DSP and the custom editor implementation in C.

**Edit summary.**

- Vendored the official MIT VST3 SDK plugin-side sources under
  `third_party/vst3sdk`.
- Added VST3 metadata to the KDL amp model and generated native model data.
- Added `native/src/nilamp_host.c` as shared plugin host glue for parameter
  mapping, state v3 serialization, stereo processing, and output sanitization.
- Added a VST3 processor/controller/editor shell in Objective-C++, reusing the
  C DSP engine and Cocoa Pugl/Sokol/Nuklear editor.
- Added `make install-vst3-user`, macOS VST3 bundle packaging, and a native
  VST3 loader smoke test.
- Changed the macOS CLAP build from a flat `.clap` dylib to a validator-friendly
  `.clap` bundle with `Contents/MacOS/nilamp-twd-mkii` and `Info.plist`.

**Verification.**

- `make native` passes and builds `native/bin/nilamp-twd-mkii.vst3`.
- `make native-test` passes, including the new VST3 factory/load/process/state
  and automation smoke test.
- `make native-host-test` passes with installed `clap-validator` and Steinberg
  `validator`: CLAP reports 21 tests run, 16 passed, 0 failed, 5 skipped; VST3
  reports 47 tests passed, 0 failed.

### Session: Windows MSVC CLAP build and install

**Context.** Ported the native CLAP build to Windows with the MSVC toolchain so
nilamp can be loaded in REAPER as a native Windows CLAP plugin.

**Edit summary.**

- Added `Makefile.msvc` for VS 2022 Build Tools / NMake builds.
- Added Windows compatibility shims for case-insensitive string compares,
  dynamic CLAP loading in smoke hosts, and MSVC x64 FTZ/DAZ support.
- Vendored Pugl Win32/OpenGL backend files and enabled the existing
  Pugl/Sokol/Nuklear editor through `CLAP_WINDOW_API_WIN32`.
- Added Windows docs and made CLAP validation find `.exe` tools plus a local
  `clap-validator` build.
- Added a per-user Windows install target for
  `%LOCALAPPDATA%\Programs\Common\CLAP`.

**Verification.**

- `nmake /f Makefile.msvc native` passes.
- `nmake /f Makefile.msvc native-test` passes.
- `nmake /f Makefile.msvc native-host-test` passes with `clap-validator`:
  21 tests run, 15 passed, 0 failed, 6 skipped.
- `nmake /f Makefile.msvc install-clap-user` installs
  `%LOCALAPPDATA%\Programs\Common\CLAP\nilamp-twd-mkii.clap`.
- `native\bin\test_clap_load.exe %LOCALAPPDATA%\Programs\Common\CLAP\nilamp-twd-mkii.clap`
  exits `0`.

### Session: CLAP control audit and Sag mapping

**Context.** Audited CLAP-exposed controls against the generated KDL specs and
native DSP path after Sag was reported as inaudible.

**Edit summary.**

- Changed Sag from a linear `0..1x` p3 supply-resistance multiplier to a
  squared `0..4x` curve where `50%` is the Keller JSFX fixed PSS reference.
- Updated JSFX comparison defaults so Sag `50%` remains the parity point.
- Added CLAP smoke coverage that checks every generated control against CLAP
  metadata, enum text conversion, automation writes, and post-automation
  finite processing.
- Added native audio-impact coverage for Sag, Tube 1, Splitter modes, and
  Gain Compensation.

**Verification.**

- `make native-test` passes; Sag 0 vs 100 under hot drive now produces
  `rms_diff=1.440655e-03`.
- `make native-host-test` passes (`clap-validator` absent, skipped).
- `make native-jsfx-test` passes:
  - sine ABX residual `-40.6 dB`, correlation `0.999919`;
  - all staged taps within diagnostic threshold;
  - low-input regression passes.
- `git diff --check` passes.
- `make install-clap-user` installs and re-signs
  `~/.clap/nilamp-twd-mkii.clap`.
- `native/bin/test_clap_load ~/.clap/nilamp-twd-mkii.clap` exits `0`.

### Session: Tube 1 and Splitter DSP controls

**Context.** Implemented the Keller TWD DLX II Tube 1 and Splitter topology
switches in the native DSP engine instead of leaving the GUI selectors static.

**Edit summary.**

- Added KDL-backed enum controls for `Tube 1` and `Circuit`, appending CLAP
  params so existing parameter IDs remain stable.
- Added the 12AY7 T1 table and LTP branch ADNL tables to the generated 5E3
  table set.
- Implemented Tube 1 selection, cathodyne modes, balanced cathodyne, and LTP
  modes 1-3 in the C sample path, including JSFX gain-comp behavior.
- Bumped CLAP state to v3; v1/v2 states backfill `Tube 1=12AX7` and
  `Splitter=CD 5E3` to preserve old native session sound.
- Replaced the main-screen static Tube 1/Splitter labels with real enum
  dropdowns and generalized enum labels through generated KDL metadata.
- Updated native/JSFX comparison tools so topology params pass through to both
  renderers, and fixed the staged tap harness to capture LTP `res4_v`.

**Verification.**

- `make native-test` passes.
- `make native-host-test` passes (`clap-validator` absent, skipped).
- `make native-jsfx-test` passes:
  - sine ABX residual `-40.6 dB`, correlation `0.999919`;
  - all staged taps within diagnostic threshold;
  - low-input regression passes.
- `git diff --check` passes.
- `make install-clap-user` installs and re-signs
  `~/.clap/nilamp-twd-mkii.clap`.
- `native/bin/test_clap_load ~/.clap/nilamp-twd-mkii.clap` exits `0`.

### Session: CLAP About screen and main title sizing

**Context.** Matched the CLAP main-screen title hierarchy more closely to the
Keller JSFX and implemented the JSFX-style About screen from Options.

**Edit summary.**

- Added larger custom Nuklear font handles for title/subtitle/about text with
  fallback to the normal GUI font if the custom atlas is unavailable.
- Increased the centered `nilamp` and `Keller TWD DLX II` main-screen text
  sizes to match the JSFX visual hierarchy.
- Added an About screen reachable from Options, including `Ported to C by
  niltempus`, with `< back` returning to Options.

**Verification.**

- `make native-host-test` passes (`clap-validator` absent, skipped).
- `make install-clap-user` installs and re-signs `~/.clap/nilamp.clap`;
  `native/bin/test_clap_load ~/.clap/nilamp.clap` exits `0`.

### Session: CLAP text-key hardening and bipolar input gain

**Context.** Backspace in CLAP value boxes could still crash REAPER, and the
input gain control needed to match the output gain range/knob behavior.

**Edit summary.**

- Kept Backspace/Delete/Enter text-edit keys inside nilamp's custom value-box
  editor instead of forwarding them to Nuklear while a text box is active.
- Hardened empty text draw/transient input handling for value boxes.
- Changed input gain from `0..24 dB` to `-12..12 dB`, keeping the `0 dB`
  default and matching output gain's bipolar knob style.
- Added CLAP smoke assertions for matching input/output gain ranges and
  negative input-gain automation/state restore.

**Verification.**

- `make native-host-test` passes (`clap-validator` absent, skipped).

### Session: CLAP preamp text crash hardening and dropdown picker

**Context.** Hardened custom text entry after REAPER crashes while typing in
preamp value boxes, and replaced Gain Compensation click-cycling with an actual
dropdown picker.

**Edit summary.**

- Stopped forwarding typed characters into Nuklear; custom value boxes now own
  text input exclusively.
- Guarded custom text drawing/caret code against unterminated edit buffers and
  missing font state.
- Added open/close dropdown state and rendered Gain Compensation as a picker.
- Dropdowns close on outside click, Escape, and screen switches.

**Verification.**

- `make native-host-test` passes (`clap-validator` absent, skipped).
- `make install-clap-user` installs and re-signs
  `~/.clap/nilamp.clap`; `native/bin/test_clap_load ~/.clap/nilamp.clap`
  exits `0`.

### Session: CLAP selector and text box polish

**Context.** Fixed Options screen selector layout and text-entry behavior from
REAPER screenshots, and added a visual-only main Splitter dropdown placeholder.

**Edit summary.**

- Replaced the Options Gain Compensation control with a custom text dropdown.
- Replaced the static main Splitter text with a visual-only `LTP 1` dropdown.
- Restricted knob double-click reset to the knob circle, not the value box.
- Made percent value boxes display/edit whole numbers and added an edit caret.

**Verification.**

- `make native-host-test` passes (`clap-validator` absent, skipped).
- `make install-clap-user` installs and re-signs
  `~/.clap/nilamp.clap`; `native/bin/test_clap_load ~/.clap/nilamp.clap`
  exits `0`.

### Session: CLAP editor knob interaction and Options render hardening

**Context.** Fixed follow-up GUI issues from REAPER testing: percent knob bubble
placement, double-click reset behavior, Options screen crash, and explicit gain
default checks.

**Edit summary.**

- Moved percent knob bubble indicators onto the 0% radius.
- Added Pugl-side double-click detection and visual-noon knob reset.
- Increased the Nuklear/Sokol render vertex budget for the denser Options page.
- Added CLAP smoke assertions for input/output gain `default_value == 0 dB`.

**Verification.**

- `make native-host-test` passes (`clap-validator` absent, skipped).
- `make install-clap-user` installs and re-signs
  `~/.clap/nilamp.clap`; `native/bin/test_clap_load ~/.clap/nilamp.clap`
  exits `0`.

### Session: compact CLAP editor polish against JSFX screenshots

**Context.** Polished the CLAP editor layout against the JSFX main/options
screenshots after the first options implementation showed clipped value boxes,
missing footer labels, and over-wide panels.

**Edit summary.**

- Switched the editor design grid to a compact JSFX-like `500x340` footprint.
- Restored panel footer captions and tightened main/options panel geometry.
- Replaced Nuklear edit widgets with centered custom numeric value boxes.
- Split gain/percent knob drawing modes: gain zero points now sit at noon,
  gain knobs omit bubble highlights, and percent knobs keep the JSFX sweep.
- Added a slightly larger nilamp-owned Nuklear default font atlas.
- Kept sokol_nuklear's internal default atlas alive; the vendored shutdown path
  aborts if `snk_setup` is called with `no_default_font`.
- Made the larger font best-effort so GUI realization falls back to the
  internal Nuklear font instead of failing the editor.

**Verification.**

- `make native-host-test` passes (`clap-validator` absent, skipped).
- `make install-clap-user` installs and re-signs
  `~/.clap/nilamp.clap`; `native/bin/test_clap_load ~/.clap/nilamp.clap`
  exits `0`.

### Session: KDL-backed CLAP options controls and two-screen editor

**Context.** Added the JSFX-style options page and promoted the amp option
settings into automatable CLAP parameters sourced from the KDL amp spec.

**Edit summary.**

- Added KDL-declared control specs for main and options controls, generated
  into native model data and exposed through the DSP API.
- Appended CLAP params for output gain, tone stack, speaker resonance,
  speaker inductor, and gain compensation while preserving existing param IDs.
- Reworked the editor to a main/options screen split, with output gain on the
  main screen and sag moved to options.
- Wired tone stack, speaker resonance, speaker inductor, output gain, and gain
  compensation into the native DSP path with defaults matching Keller JSFX.
- Added `make install-clap-user`, which installs to `~/.clap` by default and
  re-signs the copied dylib on macOS. A plain `cp` can leave a stale
  linker-generated ad-hoc signature and get killed by AMFI on `dlopen`.

**Verification.**

- `make native` passes.
- `make native-test` passes.
- `make native-host-test` passes (`clap-validator` absent, skipped).
- `make install-clap-user` installs and re-signs
  `~/.clap/nilamp.clap`; `native/bin/test_clap_load ~/.clap/nilamp.clap`
  exits `0`.
- `make native-jsfx-test` passes:
  - sine ABX residual `-40.8 dB`, correlation `0.999919`;
  - tap/public guard residual `-48.5 dB`, correlation `0.999986`;
  - low-input regression: zero / `1e-6` / `1e-4` cases all pass.

### Session: amp-panel editor skin

**Context.** Replaced the temporary vertical slider editor with an amp-panel
layout inspired by the HK Audio screenshot, while keeping the current six CLAP
parameters and DSP behavior unchanged.

**Edit summary.**

- `native/src/nilamp_gui.c`
  - Added a custom Nuklear-drawn rotary knob control using canvas primitives
    and vertical drag.
  - Reworked the editor into dark blue/gold framed modules:
    `Input` gain, center `nilamp` model label, `Power` sag, `Pre Amp` volume /
    bass / mid / treble, and static tube/cab context labels.
  - Preserved the existing GUI message/callback path for parameter changes.

**Verification.**

- `make native` passes.
- `make native-test` passes.
- `make native-host-test` passes (`clap-validator` absent, skipped).
- `make native-jsfx-test` passes:
  - sine ABX residual `-40.8 dB`, correlation `0.999919`;
  - tap/public guard residual `-48.5 dB`, correlation `0.999986`;
  - low-input regression: zero / `1e-6` / `1e-4` cases all pass.

### Session: macOS Cocoa CLAP editor enabled

**Context.** Continued from the macOS native build session. The plugin could
build and process on macOS, but intentionally omitted `CLAP_EXT_GUI` because
the vendored Pugl source drop only had X11 backend files.

**Edit summary.**

- Vendored Pugl Cocoa/OpenGL backend files into `third_party/pugl/src/`.
- `Makefile`
  - Enables `NILAMP_ENABLE_CLAP_GUI=1` on both Linux and Darwin.
  - Keeps Linux GUI objects on Pugl X11/X11-GL.
  - Adds Darwin GUI objects for Pugl Cocoa/Cocoa-GL and links Cocoa,
    CoreVideo, and OpenGL frameworks.
  - Adds `OBJC` and Objective-C vendor flags for the Pugl `.m` files.
- `native/src/nilamp_gui.{c,h}`
  - Replaced the X11-specific parent setter with a platform-neutral
    `NilampGuiApi` / `NilampGuiParent` boundary.
  - Keeps the shared Nuklear/Sokol editor implementation unchanged.
- `native/src/nilamp_clap.c`
  - Maps CLAP GUI support to the platform API:
    `CLAP_WINDOW_API_X11` on Linux, `CLAP_WINDOW_API_COCOA` on macOS, and a
    reserved Win32 path for later.
- `native/tests/test_clap_load.c`
  - Expects the platform-native GUI API when GUI support is enabled.

**Verification.**

- `make clean-native && make native` passes on macOS.
- `make native-test` passes on macOS.
- `make native-host-test` passes on macOS (`clap-validator` absent, skipped).
- `make native-jsfx-test` passes on macOS:
  - sine ABX residual `-40.8 dB`, correlation `0.999919`;
  - tap/public guard residual `-48.5 dB`, correlation `0.999986`;
  - low-input regression: zero / `1e-6` / `1e-4` cases all pass.

**Next work.**

1. Install `native/bin/nilamp.clap` into
   `~/Library/Audio/Plug-Ins/CLAP/nilamp.clap`.
2. In REAPER on macOS, rescan CLAP plugins and manually verify:
   editor open/close, parameter moves, host automation reflection, and
   save/reload state with the editor present.
3. If Cocoa/OpenGL works in REAPER, preserve this path and defer Metal.
   If it fails because of host/OpenGL behavior, investigate Sokol Metal as the
   macOS renderer backend behind the same GUI boundary.

### Session: macOS native build and dev-chain enablement

**Context.** Portability work to get the current native toolchain running on
macOS without forking the DSP path.

**Edit summary.**

- `Makefile`
  - Defaulted `YSFX_ROOT` to `~/src/ysfx` instead of the old Linux-only path.
  - Added platform-aware linker flags for CLAP builds (`-dynamiclib` on
    macOS, `-shared` on Linux) and stopped linking `-ldl` on Darwin.
  - Made CLAP GUI support conditional so non-Linux builds skip the X11/Pugl
    objects and compile the plugin without the editor extension.
  - Added `CMAKE` discovery for Homebrew installs and `setup-python` for a
    local `.venv` with NumPy/SciPy.
  - Switched Python-driven targets to prefer `./.venv/bin/python3`.
- `native/src/nilamp_clap.c`
  - Guarded `CLAP_EXT_GUI` / timer extension exposure behind
    `NILAMP_ENABLE_CLAP_GUI`.
- `native/tests/test_clap_load.c`
  - Made GUI-extension expectations conditional on the build configuration.
- `README.md`, `AGENTS.md`
  - Documented the macOS path, `~/src/ysfx`, `.venv` bootstrap, and the
    current Linux-only GUI limitation.

**Verification.**

- `make native` passes on macOS.
- `make native-test` passes on macOS.
- `make native-host-test` passes on macOS (`clap-validator` absent, skipped).
- `make setup-python` passes after creating `.venv` and installing
  `numpy 2.4.4` and `scipy 1.17.1`.
- `make native-jsfx-test` passes on macOS:
  - sine ABX residual `-40.8 dB`, correlation `0.999919`;
  - tap/public guard residual `-48.5 dB`, correlation `0.999986`;
  - low-input regression: zero / `1e-6` / `1e-4` cases all pass.

### Session: low-input static fixed in ADNL startup/small-step path

**Context.** Continued from the REAPER static root-cause session below. The
low-input regression harness was already present and reproduced the issue:
exact silence stayed quiet, but tiny non-zero inputs could excite the native
engine into audible hash while Keller's JSFX reference stayed bounded.

**Root cause.** The native ADNL state was zero-initialized even though Keller's
JSFX seeds the ADAA history at table x=0:
`x0 = 0`, `y0 = y(0)`, `z0 = z(0)` in
`vendor/keller-jsfx/Libs/HK_LIB_ADNL.jsfx-inc`. The C table antiderivative has
a non-zero integration constant, so `(z(x) - prev_z) / (x - prev_x)` produced
a huge first non-zero quotient when `prev_z` started at 0. After seeding
`z(0)`, float table precision still made tiny near-zero antiderivative
differences ill-conditioned, so the native ADNL now evaluates its history and
polynomial in double precision and uses the stable direct-value average for
absolute steps below `0.001`. The generated table's tiny numerical `y(0)`
residue is clamped to the ideal zero so exact silence cannot self-excite.

**Edit summary.**

- `native/src/nilamp_dsp.c`
  - `Adnl` history changed to double precision.
  - Engine creation now routes through `nilamp_engine_reset()`.
  - Reset seeds all TWD DLX II ADNL blocks at x=0 with `z(0)` from the table
    and ideal `y(0)=0`.
  - ADNL processing uses double evaluation and falls back to the average
    form for tiny absolute steps.
  - Test-only ADNL/tube/power-pair helpers use the same seed.
- `tools/keller_oracle.py`
  - Oracle ADNL startup and small-step behavior now mirrors native.
- `tests/fixtures/*.f32`
  - Regenerated with `python3 tools/gen_fixtures.py`.

**Verification.**

- `make native-test` passes.
- `make native-jsfx-test` passes:
  - sine ABX residual `-40.8 dB`, correlation `0.999919`;
  - tap/public guard residual `-48.5 dB`, correlation `0.999986`;
  - all diagnostic taps within threshold;
  - low-input regression passes with zero peak `0.000e+00`, 1e-6 RMS noise
    native peak `4.369e-06`, and 1e-4 RMS noise native/JSFX peak ratio `0.11x`.
- `python3 tools/abx_compare.py --preset sweep --rms-threshold-db -11.2`
  passes: residual `-21.6 dB`, correlation `0.990743`.
- Installed the rebuilt plugin to `~/.clap/nilamp.clap`.

**Next work.**

1. Confirm the original REAPER live-input static is gone with the installed
   `~/.clap/nilamp.clap`.
2. Consider making `native-low-input-test` part of `native-test` if the
   optional JSFX comparison remains skipped by default.

### Session: REAPER static root-caused to low-input DSP instability

**Context.** Continuing from the loaded-CLAP diagnostic session below. The
in-process driver had exonerated the CLAP wrapper for every buffer-shape
variant we could think of, so we instrumented `nilamp_clap.c` with an
env-gated stdio logger (`NILAMP_DEBUG_LOG=<path>`, captures the first N
`process()` calls' frames / port shape / channel pointer aliasing /
`constant_mask` / first input samples / post-process L/R peaks / first+last
output samples) and asked the user to capture REAPER traces under three
conditions:

1. Live mono guitar track, strings touched before launch.
2. Live mono guitar track, hands off the strings.
3. Track input fully muted in REAPER.

Logger raised `max_calls` to 4096 (~5 s @ 64-frame blocks) for the longer
captures.

**Findings from REAPER traces.**

- All REAPER blocks: `frames=64 in_ports=1 out_ports=1`, both input pointers
  distinct (`in0 != in1`), `constant_mask=0`, single shape throughout. No
  buffer aliasing or shape variation.
- Output `peakL == peakR` bit-exactly in all 3989 captured blocks. The
  stereo path is fine.
- Run #1 (strings touched): peak 0.366 on block 0, smooth decay matching
  offline `nilamp_render` of a mid-stream sine bit-for-bit. Normal engine
  startup transient on a non-silent input. Not the static.
- Run #2 (hands off strings): input first-sample noise floor ~1e-4. Output
  median peak 0.0127, max 0.354, 54 % of blocks above 0.01 for the entire
  5 s capture. Continuous hash audible as "static."
- Run #3 (track input muted): silent. Confirms the plugin requires a
  non-zero input to misbehave.

**Offline reproduction and root cause.**

Reproduced bit-for-bit offline with `nilamp_render` on synthetic gaussian
noise inputs at varying amplitudes:

| Input peak  | Native peak | JSFX peak | Status                             |
| ----------- | ----------- | --------- | ---------------------------------- |
| 0 (exact)   | 4.5e-14     | 5.8e-13   | both stable                        |
| 1e-8 noise  | 2.6e+17     | 1.3e-5    | native explodes within 3 samples   |
| 1e-6 noise  | 3.2e+5      | 4.3e-5    | native explodes within 5 samples   |
| 1e-4 noise  | 0.33        | 4.3e-3    | native ~80x louder than reference  |
| 1e-2 noise  | 0.37        | 0.34      | comparable                         |
| ABX sine 0.15 | passes    | passes    | (ABX gate amplitude; no regression)|

The native engine is numerically unstable for input magnitudes between
roughly 1e-9 and 1e-3. Above ~1e-2 it converges with the JSFX reference.
At exact zero the per-sample feedback paths stay at zero. This is why:

- The offline ABX gate at amp=0.08-0.15 passes.
- Hosts feeding silence (or our in-process driver feeding silence) see no
  problem.
- REAPER feeding ~1e-4 input noise produces audible hash even with hands
  off the strings.
- Once the user actually plays (input >= 1e-2), the native and JSFX
  outputs become similar in level and the static is masked by signal.

Both `nilamp_render` and the CLAP wrapper call
`nilamp_cpu_enable_realtime_float_mode` (DAZ + FTZ on), so this is not
a denormal-flushing toggle. Most likely candidates:

1. A self-driving feedback path (PSS or tube DC working point) whose
   linearization around zero state has spectral radius > 1, so any tiny
   non-zero excitation grows until a saturating element clamps it.
2. ADNL extrapolation outside the table support producing astronomical
   currents on the first non-zero step.
3. Numerical NR convergence path near zero choosing a wrong root.

**Edit summary.**

- Reverted the env-gated logger from `native/src/nilamp_clap.c`. The
  rebuilt `~/.clap/nilamp.clap` is back to `f58f25b6...`. No code shipped
  from this diagnostic.
- This log entry added to `docs/next-session.md`.

**Next work.**

Highest priority - lock in the regression as a test before fixing:

1. Add a low-input ABX/regression test that fails on the current native
   engine. Suggested cases:
   - Pure zero in for >= 1 s; assert native output peak < 1e-10.
   - Gaussian noise at 1e-4 RMS for >= 1 s; assert native output peak <
     10x JSFX output peak.
   - Gaussian noise at 1e-6 RMS for >= 0.5 s; assert native output peak
     < 1e-3 (no overflow).
   Decide whether to wire into `make native-test` (cheap, no ysfx) or
   gate behind `make native-jsfx-test` (uses the existing harness).

Then DSP investigation:

2. Add per-stage probe taps (PSS state, tube currents, NR iteration count,
   filter feedback states) at the engine boundary. Render the 1e-6 case
   and find which stage produces the first |x| > 1.0 sample.
3. Compare the same per-stage taps with the JSFX harness through ysfx to
   pin the divergent block.
4. Read Keller's JSFX reference for that block under `vendor/keller-jsfx/`
   and `tools/keller_oracle.py`; check whether the native port has a
   bias / state-reset / damping difference that only manifests at low
   amplitude.
5. Fix the smallest possible thing in `native/src/nilamp_dsp.c` (or the
   model data) to restore parity. Re-run the new low-input tests and the
   existing ABX gate. Both must pass.
6. Optionally bump the public ABX gate to also exercise low amplitudes
   so this category of regression is caught in CI.

**Relevant files.**

- `native/src/nilamp_dsp.c` - hot zone for the actual fix, especially the
  PSS/tube ordering loop around line 689 and `nilamp_engine_reset` line
  543.
- `native/src/nilamp_render.c` - offline renderer used for repro.
- `tools/abx_compare.py` - existing public gate (sine 0.15, sweep 0.08).
- `tools/keller_oracle.py` - numerical reference for JSFX behaviour.
- `native/build/jsfx/Effects/nilamp_abx/twd_dlx_ii_harness.jsfx` - JSFX
  reference (regenerate via `python3 -m tools.jsfx_render.stage_jsfx`).
- `native/src/render_loaded_clap.c` + `tools/clap_validate/render_loaded_clap.py`
  - in-process driver from the previous session; useful as a CLAP-side
  bit-exactness sanity check after any DSP fix.

**Critical context.**

- The two prior fixes (`ff0b8a4` "Fold mono-on-stereo" and `1229f14`
  "in-process diagnostic") remain valid; they did not address this bug
  but they are not wrong.
- The CLAP wrapper has been thoroughly cleared. Do not chase wrapper
  bugs further until the DSP low-input issue is fixed.
- Branch `main` is 5 commits ahead of `origin/main`; nothing from this
  diagnostic session is committed.

### Session: in-process loaded-CLAP diagnostic (REAPER static still present)

**Context.** User reports the REAPER static is NOT fixed by the prior
`single-engine fold` commit (`ff0b8a4`). Built an in-process diagnostic that
`dlopen`s `native/bin/nilamp.clap` and renders mono content through it without
involving REAPER, to isolate the plugin from the host.

**Edit summary.**

- New `native/src/render_loaded_clap.c`: minimal CLAP host that `dlopen`s the
  plugin, activates at SR 48000 / block 512, sets default params via params
  flush, and renders a mono float32 WAV through the stereo input port in two
  presentation modes:
  - `shared`: both `data32[0]` and `data32[1]` point at the same buffer
    (matches the REAPER mono-on-stereo case the previous fix targeted).
  - `distinct`: two separate buffers holding identical samples (defeats
    pointer-equality detection; what real hosts may actually do).
- New `tools/clap_validate/render_loaded_clap.py`: synthesizes a mono
  sweep+chord input, runs `nilamp_render` for the offline reference, runs the
  C driver in both modes, then reports peak/RMS/residual-dB/correlation and
  the >8 kHz residual-energy fraction per channel, and asserts L==R
  bit-exactness for `shared` mode.
- `Makefile`: added `make native-loaded-clap-diagnose` (manual; not in `all`
  or `native-test`).

**Verification (key result).**

```
=== mode=shared block=512 ===
  L==R bit-exact: True  max|L-R|=0.000e+00
  L: residual=-280.62 dB corr=1.000000 hi(>8k)=-377.26 dB
  R: residual=-280.62 dB corr=1.000000 hi(>8k)=-377.26 dB

=== mode=distinct block=512 ===
  L==R bit-exact: True  max|L-R|=0.000e+00
  L: residual=-280.62 dB corr=1.000000 hi(>8k)=-377.26 dB
  R: residual=-280.62 dB corr=1.000000 hi(>8k)=-377.26 dB
```

The plugin is bit-exact match to the offline reference in both modes. The two
independent engines, fed identical samples from a clean reset state, produce
bit-exact identical output - so engine indeterminism / dual-engine drift is
NOT the root cause of the static at default params. The previous
`single-engine fold` is still correct as a defensive optimization but did not
address the actual REAPER bug.

**Next work.**

The static the user hears in REAPER is therefore caused by something the
in-process driver does not yet reproduce. Candidate triggers, in priority
order:

1. Variable / non-power-of-two block sizes (REAPER often dispatches odd
   tail-blocks). Extend the diagnostic to randomize block sizes between
   `process()` calls.
2. Non-zero `steady_time` and `transport` info (we currently pass
   `transport=NULL` and `steady_time=pos`).
3. Sample-rate mismatches: try 44100, 88200, 96000.
4. `audio_inputs_count == 1` with a mono input bus despite the plugin
   declaring stereo - test what happens if a host passes a 1-channel input
   buffer rather than stereo. If the wrapper's `input_channels` branch reads
   `data32[1]` it will dereference an undefined pointer.
5. State restore between activate/deactivate cycles (a host scan/load may
   call set-state with stale data and then process before reset).
6. Initial-sample weirdness: feed a buffer whose first samples are exactly
   `1e-30f` (denormal) or large negative values.
7. If 1-6 still don't reproduce: build a focused REAPER repro project with
   only a mono guitar audio item, peakcache disabled, render to disk via
   `-renderproject`, and diff that render against the in-process driver
   render of the same WAV item.

**Relevant files.**

- `native/src/render_loaded_clap.c` (new) - the C driver.
- `tools/clap_validate/render_loaded_clap.py` (new) - the Python harness.
- `Makefile` - new `native-loaded-clap-diagnose` target.
- `native/src/nilamp_clap.c` - `nilamp_process_segment` and the
  `audio_inputs_count == 1` branch are the next inspection sites.

### Session: REAPER static (mono on stereo) -> SINGLE-ENGINE FOLD

**Edit summary.**

- Root-caused the residual REAPER static to two independent nonlinear amp
  engines processing nominally-identical mono content on a stereo bus. Tiny
  per-channel divergence in the high-gain tube/PSS state produced audible
  decorrelation between L and R that read as static.
- Added a mono-equivalent input detector in `nilamp_process_segment`: when both
  stereo input channels point at the same buffer (REAPER's mono-track default
  on a stereo bus) or both are constant with identical first samples, the
  plugin now runs a single engine on channel 0 and `memcpy`s the result to
  channel 1. The dual-engine path remains for genuinely stereo input.
- The pre-existing in-place mono->stereo branch (`input_channels == 1`) and
  the per-channel `nilamp_sanitize_host_sample` clamp are unchanged.
- Extended `tests/test_clap_load.c` with a mono-on-stereo case that asserts
  bit-exact L/R equality and matches the offline `NilampEngine` reference,
  catching any future regression that re-introduces the dual-engine drift.

**Verification.**

- `make native-test` passes (includes the new bit-exact mono-on-stereo
  assertion).
- `make native-host-test` passes; `clap-validator` reports 21 tests run, 16
  passed, 0 failed, 5 skipped, 0 warnings.
- `make native-jsfx-test` passes; sine ABX residual `-31.4 dB`,
  correlation `0.998804`, best native-to-JSFX gain unchanged.
- Installed `native/bin/nilamp.clap` to `~/.clap/nilamp.clap`; both hashes are
  `f58f25b6048e24a0bd66ba93f221048e07e8299f0ed0d4af209a4b3b5c197ccb`.

**Next work.**

1. Manually retest in REAPER on a mono guitar track at default settings to
   confirm the static is gone end-to-end.
2. If any host turns out to feed two distinct buffers with identical content
   (defeats pointer equality), add a cheap first-N-sample probe before
   dropping into the dual-engine path. Defer until a real host shows that.
3. Coordinate per-channel NaN resets across both engines so a single-channel
   reset cannot desync L vs R on truly stereo input. Defer.
4. Consider advertising `CLAP_PORT_MONO` for v1 since a guitar amp is
   fundamentally mono. Larger surface change; defer unless mono-on-stereo
   continues to surface host-specific edge cases.

### Session: REAPER static report -> CLAP OUTPUT SAFETY LIMIT

**Edit summary.**

- Added a CLAP-only host output sanitizer after native DSP processing. Hosted
  samples sent to REAPER are now finite, denormal-free, and bounded to
  `[-1.0, +1.0]`; the raw DSP engine, offline renderer, tap renderer, and JSFX
  parity path remain unchanged.
- Extended the loaded-CLAP smoke test with a REAPER-like stress render
  (`gain=6`, `volume=80`, `bass=30`, `mid=60`, `treble=70`, `sag=100`) and a
  peak assertion so the old `~1.1e5` hosted-output failure cannot pass.
- Updated the REAPER validation script with `--max-peak` so real host renders
  fail on runaway level instead of only checking for silence.

**Verification.**

- `make native-test` passes.
- `make native-host-test` passes; `clap-validator` reports 21 tests run, 16
  passed, 0 failed, 5 skipped, 0 warnings.
- `make native-jsfx-test` passes; sine ABX remains residual `-31.4 dB`,
  correlation `0.998804`, best native-to-JSFX gain `+0.00 dB`; tap/public guard
  remains residual `-48.5 dB`, correlation `0.999986`.
- `python3 -m py_compile tools/clap_validate/validate_reaper_clap.py` passes.
- Installed `native/bin/nilamp.clap` to `~/.clap/nilamp.clap`; both hashes are
  `192205cc639dcd4ef2f89d64f837880c1c671f5a46af21d7d40984215251ce76`.
- `make native-reaper-host-test` did not reach the REAPER Lua driver in this
  run: `/tmp/nilamp_clap_validate.log` was never created and REAPER timed out.
  Treat this as a harness launch issue, not an audio peak result.

**Next work.**

1. Retest the installed CLAP manually in REAPER.
2. If static remains with the bounded host output, capture whether REAPER is
   loading another CLAP path or whether the noise is already present at plugin
   input/track routing.
3. Make the REAPER harness more reliable before depending on it as a required
   CI-style gate.

### Session: REAPER static report -> CLAP GUI TIMER AND RENDER GUARD

**Edit summary.**

- Replaced the editor's tight `request_callback()` repaint loop with CLAP
  timer-support pumping at roughly 30 Hz when the GUI is visible.
- Added dirty-gated parameter application so the audio thread no longer reapplies
  unchanged GUI/state params every process block; automation still applies at
  its event boundary.
- Extended the CLAP loader smoke test to compare loaded-plugin audio against
  direct `NilampEngine` output for stereo, stereo in-place, mono-to-stereo,
  constant input, and variable block sizes.

**Verification.**

- `make native-test` passes.
- `make native-host-test` passes; `clap-validator` reports 21 tests run, 16
  passed, 0 failed, 5 skipped, 0 warnings.
- `make native-jsfx-test` passes; sine ABX remains residual `-31.4 dB`,
  correlation `0.998804`, best native-to-JSFX gain `+0.00 dB`; tap/public guard
  remains residual `-48.5 dB`, correlation `0.999986`.

**Next work.**

1. Retest the installed CLAP in REAPER with the editor closed and open.
2. If static remains, capture whether it depends on editor visibility, sample
   rate/block size, or mono/stereo track routing.

### Session: CLAP editor stack -> PUGL SOKOL NUKLEAR SHELL

**Edit summary.**

- Added an embedded X11 CLAP GUI extension around the existing native plugin.
- Introduced a separate C GUI module using Pugl for host embedding/event
  dispatch, `sokol_gfx` for GPU rendering, and Nuklear via `sokol_nuklear.h`
  for the first parameter editor.
- Kept GUI-to-DSP sync on explicit CLAP parameter state and host param flush;
  no Pugl, Sokol, or Nuklear calls enter `process()`.
- Recorded the GUI direction in AGENTS and GUI notes: C-only runtime,
  X11/XWayland v1, no C++, no Dear ImGui/cimgui, no NanoVG, no `sokol_app.h`,
  and no copied LSP renderer code.

**Verification.**

- `make native` passes.
- `make native-test` passes.
- `make native-host-test` passes; `clap-validator` reports 21 tests run, 16
  passed, 0 failed, 5 skipped, 0 warnings.
- `make native-jsfx-test` passes; sine ABX remains residual `-31.4 dB`,
  correlation `0.998804`, best native-to-JSFX gain `+0.00 dB`, and the
  tap/public guard remains residual `-48.5 dB`, correlation `0.999986`.
- `ldd native/bin/nilamp.clap` shows X11/OpenGL GUI dependencies plus `libm`
  and `libc`; no Lua, LuaJIT, KDL, Rust, or C++ runtime dependency is linked
  directly.

**Next work.**

1. Manually host-test the editor in REAPER or another CLAP host after the
   native smoke checks pass.
2. Add a focused GUI interaction smoke only when there is a practical embedded
   X11 host harness.
3. Keep native Wayland support as a later explicit feature; v1 is X11 through
   XWayland.

### Session: KDL amp specs -> GENERATED TWD MODEL DATA

**Edit summary.**

- Replaced the interrupted Lua amp-model direction with KDL 2 model data.
- Added `models/amps/keller_twd_dlx_ii.kdl` as the declarative source for the
  current baseline amp model.
- Added `tools/gen_amp_models.py`, a strict stdlib-only KDL subset parser and
  C generator for native amp model data.
- Added generated `native/generated/nilamp_models.inc` and wired Make to
  regenerate it before native DSP compilation.
- Updated README, AGENTS, and DSP notes to make KDL build-time-only and keep
  Lua out of renderer/plugin/audio callback paths.

**Verification.**

- `make native-test` passes.
- `make native` passes.
- `make native-jsfx-test` passes.
- `python3 -m py_compile tools/gen_amp_models.py` passes.
- local `kdl models/amps/keller_twd_dlx_ii.kdl` exits successfully.
- `ldd native/bin/nilamp.clap` shows only `libm`, `libc`, and the dynamic
  loader; no KDL, Lua, or LuaJIT runtime dependency.
- ysfx sine ABX remains at residual `-31.4 dB`, correlation `0.998804`,
  best native-to-JSFX gain `+0.00 dB`.
- ysfx sine tap diagnostics still pass the public guard: residual `-48.5 dB`,
  correlation `0.999986`.

**Next work.**

1. Add the next amp by extending KDL model data and only adding C topology code
   when a topology cannot share an existing runner.
2. Keep KDL as declarative method/table/constant selection, not an executable
   graph language.

### Session: Multi-amp model boundary -> TWD DLX II BASELINE PRESERVED

**Edit summary.**

- Introduced native amp model identity with `Keller TWD DLX II` as the default
  model.
- Wrapped the engine around a model registry plus model-owned state, leaving
  the current TWD DLX II process order and tap diagnostics intact.
- Started moving repeated TWD constants into explicit model data so future amp
  topologies can share Keller blocks without duplicating formulas.
- Updated agent guidance to prefer DRY, data-oriented amp expansion while
  avoiding opaque runtime graphs that would hide feedback/sample ordering.

**Verification.**

- `make native-test` passes.
- `make native-jsfx-test` passes.
- ysfx sine ABX remains at residual `-31.4 dB`, correlation `0.998804`,
  best native-to-JSFX gain `+0.00 dB`.
- ysfx sine tap diagnostics still pass the public guard: residual `-48.5 dB`,
  correlation `0.999986`.

**Next work.**

1. If verification stays clean, split reusable blocks into separate source
   files only when a second amp needs them; avoid churn before that.
2. Add the next amp by adding model data and a topology-specific process
   function, not by changing the TWD DLX II baseline.
3. Move per-amp source impedance/damping and IR/line-output choices into model
   descriptors when the first non-TWD model is introduced.

### Session: ysfx replaces REAPER parity harness -> HEADLESS JSFX PASS

**Edit summary.**

- Added `native/bin/ysfx_render`, a Make-built C runner around the maintained
  ysfx checkout at `/home/niltempus/src/ysfx`.
- Moved staged Keller harnesses to `native/build/jsfx/Effects/nilamp_abx/`;
  ABX and tap diagnostics now use `tools.jsfx_render.render_ysfx` by default.
- `make native-host-test` is now REAPER-free: native CLAP loader plus optional
  `clap-validator`. The old REAPER CLAP smoke target is
  `make native-reaper-host-test`.
- The ysfx wrapper applies `0.5` input gain before JSFX processing to preserve
  the prior REAPER mono-harness calibration that native currently matches.

**Verification.**

- `make native/bin/ysfx_render` passes.
- `python3 -m py_compile tools/jsfx_render/render_ysfx.py tools/jsfx_render/render_jsfx.py tools/jsfx_render/stage_jsfx.py tools/abx_compare.py tools/compare_taps.py tools/clap_validate/validate_clap.py` passes.
- `python3 -m tools.jsfx_render.stage_jsfx` stages to
  `native/build/jsfx/Effects/nilamp_abx`.
- ysfx sine ABX passes: residual `-31.4 dB`, correlation `0.998804`,
  best native-to-JSFX gain `+0.00 dB`.
- ysfx sweep ABX passes: residual `-23.9 dB`, correlation `0.980020`,
  best native-to-JSFX gain `+0.29 dB`.
- ysfx sine tap diagnostics run headlessly; tap/public guard is `-48.5 dB`,
  correlation `0.999986`.
- `make native-host-test` passes: native tests, native CLAP loader, and
  `clap-validator` 21-test suite all pass (`16 passed`, `5 skipped`).
- `make native-jsfx-test` passes.

**Next work.**

1. Re-baseline tap diagnosis under ysfx. The old REAPER 23-tap T5/DIA numbers
   should not be mixed with the new ysfx-hosted numbers.
2. Decide whether to keep native's REAPER-era `0.5 * sqrt(1.2)` input
   calibration long term or move to true ysfx/Keller mono calibration in a
   separate DSP-affecting change.
3. Continue parity work from the ysfx tap residuals, not from legacy REAPER
   render artifacts.

### Session: Power-tube current taps -> T5/DIA LOOP SUSPECT

**Edit summary.**

- Expanded native and JSFX selected-tap diagnostics from 16 to 23 channels.
  The original first 16 taps are unchanged; appended taps are:
  `t4_advk_in, t5_advk_in, t4_dia, t5_dia, t4_advk_out, t5_advk_out,
  dia1_next`.
- Regenerated native tap fixtures and extended native tap tests to pin the new
  channel order.
- No DSP correction was made; this pass is diagnostic-only.

**Verification.**

- `python3 -m py_compile tools/compare_taps.py tools/jsfx_render/stage_jsfx.py tools/gen_fixtures.py` passes.
- `python3 tools/gen_fixtures.py` regenerated tap fixtures.
- `make native-test` passes.
- `make native` passes.
- `python3 -m tools.jsfx_render.stage_jsfx` passes.
- `make native-host-test` passes: `clap-validator` reports 21 tests run,
  16 passed, 0 failed, 5 skipped; REAPER render remains finite with peak
  `7.398350e+14`.
- Tap/public guard passes for sine and sweep.
- Sine 23-tap comparison: final `v_out` unchanged at `-20.9 dB`; `t4_dia`
  best native-to-JSFX gain is `+2.03 dB`, `t5_dia` is `+7.27 dB`, and
  `dia1_next` is badly mismatched (`+9.00 dB`, correlation `0.605951`).
- Sweep 23-tap comparison: final `v_out` unchanged at `-19.7 dB`; `t4_dia`
  has poor/negative correlation, `t5_dia` gain is `+5.84 dB`, and
  `dia1_next` remains badly mismatched (`+9.90 dB`, correlation `0.591727`).

**Next work.**

1. Focus on the T4/T5 `dia` path and the summed `dia1_next` feedback into PSS;
   the mismatch is not merely `v_out = -rl * dia + ksva * dvs` scaling.
2. Verify whether JSFX current taps are affected by tap timing or field-access
   order before changing DSP constants. A good next probe is a selected tap
   immediately after `dia1 = t4.dia + t5.dia` at the top of the following
   sample, compared against native `prev_dia1`.
3. Continue running REAPER harnesses serially. For headless CI, use a virtual
   display wrapper such as `xvfb-run -a`; REAPER is still a GUI process, not a
   true headless engine.

### Session: Native performance hygiene -> ADNL NOTES CORRECTED

**Edit summary.**

- Added an x86-gated FTZ/DAZ helper and enabled it in the CLAP processing path
  and native CLI renderers.
- Added `make native-bench` for quick local timing of the ADNL hot path and
  full-engine sine/silence throughput.
- Updated DSP project notes to clarify that the native ADNL runtime uses
  generated `float` polynomial coefficient tables with a small-delta ADAA
  fallback, not `tanhf()` or linear interpolation.

**Verification.**

- `make native-test` passes.
- `make native-bench` passes locally:
  `adnl_t4_6v6` `3.67 ns/sample`, `engine_sine` `270.40 ns/sample`
  (`77.04x` realtime), `engine_silence` `256.31 ns/sample` (`81.28x`
  realtime).
- `make native-host-test` passes: `clap-validator` reports 21 tests run,
  16 passed, 0 failed, 5 skipped; REAPER render remains finite with peak
  `7.398350e+14`.
- Tap/public guard and 16-channel tap comparisons are unchanged from the
  late-stage tap baseline: sine final `v_out` residual `-20.9 dB`, sweep final
  `v_out` residual `-19.7 dB`.

**Next work.**

1. Use benchmark numbers to decide whether table/cache optimization is worth
   touching. Do not change table format without profiling evidence.
2. Keep Keller parity work focused on the T4/T5 power-pair output mismatch
   found by the 16-channel tap diagnostics.

### Session: Late-stage tap expansion -> POWER PAIR REMAINS FIRST USEFUL SUSPECT

**Edit summary.**

- Expanded the native and JSFX selected-tap diagnostics from 9 to 16 channels.
  The original first nine taps are unchanged; appended taps are:
  `p2_s, p3_s, drive_t5, post_pp, post_peq3, post_hs3, post_hp5`.
- Regenerated native tap fixtures and updated the loose native tap test
  tolerances for the new volt-level post-filter checkpoints.
- No DSP behavior was intentionally changed; this session adds visibility only.

**Verification.**

- `python3 -m py_compile tools/compare_taps.py tools/jsfx_render/stage_jsfx.py tools/gen_fixtures.py` passes.
- `make native-test` passes.
- `make native-host-test` passes: `clap-validator` reports 21 tests run,
  16 passed, 0 failed, 5 skipped; REAPER render is finite with peak
  `7.398350e+14`.
- ABX sine: `-20.9 dB` residual below native peak; correlation `0.997197`;
  best native-to-JSFX gain `1.140195` (`+1.14 dB`).
- ABX sweep: `-19.7 dB` residual below native peak; correlation `0.958873`;
  best native-to-JSFX gain `1.109749` (`+0.90 dB`).
- Sine taps: `drive_t4` and `drive_t5` remain near unity (`-0.53 dB`,
  `-0.44 dB`), while `res5_v`, `res_t5_v`, and `post_pp` jump to about
  `-2.4` to `-2.8 dB` native-to-JSFX best-fit gain.
- Sweep taps show the same useful localization: drive taps stay within about
  `-0.6 dB`, while power-pair and post-power taps sit around `-1.3` to
  `-1.5 dB`.

**Next work.**

1. Focus the next DSP hypothesis on T4/T5 tube output behavior and power-pair
   interaction, not PEQ3/HS3/HP5/LP2; the post filters mostly preserve the
   `post_pp` mismatch.
2. Treat `p2_s`/`p3_s` as untrusted until the JSFX instance-field tap is
   verified; their selected-tap values do not agree with the `dvs2`/`dvs3`
   magnitudes and are not yet source-backed enough for a PSS scaling edit.
3. Continue running REAPER render harnesses serially. A parallel ABX attempt
   collided as expected; the sine result was rerun serially.

### Session: Source-backed input calibration -> BEST GAIN NEAR ZERO

**Edit summary.**

- Added a staged `twd_dlx_ii_tap_select.jsfx` harness that renders one selected
  JSFX tap as mono output through the same public ABX path.
- Updated `tools/compare_taps.py` to render selected taps one at a time and
  verify selected `v_out` against the public JSFX render before reporting
  per-stage metrics.
- Ported Keller's `flt_df2_set_adnl_eq()` into the native tube stages and
  switched the native DF2 helper to Keller's state form.
- Added the source-backed native input calibration
  `0.5 * sqrt(1.2)`: Keller's `sqrt(1.2)` g1 factor plus the REAPER mono JSFX
  feed factor measured by the selected tap harness.
- Regenerated native fixtures and adjusted only the affected loose tap/power
  tolerances.
- Fixed the CLAP wrapper's mono-input/stereo-output in-place processing order
  and added a smoke-test case for that host layout.
- Added a DSP output-boundary guard so non-finite host input or runaway state
  resets the engine and emits silence for the affected frame.

**Verification.**

- `python3 -m py_compile tools/abx_compare.py tools/jsfx_render/render_jsfx.py tools/jsfx_render/stage_jsfx.py tools/compare_taps.py tools/gen_5e3_tables.py tools/gen_fixtures.py tools/keller_oracle.py` passes.
- `make native-test` passes.
- `make native-host-test` passes: `clap-validator` reports 21 tests run,
  16 passed, 0 failed, 5 skipped; REAPER render is finite with peak
  `7.398350e+14`.
- Tap/public guard: selected `v_out` matches public JSFX render at about
  `-135 dB` to `-144 dB` residual, depending on input.
- Tap sine after calibration: early signal taps are now near unity
  (`res1_v` best native-to-JSFX gain `-0.17 dB`, `res3_v` `-0.40 dB`);
  final `v_out` best gain is `+1.14 dB`.
- Tap sweep after calibration: final `v_out` best gain is `+0.90 dB`.
- ABX sine: `-20.9 dB` residual below native peak, threshold `-16.0 dB` -> PASS.
  Correlation `0.997197`; best native-to-JSFX gain `1.140195` (`+1.14 dB`).
- ABX sweep: `-19.7 dB` residual below native peak, threshold `-11.2 dB` -> PASS.
  Correlation `0.958873`; best native-to-JSFX gain `1.109749` (`+0.90 dB`).

**Next work.**

1. Remaining gain error is no longer the broad `-1.88/-2.70 dB` offset; look at
   late power-stage/PSS scaling and phase/shape residuals.
2. Do not run multiple REAPER render harnesses in parallel; they share temp
   driver paths and can collide.

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
- Added `native/src/nilamp_clap.c`, then a no-GUI CLAP audio effect exposing the
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
2. Superseded GUI direction: current GUI work follows Pugl embedded X11,
   `sokol_gfx`, and Nuklear. Do not revive the earlier NanoVG plan.

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
the native C/KDL path.

**Current architecture.**

- C owns realtime DSP and offline rendering.
- KDL owns build-time amp model data.
- Lua is limited to REAPER/non-DSP helper tooling.
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
