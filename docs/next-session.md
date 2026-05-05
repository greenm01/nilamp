# Next session — pre-T4 EQ regression confirmed in audio path

## SESSION LOG (most recent first)

### Session: PSS topology mismatch identified as runaway root cause → INCONCLUSIVE (analysis only, no edits)

**Hypothesis tested.**  Phase 0 established that JSFX's pre-T4 chain
(`*k1 → hp3 → peq1 → hs1`) is canonical and missing from Faust.
Phase 1' / Phase 1'' showed that adding it produces runaway (sweep
peak 32.6, sine residual −6.6 dB). Bisecting the chain element-by-
element was the planned next step.  Before running the bisection,
read the JSFX PSS feeding to check whether `total_dia` is fed
correctly.

**Finding.**  JSFX (twd_dlx_ii.jsfx:379-390) feeds three PSS lumps
asymmetrically:

```
dia1 = t4.dia + t5.dia                           // power tubes
dig  = kgrid * dia1                              // grid-current
dia3 = t1.dia + t2.dia + t3.dia                  // preamp tubes
dvs1 = p1.tube_pss_process(0,    p2.s, dia1)     // R=125 Ω,   τ=8 ms
dvs2 = p2.tube_pss_process(dvs1, p3.s, dig)      // R=5.1 kΩ,  τ=82 ms
dvs3 = p3.tube_pss_process(dvs2, 0,   dia3)      // R=22 kΩ,   τ=352 ms
```

Faust (nilamp.dsp:192-199) collapses to a single lump:

```
total_dia = res1_dia + res3_dia + res4_dia + res5_dia  // ALL tubes
tube_pss(r=22000, tau=0.05, snext=0, total_dia, old_dvs)
```

**Why this matters for the pre-T4 chain runaway.**  In JSFX, preamp
B+ (`dvs3`) is decoupled from instantaneous power-tube plate-current
swings: t4/t5 currents hit p1 (R=125 Ω, fast/small) and only the
kgrid-scaled grid current reaches p3 via the chain.  Preamp tubes
see a slow-moving B+.  In Faust, **all four stage currents pour
into one 22 kΩ / 50 ms lump**, so any boost to T4's drive (e.g. via
the missing `peq1+hs1`) directly modulates the B+ that T1/T3 see,
and the closed loop gains energy at the boost band → runaway.

This explains the Phase 1' diagnostic: switching the lump's r/τ to
p2 values (5100 / 0.0816) **halved** sweep peak (32.6 → 10.7) but
didn't stabilise — it tweaked the lump's poles but kept the wrong
topology.  Phase 1'' kgrid scaling on `total_dia` had no clean way
to reproduce the JSFX selectivity (different stages feed different
lumps in JSFX) and also regressed.

**Conclusion.**  Bisecting `*k1 → +hp3 → +peq1 → +hs1` won't reveal
new information.  The chain is canonical; its insertion is blocked
by a topology bug in the PSS, not a parameter or numerical issue.
The TODO at `dsp/nilamp.dsp:36` (`TODO(5e3-v2): split` the 3-stage
PSS) is the *prerequisite* for landing the pre-T4 chain.

**Suggested next-session plan.**

