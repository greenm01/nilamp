# Tube Amp DSP Project Notes

Notes from exploring how to build a real-time tube amp plugin extending Helmut Keller's approach with insights from Hegglun's PAK Project, Mačák's DK-method work, and current-drive theory (Meriläinen, Hegglun's Linear Audio articles).

Conversation that produced these notes: `dsp.txt` (same directory).

Status update, 2026-05-05: these notes were written before the project moved
away from the Faust/Rust experiment. The current implementation direction is
native C for DSP and plugin code, plain `make` for builds, Lua for REAPER host
harness automation, and Python only for offline/reference tooling where it is
already useful.

---

## 1. TL;DR

**What**: Linux-native CLAP guitar amp plugin built on Keller's architecture, extended to support multiple amps. For fun. Fills a real gap in the Linux audio ecosystem (most amp plugins are Mac/Windows-only or paid).

**Foundation**: Helmut Keller's "A Tube Amp Modeling Project" 5E3 emulation (JSFX, runs in REAPER or via YSFX wrapper). Block-diagram + ADAA + lookup tables. Already sounds good and runs on a tablet. We extend, we don't replace.

**Tech stack**: native C DSP + native C CLAP shell, built with plain `make`.
Lua drives REAPER validation/render harnesses. Python remains for offline
lookup-table/reference generation and parity diagnostics. Linux primary target.

**Direction**: see §2.

---

## 2. Direction — actionable improvements

Improvements ranked by value/effort, sourced from the broader exploration. Each carries a reference back to the supporting evidence.

### Tier 1 — high-value, low-effort (do first)

**T1.1 — Per-amp source impedance / damping factor** (current drive — see §6.2, §6.3)

- Keller hardcodes `Rx ≈ 80Ω` small-signal, `≈ 2.5Ω` saturated for the 5E3
- Make these data-driven per amp: low-NFB amps high Rx (strong current-drive feel), high-NFB amps low Rx
- Pre-filter peak EQ + high-shelf gains derived from `Rx / (Rx + Rs)` instead of constants
- Files to touch (when porting): `HK_LIB_TUBE.jsfx-inc` parameter signatures, per-amp config equivalents
- Why it matters: explains the "feel" difference between Tweed and Bassman that's not captured by preamp curves alone
- Decision gate: ship as table-driven from the start; no ABX needed

**T1.2 — Cut speaker post-filter, route to external IR loader** (see §4)

- Remove `peq3` + `hs3` (post-filter) from output chain; keep `peq1/hs1` + `peq2/hs2` (pre-filter, current-drive coupling)
- Output at ~-18 to -12 dBFS line level for IR loader input
- User chains in their preferred IR/cab loader (NadIR, LSP Cabinet IR, Cab Lab Lite — all free on Linux)
- Decision gate: ship as standard chain; no decision required

### Tier 2 — medium-value, medium-effort (do where audible)

**T2.1 — Pentode model for power tubes** (PAK / Cohen-Hélie / Dempwolf — see §6.1, §7)

- Keller's GLF treats 6V6/6L6 power tubes as triode-shaped — misses screen-grid current behavior
- Mačák's `pentode.m` is a DK-matrix-wiring stub with no I-V function — implement ourselves
- Implementation: offline Python script using Cohen-Hélie or Dempwolf-style pentode formulas, generates pentode tables in same 1500-segment polynomial format Keller uses; runtime unchanged
- Apply to: any amp where power-amp clipping is a tone feature (Plexi, AC30, Bassman)
- Decision gate: ABX vs. triode-shaped GLF; ship pentode where audible

**T2.2 — Dempwolf-Zölzer triode tables for non-symmetric amps** (PAK — see §6.1, §7)  *(explored in the oracle/native test path — see status note below)*

- For amps where asymmetric grid-current behavior matters (high-gain Marshalls, AC30 boost, grid-blocking effects)
- ~~For 5E3 (`b=0, type=0.5` in Keller, effectively symmetric tanh), the delta is negligible — keep GLF~~
  Retracted.  Keller's 5E3 ECC83 fits use `b=0, type=0.5` not because the underlying tube is symmetric but because that's the simplest GLF that lands in the right ballpark.  Comparing to a load-line-solved Dempwolf-Zölzer ECC83 at the same operating point shows clear asymmetry around the quiescent (~5–35% depending on stage), and the DZ-derived `kbias_actual` differs from Keller's hand-fitted `kbias` by 5–80%.  See `tools/plot_dz_vs_glf.py` (figures in `docs/notes/figures/dz_vs_glf*.png`).
