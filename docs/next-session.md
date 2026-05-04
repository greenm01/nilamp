# Next session - inspect backend pre-chain mismatch

## State of the tree

Public `dsp/nilamp.dsp` includes the ABX-safe T5 subtractive audio branch plus
the JSFX post-power backend filter subset. The public output path is:
`post_pp = res5_v - res_t5_v`, `peq3 -> hs3 -> hp5 -> lp2`, then full T4+T5
output denominator. PSS dia feedback intentionally remains on the existing
T4-only path.

Current normal-renderer ABX baseline at `gain=+6, defaults`:

| Test | Current public baseline |
|---|---:|
| 440 Hz sine RMS residual | -16.0 dB |
| 5 s log-sweep RMS residual | -11.2 dB |
| Sweep peak A / B | 0.4195 / 0.3698 |
| Sweep align lag | 1 sample |

## What was added

`tools/abx_compare.py` now caches JSFX reference renders under
`/tmp/abx_compare/jsfx_cache/`, keyed by input audio content and JSFX slider
settings. This avoids rerunning REAPER for every nilamp diagnostic variant.
Use `--no-jsfx-cache` to force a fresh JSFX render.

The T5 branch-balance diagnostic now splits the rejected full backend path into
variants `v7` through `v12`:

| Variant | 440 Hz sine | 5 s sweep | Result |
|---|---:|---:|---|
| current public baseline / `v5` | -16.0 dB | -11.2 dB | keep |
| `v6_full_backend_current_sag` | -15.8 dB | -9.4 dB | reject |
| `v7_t4_k1_current_t3` | -15.1 dB | -11.1 dB | `k1` alone regresses sine |
| `v8_t4_hp3_current_t3` | -15.5 dB | -10.7 dB | `hp3` regresses sweep |
| `v9_t4_peq_hs_current_t3` | -15.6 dB | -9.9 dB | PEQ/HS regresses sweep |
| `v10_t4_full_pre_current_t3` | -15.1 dB | -9.5 dB | full T4 pre-chain regresses |
| `v11_hp2_t5_source_only` | -16.1 dB | -11.1 dB | diagnostic-only, sweep misses gate |
| `v12_hp2_both_raw` | -16.6 dB | -11.1 dB | HP2 helps sine, sweep misses gate |

No split variant clears both public gates, so no new public `dsp/nilamp.dsp`
audio-path edit was made.

## Next steps

1. Inspect the T4 pre-chain coefficient math and ordering against JSFX:
   `k1`, `hp3`, `peq1`, and `hs1`.
2. Add smaller diagnostics for the PEQ/HS block:
   - PEQ1 only.
   - HS1 only.
   - PEQ1/HS1 with alternate ordering if JSFX state/order inspection suggests it.
3. Add T5-side mirrors for the same block only if T4-side diagnostics implicate
   branch imbalance rather than coefficient error.
4. Keep using the current public gate: sine at least -16.0 dB and sweep at
   least -11.2 dB.
5. Keep T5 dia/PSS feedback separate; do not add T5 dia to `total_dia` during
   backend EQ work.

## Files to read first

- `dsp/diagnostics/nilamp_t5_balance.dsp` - split backend variants.
- `src/bin/nilamp_t5_balance_render.rs` - diagnostic variant selection.
- `tools/abx_compare.py` - JSFX cache and ABX driver.
- `~/.config/REAPER/Effects/nilamp_abx/twd_dlx_ii_harness.jsfx` - backend
  chain around lines 395-434.
