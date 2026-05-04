# ABX Harness

Offline numerical-comparison harness pitting the Rust/Faust port of the 5E3
(`nilamp_render`) against the canonical Keller JSFX
(`vendor/keller-jsfx/TWD DLX  II.jsfx`) to verify DSP equivalence.

The harness is **not** a real-time human-listening A/B/X comparator. It is a
batch tool: feed both renderers the same input WAV with the same parameters,
and report numerical metrics on the difference signal.

## Pipeline

```
input.wav ──┬── nilamp_render ─────────────────► nilamp.wav ─┐
            │                                                 ├─► time-align
            └── render_jsfx.py ─► REAPER ─► JSFX ─► jsfx.wav ─┘   (xcorr, ±5 ms)
                                                                       │
                                                          trim 100 ms warm-up
                                                                       │
                                                                       ▼
                                              metrics: peak, RMS, residual,
                                                       max abs, group delay
```

## Components

| File                                  | Role                                         |
|---------------------------------------|----------------------------------------------|
| `tools/jsfx_render/stage_jsfx.py`     | Stages Keller bundle into REAPER's `Effects/nilamp_abx/` directory and emits the harness-patched amp `twd_dlx_ii_harness.jsfx`. |
| `tools/jsfx_render/render_jsfx.lua`   | ReaScript driver: opens project, inserts WAV, adds JSFX, sets sliders, renders, quits. |
| `tools/jsfx_render/render_jsfx.py`    | Python wrapper: parses sliders from staged JSFX, emits Lua config, spawns headless REAPER, polls for output. |
| `tools/abx_compare.py`                | Top-level comparator: drives both renderers, time-aligns, computes metrics, pass/fail. |

## Why the harness JSFX patch

Keller's amp gates output for ~100 ms after any slider change to mask
transient artifacts:

```eel2
@slider
parameter_update();   // sets is_muted = 1; t_unmute = time_precise() + 0.1

@block
time_precise() > t_unmute ? is_muted = 0;
```

In offline rendering, `time_precise()` (wall-clock) advances at a
render-speed-dependent rate. The un-mute therefore fires at a different
sample boundary on every render, and the resulting nonlinear feedback (PSS,
load-line interaction) amplifies that timing jitter into macroscopic
divergence (max abs diff up to 0.24 across runs of an identical project).

`stage_jsfx.py` writes a second amp `twd_dlx_ii_harness.jsfx` that replaces
the wall-clock check with an unconditional un-mute:

```eel2
is_muted = 0; // harness: removed wall-clock dependency
```

The original `twd_dlx_ii.jsfx` is also staged (verbatim from vendor) for any
manual GUI inspection.

## Why the 100 ms warm-up trim

Even with the wall-clock mute removed, `is_muted = 1` is still set in
`@slider` and only cleared by the next `@block`. The first audio block after
slider initialization is therefore partially muted at a render-speed-dependent
sample boundary (max abs diff ~0.26 in the first 100 ms across runs).

After the first 100 ms the output is reproducible to ~−85 dB RMS / −66 dB max
across runs. `abx_compare.py` discards the first 100 ms unconditionally
before computing metrics. (4800 samples at 48 kHz.)

## Slider mapping (Rust ↔ JSFX)

The Rust plugin exposes 6 normalized parameters; Keller's JSFX exposes 18.
For ABX we map 1:1 in native units and pin the rest to topology-matched
defaults.

| Rust param        | Range          | JSFX slider | Range          | Mapping  |
|-------------------|----------------|-------------|----------------|----------|
| `gain` (Input)    | −12..+12 dB    | `gin`       | −12..+12 dB    | identity |
| `volume`          | 0..100 %       | `vol`       | 0..100 %       | identity |
| `bass`            | 0..100 %       | `bass`      | 0..100 %       | identity |
| `mid`             | 0..100 %       | `mid`       | 0..100 %       | identity |
| `treble`          | 0..100 %       | `treble`    | 0..100 %       | identity |
| `sag`             | 0..100 %       | (none)      |                | Rust held at 100 |

