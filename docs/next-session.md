# Next session - inspect backend interaction mismatch

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

Follow-up filter semantics probes now cover the backend pre-chain filters
directly:

- Faust `hp3`, `hp4`, `peq1`, `hs1`, `peq1 -> hs1`, and T4/T5 pre-chain
  harnesses pass against the Python oracle in `cargo test --release`.
- Actual REAPER/JSFX probe renders match the oracle with smoothing disabled:
  max error <= `1.2e-7`.
- Actual REAPER/JSFX probe renders also match the oracle with JSFX coefficient
  smoothing enabled: max error <= `1.2e-7`.
- The probe exposed one harness detail: standalone mono probes must write
  `spl1 = spl0`, or REAPER's mono render sees a 50/50 dry/wet blend.

Conclusion: the backend regression is not explained by JSFX compiler semantics
or by the standalone backend filter formulas. The remaining issue is likely a
higher-level interaction: startup state history, tube operating point, branch
balance, PSS timing, or the way pre-chain changes alter T4/T5 nonlinear drive.

## Next steps

1. Stop looking at standalone backend filter formulas; they are now pinned.
2. Add diagnostics that emit the T4/T5 branch drive signals before the tubes:
   current `t4_raw_in`, current `t5_in`, and each backend candidate input.
3. Compare those drive signals against a JSFX/Python cascade oracle after the
   same 100 ms warm-up trim used by ABX.
4. If drive signals match but ABX still regresses, focus on nonlinear tube
   state and PSS timing under backend drive rather than filter coefficients.
5. Keep using the current public gate: sine at least -16.0 dB and sweep at
   least -11.2 dB.
6. Keep T5 dia/PSS feedback separate; do not add T5 dia to `total_dia` during
   backend EQ work.

## Files to read first

- `dsp/diagnostics/nilamp_t5_balance.dsp` - split backend variants.
- `src/bin/nilamp_t5_balance_render.rs` - diagnostic variant selection.
- `dsp/tests/test_filter_backend.dsp` - standalone Faust backend filter checks.
- `tools/compare_filter_semantics.py` - REAPER/JSFX black-box filter probe.
- `tools/abx_compare.py` - JSFX cache and ABX driver.
- `~/.config/REAPER/Effects/nilamp_abx/twd_dlx_ii_harness.jsfx` - backend
  chain around lines 395-434.
