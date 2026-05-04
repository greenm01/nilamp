# nilamp

A Linux-native CLAP guitar amp plugin built on the architecture of Helmut Keller's "A Tube Amp Modeling Project," extended for multiple amp models. For fun.

The name is "no amp" — `nil` + `amp`.

## Status

Pre-implementation. The repo currently contains research notes and reference papers from a long exploration of the design space (Keller's block-diagram + ADAA approach, Mačák's DK-method work, Hegglun's PAK Project, current-drive theory). Implementation hasn't started yet.

Read `docs/notes/dsp-project-notes.md` § 2 for the actionable direction.

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
- **Python** (NumPy/SciPy) for offline lookup-table generation
- Primary target: Linux x86_64; Mac/Windows fall out of nih-plug for free

## Repository layout

```
docs/                 Research notes, reference papers, conversation log
dsp/                  Faust source files (.dsp)
src/                  Rust source for nih-plug shell
tools/                Python scripts (lookup-table generation, etc.)
tests/                Test fixtures, audio files for ABX/regression
vendor/keller-jsfx/   Keller's reference JSFX source (non-commercial license)
```

## License

MIT — see `LICENSE`.

**Exception**: `vendor/keller-jsfx/` contains Helmut Keller's JSFX source as reference material, licensed for non-commercial use only. See `vendor/keller-jsfx/NOTICE.md`. The MIT license does **not** apply to that directory.

## Author

Mason Austin Green — <mason@greenm01.net>
GitHub: [@greenm01](https://github.com/greenm01)
