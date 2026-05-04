# src/

Rust source for the nih-plug plugin shell.

Empty for now. Eventually:

- `lib.rs` — nih-plug `Plugin` impl, parameter mapping, audio thread wiring
- `dsp.rs` — Faust-generated DSP module (auto-generated from `dsp/nilamp.dsp`)
- `params.rs` — parameter definitions and per-amp parameter sets

Build: `cargo build --release` then look for the `.clap` and `.vst3` artifacts in `target/release/`.