- Reference impl: `~/src/NodalDKFramework/triode.m` (lines 35–58)
  - SoftPlus + power-law: `Ip = -Gp · (ln(1+exp(Cp·(Vpk/μ+Vgk)))/Cp)^γ − Ig`
  - ECC83 params: `Gg=606e-6, xi=1.354, Cg=13.9, Gp=2.14e-3, gamma=1.303, Cp=3.04, mu=100.8`
  - Full Jacobian provided (useful for offline load-line iteration)
- Implementation: `tools/keller_oracle.py` provides `triode_dz_ecc83`, its Jacobian, and warm-startable `loadline_solve_ck/cd` + outward-sweep `loadline_curve_ck/cd` helpers. Current native C table generation is centered on `tools/gen_5e3_tables.py` and `native/generated/nilamp_tables.h`; fixtures come from `tools/gen_fixtures.py`.
- Regression: native C tests in `native/tests/test_native.c` pin DZ-table test paths against generated fixtures under `tests/fixtures/`; production still follows the Keller GLF table path unless deliberately changed.
- Decision gate: ABX per amp; keep GLF where Dempwolf delta isn't hearable (default still ships GLF on all stages).

**T2.3 — Mačák Algorithm 1 data reduction on tables** (Mačák spline-DK — see §6.4)

- Keller's uniform 1500-segment tables → ~100–300 nonuniform segments at same accuracy
- Implementation: in offline Python table generator, iteratively remove least-important data points
- Benefit: smaller binary, better cache locality
- Decision gate: optional polish; not on critical path

### Tier 3 — lower priority / scope-dependent

**T3.1 — Frequency-dependent source impedance Rx(f)** (current drive — see §6.3)

- OPT primary inductance + leakage cause Rx to vary with frequency
- Keller approximates with two endpoint values (small-signal vs. saturated)
- Skip unless full Rx(f) curve gives ABX-audible delta

**T3.2 — Mačák spline-DK for tone-stack-between-tubes amps** (Mačák spline-DK — see §6.4)

- Captures inter-stage coupling that Keller's block-diagram cannot
- Affects: Bassman, Plexi, Twin, Princeton, most Marshalls (tone stack between tubes loads first plate, shapes drive into second tube)
- Implementation: separate code path with full DK + parametric multi-D splines
- Decision: Phase 4b refinement at most. Most commercial plugins ignore this.

### What NOT to do — validated by the exploration

- **OPT hysteresis modeling (JA, GC, Frohlich)** — Mačák ABX listening test (§6.1) shows linear OPT is statistically indistinguishable from Jiles-Atherton hysteresis. Mačák doesn't ship transformer code himself despite developing all three models. Strong implicit confirmation.
- **Replace block-diagram architecture with WDF/DK** for solo-tube stages — Keller's Wiener-Hammerstein is correct for single-tube circuits.
- **NAM-style ML capture** — different category. Users can chain a NAM plugin downstream if they want.
- ~~**Replace GLF for 5E3 specifically** — `b=0, type=0.5` is already perceptually adequate; the curve-fit is acoustically equivalent to a Dempwolf-Zölzer triode for this circuit.~~  Retracted; see T2.2 status note above. Whether to *ship* DZ-derived ECC83 tables as the default for the 5E3 still needs an ABX listening test against the GLF baseline.

### Implementation order (when work begins)

1. Keep the native C renderer/plugin path authoritative; verify with `make native-test`.
2. Continue Keller JSFX parity using `tools/abx_compare.py` and `tools/compare_taps.py`.
3. Finish source-backed 5E3 parity before expanding amp models.
4. T1.2 (cut post-filter, output line level for IR loader) after parity stabilizes.
5. T1.1 (refactor source impedance into per-amp data structure).
6. Add a second amp (Bassman or Plexi) with different damping factor; verify "feel" difference.
7. T2.1 (pentode for power amps); ABX-decide per amp.
8. T2.2 (Dempwolf-Zölzer triode tables) where audible; ABX vs GLF default still pending.
9. T2.3 (data reduction) as polish.
10. Tier 3 only if motivated by specific listening complaints.

