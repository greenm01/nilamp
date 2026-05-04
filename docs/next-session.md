# Next session — port ABX-safe T5 branch with current sag loop

## State of the tree

As of committed base `5603984` plus the current diagnostic work, branch
`main` is 8 commits ahead of `origin/main`.  T5 table/constants are
generated and committed.  Isolated T5, synthetic T4/T5 power-pair
diagnostics, frozen-default top-level taps, and T5 balance ABX variants now
pass far enough to identify a safe public-path candidate.

Recent commits (newest last):
- `d98f17a` — HP5 40 Hz output subsonic blocker
- `1f42a49` — harness gin equalization, `--input-scale`, WAV helpers
- `0d6f549` — tone-stack centre 500 → 630 Hz (`iso266(56)`)
- `30c36cd` — investigation notes appended to `docs/notes/abx-harness.md`
- `7dab2c3` — generated T5 6V6 table/constants and committed this handoff doc
- `bb8458a` — isolated T5 and synthetic power-pair diagnostics
- `aded13e` — frozen-default top-level power-stage tap comparison
- `5603984` — documented the failed public-path T5 attempt

22/22 regression tests pass; `cargo build --release --bin nilamp_render` clean.

Current baseline ABX at `gain=+6, defaults` is unchanged:

| Test | Current baseline |
|---|---|
| 440 Hz sine RMS residual | −13.4 dB |
| 5 s log-sweep RMS residual | −9.2 dB |
| Sweep peak A / B | 0.366 / 0.370 |
| Sweep align lag | 1 sample |

## What was attempted this session

Ported the JSFX back-end chain (mode==0 / ltp==0, lines 395–434 of
`twd_dlx_ii_harness.jsfx`) into `dsp/nilamp.dsp` as a single coupled block,
per the carryover plan:

- Pre-T4: `hp2(0.41) → t3 → *k1=0.797 → hp3(5.8) → peq1(80 Hz, +1 dB, Q=2.67) → hs1(2.1 kHz, +3 dB)`
- Post-T4: `peq3(80 Hz, +2 dB, Q=2.24) → hs3(1.5 kHz, +3 dB) → hp5(40 Hz) → lp2(10 kHz Butterworth)`

Coefficient derivations verified against `HK_LIB_TOOLS.jsfx-inc:26-49`
(`iso266`) and `twd_dlx_ii_harness.jsfx:243-268`:

| JSFX symbol | derivation | value |
|---|---|---|
| `fp`  | `iso266(p.fp=38)`         | 80 Hz |
| `qp`  | `iso266(p.qp=6)`          | 2.0 |
| `fs`  | `iso266(p.fs=62)`         | 1250 Hz |
| `kp1` | `10^(0.05 * 1)`           | 1.1220184543 |
| `kp2` | `10^(0.05 * 2)`           | 1.2589254118 |
| `ks1` = `ks2` | `10^(0.05 * 3)`   | 1.4125375446 |
| `qp2` | `qp * sqrt(kp2)`          | 2.2440931043 |
| `qp1` | `qp2 * sqrt(kp2 * kp1)`   | 2.6685237666 |
| `fs2` | `fs * sqrt(ks2)`          | 1485.8089753 Hz |
| `fs1` | `fs2 * sqrt(ks2 * ks1)`   | 2098.1359672 Hz |
| `k1`  | hard-coded mode==0        | 0.797 |
| `hp2` | hard-coded mode==0        | 0.41 Hz |
| `hp3` | hard-coded mode==0        | 5.8 Hz |

Build clean, 17/17 tests pass. ABX measurements at `gain=+6, defaults`:

| Test | Pre-edit baseline | Post-edit | Δ |
|---|---|---|---|
| 440 Hz sine RMS residual | −13.4 dB | **−10.9 dB** | regression |
| 5 s log-sweep RMS residual | −9.2 dB | **−7.7 dB** | regression |
| Sweep peak A / B | 0.366 / 0.370 | 0.310 / 0.370 | A drops |
| 440 Hz align lag | 0 sample | 217 sample | new phase rotation |

