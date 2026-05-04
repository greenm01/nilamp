# Next session — back-end chain blocked on T5 push-pull port

## State of the tree

Branch `main`, 4 commits ahead of `origin/main`, working tree clean.

Recent commits (newest last):
- `d98f17a` — HP5 40 Hz output subsonic blocker
- `1f42a49` — harness gin equalization, `--input-scale`, WAV helpers
- `0d6f549` — tone-stack centre 500 → 630 Hz (`iso266(56)`)
- `30c36cd` — investigation notes appended to `docs/notes/abx-harness.md`

17/17 regression tests pass; `cargo build --release --bin nilamp_render` clean (~80 s).

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

## Next steps (in order)

### 1. Survey existing tube infra to confirm T5 portability

- `dsp/nilamp.dsp:150-158` — current T4 invocation via `tube.tube_ck_simple`.
  T5 is the same primitive with different constants and a different drive
  signal; structurally a clone of the T4 block.
- `tools/gen_tables.py` — table generator. Need to confirm it accepts
  (mu, ra, isat, ibias, b) and emits a `t5_table` data file usable from
  `dsp/5e3_tables.lib`. The lookup table depends only on (mu, ra, isat,
  ibias, b) — T4 = (125, 40000, 0.11, 0.042, 2.0), T5 = (125, 40000, 0.12,
  0.042, 2.5), so T5 needs its own table.
- `dsp/5e3_tables.lib` — find how `t4_6v6_table` is exported and clone the
  pattern for `t5_6v6_table`.

### 2. Add T5 constants to `dsp/5e3_constants.lib`

Source: `twd_dlx_ii_harness.jsfx:296`:
```
t5.tube_ck_set(125, 40000, 0.1200, 0.04200, 2.5, 0.5, 346, 3000, 540,
               1, 0.18, 0.325, 0.388, 0.00155, 0.0234, neq, 0.00675);
```
Field map (from `HK_LIB_TUBE.jsfx-inc:33`):
`(mu, ra, isat, ibias, b, type, vs, rl, rk, kcomp, kpk, xth, xdrop,
 tattack, trelease, neq, tck)`.

T5 constants needed:
- `t5_isat=0.12, t5_rl=3000` (already partially used inline as
  `t5_*_mode0` in the reverted edit; promote to canonical constants).
- `t5_kpre, t5_kpk, t5_kspre, t5_kspost, t5_ksva, t5_ksib, t5_kfb,
   t5_pk_xth, t5_pk_xdiode, t5_pk_k1, t5_pk_k2, t5_avg_f` —
   computed the same way as the T4 equivalents from the raw JSFX
   set-call args. Cross-reference how `t4_*` are derived from the
   T4 set-call (line 295) in current `5e3_constants.lib`.

### 3. Generate T5 wave-shaper table

- Run `tools/gen_tables.py` with T5 parameters (mu=125, ra=40000,
  isat=0.12, ibias=0.042, b=2.5).
- Emit alongside `t4_6v6_table`. Confirm same `XMAX`, `DX`, `TBL_SIZE`
  (13503 cells).
- Wire into `dsp/5e3_tables.lib` as `t5_6v6_table`.
- Add `t5_table = tables.t5_6v6_table;` near
  `dsp/nilamp.dsp:65`.

### 4. Implement T5 stage + aux subtractive branch in `process()`

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

Then update `total_dia` to include `res_t5_dia`, and feed `post_pp` into
the post-T4 chain (currently `v_out`).

Constants needed inline (mode==0): `k2_mode0 = 0.940`, `hp4_hz = 6.4`.

### 5. Re-attempt full back-end chain on top of T5

After T5 is in and ABX-stable, layer in pre-T4 (hp2, k1, hp3, peq1, hs1)
and post-T4 (peq3, hs3, lp2) chain pieces. With T5 present the gain
budget should now match JSFX, and the back-end EQ should stop regressing
residuals.

Switch the gout denom to the full `0.5 / (t4.rl*t4.isat + t5.rl*t5.isat)`
when committing the T5 stage — the existing `0.5/(c.t4_rl*c.t4_isat)`
was a half-denom hack to compensate for missing T5.

### 6. Verify and commit

For each commit:
1. `cargo build --release --bin nilamp_render` clean.
2. `cargo test --release` — 17/17 still pass.
3. `python3 tools/abx_compare.py /tmp/sine_440.wav --gain 6 --label sine440_<step>`
4. `python3 tools/abx_compare.py /tmp/sweep_5s.wav --gain 6 --label sweep_<step>`
5. RMS residual MUST be at least as good as the pre-edit baseline (sine
   −13.4 dB, sweep −9.2 dB) — any regression triggers immediate revert.

Suggested commit slicing:
1. `feat(dsp): add T5 6V6 wave-shaper table` (gen_tables.py + tables.lib)
2. `feat(dsp): add T5 constants to 5e3_constants.lib`
3. `feat(dsp): port T5 push-pull aux branch and subtractive mix`
4. `fix(dsp): use full t4+t5 gout denom now that T5 is present`
5. `feat(dsp): port JSFX back-end EQ chain (peq1/hs1/peq3/hs3, k1, hp2/3)`

## Files to read first

- `dsp/nilamp.dsp` — `process()` body, T4 block at lines 150–158, output
  at lines 170–188.
- `dsp/5e3_constants.lib` — T4 constants pattern at lines 59–60 and
  surrounding; T5 will mirror.
- `dsp/5e3_tables.lib` — locate `t4_6v6_table` export.
- `tools/gen_tables.py` — table generator entry point.
- `~/.config/REAPER/Effects/nilamp_abx/twd_dlx_ii_harness.jsfx` — lines
  295–296 (t4/t5 set), 411–428 (chain).
- `~/.config/REAPER/Effects/nilamp_abx/HK_LIB_TUBE.jsfx-inc` — line 33
  (`tube_ck_set` arg map).
- `docs/notes/abx-harness.md` — full investigation log incl. gain-budget
  table.

## Open uncertainties

- Whether `tube.tube_ck_simple` correctly handles two independent invocations
  in one `process()` cycle sharing `old_dvs`. Should — it's pure Faust — but
  worth confirming on first build.
- T5 NEQ coefficients in JSFX line 296 use `neq = 0` (same as T4 mode==0).
  Identity NEQ (1,0,0,0,0) → no extra biquad needed; matches current T4
  treatment. Confirmed by reading current T4 invocation.
- `tube_ck_simple` averaging-filter constant `t5_avg_f` derivation — check
  how `t4_avg_f` is computed from JSFX `tattack=0.00575, trelease=0.0276`
  vs T5 `tattack=0.00155, trelease=0.0234`. Likely a one-pole-per-rate
  formula already in `gen_tables.py` or `5e3_constants.lib`'s build helper.

## Decisions carried forward

- ABX target: −60 dB RMS residual / <1e-3 abs per sample. Not bit-exact.
- T5 push-pull is now in scope (was: deferred to "5e3-v2").
- 3-stage PSS chain remains deferred to v2 — single PSS lump is
  acceptable for residual targets per current evidence.
- ADNL post-EQ remains identity (1,0,0,0,0) — deferred to v2.
- Mode pin: ABX always uses mode==0 (CD 5E3, ltp==0).
- HP5 (`d98f17a`) and tone-stack centre 630 Hz (`0d6f549`) fixes stay.