---

## 3. Keller's Tube Amp Plugin — the foundation

### What it is

- "A Tube Amp Modeling Project" — Helmut Keller, 2024
- Real-time 5E3 Fender Tweed Deluxe emulation
- JSFX format, runs in REAPER directly or via YSFX wrapper (free CLAP/VST3/AU)
- Targets fanless tablets (Surface Pro 7) with low latency

### Method: block-diagram + ADAA

Not WDF, not DK, not NAM. A fourth approach:

1. **Static nonlinearity**: Generalized Logistic Function (GLF) — parameterized sigmoid family
2. **Antialiasing**: ADAA (Parker/Esqueda/Bilbao 2016) via 1500-segment polynomial lookup
3. **Tube stages as Wiener-Hammerstein**: linear pre-gain → memoryless NL → linear post-gain → bias dynamics LPF
4. **Local feedback**: solved by offline lookup-table resampling, no per-sample Newton
5. **PSU sag**: RC ladder with current feedback into rails (better than most plugins)
6. **Speaker**: linear Thiele-Small filter (peak EQ + high-shelf), pre- and post-filter banks
7. **Tone stack**: universal SVF, not the literal 5E3 RC network

### Source code structure

Located at: `/home/niltempus/Downloads/hk jsfx v1.0.4 b/HK JSFX V1.0.4 B/`

Key files:

- `TWD DLX  II.jsfx` (585 lines) — main plugin (5E3 wiring)
- `Libs/HK_LIB_TUBE.jsfx-inc` (504 lines) — tube stage classes (CC, CD, CF, LTP, PSS)
- `Libs/HK_LIB_ADNL.jsfx-inc` (257 lines) — ADAA + GLF table generator
- `Libs/HK_LIB_FLT_SV.jsfx-inc` — state-variable filters
- `Libs/HK_LIB_PKD.jsfx-inc` — peak detector
- `Libs/hk-ui-lib.jsfx-inc` (97 KB) — custom UI

### What's already physical in Keller's code

`tube_ck_set` in `HK_LIB_TUBE.jsfx-inc`:

```
tube_ck_set(mu, ra, isat, ibias, b, type, vs, rl, rk, ...)
//          μ   Ra  Isat   Ibias  curve  Vs  RL  RK
```

- `kbias = ibias / isat` becomes the GLF's `k0` (bias position)
- Small-signal coefficients (`kpre`, `ksva`, etc.) derived from physical `R`, `μ`
- Only the **shape** (`b`, `type`) is curve-fit
- For 5E3 he sets `b=0, type=0.5` → effectively symmetric tanh

So Keller is already partly physical. What needs replacing (T2.2) is the *shape*, not the operating point.

### Integration point for T2.1 / T2.2 (physical tube models)

Only **one function** needs replacement: `adnl_set_glf` in `HK_LIB_ADNL.jsfx-inc` (lines 27–185).

That function's only job is to fill the lookup table with NL function values on a grid. Everything downstream (closed-loop resampling lines 79–117, polynomial fitting lines 119–180, ADAA runtime lines 187–257) is curve-agnostic.

---

## 4. Speaker Filter Decision (T1.2)

Keller has TWO speaker filter banks (paper chapter 7):

1. **Pre-filter** (PEQ1/HS1 + PEQ2/HS2): models speaker impedance loading on power tubes
   - Speaker resonance presents high impedance → changes AC plate load → affects clipping
   - Difference between small-signal Z_out (~80Ω) and saturated Z_out (~2.5Ω)
   - **This affects amp character under saturation** — must keep

2. **Post-filter** (PEQ3/HS3): models speaker frequency response
   - Resonance bump + voice coil inductance high-shelf
   - **This is what an IR captures** — cut and replace with external IR loader

### Decision

- **CUT post-filter** — IR loader handles this better
- **KEEP pre-filter** — models loading-affects-clipping interaction; IR can't recover this
- Output convention: ~-18 to -12 dBFS line level for IR loader input
- Output mono; let IR loader handle stereo mic positions

### Free IR loaders on Linux

- **NadIR** (Ignite Amps, free, all platforms)
- **LSP Cabinet IR** (Linux/cross-platform, free, native)
- **Cab Lab Lite** (Two Notes, free)
- All accept WAV format

