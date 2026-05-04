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

    (More precisely, `iso266(56)` quantizes `10^2.8 ≈ 630.957` to the
    nearest multiple of 10 → 630 Hz.  Verified by tracing
    `HK_LIB_TOOLS.jsfx-inc:26-49`.)
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

## Investigation 2026-05-04: input-gain equalization, DC leak, HP5 fix

This session addressed two of the discrepancies above with measurement.

### Finding 1: JSFX has a +12.79 dB internal input-gain offset

Reading `twd_dlx_ii_harness.jsfx:229`:

```
gin_eff = 10^(0.05 * (p.gin + 12)) * sqrt(1.2)
```

At `p.gin = 0` this is +12.78 dB linear gain into T1.  Faust uses
`db2linear(p.gain)`: at `p.gain = 0` that is unity.  The JSFX runs ~12.79 dB
hotter at the T1 input than the Faust port at any same nominal slider
value.  Even at the JSFX slider minimum `p.gin = -12`, JSFX still applies
+0.79 dB (never reaches unity).

**Fix (harness-side, no DSP change):** `tools/abx_compare.py:Params.jsfx_gin()`
translates the requested Rust `gain_db` to JSFX `p.gin = gain_db − 12.79`,
and raises `ValueError` if the requested gain is outside the equalizable
range `[+0.79, +24.79] dB`.  See constants `JSFX_GIN_OFFSET_DB`,
`EQUALIZABLE_GAIN_MIN_DB`, `EQUALIZABLE_GAIN_MAX_DB`.

The DSP-side fix (rewriting `dsp/nilamp.dsp:gain1` to mirror the JSFX
formula, or rewriting `twd_dlx_ii_harness.jsfx` to drop the offset) is
deferred to Scope-1.

### Finding 2: nilamp had a steady-state DC offset of −0.040 (−28 dBFS)

A diagnostic scan of the rendered baseline output (`gain=+6, defaults`)
revealed:

| signal           | mean (700–800 ms window) | rms      |
|------------------|-------------------------:|---------:|
| nilamp pre-fix   |                  −0.0400 |   0.1918 |
| jsfx             |                  +3.2e-7 |   0.2199 |

The ~20 dB DC bias dominated the per-sample residual and made the
comparator's linear-regime probe (`--input-scale 1e-3`) useless: the
nilamp output peak in linear regime (0.40) was *higher* than at full
input (0.30), proving the output was dominated by an input-independent
bias path.  Linearity ratio (linear/baseline RMS) was 0.196 instead of
the expected 0.001 — i.e. only ~5 dB of the 60 dB input attenuation made
it through.

Root cause: Faust chain has no output-side subsonic / DC blocker.  JSFX
applies five HP filters (`hp1..hp5`); the Faust port had only `hp1` (10 Hz,
between T1 and the tone stack at `nilamp.dsp:96`).

**Scope-0 fix (commit `<scope-0-hp5>`):** added `flt_ii1_hp(40)` on `v_out`
in `dsp/nilamp.dsp`, mirroring JSFX `hp5.flt_ii1_set_frequency(40)` at
line 184 (applied at line 431, just before `spl0` is written).  17/17
regression tests still pass — HP5 is downstream of every per-stage
harness.

Post-fix (same input):

| signal            | mean (700–800 ms window) | rms      |
|-------------------|-------------------------:|---------:|
| nilamp post HP5   |                  −4.0e-8 |   0.1863 |
| jsfx              |                  +3.2e-7 |   0.2199 |

DC fell by ~5 orders of magnitude.  Per-sample residual: −12.5 → −13.4 dB
(small improvement; the missing tone-shaping back-end remains the
dominant error source, see below).  Peak agreement: 0.299 vs 0.273 →
0.282 vs 0.273.

### Finding 3: residual is dominated by missing back-end DSP, not input gain

Even after equalizing the JSFX input-gain offset (Finding 1) the residual
is essentially unchanged from the pre-equalization measurement
(−12.5 dB → −13.4 dB after HP5).  The remaining error is therefore not
input-side — it is the deferred chain documented in `nilamp.dsp:4-13`:

- HP2 (post-T2), HP3 (post-T3), HP4 (T5 branch), full T5 push-pull stage,
  `k1`/`k2` constants, PEQ1/PEQ2/PEQ3, HS1/HS2/HS3, LP2 (10 kHz output),
  the `gout` divisor's `t5.rl*t5.isat` term, and the 3-stage PSS chain.
- Plus a 130 Hz tone-stack center-frequency mismatch (nilamp 500 Hz vs
  JSFX `iso266(56) = 630 Hz`) and a ~4.6 ms group-delay difference.

The remaining startup transient in linear regime (~0.4 peak at 50–100 ms,
settling by ~200 ms) is now AC, not DC; it reflects the missing
intermediate HP2/HP3/HP4 stages' settling behaviour vs. nilamp's
collapsed signal path.

### New harness flags

- `--input-scale FLOAT` (default 1.0): pre-scales the input WAV before
  rendering through both engines.  Use `--input-scale 1e-3` once the
  back-end DSP is ported to isolate linear filter-shape mismatch from
  nonlinear stage mismatch.  Until then it surfaces residual transients
  rather than steady-state filter error.

## Investigation 2026-05-04 (cont.): tone-stack centre + back-end port attempt

### Tone-stack centre 500 → 630 Hz (committed)

The tone-stack-frequency mismatch identified above was patched: the
literal `500` in `dsp/nilamp.dsp:flt_sv2_tst(...)` is now `630`.  The
bass/mid/treble pot law (squaring) and pre-warping flags (`pwf=1,
pwQ=1`) were already faithful to JSFX line 240; only the centre needed
to change.