1. Port the 3-stage PSS from JSFX to Faust.  Need: `tube_pss` already
   exists and takes `snext` (the next lump's smoothed state), so the
   chaining shape is supported.  Add `p1`, `p2`, `p3` instances with
   the JSFX values (125 Ω/8 ms, 5.1 kΩ/82 ms, 22 kΩ/352 ms).  Wire
   `dia1 = res4_dia + res5_dia`, `dig = kgrid * dia1`, `dia3 =
   res1_dia + res3_dia` (Faust has no separate t2; the JSFX t2 is
   the second triode of the 12AX7 which Faust collapses into res1).
   Keep both `p1.s` and `p2.s` as 1-cycle-delayed feedback paths
   (Faust `~`).
2. ABX-gate the PSS-only change *without* the pre-T4 chain.  Public
   baseline (sine −16 dB, sweep −11.2 dB) should hold or improve.
3. If (2) passes, add the pre-T4 chain (`*k1 → hp3 → peq1 → hs1`)
   between `res4_v` and the T4 stage.  ABX-gate again.
4. If runaway returns, *then* bisect the chain — but at that point
   the runaway is no longer topology-driven so bisection is
   meaningful.

**Estimated cost.**  Step 1 is the work: `tube.tube_pss` already
takes `snext`, so it's plumbing in `nilamp.dsp` plus probably one
new state in the `~` feedback (currently a single `old_dvs`; will
need three: `old_dvs1`, `old_dvs2`, `old_dvs3` — and `p2.s` and
`p3.s` for the snext arguments).  ~1-2 hours of careful Faust
plumbing plus a build cycle (~1m44s) to ABX-gate.

**No code changes this session.**  Working tree clean.

### Session: Phase 0 reconciliation — Faust T4 anode chain is missing, not extra → INCONCLUSIVE (framing only)

**Hypothesis tested.**  After reading Keller's PDF text export
(`vendor/keller-jsfx/A Tube Amp Modeling Project V1.0.3.txt`, §7
lines 1426-1432) and re-reading `dsp/nilamp.dsp` and the canonical
JSFX `~/.config/REAPER/Effects/nilamp_abx/twd_dlx_ii.jsfx` lines
411-432, the suspicion that Faust contained extra peq/hs blocks not
in JSFX was wrong.

**Block-for-block reconciliation (mode 0, the ABX setting):**

JSFX T4 anode (jsfx:411-415):
`spl0 *= k1; hp3.process; peq1.process; hs1.process; t4.process`

Faust T4 anode (nilamp.dsp:165-171, *current* state):
`tube_ck_simple(... res4_v ...)` — the entire `*k1 → hp3 → peq1 → hs1`
chain is **absent**.  Phase 1' / Phase 1'' had added it; both were
reverted.  T4 currently sees `res4_v` raw.

JSFX T5 cathode (jsfx:419-427):
`aux = k2 * t3.vk; hp4.process; peq2.process; hs2.process; t5.process`

Faust T5 cathode (nilamp.dsp:175-187):
`res4_vk : *(k2_mode0) : flt_ii1_hp(hp4_hz) : flt_sv2_peq(kp1, fp_hz, qp1, 1, 1) : flt_sv1_hs(ks1, fs1_hz, 1)`
— matches JSFX block-for-block.

JSFX post-tube (jsfx:428-432):
`spl0 -= aux; peq3.process; hs3.process; hp5.process; lp2.process`

Faust post-tube (nilamp.dsp:208-213):
`post_pp : flt_sv2_peq(kp2, fp_hz, qp2, 1, 1) : flt_sv1_hs(ks2, fs2_hz, 1) : flt_ii1_hp(40) : flt_df2_lp(10000, sqrt(0.5), 1, 0)`
— matches JSFX (peq3 uses kp2/qp2; hp5=40; lp2=10kHz/Q=sqrt(0.5)).

**PDF context (§7 lines 1426-1432).**  Keller's PDF describes the
post-T3 chain as `100 nF coupling cap → k1 → HP3 (5.8 Hz)`.  The peq1
and hs1 blocks in JSFX correspond to the loudspeaker-impedance
pre-shaping mentioned in lines 1438-1442 ("the first stage is located
ahead of the power amp tubes"), not to the simplified §7 block diagram.
This means JSFX is canonical and the peq1/hs1 are not bugs — they
*are* the loudspeaker pre-shaping.

**Mode 0 numerical params** (jsfx:293-299, harness slider defaults):
- k1=0.797, k2=0.940
- hp3=5.8 Hz, hp4=6.4 Hz
- p.fp=38 dBHz → fp ≈ 80 Hz; p.qp=6 dB → qp ≈ 2 (then peq1 uses qp1)
- p.gp_pre=1 dB → kp1 ≈ 1.122; p.gp_post=2 dB → kp2 ≈ 1.259
- p.gs_pre=3 dB → ks1 ≈ 1.413; p.gs_post=3 dB → ks2 ≈ 1.413
- p.fs=62 dBHz → fs ≈ 1259 Hz; with sqrt-scaling fs1≈2098, fs2≈1486
- All match `nilamp.dsp:69-80` (`k2_mode0`, `hp4_hz`, `kp1`, `kp2`, `fp_hz`,
  `qp1`, `qp2`, `ks1`, `ks2`, `fs1_hz`, `fs2_hz`).  The constants Faust
  needs to add for the T4 path are `k1_mode0 = 0.797` and `hp3_hz = 5.8`.

**Conclusion.**  The pre-T4 chain is canonical and *should* be there.
Phase 1' / Phase 1'' added it as a textual block and produced runaway
(sweep peak A=10.7 / 0.385).  Naively re-adding it without further
investigation is **not** the next step.  The runaway must come from
*how* Faust feeds the chain (input level, DC offset of `res4_v` differing
from JSFX `t3.va`, or PSS feedback amplifying when k1+HP3 inserts a
delay/phase shift).

**Next.**  Targeted bisection:
1. Add T4 chain elements one at a time (k1 alone → +hp3 → +peq1 → +hs1),
   ABX gate after each.  Identifies which element triggers the runaway.
2. If even k1=0.797 alone (a static gain < 1 reducing T4 drive) regresses,
   that's strong evidence the bug is upstream (T3 plate DC vs JSFX, or
   PSS coupling to T4).
3. If runaway only appears with hp3 (the HP filter), check Faust
   `flt_ii1_hp` impulse response vs JSFX at fc=5.8 Hz; the JSFX HP1/HP2
   are already in production at 10 Hz / 0.41 Hz so the implementation
   is presumably correct, but verify.

### Session: build-time reduction (feature-gate test/diagnostic DSPs) → SUCCESS

**Hypothesis tested.**  Cold `cargo build --release` was ~12 minutes
because `build.rs` shells out `faust -lang rust` for all 20 .dsp files
even when only `dsp/nilamp.dsp` is needed by the production binary.
Gating tests and diagnostics behind Cargo features should slash cold
build time without affecting audio output.

**Edits (commits `3b2523a`, `6bdabce`, plus user-side `37ae8dd`):**
- `Cargo.toml`: added `[features]` (`dsp-tests`, `dsp-diagnostics`,
  `default = []`); added `required-features = ["dsp-diagnostics"]` to
  `nilamp_t5_balance_render` and `nilamp_drive_probe_render`; added
  `[profile.release-fast]` (lto=off, opt-level=2, codegen-units=16).
- `build.rs`: feature-gated the `dsp/tests/*.dsp` and
  `dsp/diagnostics/*.dsp` loops; emit `// skipped` stub `.rs` when the
  feature is off so any stale `include!()` still resolves.  Added
  `cargo:rerun-if-env-changed` for both feature env vars.
- `tests/regression.rs`: prepended `#![cfg(feature = "dsp-tests")]`.
- `AGENTS.md`: new project-conventions doc for AI coding agents.
- `README.md`: documented Cargo features and release-fast profile.

**Measurements.**

| scenario                                | release | release-fast |
|-----------------------------------------|--------:|-------------:|
| cold full build (default features)      |   1m44s |        1m41s |
| incremental, src/lib.rs touched         |    2.6s |         0.9s |
| incremental, dsp/nilamp.dsp touched     |       — |        1m41s |
| (old) cold build, all 20 DSPs           | ~12 min |            — |

**Verdict.**  Cold build ~12 min → ~1m44s (~7× faster); src-only
incremental 2.6s → 0.9s with release-fast.  `release-fast` does NOT
help DSP-edit rebuilds (rustc frontend on the 639 KB dsp.rs dominates).
**No DSP behaviour changed**: SHA-256 identical sine-440 Hz and
log-sweep WAVs through `nilamp_render` between `release` and
`release-fast`.  All 23 regression tests pass with `--features
dsp-tests`.  Without the feature, `cargo test` is a fast no-op.

**Deferred.**  `mold` linker (not installed system-wide); static
table extraction from dsp.rs (would require parsing generated Rust
or splitting the .dsp; not justified by current bottleneck).

### Session: Phase 1'' Variant A (uniform kgrid scale + pre-T4 EQ) → REGRESS

**Hypothesis tested.**  Phase 1'' Variant A from prior plan: keep PSS at
baseline (r=22000, tau=0.05) and multiply `total_dia` by `kgrid=0.025`
on the theory that JSFX's kgrid (twd_dlx_ii.jsfx:174, 380, 389) is the
dominant scale factor missing from Faust's collapsed single-stage PSS.

**Edit (since reverted, working tree clean):**
- Added `k1_mode0=0.797`, `hp3_hz=5.8` near line 70.
- Added `drive_t4 = res4_v : *(k1_mode0) : flt_ii1_hp(hp3_hz) :
  flt_sv2_peq(kp1, fp_hz, qp1, 1, 1) : flt_sv1_hs(ks1, fs1_hz, 1)` and
  fed it (not `res4_v`) into the T4 stage.
- Replaced `total_dia = res1_dia + res3_dia + res4_dia + res5_dia;` with
  `kgrid_pss = 0.025; total_dia = (sum) * kgrid_pss;`.
- `r_pss = sag*22000.0` and `tau_pss = 0.05` left at baseline.

**Result vs prior data points:**

| run                                  | r_pss | tau    | EQ  | dia scale | sine resid | sweep peak A | sweep RMS A | verdict |
|--------------------------------------|------:|-------:|-----|----------:|-----------:|-------------:|------------:|---------|
| public baseline                      | 22000 | 0.05   | no  |   1.0     | -16.0 dB   | 0.42         | 0.18        | works   |
| EQ only                              | 22000 | 0.05   | yes |   1.0     | -6.6 dB    | 32.6         | ~0.99       | runaway |
| EQ + p2 PSS                          | 5100  | 0.0816 | yes |   1.0     | -6.8 dB    | 10.7         | 1.15 burst  | runaway |
| **EQ + kgrid (Variant A)**           | 22000 | 0.05   | yes |   0.025   | **-8.3 dB**| **0.385**    | **0.034**   | **REGRESS** |

Sweep peak no longer runs away (0.385 vs JSFX 0.472, same order of
magnitude).  But sweep RMS A is **7.6× quieter than JSFX** (0.034 vs
0.258) and sine residual regressed from baseline -16 dB to -8.3 dB.

**Critical observation: same silence pattern as Phase 1'.**  Sweep
50 ms RMS profile (`/tmp/abx_compare/sweep_nilamp.wav`):

| t [s] | f [Hz]  | nilamp RMS | jsfx RMS | nilamp/jsfx |
|------:|--------:|-----------:|---------:|------------:|
| 0.000 |    20.0 |    0.1358  |  0.1687  | 0.81 (transient) |
| 0.200-2.800 | 26-1030 | 0.0000-0.004 | 0.28-0.30 | <0.02 |
| 3.000 |  1365.1 |    0.1160  |  0.3080  | 0.38 |
| 3.200 |  1809.0 |    0.0991  |  0.3101  | 0.32 |
| 3.400 |  2397.3 |    0.0657  |  0.3068  | 0.21 |
| 3.600+ | >3 kHz |    <0.005  |  0.05-0.29 | <0.04 |

Faust is essentially silent across 26-1000 Hz (all preamp/baseband)
and >3 kHz.  Only 1-3 kHz region (where pre-T4 EQ chain's hs1 shelf
is transitioning from flat to +3 dB at 2098 Hz) produces any
meaningful output.  Sine 440 Hz happens to fall in a "kept alive"
basin and produces RMS 0.155 (vs JSFX 0.291), decaying from 0.21
at t=0 to 0.155 by t=2.5s -- bias point drifting.

**Conclusion.**  The "kgrid is dominant scale" hypothesis is **wrong**.
Phase 1' (lower r) and Phase 1'' (lower dia) both reduce dvs magnitude,
both stop the runaway, but **neither restores normal operation**.  The
silence pattern is identical: tubes biased far past cutoff except in a
narrow transient band.

The pre-T4 EQ chain (`*k1 : hp3 : peq1 : hs1`) does something to
`res4_v` that drives the T4 operating point into cutoff regardless of
PSS scaling.  Suspects:

1. **`hp3` (5.8 Hz high-pass) strips T3's plate DC.**  T3 (cathodyne)
   has a large-magnitude DC plate voltage that biases T4's grid in
   class-AB.  Removing that DC could push T4's grid voltage out of its
   normal operating range -- exactly what we hypothesized in commit
   c3aa9a0 but I dismissed last session.  JSFX uses the same hp3=5.8 Hz
   so this would be a Faust-only sensitivity, but JSFX may have a DC
   compensation elsewhere (an offset added back, or T4's bias resistor
   network differing).
2. **`*k1=0.797` attenuation alone shifts the operating point.**
   res4_v scaled by 0.797 changes the AC swing seen by T4; combined
   with the unchanged DC bias path (in JSFX) might restore balance.
3. **`peq1` or `hs1` filter init transient** kicks the system into a
   bad basin from which it doesn't recover.  Less likely; sine works
   at 440 Hz, which is in peq1's response region.

**Action: revert; do not propose 3-stage PSS port until DC/operating-
point hypothesis is investigated separately.**  Working tree is clean.

**Recommended next session work:**
1. Compare Faust's `res4_v` waveform (T3 plate output) DC level vs
   JSFX's equivalent.  If JSFX's T3 plate DC is much smaller (or zero),
   then JSFX has implicit DC removal Faust lacks, and porting hp3 in
   isolation breaks Faust.  Existing diagnostic harness has `res4_v`
   tap; just need to dump it before pre-T4 EQ on JSFX side.
2. Test pre-T4 EQ chain **without `*k1` first**, then **without `hp3`**,
   to isolate which element causes the cutoff shift.  Two builds,
   each ~12 min (or more like 2-3 min if we gate the diag DSPs).
3. Consider whether the build-time problem is worth fixing first
   (gating `dsp/diagnostics/*.dsp` and `dsp/tests/*.dsp` behind
   features/env vars) -- would cut iteration cost by ~5-10x.

---

### Session: Phase 1' (PSS p2 values + pre-T4 EQ) → FAIL

**Hypothesis tested.**  Phase 1' from prior session's plan: keep Faust's
single-stage PSS but retune to JSFX's `p2` values (r=5100, tau=0.0816,
1.95 Hz pole) on the theory that the pre-T4 EQ destabilization came from
Faust's stiffer 3.18 Hz pole + 4.3× higher source impedance.  Combined
with adding the pre-T4 EQ chain (`drive_t4 = res4_v : *k1_mode0 : hp3 :
peq1 : hs1`) in one patch.  Build cost ~12 min.