---

## 5. Architecture

```
Python (offline/reference tooling)
  ├─ Take physical params (μ, Kp, γ, Cp, Gp, Ra, Rk, RL, Vs, ibias)
  ├─ Solve tube curve (Dempwolf-Zölzer or Cohen-Hélie) over input grid
  ├─ Apply load-line constraint, get Ia(Vg)
  ├─ Optional: Mačák Algorithm 1 data reduction
  └─ Emit native C lookup tables + binary fixtures

Native C DSP core
  ├─ Tube stages (CC, CF, CD, LTP) using imported tables
  ├─ ADAA on lookup tables (Keller's polynomial fit, ported)
  ├─ Cathode bias dynamics (first-order LPF)
  ├─ PSU sag (RC ladder with current feedback)
  ├─ Tone stack (SVF)
  └─ Pre-filter speaker loading effect (PEQ + HS, T1.1 data-driven)

Native C CLAP shell
  ├─ Plugin format adapter
  ├─ Parameters, automation, state, and audio ports
  └─ Thin wrapper over the native DSP engine

Lua REAPER harnesses
  ├─ JSFX render/tap parity automation
  └─ CLAP host validation automation

Plain make
  └─ Builds native renderer, tap renderer, tests, and CLAP plugin
```

### Component split (what comes from where)

| Component | Source | Notes |
|---|---|---|
| ADAA + table polynomial fit | Keller | Excellent and curve-agnostic; works with any-source tables. Optional T2.3 reduction. |
| Closed-loop NL (cathode follower) | Keller's offline resampling | Avoids per-sample Newton; works with any underlying curve |
| Cathode bias LPF | Keller | Physically correct, no improvement available |
| PSU sag RC ladder | Keller | Better than most plugins; no improvement available |
| Tone stack | Keller's universal SVF | Independent of NL model |
| Block-diagram per-stage architecture | Keller | Wiener-Hammerstein per stage |
| Static tube nonlinearity | **Dempwolf-Zölzer 2011** (T2.2) | Physical SoftPlus + power-law, parameter-tunable per tube. Reference impl in Mačák's `triode.m`. |
| Pentode model (power amp) | **Cohen-Hélie or Dempwolf** (T2.1) | Implement ourselves; Mačák's repo has DK matrix wiring but no I-V model |
| Source impedance (Rx) | Per-amp parameter (T1.1) | Data-driven; derived `Rx/(Rx+Rs)` for pre-filter gains |
| Speaker frequency response | External IR loader (T1.2) | Cut Keller's post-filter |
| Plugin shell | Native C CLAP | Thin host adapter around the native DSP engine. |
| DSP language | Native C | Current source of truth; Faust/Rust path has been removed. |

---

## 6. Background — key findings from the exploration

### 6.1 Mačák 2012 listening test on OPT models

Mačák's PhD thesis (file pointer in §12) gives the most complete published treatment of OPT modeling for guitar amp DSP. Three models worked through in real-time:

1. **Frohlich** (eq. 1.114): `B = H / (c + b|H|)` — saturation only, no hysteresis. Cheapest.
2. **Gyrator-Capacitor** (eq. 1.118–1.119): nonlinear capacitor for saturation + nonlinear resistor for hysteresis. Mid cost. Used by Yeh in real-time work.
3. **Jiles-Atherton** (eq. 1.115–1.117): full physics-based hysteresis via magnetization ODE + Langevin curve. Implicit form `0 = f_JA(H, B, dH/dt, dB/dt)` requires Newton at each timestep. Most expensive.

**Listening test result (§6.4 of thesis, Table 6.3)** — ABX, 60 trials each, push-pull amp with Fender NSC041318 OPT:

| Comparison | Correct ID | Threshold to reject null | Difficulty (1–5) |
|---|---|---|---|
| Linear vs Nonlinear | 32 / 60 | 36 | 4.70 |
| Hysteresis vs No hysteresis | 31 / 60 | 36 | 4.88 |

**Neither comparison reaches statistical significance.** Mačák's verbatim conclusion:

> "all transformer models lead to the same audio perception. Because the linear transformer model is much simpler than the other models, it is advantageous to use this model."

He notes the hysteresis effect "manifests very slightly" and nonlinear effects appear mainly at low frequencies (where speaker resonance and cone excursion already dominate).