Both per-tone and wideband residuals regressed. Same regression pattern as
the earlier piecemeal Scope-1.5 attempt → "single coupled block" was *not*
the missing ingredient.

**Edit was reverted.** `git checkout dsp/nilamp.dsp` brought the tree back to
the `30c36cd` baseline.

## Why it failed: the T5 push-pull aux-subtract is on the critical path

Looking at `twd_dlx_ii_harness.jsfx:411-428`:

```
spl0 *= k1;                         # post-T3 attenuator
spl0 = hp3.flt_ii1_process_hp(spl0);
spl0 = peq1.flt_sv2_process(spl0);
spl0 = hs1.flt_sv1_process(spl0);
spl0 = t4.tube_ck_process(spl0, dvs2);   # T4 (one half of 6V6 pair)

aux = k2 * t3.vk;                   # opposite-polarity tap from T3
aux = hp4.flt_ii1_process_hp(aux);
aux = peq2.flt_sv2_process(aux);
aux = hs2.flt_sv1_process(aux);
aux = t5.tube_ck_process(aux, dvs2);     # T5 (other half of 6V6 pair)
spl0 -= aux;                        # *** subtractive push-pull mix ***
```

JSFX produces `(t4_out − t5_out)` at the post-T4 node. Faust currently
produces just `t4_out`. The output normalization
`gout = 0.5 / (t4.rl*t4.isat + t5.rl*t5.isat) = 0.5/690` is calibrated for
the *difference* of two power tubes, not a single one.

The previous Faust port happened to match JSFX peak at 440 Hz despite all
these missing pieces because:

- Missing T5 subtract: ~−6 dB at output node (one tube instead of two)
- Wrong gout denom (used 330 instead of 690): +6 dB at output node
- Missing PEQ/HS lift (~+3 to +6 dB above 1 kHz): −3 to −6 dB
- Missing `*k1` attenuation: +2 dB

These cancelled at mid frequencies. Adding back-end EQ stages without
also adding T5 *unbalances* the cancellation. The right move is to land
T5 first (which restores the gain-staging baseline JSFX assumes), then
re-attempt the back-end EQ chain on top.

## Diagnostic work now added

T5 table/constants are now done:

- `tools/gen_5e3_tables.py` defines `T5_6V6`.
- `dsp/5e3_tables.lib` exports `t5_6v6_table`.
- `dsp/5e3_constants.lib` exports `c.t5_*`.

The tree now has these diagnostics:

- T5 ADNL coverage through `dsp/tests/test_adnl_t5_6v6.dsp`.
- Full T5 `tube_ck_simple` coverage through `dsp/tests/test_tube_ck_t5.dsp`.
- A focused T4/T5 branch harness through
  `dsp/tests/test_power_pair_t5.dsp`, driven by synthetic T3 plate/cathode
  taps and checking `t4_v`, `t5_v`, `post_pp`, and `total_dia`.
- A frozen-default top-level tap harness through
  `dsp/tests/test_nilamp_taps.dsp`, checking real cascade taps:
  `old_dvs`, `t3_v`, `t3_vk`, current `t4_v`, diagnostic `t5_v`,
  diagnostic `post_pp`, current/with-T5 dia totals, and current
  `next_dvs`.

These pass against the Python Keller oracle.  That narrows the failed
top-level attempts away from the generated T5 table/constants, away from
the basic T4/T5 branch equations in isolation, and away from the current
Faust top-level tap math at frozen ABX defaults.

Two top-level wiring attempts were tried and reverted because they regressed
ABX:

| Attempt | 440 Hz sine residual | Result |
|---|---:|---|
| T5 aux branch + full T4/T5 `gout` only | −12.7 dB | regression |
| T5 plus full backend EQ/HP/LP block | −10.2 dB | worse regression |

