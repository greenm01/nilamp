# Next session - isolate backend EQ regression after T5

## State of the tree

Public `dsp/nilamp.dsp` now includes the ABX-safe T5 subtractive audio branch
plus the JSFX post-power backend filter subset. The public output path is:
`post_pp = res5_v - res_t5_v`, `peq3 -> hs3 -> hp5 -> lp2`, then full T4+T5
output denominator. PSS dia feedback intentionally remains on the existing
T4-only path. Do not add T5 dia to `total_dia` without a separate sag/PSS
diagnostic; that was the source of the earlier public T5 regression.

Current normal-renderer ABX baseline at `gain=+6, defaults`:

| Test | Current public baseline |
|---|---:|
| 440 Hz sine RMS residual | -16.0 dB |
| 5 s log-sweep RMS residual | -11.2 dB |
| Sweep peak A / B | 0.4195 / 0.3698 |
| Sweep align lag | 1 sample |

Regression status before this handoff:

- `cargo test --release` passed 22/22.
- `cargo build --release --bin nilamp_render` passed.
- `cargo build --release --bin nilamp_t5_balance_render` passed.

## What was added

The T5 branch-balance diagnostic now has two extra backend-chain probes:

| Variant | Meaning |
|---|---|
| `v5_post_backend_current_sag` | current T4/T5 mix plus JSFX post-power PEQ3/HS3/HP5/LP2 |
| `v6_full_backend_current_sag` | HP2 before T3, backend T4/T5 branches, then PEQ3/HS3/HP5/LP2 |

`dsp/hk_filters.lib` also now exports `flt_df2_lp`, mirroring Keller
`flt_df2_set_lp`, so diagnostics can model JSFX `lp2` at 10 kHz.

ABX results from these probes at `gain=+6, defaults`:

| Variant | 440 Hz sine | 5 s sweep | Decision |
|---|---:|---:|---|
| prior public T5 baseline | -15.5 dB | -9.8 dB | superseded |
| `v5_post_backend_current_sag` | -16.0 dB | -11.2 dB | ported to public path |
| `v6_full_backend_current_sag` | -15.8 dB | -9.4 dB | reject for public path |

Interpretation: post-power backend filtering by itself improves both probes,
so `v5` was ported to `dsp/nilamp.dsp`. The full coupled backend path still
regresses the sweep, so `v6` remains diagnostic-only.

## Next steps

1. Keep `v5`/`v6` diagnostics as the reference harness for backend work.
2. Split the rejected full backend path into smaller diagnostic variants:
   - HP2 before T3 only.
   - T4/T5 `k1`/`k2` attenuators only.
   - HP3/HP4 only.
   - PEQ1/PEQ2 + HS1/HS2 only.
   - LP2 only.
3. Gate every variant against the current public baseline: sine must be at
   least -16.0 dB and sweep must be at least -11.2 dB.
4. If one stage regresses sweep, inspect Faust vs JSFX coefficient math and
   ordering before trying another public-path port.
5. Diagnose T5 dia/PSS feedback separately from audio EQ. It is still a known
   failure mode and should not be bundled with backend EQ work.

## Files to read first

- `dsp/nilamp.dsp` - public T5 branch and current PSS dia feedback.
- `dsp/diagnostics/nilamp_t5_balance.dsp` - backend diagnostic variants.
- `src/bin/nilamp_t5_balance_render.rs` - diagnostic variant selection.
- `dsp/hk_filters.lib` - JSFX-mirrored `flt_df2_lp` helper.
- `docs/notes/abx-harness.md` - full ABX investigation log.
- `~/.config/REAPER/Effects/nilamp_abx/twd_dlx_ii_harness.jsfx` - backend
  chain around lines 395-434.
- `~/.config/REAPER/Effects/nilamp_abx/HK_LIB_FLT_DF.jsfx-inc` -
  `flt_df2_set_lp`.