Effect on the 1 s 440 Hz sine ABX baseline: residual unchanged at
−13.4 dB.  At Q=0.5 the SVF response is broad enough that 500 vs
630 Hz centres attenuate 440 Hz similarly.

Effect on a 20 Hz–18 kHz log sweep: peaks now agree to within 0.5 dB
across the sweep (0.366 vs 0.370) and the time-alignment lag drops
from 218 to 1 sample once we excite a wide band — so the previous
4.6 ms "lag" was actually a frequency-dependent phase shift at the
fundamental, not a bulk delay.

### Failed attempt: partial back-end port

Tried to land HP2 (0.41 Hz, post-T2), HP3 (5.8 Hz, post-T3),
`*k1=0.797` post-T3, and LP2 (10 kHz Butterworth, post-HP5) in one
DSP commit.  The intent was Scope-1.5 from the prior session plan:
piecemeal additions to chip away at the missing back-end chain.

Result: residual got **worse**.  Sine: −13.4 → −8.9 dB.  Sweep:
−9.2 → −6.8 dB.  Sweep peak fell from 0.366 to 0.267 (≈−2.7 dB).
Reverted (kept only the tone-stack-centre fix from this session).

**Lesson — the JSFX back-end is a tightly coupled gain-staging
block, not a sequence of independent filters.**  The pieces I added
were all **lossy** (k1 = −1.97 dB; LP2 = ~−1 dB at HF) while the
gains they're meant to balance were **deferred**:

| JSFX element                        | net effect                | status this session |
|-------------------------------------|---------------------------|---------------------|
| `*k1 = 0.797` (mode==0)             | −1.97 dB flat             | added → reverted    |
| `hp2.flt_ii1_set_frequency(0.41)`   | DC blocker, ~0 dB audio   | added → reverted    |
| `hp3.flt_ii1_set_frequency(5.8)`    | DC blocker, ~0 dB audio   | added → reverted    |
| `peq1.flt_sv2_set_peq(kp1, 38, qp1)`| +1 dB peak at 38 Hz       | not ported          |
| `hs1.flt_sv1_set_hs(ks1, fs1, …)`   | +3 dB shelf above ~1.3 kHz| not ported          |
| `peq3.flt_sv2_set_peq(kp2, 38, qp2)`| +2 dB peak at 38 Hz       | not ported          |
| `hs3.flt_sv1_set_hs(ks1, fs1, …)`   | +3 dB shelf above ~1.3 kHz| not ported          |
| `lp2.flt_df2_set_lp(10000, …)`      | −1..−2 dB at HF           | added → reverted    |

(`kp1=10^0.05=1.122`, `kp2=10^0.10=1.259`, `ks1=ks2=10^0.15=1.413`,
all derived in JSFX lines 245–263 from the default slider values
`gp_pre=1, gp_post=2, gs_pre=3, gs_post=3 dB`.)

The HS1 + HS3 pair adds **+6 dB above ~1.3 kHz** that I omitted while
adding −3 dB of fixed loss.  At the 440 Hz sine that net imbalance is
mostly invisible (below the shelf knee); at the broadband sweep peak
it shows up as a ~3 dB level shortfall, exactly what was measured.

### Recommendation for next session

Port the back-end chain **as a single block**, ideally as a
self-contained Faust function

    back_end_chain(spl0, dvs2) =
        spl0 : flt_ii1_hp(0.41)            // hp2
             : tube_cd(t3, ...)             // T3 cathodyne
             : *(k1)                        // -1.97 dB
             : flt_ii1_hp(5.8)              // hp3
             : flt_sv2_peq(kp1, 38, qp1)    // peq1: +1 dB @ 38 Hz
             : flt_sv1_hs(ks1, fs1, 1, 1)   // hs1: +3 dB shelf
             : tube_ck(t4, ..., dvs2)       // T4 6V6
             : ...                          // (peq3, hs3, hp5, lp2)

— landing all coupled elements together so each commit is a
self-consistent gain-staging block.  Verify per-commit with both the
sine and the sweep input.  Defer T5 + the aux subtractive branch and
the multi-stage PSS to 5e3-v2 as today.

Treat any back-end commit that **regresses** the sweep residual on its
own as a sign of a broken gain-staging assumption rather than as a
trade-off to accept.

## Investigation 2026-05-04 (cont.): T5 diagnostics before retrying top-level wiring

After adding the generated T5 6V6 table/constants, two direct attempts
to wire T5 into `dsp/nilamp.dsp` regressed the current T4-only
baseline:

| Attempt | Sine residual | Sweep residual |
|---------|---------------|----------------|
| T5 aux/subtractive branch only | -12.7 dB | not kept |
| fuller back-end/T5 block | -10.2 dB | not kept |

Both attempts were reverted from `dsp/nilamp.dsp`; only the generated
T5 data stayed committed.  The follow-up diagnostic work adds
isolated Faust fixtures for:

* `tube_ck_simple` using T5 constants/table.
* the mode-0 T4/T5 push-pull branch driven from synthetic T3 plate and
  cathode taps.

Those fixtures pass against the Python Keller oracle, so the regression
is not explained by the generated T5 table, T5 constants, or the basic
T4/T5 branch equations in isolation.  The next top-level attempt should
start from actual `nilamp.dsp` internal taps and compare them against
oracle taps before changing the public render path.  Suspect areas are
shared sag/`dvs` timing, the missing multi-stage PSS block, phase/sign
assumptions around the subtractive aux branch, and gain staging around
the downstream back-end filters.