After `aded13e`, the T5 aux branch was wired into `dsp/nilamp.dsp` as
`res5_v - res_t5_v`, T5 dia was added to `total_dia`, and the output
normalization switched to the full T4+T5 denominator.  The edit required
raising Faust's compile timeout to build, then ABX regressed:

| Attempt after tap diagnostic | 440 Hz sine residual | 5 s sweep residual | Sweep peak A / B |
|---|---:|---:|---:|
| T5 aux branch + full T4/T5 `gout` | −12.7 dB | −8.9 dB | 0.2905 / 0.3698 |

That edit was reverted from `dsp/nilamp.dsp` and `build.rs`.

Conclusion: the tap math is not the blocker, but the public T5 branch is
still not ABX-safe.  Do **not** retry the same public-path T5-only patch.
Follow-up diagnostics added a separate `nilamp_t5_balance_render` path with
five variants.  ABX results:

| Variant | 440 Hz sine | 5 s sweep | Note |
|---|---:|---:|---|
| `v0_current` | −13.4 dB | −9.2 dB | reproduces baseline |
| `v1_raw_t4_filtered_t5` | **−15.5 dB** | **−9.8 dB** | best candidate |
| `v2_filtered_t4_filtered_t5` | −15.1 dB | −9.4 dB | also improves |
| `v3_sign_add` | +4.6 dB | +2.8 dB | wrong sign/phase |
| `v4_half_denom_control` | −7.6 dB | −9.2 dB | wrong gain |

Conclusion: the earlier public-path T5 regression came from changing the sag
feedback current to include T5 dia at the same time.  The ABX-safe candidate
is `res5_v - res_t5_v` with the full T4+T5 output denominator, while leaving
the PSS `total_dia` on the current T4-only path for now.

## Next steps (in order)

### 1. Keep T5 table/constants as done

No further generator work is needed unless the JSFX T5 parameters are found
to be wrong.

### 2. Port the ABX-safe T5 branch

In `dsp/nilamp.dsp`, after the current T4 invocation, add the `v1` branch:

In `dsp/nilamp.dsp`, after the T4 invocation:

```
// Aux branch: T5 driven by k2 * t3.vk through its own EQ.
// Mirrors twd_dlx_ii_harness.jsfx:419-427.
aux_in = res4_vk : *(k2_mode0)
                 : flt.flt_ii1_hp(hp4_hz)            // 6.4 Hz
                 : flt.flt_sv2_peq(kp1, fp_hz, qp1, 1, 1)
                 : flt.flt_sv1_hs (ks1, fs1_hz, 1);

res_t5 = tube.tube_ck_simple(
    TBL_SIZE, t5_table, XMAX, DX,
    c.t5_kpre, c.t5_isat, c.t5_rl, c.t5_kpk,
    c.t5_kspre, c.t5_kspost, c.t5_ksva, c.t5_ksib, c.t5_kfb,
    c.t5_pk_xth, c.t5_pk_xdiode, c.t5_pk_k1, c.t5_pk_k2,
    c.t5_avg_f,
    aux_in, old_dvs);
res_t5_v   = res_t5 : _ , !;
res_t5_dia = res_t5 : ! , _;

// Push-pull subtract.  twd_dlx_ii_harness.jsfx:428: spl0 -= aux.
post_pp = res5_v - res_t5_v;
```

Then feed `post_pp` into the output HP5 and switch `gout` to:

```
0.5 / (c.t4_rl*c.t4_isat + c.t5_rl*c.t5_isat)
```

Constants needed inline (mode==0): `k2_mode0 = 0.940`, `hp4_hz = 6.4`.

Do **not** add `res_t5_dia` to `total_dia` in this commit.  Keeping the
current T4-only sag loop is what made `v1_raw_t4_filtered_t5` ABX-safe.