**Edit (since reverted, working tree clean):**
- `dsp/nilamp.dsp:37`  `r_pss = sag * 22000.0` → `sag * 5100.0`
- `dsp/nilamp.dsp:38`  `tau_pss = 0.05` → `0.0816`
- Added `k1_mode0 = 0.797`, `hp3_hz = 5.8` constants.
- Added `drive_t4` chain and fed it (not `res4_v`) to the T4 stage.

**Result vs prior data points:**

| run                                  | r_pss | tau    | EQ  | sine resid | sweep peak A | sweep RMS A | verdict |
|--------------------------------------|------:|-------:|-----|-----------:|-------------:|------------:|---------|
| public baseline (commit before)      | 22000 | 0.05   | no  | -16.0 dB   | 0.42         | 0.18 cont.  | works   |
| prior session: EQ only               | 22000 | 0.05   | yes | -6.6 dB    | 32.6         | ~0.99       | runaway |
| **this session: EQ + p2 PSS**        | 5100  | 0.0816 | yes | -6.8 dB    | 10.7         | 1.15 burst  | runaway |

PSS retuning halved sweep peak (32.6 → 10.7) but did not stabilize.
Sine residual unchanged.

**Critical new observation: Faust output is silent for the bulk of the
sweep.**  50 ms RMS profile of `/tmp/abx_compare/sweep_nilamp.wav`:

