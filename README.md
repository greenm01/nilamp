# nilamp

A Linux-native CLAP guitar amp plugin built on the architecture of Helmut Keller's "A Tube Amp Modeling Project," extended for multiple amp models. For fun.

The name is "no amp" — `nil` + `amp`.

## Status

5E3 prototype builds and compiles to a CLAP/VST3 plugin. Per-stage DSP validation is in progress (see `tests/` and `tools/keller_oracle.py`). The DSP layer is a Faust port of Keller's block-diagram + ADAA tube-amp model with additional analytical tube models (Dempwolf-Zölzer triode for ECC83). Read `docs/notes/dsp-project-notes.md` § 2 for the full roadmap.

## Goals

- Native Linux CLAP plugin (no YSFX wrapper dependency)
- Multi-amp platform: 5E3 → Bassman → Plexi → AC30 → Twin → ...
- Real-time tweakable parameters (tone stack, gain, etc.)
- External IR loader for cab; we don't bundle a convolution engine

Fills a real gap: most amp plugins are Mac/Windows-only or paid. A free, native Linux CLAP plugin extending an open-source foundation is something that doesn't really exist.

## Tech stack

- **Faust** for DSP — block-diagram syntax matches Keller's architecture
- `faust -lang rust` to compile DSP to Rust source
- **nih-plug** (Rust) for plugin shell, CLAP + VST3 export
- **Python** (NumPy/SciPy) for offline lookup-table generation and regression testing
- Primary target: Linux x86_64; Mac/Windows fall out of nih-plug for free

## Repository layout

```
docs/                 Research notes, reference papers, conversation log
dsp/                  Faust source files (.dsp) and library imports
dsp/tests/            Per-stage Faust test harnesses (for validation)
src/                  Rust source for nih-plug shell + Faust-generated DSP
tests/                Rust regression tests and float32 fixture binaries
tools/                Python scripts (lookup-table generation, oracle, etc.)
xtask/                nih-plug xtask scaffolding for bundling
vendor/keller-jsfx/   Keller's reference JSFX source (non-commercial license)
```

## Build

```bash
cargo build --release
# Produces libnilamp.so in target/release/
# Use xtask for CLAP/VST3 bundling:
cargo xtask bundle nilamp --release
```

Requires [Faust](https://faust.grame.fr/) on the build machine; the DSP is compiled from `dsp/nilamp.dsp` at build time via `build.rs`.

### Cargo features

The Faust build step generates several megabytes of Rust per `.dsp` file. To keep the default iteration loop fast, the regression-test and diagnostic DSPs are feature-gated:

| Feature | Compiles | Needed for |
|---|---|---|
| *(default)* | `dsp/nilamp.dsp` only | `cargo build`, `nilamp_render`, plugin bundle |
| `dsp-tests` | `dsp/tests/*.dsp` (18 files) | `cargo test` |
| `dsp-diagnostics` | `dsp/diagnostics/*.dsp` (2 files) | `nilamp_t5_balance_render`, `nilamp_drive_probe_render` |

```bash
cargo test --features dsp-tests
cargo build --release --bin nilamp_drive_probe_render --features dsp-diagnostics
```

### Build profiles

- `--release`: production profile (`lto=thin`, `strip=symbols`). Use this for ABX renders, plugin bundles, and any numerically-authoritative output.
- `--profile release-fast`: iterative profile (`lto=off`, `opt-level=2`). Currently bit-identical to `--release` on the test sine and sweep; saves ~1.7 s per src-only incremental rebuild. Has no effect on cold or DSP-edit rebuilds (rustc frontend on the generated `dsp.rs` dominates).

## License

MIT — see `LICENSE`.

**Exception**: `vendor/keller-jsfx/` contains Helmut Keller's JSFX source as reference material, licensed for non-commercial use only. See `vendor/keller-jsfx/NOTICE.md`. The MIT license does **not** apply to that directory.

## Author

Mason Austin Green — <mason@greenm01.net>
GitHub: [@greenm01](https://github.com/greenm01)