### 3. Re-attempt full back-end chain after public T5 is committed

After public T5 is ABX-stable, layer in pre-T4 (hp2, k1, hp3, peq1, hs1)
and post-T4 (peq3, hs3, lp2) chain pieces. Keep T5 dia/PSS changes separate;
the failed public attempt shows that sag feedback needs its own diagnostic.

Switch the gout denom to the full `0.5 / (t4.rl*t4.isat + t5.rl*t5.isat)`
when committing the T5 stage — the existing `0.5/(c.t4_rl*c.t4_isat)`
was a half-denom hack to compensate for missing T5.

### 4. Verify and commit

For each commit:
1. `cargo build --release --bin nilamp_render` clean.
2. `cargo test --release` — 22/22 still pass.
3. `python3 tools/abx_compare.py /tmp/sine_440.wav --gain 6 --label sine440_<step> --jsfx-timeout 120`
4. `python3 tools/abx_compare.py /tmp/sweep_5s.wav --gain 6 --label sweep_<step> --jsfx-timeout 120`
5. RMS residual MUST be at least as good as the pre-edit baseline (sine
   −13.4 dB, sweep −9.2 dB) — any regression triggers immediate revert.

Suggested commit slicing from here:
1. `test(dsp): add T5 branch-balance diagnostic variants`
2. `feat(dsp): port ABX-safe T5 push-pull aux branch`
3. `feat(dsp): port JSFX back-end EQ chain`
4. `test(dsp): diagnose T5 dia/PSS feedback separately`

## Files to read first

- `dsp/nilamp.dsp` — `process()` body, T4 block at lines 150–158, output
  at lines 170–188.
- `dsp/5e3_constants.lib` — T4 constants pattern at lines 59–60 and
  surrounding; T5 will mirror.
- `dsp/5e3_tables.lib` — locate `t4_6v6_table` export.
- `tools/gen_5e3_tables.py` — table generator entry point.
- `dsp/tests/test_tube_ck_t5.dsp` — isolated T5 diagnostic.
- `dsp/tests/test_power_pair_t5.dsp` — isolated T4/T5 branch diagnostic.
- `dsp/tests/test_nilamp_taps.dsp` — frozen-default top-level tap diagnostic.
- `~/.config/REAPER/Effects/nilamp_abx/twd_dlx_ii_harness.jsfx` — lines
  295–296 (t4/t5 set), 411–428 (chain).
- `~/.config/REAPER/Effects/nilamp_abx/HK_LIB_TUBE.jsfx-inc` — line 33
  (`tube_ck_set` arg map).
- `docs/notes/abx-harness.md` — full investigation log incl. gain-budget
  table.

## Open uncertainties

- Whether JSFX's runtime branch sign/phase and Faust's planned T5 wiring
  agree once the public audio path uses `res5_v - res_t5_v`.
- T5 NEQ coefficients in JSFX line 296 use `neq = 0` (same as T4 mode==0).
  Identity NEQ (1,0,0,0,0) → no extra biquad needed; matches current T4
  treatment. Confirmed by reading current T4 invocation.
- `tube_ck_simple` averaging-filter constant `t5_avg_f` now matches the
  generator/oracle path closely enough for isolated T5 diagnostics, but may
  still be worth revisiting if real top-level taps expose a sag-timing issue.

## Decisions carried forward

- ABX target: −60 dB RMS residual / <1e-3 abs per sample. Not bit-exact.
- T5 push-pull is now in scope (was: deferred to "5e3-v2").
- 3-stage PSS chain remains deferred to v2 — single PSS lump is
  acceptable for residual targets per current evidence.
- ADNL post-EQ remains identity (1,0,0,0,0) — deferred to v2.
- Mode pin: ABX always uses mode==0 (CD 5E3, ltp==0).
- HP5 (`d98f17a`) and tone-stack centre 630 Hz (`0d6f549`) fixes stay.