- t=0.000 (f=20 Hz)         RMS 0.7986   peak 1.701   ← startup transient
- t=0.200-2.800 (26-1030 Hz) RMS ~0.0001 peak ~0.000  ← total silence
- t=3.000 (f=1365 Hz)        RMS 1.1531  peak 10.744  ← runaway burst
- t=3.200 (f=1809 Hz)        RMS 0.1402  peak 1.607   ← ringing decay
- t=3.400+ (>2.4 kHz)        RMS ~0.001  peak ~0.01   ← silence

JSFX same input shows continuous RMS 0.17-0.31 throughout.

**Conclusion:** the runaway is not "loop oscillation in a band of the
sweep".  Faust is producing essentially zero output across the entire
20-1000 Hz steady-state range, then a single transient burst at the
1.3-1.8 kHz region.  This pattern is consistent with **operating point
collapse**: dvs grows so large in steady state that all tubes are
biased into hard cutoff; only fast transients (sweep onset, sweep
crossing the hs1 high-shelf transition near 2 kHz) momentarily kick
the system out of cutoff and produce output.

**Why r=5100 helped less than expected:**  reducing r reduces dvs
sensitivity to dia, but Faust's `total_dia = res1+res3+res4+res5`
sums **preamp + power-tube** dia into the same single PSS node.  In
JSFX's p2 stage, the dia entering the multiplier is
`dig = kgrid * dia1` with `kgrid = 0.025` and `dia1 = t4.dia + t5.dia`
only — so JSFX's effective scale of "power-tube dia × p2.r" is
`0.025 * 5100 = 127.5`, while Faust at r=5100 with all four tubes'
dia summed has effective scale `5100 * 1` (no kgrid) = ~40× larger.
That extra DC bias is what's driving tubes into cutoff.