**Caveats**: specific transformer (well-designed Fender, not undersized); guitar riffs at moderate levels; doesn't cover vintage low-watt tweeds or single-ended Class A with smaller, more easily saturated OPTs.

**Additional supporting evidence**: Mačák's NodalDKFramework (§12) ships with **no transformer code at all**. The author of the OPT models doesn't ship them — strong implicit confirmation.

### 6.2 Why Mačák's result makes sense — the current-drive framing

The OPT does two things in DSP terms — only one is the magnetic core's nonlinearity:

1. **Impedance transformation** (linear): turns ratio steps `(Np/Ns)²` ohms down. Pure scaling.
2. **Current-source-like coupling to the speaker** (linear interaction): tube power stages without heavy NFB have plate impedance in the hundreds of kΩ. After OPT step-down, the source impedance presented to the speaker is comparable to the speaker's nominal impedance. Damping factor ~1–10 (vs. ~100–1000 for solid-state). The amp behaves more like a current source than a voltage source.

Speaker behavior under current drive:

- Cone excursion ∝ current, not voltage
- Voltage at speaker terminals = current × Z_speaker(f)
- The speaker's **impedance peak at resonance** gets emphasized in acoustic output
- The **HF rise from voice-coil inductance** gets emphasized
- This is a *linear* interaction between source impedance and reactive speaker load — no hysteresis required

This is exactly what Keller's pre-filter (PEQ1/HS1 + PEQ2/HS2) implements: peak EQ at resonance + high-shelf for inductance, derived from Thiele-Small parameters.

**The unification**: the OPT's actual sonic contribution is impedance transformation + current-drive coupling. Both linear. The magnetic core's nonlinearity was never carrying the OPT's tone — that's why Mačák's listening test couldn't distinguish linear from nonlinear models. Keller's "pre-filter as speaker impedance loading on tubes" is the correct model, just framed dually as "tube source impedance into reactive speaker."

This also explains why solid-state guitar amps with the same speakers sound different from tube amps even with matched preamp curves; why "tube preamp + SS power amp" rigs feel different; and why some modern Class-D designs add deliberate output-impedance modeling to recreate the current-drive feel.

### 6.3 Source impedance / damping factor by amp type

Approximate damping factors:

| Amp class | Damping factor | Source Z (8Ω speaker) | Current-drive character |
|---|---|---|---|
| 5E3 / Champ (no NFB) | ~1–3 | ~3–8 Ω | Strong |
| Marshall Plexi (moderate NFB) | ~5–10 | ~1–2 Ω | Moderate |
| Bassman / Twin (more NFB) | ~10–20 | ~0.4–0.8 Ω | Light |
| Hi-Fi tube amp (heavy NFB) | ~30+ | <0.3 Ω | Almost voltage drive |
| Solid-state | ~100–1000 | <0.08 Ω | Pure voltage drive |

This explains the well-known "feel" difference between low-NFB combos and high-NFB amps that's not just preamp gain or tone stack. → **T1.1**: expose source impedance as a per-amp parameter; derive pre-filter gains from `Rx/(Rx+Rs)`.

### 6.4 Mačák, Holters, Schimmel 2012 DAFx — spline-DK contributions

Beyond the thesis, this DAFx-12 paper introduces engineering techniques worth knowing:

1. **Cubic spline interpolation with constant access** — `i = ⌊m·x⌋ + o` direct lookup, Horner-form evaluation. C² continuous derivatives, smoother than linear, less aliasing. Same idea as Keller's polynomial-segment fits, framed differently.
2. **Nonuniform grids with mapping function** — `i = fmapping[⌊m·x⌋ + o]`. Order-of-magnitude table size reduction at same accuracy. Applicable as **T2.3** to Keller's tables.
3. **Data reduction algorithm (Algorithm 1)** — iteratively removes least-important points by error contribution. For their parametric Fender preamp: 53 GB → 65 kB. Applicable as **T2.3**.
4. **Decomposition trick for two-tube circuits** — splits a 4D coupled solve into two 2D solves with cross-feedback. Avoids combinatorial blowup.
5. **Parametric tone stack inside the K matrix** — pot positions modulate K entries, preserving inter-stage coupling. **Relevant only to T3.2 (tone-stack-between-tubes amps)** — Bassman, Plexi, Twin, Princeton, most Marshalls.

