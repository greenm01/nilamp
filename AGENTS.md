# AGENTS.md — guide for AI coding agents

This file documents project conventions, hot zones, build mechanics, and
gotchas for AI coding agents (Claude Code, Codex CLI, Aider, etc.) working
on `nilamp`. Humans should read `README.md` first; this file assumes that
context and adds the operational details an agent needs to act safely.

## TL;DR

- **Language stack**: Faust DSP → `faust -lang rust` → embedded into a
  Rust crate using `nih-plug`. Python (NumPy/SciPy) for offline LUT
  generation and oracle/regression tests.
- **Reference behaviour**: Helmut Keller's JSFX implementations under
  `vendor/keller-jsfx/` (and a working copy in
  `~/.config/REAPER/Effects/nilamp_abx/`) are **canonical**. When Faust
  output diverges from JSFX, the bug is in Faust unless proven otherwise.
- **Default build is fast**: `cargo build --release` compiles only
  `dsp/nilamp.dsp` (~1m44s cold). Test and diagnostic DSPs are gated.
- **Don't edit JSFX harness files** in `~/.config/REAPER/...` — that
  invalidates the cached A-channel renders used by ABX comparisons.
- **Read `docs/next-session.md` first.** Most recent entry first; that's
  the live state of any in-progress investigation.

## Repository map (agent-relevant subset)

```
dsp/
  nilamp.dsp             Production DSP. Single source of truth for the plugin path.
  hk_*.lib               Keller building blocks ported to Faust (filters, tubes, PKD, ADAA).
  5e3_constants.lib      Tweed Deluxe model constants.
  5e3_tables.lib          Generated lookup tables (do not hand-edit; see tools/gen_5e3_tables.py).
  tests/*.dsp            Per-stage harnesses; built only with --features dsp-tests.
  diagnostics/*.dsp      Full-pipeline tap probes; built only with --features dsp-diagnostics.

src/
  lib.rs                 nih-plug shell, parameter table, audio callback.
  faust.rs               Wraps the generated DSP and provides the runtime trait shims
                         that the Faust-emitted Rust expects in scope.
  bin/nilamp_render.rs   Offline CLI renderer (the production-path bin).
  bin/nilamp_t5_balance_render.rs    Diagnostic; required-features = ["dsp-diagnostics"]
  bin/nilamp_drive_probe_render.rs   Diagnostic; required-features = ["dsp-diagnostics"]

tests/
  regression.rs          Gated by #![cfg(feature = "dsp-tests")]; 23 oracle-comparison tests.
  common/mod.rs          Faust runtime types (FaustDsp, UI, Meta, ParamIndex) + fixture I/O.
  fixtures/              Raw little-endian f32 buffers (do not hand-edit; regenerate with tools/gen_fixtures.py).

tools/
  keller_oracle.py       Python reference implementation of Keller's blocks.
  gen_fixtures.py        Generates tests/fixtures/*.bin from the oracle.
  abx_compare.py         Sine + sweep ABX gate against JSFX.
  jsfx_render/           Standalone JSFX runner used by abx_compare.

vendor/keller-jsfx/      Reference JSFX source (non-commercial; MIT does NOT apply here).

docs/
  notes/dsp-project-notes.md   Living design doc; § 2 has the roadmap.
  next-session.md              Most-recent-first running log; READ FIRST.
  references.md                Bibliography pointer.

xtask/                   nih-plug bundle helper (CLAP/VST3 packaging).
build.rs                 Drives the Faust → Rust compilation.
```

## Build mechanics

`build.rs` shells out to `faust -lang rust` for every required `.dsp`
file. The Faust output is dense generated Rust (~640 KB for `nilamp.dsp`
alone). Cargo features control which `.dsp` files are compiled:

| Feature           | Compiles                               | Required for                            |
| ----------------- | -------------------------------------- | --------------------------------------- |
| *(default)*       | `dsp/nilamp.dsp`                       | `cargo build`, `nilamp_render`, plugin  |
| `dsp-tests`       | `dsp/tests/*.dsp` (18 files)           | `cargo test`                            |
| `dsp-diagnostics` | `dsp/diagnostics/*.dsp` (2 files)      | `nilamp_t5_balance_render`, `nilamp_drive_probe_render` |

When a feature is off, `build.rs` writes a stub `.rs` file in `OUT_DIR`
so any stale `include!()` still resolves. The `required-features`
declarations on the diagnostic bins make `cargo build --bin <name>`
fail fast with a useful message rather than dying mid-Faust-compile.

### Profiles

- `--release` (`lto=thin`, `strip=symbols`): production. Use for ABX
  renders, plugin bundles, anything numerically authoritative.
- `--profile release-fast` (`lto=off`, `opt-level=2`, `codegen-units=16`):
  iterative. Currently bit-identical to `--release` on the test sine and
  sweep through `nilamp_render`. Saves ~1.7 s per src-only incremental
  rebuild. **No effect on cold or DSP-edit rebuilds** — rustc frontend
  on the generated `dsp.rs` dominates LLVM optimization there.

If `release-fast` ever produces output that drifts from `release`, fall
back to `--release` for the affected work and update this doc.

### Typical commands

```bash
# Production / ABX-authoritative renders
cargo build --release --bin nilamp_render

# Iterative Rust-side development (DSP unchanged)
cargo build --profile release-fast --bin nilamp_render

# Plugin bundle
cargo xtask bundle nilamp --release

# Run regression tests
cargo test --features dsp-tests

# Build diagnostic renderers
cargo build --release --bin nilamp_drive_probe_render --features dsp-diagnostics
cargo build --release --bin nilamp_t5_balance_render --features dsp-diagnostics

# Regenerate fixtures (after keller_oracle.py changes)
python3 tools/gen_fixtures.py

# ABX gate (sine + sweep vs JSFX)
python3 tools/abx_compare.py
```