**Implication:** the `kgrid = 0.025` scale factor I had previously
classified as "2nd-order, defer" (in prior session log) is actually
**the dominant scale factor of the power-tube supply**.  Without it,
no single-stage PSS retune will stabilize the audio path with EQ.

**Next steps left for next session (no edits this session):**
1. Try Phase 1'' (cheap): revert PSS to baseline (r=22000, tau=0.05),
   keep pre-T4 EQ, divide `total_dia` by ~40 (= 1/kgrid) entering the
   PSS.  One-line edit, one rebuild.  Tests "is the issue dia magnitude
   alone?".
2. Or go directly to Phase 1: full 3-stage PSS port matching JSFX
   (p1/p2/p3 cascaded, separate dvs2 for T4/T5 vs dvs3 for T1-T3,
   `dig = kgrid * dia1`).  More work but most likely correct.
3. Working tree is clean; revert is already in place.

---

### Session: JSFX-faithful pre-T4 chain in public audio path → FAIL

**Goal:** test whether public's `-16 dB sine / -11.2 dB sweep` residual
vs JSFX is caused by Faust public missing JSFX's pre-T4 EQ chain
(`*k1 -> hp3(5.8) -> peq1 -> hs1`).

**Discovery before testing:** JSFX harness `twd_dlx_ii.jsfx:411-414`
applies the v6/v10-style pre-T4 chain. JSFX `tube_ck_process`
(`HK_LIB_TUBE.jsfx-inc:81-111`) is structurally identical to Faust
`tube_ck` (`dsp/hk_tube.lib:15-37`). JSFX `peq1`/`hs1` and `peq2`/`hs2`
share the same constants (`kp1, fp, qp1` and `ks1, fs1`) per JSFX
lines 251-252, 265-266. `k1=0.797`, `hp3=5.8 Hz` for mode 0
(`twd_dlx_ii.jsfx:293, 298`).