Pinned JSFX sliders (matching Rust DSP topology):

| Slider | Value | Reason                                         |
|--------|-------|------------------------------------------------|
| `tube1`| `1`   | 12AX7 path; matches `t1_12ax7_table` in nilamp.dsp |
| `mode` | `0`   | CD 5E3 cathodyne; matches `tube_cd()` stage 4 in nilamp.dsp |

All other JSFX sliders (`fm`, `qm`, `gp_pre/post`, `fp`, `qp`, `gs_pre/post`,
`fs`, `gout`, `gcomp`) are left at their JSFX defaults, which is the
"factory" voicing that the Rust port targets.

JSFX has no equivalent to Rust's `sag` (its 3-stage PSS is internal and
fixed). For ABX, Rust's `sag` is held at 100 %, which scales `r_pss` to
22 kΩ — matching Keller's hardcoded `c.p3_R` final-stage value (per comment
in `dsp/nilamp.dsp`).

## Determinism summary

| Configuration                      | Across-run residual RMS | Max abs diff |
|------------------------------------|-------------------------|--------------|
| Original JSFX (full output)        | −28 dB                  | 0.26         |
| Harness JSFX (full output)         | −28 dB                  | 0.26         |
| Harness JSFX, first 100 ms trimmed | **−85 dB**              | **1.4e-4**   |

So: **harness JSFX + 100 ms trim** is the operating point.

## Usage

Stage the JSFX bundle (one-time, or rerun if vendor updates):

```sh
python -m tools.jsfx_render.stage_jsfx
```

Build the Rust renderer:

```sh
cargo build --release --bin nilamp_render
```

Run a single comparison:

```sh
python -m tools.abx_compare /path/to/input.wav \
    --gain 0 --volume 50 --bass 50 --mid 50 --treble 50 \
    --label clean --rms-threshold-db -60
```

Output (example, 1 s 440 Hz sine at 0.3 amplitude, clean settings):

```
results [clean]:
  samples:       42981  (sr=48000 Hz)
  align lag:     -219 samples (-4.562 ms)
  peak A / B:    0.2991 / 0.2723
  rms A / B:     0.1862 / 0.2234
  rms residual:  7.1014e-02  (-12.5 dB below peak_a)
  max |A-B|:     1.6952e-01  (-4.9 dB below peak_a)
  verdict: FAIL (threshold -60.0 dB)
```

Interpretation: at clean settings on a 440 Hz sine, the Rust port differs
from the JSFX by −12.5 dB residual RMS and runs ~4.6 ms slower (more group
delay). Investigation deferred — see "Open work" below.

## Open work

- **Bring the Rust port into ABX agreement with the JSFX.** First-pass
  suspects (each warrants its own measurement):
  - **Tone-stack center frequency.** `dsp/nilamp.dsp` line 99 hardcodes
    `flt_sv2_tst(... 500, 0.5, 1, 1)`. JSFX runs the SVF at `fm=56 dBHz` =
    631 Hz with `qm=−6 dB` = 0.5. Frequency mismatch.
  - **Volume law.** `dsp/nilamp.dsp` applies `volume * volume`; JSFX applies
    `vol/100` linearly. At default `volume=50`, Rust attenuates ~6 dB more.
  - **Group delay.** 4.6 ms (≈220 samples) is consistent with one or two
    extra IIR1 filters or different filter ordering.
- **Expand the comparator.** Per-band level deltas (5-band FFT), THD ratio
  at fundamental, spectral centroid, write residual WAV for listening.
- **Build a corpus.** Sine sweep 20 Hz–20 kHz, sine burst, short DI.
- **PSS topology.** Rust collapses Keller's 3-stage PSS chain into a single
  lump (`r_pss = sag * 22000`). For tight ABX agreement under sag-active
  conditions, port the full chain (`p1`, `p2`, `p3` per JSFX lines
  189–191). Tracked under "5E3 v2" in `dsp-project-notes.md`.