Performance benchmark: 10% CPU on a 2.66 GHz Intel for a full parametric two-tube preamp; ~7.6× speedup over non-approximated state-space. On modern CPUs ~1–2%.

### 6.5 IR convolution and current-drive mismatch

Subtlety: standard guitar IRs already encode the current-drive interaction of the amp they were captured with. If the IR was captured from a 5E3 (high source Z), it includes the resonance peak emphasis from current-drive coupling into that specific speaker. If you stick that IR after a different amp model with different damping factor, you mismatch.

Three approaches in increasing fidelity:

1. **Ignore it** (most plugins). Acceptable for casual users.
2. **Match IRs to amps**. Capture IR with similar damping factor.
3. **Two-port IRs (DynIR-style)**. IR captures speaker impedance + acoustic transfer separately; plugin computes terminal voltage from its own source impedance + IR's impedance curve, then convolves. Two Notes' DynIR does this commercially.

For our project, **Approach 1** is fine. Worth flagging Approach 3 as future refinement.

### 6.6 Why "ignored" gaps stay ignored

The non-musical imperfections (microphonics, aging, hum, drift) are things tube amp *designers* fight against. They're engineering failures, not features. A plugin that doesn't model them is closer to the designer's intent than the leaky physical amp. **Modeling effort should track design intent.**

What "audiophile warmth" actually is: mostly designable behavior listeners don't realize is designable — PSU sag, OPT saturation, transformer phase shift, speaker resonance interaction, output stage breakup, coupling cap behavior. All modelable. The truly stochastic stuff (noise floor, microphonics) is either undesirable or easily faked.

---

## 7. Background — Hegglun's PAK Project

### What PAK is

- Free CC-BY analytical method for BJT/diode circuits
- Uses **Lambert W function** (`W(eˣ)`) to give closed-form DC solutions
- Replaces SPICE-style Newton-Raphson iteration with explicit equations
- Built on Banwell 1971 + Lambert W framework

### Project structure

5 levels, PAK101–PAK503. L1: W-function for diodes/BJTs. L2: multi-diode, MOSFET, **tubes (PAK213)**. L3: multi-transistor (Darlington, CFP, LTP, current mirror, cascode). L4: push-pull amplifiers. L5: open research.

### Canonical PAK form

```
Diode + Rs (PAK102):
    Id = (N·Vt/Rs) · W( exp((Vin - Vk) / (N·Vt)) ) - Is
    Vk = N·Vt · ln(N·Vt / (Is·Rs)) - Is·Rs

CE BJT + Re + Rin (PAK105):
    Ic = (N·Vt/Req) · W( exp((Vin - Vk)/(N·Vt)) ) - Is
    Req = Rin/β + Re·(1+β)/β
```

The `Req` β-folding trick reduces a multi-port BJT to a one-port nonlinearity at the collector.

### Tube work in PAK

- **PAK213 / TriJmos** — 27 pp paper, Feb 2023. LTspice subcircuit modeling triodes/SITs/jFETs on top of VDMOS. Power-law (3/2) + Island-effect lookup table + SoftPlus Anlauf region.
- **Zero mentions of Lambert W in the tube paper.** Tubes are power-law, not exponential → W doesn't apply directly.
- For tubes: use Cardano cubic (pure 3/2) or Cohen-Hélie's W-reformulation (Koren-style + SoftPlus regularization → W-solvable) or Dempwolf-Zölzer (SoftPlus + power-law, what we're using).

### Hegglun's connection to current drive

PAK410, PAK413, PAK414 are the analytical foundations behind his published current-drive amplifier designs in **Linear Audio** (Vol 1 simple square-law, Vol 8 Cube amp, Vol 13 Current-Source Driven). Multiple long DIYAudio threads. His stated position: current drive for moving-coil drivers offers a bigger sonic improvement than reducing voltage-drive distortion from 0.1% to 0.01%. Not directly applicable to plugin DSP, but background that explains why the PAK math and current-drive theory are intellectually unified.

---

## 8. Background — method landscape