**Reframe of c3aa9a0:** the "DC-stripping breaks bias loop" framing in
that commit is mechanically incomplete. The averager closes its loop
on the **output** voltage (`v_out - dvs`), not the input. Stripped
input DC doesn't break the feedback; it shifts the operating point.
JSFX uses `hp3=5.8 Hz` and works fine, so the v6/v10 numerical
divergence I observed in the diagnostic must come from somewhere
other than "averager can't see DC".

**Edit applied to `dsp/nilamp.dsp`:** added `k1_mode0=0.797`,
`hp3_hz=5.8`; inserted `drive_t4 = res4_v : *(k1_mode0) :
flt_ii1_hp(hp3_hz) : flt_sv2_peq(kp1, fp_hz, qp1, 1, 1) :
flt_sv1_hs(ks1, fs1_hz, 1)` and fed `drive_t4` (not `res4_v`) to T4.
Mirrors the existing pre-T5 `aux_in` chain.

**Build:** `cargo build --release --bin nilamp_render` (12 min).

**ABX result:**

| Test | Public baseline | Public + pre-T4 chain |
|---|---:|---:|
| Sine 440 Hz residual | -16.0 dB | **-6.6 dB** (9 dB worse) |
| Sweep peak A / B | 0.4195 / 0.3698 | **32.6 / 0.47** (70× peak blowup) |

**Verdict:** experiment FAILED. Adding the chain widened the gap and
caused massive sweep-peak runaway. **Reverted.**