### What `build.rs` does NOT do

- No `cargo:rerun-if-changed` on Faust library files (`hk_*.lib`,
  `5e3_*.lib`). If you edit a library, **touch `dsp/nilamp.dsp`** to
  force a rebuild, or `cargo clean -p nilamp`.
- No incremental Faust caching beyond cargo's own. Any `.dsp` change
  triggers a full Faust compile of that file (~1m40s for `nilamp.dsp`).

## Conventions and house style

### Rust

- Edition 2021. `rustfmt.toml` is committed; run `cargo fmt`.
- Panics in DSP code are unacceptable — the plugin runs in real-time
  audio threads. Errors at the Faust boundary are upstream-author bugs
  and should never reach user audio paths.
- `src/faust.rs` deliberately re-exports trait shims (`FaustDsp`, `UI`,
  `Meta`, `ParamIndex`, `F32`, `FaustFloat`) into the scope where the
  generated code is `include!`d. Mirror this pattern in `tests/common/mod.rs`
  for any new test harness that includes Faust output.
- Generated DSP code lives in `OUT_DIR/<stem>.rs` and is included via
  `include!(concat!(env!("OUT_DIR"), "/<stem>.rs"))`. Do not check
  generated files into the repo.

### Faust

- Library files use `hk_` prefix for Keller-derived blocks
  (`hk_filters.lib`, `hk_tube.lib`, ...). Per-amp constants/tables use
  the model number prefix (`5e3_constants.lib`).
- The single production graph lives in `dsp/nilamp.dsp`. Diagnostic
  variants in `dsp/diagnostics/` should keep the same parameter UI so
  output WAVs are comparable across builds.
- When porting a JSFX block, **link to the JSFX line range in a comment**
  so the next agent can verify the port mechanically.

### Python

- `tools/keller_oracle.py` is the reference. It implements every
  algorithm in the same equation form as Keller's PDF
  (`vendor/keller-jsfx/Libs/A Tube Amp Modeling Project V1.0.3.pdf`).
  Functions exported as `flt_<name>_block`, `tube_<name>_block`, etc.
  are used both for fixture generation and for cross-checking Faust
  outputs in tests.
- Fixtures (`tests/fixtures/*.bin`) are raw little-endian `f32`. Use
  `read_f32_bin` / `write_f32_bin` in `tests/common/mod.rs` (and the
  matching helpers in `tools/gen_fixtures.py`).

### Documentation

- `docs/next-session.md` is **most-recent-first**. New session entries go
  at the top, under a short H3 heading describing the hypothesis tested
  and the verdict (`SUCCESS` / `REGRESS` / `INCONCLUSIVE`).
- Each entry should record: edit summary, ABX/oracle numbers, what was
  reverted, what hypothesis is now leading.
- Long-form design notes go in `docs/notes/dsp-project-notes.md`.

## Hot zones — touch with care

These files are load-bearing for current correctness debugging:

- `dsp/nilamp.dsp` — production graph; the PSS topology, tone-stack
  position, and pre-stage EQ chains are all under active investigation.
  Read the most recent `docs/next-session.md` entry before editing.
- `dsp/hk_tube.lib` — `tube_pss`, `tube_ck_*`, `tube_cd_*`. Mathematical
  equivalence to JSFX has been verified; deviations from the JSFX
  formulation will silently break ABX gates.
- `tools/keller_oracle.py` — changing oracle output invalidates every
  fixture. Always regenerate fixtures and re-run `cargo test --features
  dsp-tests` after touching this file.
- `~/.config/REAPER/Effects/nilamp_abx/*.jsfx*` — **do not edit**. The
  ABX harness caches a JSFX channel render keyed off the source-file
  hash. Editing invalidates the cache and forces a slow re-render.

## Reference-checking workflow

When making any DSP-affecting change:

1. State the hypothesis in one sentence (what JSFX behaviour are we
   matching? how do we know we matched it?).
2. Make the smallest possible change.
3. `cargo build --release --bin nilamp_render` and run
   `python3 tools/abx_compare.py`. Public gate is sine residual ≥ -16 dB
   and sweep residual ≥ -11.2 dB.
4. If the gate passes, run `cargo test --features dsp-tests` to confirm
   no per-stage regression.
5. Record the result in `docs/next-session.md` (most-recent-first).
6. Commit with a message that captures the **why**, not just the
   **what**. See recent commits for style.

## Known sharp edges

- Faust 2.85.5 is the tested compiler version. Newer versions occasionally
  re-emit equivalent code with different variable names; cargo will see
  this as a real change and rebuild.
- `mold` linker is **not installed** on the dev machine; would shave
  trivial seconds off final-link of `libnilamp.so` if added later.
- Diagnostic bins compile two ~670 KB files each — expect a 4-5 minute
  cold build the first time you enable `dsp-diagnostics`.
- The Faust-emitted code triggers many `clippy` lints; the `tests/`
  modules suppress them with `#![allow(...)]`. Do the same in any new
  module that `include!`s Faust output.

## When in doubt

1. Read `docs/next-session.md` (most-recent-first).
2. Check the JSFX original under `vendor/keller-jsfx/` or in
   `~/.config/REAPER/Effects/nilamp_abx/`.
3. Compare against `tools/keller_oracle.py`.
4. Ask the user before reverting work that previous sessions chose to
   leave in place — there's usually history behind it that isn't in the
   diff.