| Method | Approach | Per-sample cost | Knows physics? |
|---|---|---|---|
| **WDF** | Wave digital filters; tree of scattering elements | Closed form for tree, iter at NL roots | Yes (topology) |
| **DK-method** | State-space + bilinear discretization | Newton at each step | Yes |
| **PAK / analytical** | Closed-form W-function per topology | Single W eval, no iter | Yes (per stage) |
| **Keller's block-diagram** | Wiener-Hammerstein + ADAA + lookup tables | Table lookup | Partial (small-signal only) |
| **NAM / black-box ML** | WaveNet-style neural network | NN forward pass | No |

Real-time tube amp sim is **already solved**, not theoretical. Yeh 2009 (Stanford): full Bassman/AC30 emulation in real-time via DK-method. Cohen-Hélie 2010–11 (IRCAM): closed-form W-tube reformulation. Werner 2016: multi-nonlinearity WDF with R-type adaptors. Production: UAD, AmpliTube, Fender Tone Master (analytical), NeuralDSP/Tone3000 (ML).

CPU budget at 48 kHz (~20 µs per sample, modern CPU):

| Operation | Cycles | Time |
|---|---|---|
| Lambert W (Veberič rational) | ~50–100 | ~25 ns |
| Cardano cubic | ~100–200 | ~50 ns |
| Newton on BJT (3–5 iters) | ~600–2500 | ~600 ns |
| Table lookup (Keller-style) | ~30–50 | ~15 ns |

Plenty of headroom for multi-amp + oversampling on commodity hardware.

---

## 9. Language / tech stack decisions

| Option | Verdict |
|---|---|
| C++ + JUCE | Industry default but paid license; not preferred |
| C++ + iPlug2 | Free, smaller community |
| Rust + nih-plug | Rejected for this project; previous experiment was removed. |
| Zig | Best of the exotic options; comptime useful for DSP; pre-1.0; no audio plugin ecosystem |
| Odin | No audio infra at all; skip |
| Nim | GC concerns; stalled momentum; skip |
| Faust → Rust | Rejected for this project; useful conceptually, but not the current stack. |
| **C + CLAP** | **Current runtime stack** — direct control, simple build, no Rust/Faust dependency. |
| **Lua** | **Current host/harness scripting stack** — REAPER automation and later lightweight scripting where useful. |

**Final stack**: native C DSP + native C CLAP shell, plain `make`, Lua for
REAPER harnesses, Python for offline/reference tooling, primary target Linux.

---

## 10. References

### Foundational

- **Banwell 1971** — original Lambert W BJT solution (EW-WW, in `IansPAKproject/Related/`)
- **Hegglun (PAK Project)** — extensions: Banwell + multi-stage + tube model. PAK410 = Linear Audio Vol 13's Current Source Driven amp, PAK414 = Vol 8's Cube amp, PAK413 = Faran-AB current-driven DoubleCross.

### Current drive

- **Meriläinen, "Current-Driving of Loudspeakers"** (current-drive.info, 2nd ed. ~2010) — canonical text on why voltage drive of moving-coil speakers introduces avoidable distortion (back-EMF, position-dependent voice-coil inductance, eddy currents) that pure current drive eliminates.
- **Ian Hegglun on diyAudio** — multiple long threads:
  - "Square Law Class A Amps": https://www.diyaudio.com/community/threads/square-law-class-a-amps.189766/
  - "Ian Hegglun's ClassA³ Cube-Law Amp": https://www.diyaudio.com/community/threads/ian-heggluns-classa-3-cube-law-amp.261458/
  - "Low distortion current driven Class-B output stage": https://www.diyaudio.com/forums/solid-state/350507-low-distortion-current-driven-class-output-stage.html
  - "Current source driven Class-AB amplifier": https://www.diyaudio.com/community/attachments/hegglun_current-driven-push-pull-lav13-snippet-pdf.778028/
- **Hegglun's Linear Audio articles** — Vol 1 (simple square-law), Vol 8 (Cube), Vol 13 (Current Source Driven). PAK410/413/414 are their analytical foundations.
- **Pass / First Watt F1, F2** — Nelson Pass's commercial current-source amplifier designs.

### Real-time tube amp DSP