**Mechanism candidate:** the public audio path runs T4 inside the
global PSS sag loop with `total_dia = res1_dia + res3_dia + res4_dia
+ res5_dia`. The pre-T4 EQ changes `res5_dia` (T4 plate current draw)
in a frequency-dependent way; this couples back through PSS and may
oscillate or drive runaway gain at the EQ peak frequency. The
diagnostic harness `nilamp_drive_taps.dsp` deliberately *excludes*
v13/v15 T4 dia from PSS feedback (line 305: `total_dia_current =
res1_dia + res3_dia + res4_dia + t4_dia_public`) -- only public T4
loops back. So v13's "0.3 V post-tube DC delta vs public" was measured
under PSS isolation, which the audio path does not provide.

**Pre-existing warning underweighted:** `dsp/nilamp.dsp:205-207`:
> "The pre-T4 backend chain remains diagnostic-only until its sweep
> regression is isolated."
This is the regression. Independently rediscovered.

### Reframed conclusions

1. **Faust public's `-16 dB / -11.2 dB` residual vs JSFX is NOT just
   "missing pre-T4 EQ".** Adding the EQ alone makes it worse, much
   worse. There is a coupling problem between pre-T4 EQ + T4 dia +
   PSS sag loop that JSFX either lacks or compensates for differently.

2. **The c3aa9a0 / 007dcb7 commits' findings about v13 still stand
   *within the diagnostic harness*** (T4 voltage at fixed PSS),
   but their relevance to the public audio path is now in question.
   v13 may still be a useful comparison point, but only after the
   pre-T4-EQ-vs-PSS coupling is understood.

3. **The next-session.md prior content (below) is preserved for
   continuity but its "v13 is the right structural fix" claim should
   be read with caution.** v13 has not been tested in a configuration
   that includes PSS feedback from T4 dia (the audio path).

### Open questions for next session

1. **Does JSFX's PSS implementation differ?** Worth comparing
   `tube_pss` Faust vs JSFX. The pre-T4 EQ + PSS coupling that
   destabilizes Faust may be tamed in JSFX by a different sag
   topology (e.g. JSFX may not include T4 dia in its PSS feedback,
   or may use different `r_pss / tau_pss`).

2. **Can the pre-T4 chain be added without including T4 dia in PSS?**
   I.e. add the chain but keep `total_dia = res1_dia + res3_dia +
   res4_dia` (drop `res5_dia` from the sum). This would isolate
   whether the regression is "EQ + T4-into-PSS" or "EQ + something
   else in T4."

3. **Is the regression at a specific frequency?** Sweep blew up to
   peak 32.6; useful to render the post-pre-T4-chain Faust on an
   actual sweep recording and inspect where the peak occurs in the
   sweep. If at ~80 Hz (peq1 center) or ~2098 Hz (hs1 corner), it
   confirms EQ-driven runaway. Bash-level: `python3 -c "..."` to find
   peak sample index, divide by sr, map to instantaneous sweep
   frequency.

4. **Is `flt_sv2_peq` Faust signature truly equivalent to JSFX
   `flt_sv2_set_peq`?** I assumed yes based on the working pre-T5
   chain, but the pre-T5 chain is fed by `res4_vk` (T3 cathode, much
   smaller magnitude than T3 plate `res4_v`). The pre-T4 chain is fed
   by the much-higher-amplitude T3 plate. Possibly the filter is
   numerically unstable at that input scale.

5. **What about `flt_ii1_hp` at 5.8 Hz at sr=48000?** Pole near unity
   (`tau ~= 27 ms`). Combined with PSS feedback this could be a
   long-time-constant integrator with positive loop gain. Worth
   isolating.

### Files touched this session
- (no commits) — `dsp/nilamp.dsp` was edited then reverted.
- `docs/next-session.md` (this file) prepended with session log.

### What was actually committed across the broader work span
- `75e082f` docs: write up v13/v15 results and DC-coupling fix verification
- `007dcb7` test(dsp): v13/v15 confirm DC-bias hypothesis at T4
- `c3aa9a0` test(dsp): localize T4 divergence to DC bias stripped by hp3/hp4
- All of these now flagged as "framing too confident; mechanism may be
  different; v13's success is PSS-isolated and may not transfer."

---

## ORIGINAL CONTENT (prior session — read with caution)

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
