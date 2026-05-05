# ABX Harness

Offline numerical-comparison harness pitting the native C renderer
(`native/bin/nilamp_render`) against the canonical Keller JSFX
(`vendor/keller-jsfx/TWD DLX  II.jsfx`) to verify DSP equivalence.

The harness is not a realtime human-listening A/B/X comparator. It is a batch
tool: feed both renderers the same input WAV with the same parameters, then
report numerical metrics on the difference signal.

## Pipeline

```
input.wav --+--> native/bin/nilamp_render ----------> nilamp.wav --+
            |                                                     +--> metrics
            +--> render_jsfx.py -> REAPER -> JSFX -> jsfx.wav ----+
```

The comparator time-aligns outputs, trims the JSFX warm-up window, then reports
peak, RMS, residual RMS, max absolute difference, band deltas, group delay, and
THD-oriented metrics for sine inputs.

## Components

| File | Role |
|---|---|
| `tools/jsfx_render/stage_jsfx.py` | Stages Keller source into REAPER's `Effects/nilamp_abx/` directory and emits the harness-patched amp. |
| `tools/jsfx_render/render_jsfx.lua` | ReaScript driver that renders JSFX output. |
| `tools/jsfx_render/render_jsfx.py` | Python wrapper around the headless JSFX render. |
| `tools/abx_compare.py` | Drives native and JSFX renderers, aligns outputs, computes metrics. |
| `tools/compare_taps.py` | Renders native and JSFX diagnostic taps and reports per-stage residuals. |

## Parameters

The native renderer exposes:

| nilamp param | Range | JSFX slider | Mapping |
|---|---:|---|---|
| `gain` | dB | `gin` | `gin = gain - 12 dB` |
| `volume` | 0..100 % | `vol` | identity |
| `bass` | 0..100 % | `bass` | identity |
| `mid` | 0..100 % | `mid` | identity |
| `treble` | 0..100 % | `treble` | identity |
| `sag` | 0..100 % | none | native-only; ABX default is 100 |

Pinned JSFX sliders:

| Slider | Value | Reason |
|---|---:|---|
| `tube1` | `1` | 12AX7 path |
| `mode` | `0` | CD 5E3 cathodyne |
| `gcomp` | `2` | TWD DLX II default compensation mode |
| `gp_pre` / `gp_post` | `1` / `2` | TWD DLX II speaker-resonance defaults |
| `fp` / `qp` | `38` / `6` | TWD DLX II speaker-resonance defaults |
| `gs_pre` / `gs_post` | `3` / `3` | TWD DLX II speaker-inductor defaults |
| `fm` / `qm` / `fs` / `gout` | `56` / `-6` / `62` / `0` | TWD DLX II defaults |

The REAPER driver forces the project/render sample rate before inserting media
or instantiating JSFX, so Keller's `srate`-derived coefficients initialize at
the same rate used by the native renderer.

## Warm-up Trim

The harness JSFX removes Keller's wall-clock mute, but the first rendered block
can still contain initialization artifacts. `tools/abx_compare.py` discards the
first 100 ms before computing metrics.

## Gates

Public gates:

- sine residual >= `-16 dB`
- sweep residual >= `-11.2 dB`

Use a WAV input directly or generate deterministic inputs on demand:

```bash
make native
python3 tools/abx_compare.py input.wav
python3 tools/abx_compare.py --preset sine --rms-threshold-db -16
python3 tools/abx_compare.py --preset sweep --rms-threshold-db -11.2 --jsfx-timeout 120
```

Generated preset WAVs are written under the selected `--out-dir` and are not
tracked in git.

Stage the JSFX harnesses, including the tap and selected-tap harnesses:

```bash
python3 -m tools.jsfx_render.stage_jsfx
```

Run stage-level tap diagnostics:

```bash
python3 tools/compare_taps.py --preset sine --out-dir /tmp/nilamp_tap_compare --label sine
python3 tools/compare_taps.py --preset sweep --out-dir /tmp/nilamp_tap_compare --label sweep --jsfx-timeout 120
```

Latest native C run:

- sine preset: `-20.9 dB` residual below native peak, threshold `-16.0 dB` -> PASS; correlation `0.997197`, best A->B gain `+1.14 dB`
- sweep preset: `-19.7 dB` residual below native peak, threshold `-11.2 dB` -> PASS; correlation `0.958873`, best A->B gain `+0.90 dB`