- **Yeh 2009** — Stanford CCRMA dissertation on real-time tube amp DK-method
- **Cohen & Hélie 2010, 2011** — IRCAM closed-form Lambert W triode/pentode
- **Dempwolf & Zölzer 2011** — "A physically-motivated triode model for circuit simulations". `Ip = Gp·(ln(1+exp(Cp·(Vpk/μ+Vgk)))/Cp)^γ − Ig`. **Recommended triode model for T2.2.** Reference impl in Mačák's `triode.m`.
- **Macak & Schimmel 2010** — efficient triode model
- **Mačák 2012** — Brno UT PhD thesis "Real-time Digital Simulation of Guitar Amplifiers as Audio Effects". Three OPT models with equations + ABX listening test (§6.1).
- **Mačák, Holters, Schimmel 2012** — DAFx-12 paper, spline-DK for parametric Fender preamp (§6.4).
- **Karjalainen, Pakarinen** — earlier real-time tube work

### Frameworks

- **Werner 2016** — R-type WDF for multi-nonlinearity circuits
- **Holters & Zölzer** — DK-method, ACME library
- **Parker, Esqueda, Bilbao 2016** — antiderivative anti-aliasing (ADAA)

### Modern reference

- **Keller 2024** — "A Tube Amp Modeling Project V1.0.3" (the JSFX plugin we extend)

### ML / hybrid

- **Engel et al. 2020** — DDSP (differentiable DSP)
- **Damskägg, Wright 2019** — WaveNet for guitar amp modeling

---

## 11. Open Questions

- Will Cohen-Hélie pentode produce perceptually distinct power amp behavior on a 5E3, or is the symmetric tanh good enough for this circuit?
- Is offline-table-generation really faster than runtime closed-form W evaluation in native C on modern CPUs?
- Does Mačák's "linear OPT is fine" finding hold for vintage low-watt tweeds (5E3, Champ) where the OPT is small enough to saturate audibly?
- Does keeping Keller's pre-filter (impedance loading effect) audibly matter when paired with arbitrary IRs of different speakers?
- Can negative feedback loops in Marshall/Twin-style amps be handled cleanly in the native block graph without per-sample Newton, or do those amps need a separate solver path?
- How audible is the current-drive / IR-impedance mismatch when using IRs captured from a high-NFB amp on a low-NFB amp model (and vice versa)?
- For amps Hegglun has analytically solved (PAK410, PAK413, PAK414), would those closed forms be useful as reference implementations for the *power amp* side of plugin DSP?

---

## 12. File Pointers

- **PAK material**: `/home/niltempus/Documents/2026-04-03-PAK-Project/IansPAKproject/`
  - User Guide (PAK1): `ArchivePAK1/PAK1-User-Guide-1v0_01-Nov-2012.pdf`
  - Tube paper: `MorePAK2/PAK213-TriJmos-Triode-and-jFET-model-using-the-VDMOS-1v0.pdf`
  - Project outline: `MorePAK2/PAK-2021-Outline.pdf`
- **Keller plugin source**: `/home/niltempus/Downloads/hk jsfx v1.0.4 b/HK JSFX V1.0.4 B/`
- **Keller paper**: `/home/niltempus/Downloads/A Tube Amp Modeling Project V1.0.3.pdf`
- **Mačák thesis** (OPT models + listening test): `/home/niltempus/Documents/2026-04-03-PAK-Project/Real-time Digital Simulation of Guitar Amplifiers as Audio Effects.pdf`
- **Mačák/Holters/Schimmel 2012 DAFx** (spline-DK for parametric preamp): `/home/niltempus/Documents/2026-04-03-PAK-Project/Macak2012-Simulation_of_Fender_Type_Guitar_Preamp_using_Approximation_and_State-Space_Model.pdf`
- **Mačák NodalDKFramework** (MATLAB reference, Dempwolf-Zölzer triode in `triode.m`): `~/src/NodalDKFramework/`
  - License: non-commercial only, or GPL3 alternative. Commercial use requires contacting author.
  - Notable absences: no transformer code, pentode is a stub, `solve_nonlinear_func` referenced but not defined. Reference, not usable framework.
- **Conversation log**: `/home/niltempus/Documents/2026-04-03-PAK-Project/IansPAKproject/MorePAK2/dsp.txt`
- **Current native DSP**: `native/src/nilamp_dsp.c`
- **Current CLAP shell**: `native/src/nilamp_clap.c`
- **Current table generation**: `tools/gen_5e3_tables.py`
- **Current JSFX parity tools**: `tools/abx_compare.py`, `tools/compare_taps.py`, `tools/jsfx_render/`
