# nilamp

A native Linux guitar amp model based on Helmut Keller's "A Tube Amp Modeling
Project," extended toward a multi-amp platform. For fun.

The name is "no amp" — `nil` + `amp`.

## Status

The active implementation is a native C DSP engine with Make-built offline
renderers. The current milestone is numerical parity against Keller's JSFX
reference before adding a C CLAP plugin shell.

## Goals

- Native Linux CLAP plugin with no YSFX wrapper dependency
- Multi-amp platform: 5E3 -> Bassman -> Plexi -> AC30 -> Twin -> ...
- Realtime tweakable amp parameters
- External IR loader for cab simulation

## Tech stack

- **C** for realtime DSP and offline rendering
- **Lua** for build-time config/codegen only
- **Python** with NumPy/SciPy for table generation, oracle fixtures, and ABX
  analysis
- **Make** as the build system
- **JSFX** reference renders from Keller's source for equivalence checks

Lua, Python, allocation, file I/O, and locks are not allowed in future audio
callbacks.

## Repository layout

```
native/               C engine, renderers, generated ADNL tables, native tests
tools/                Python oracle, table/fixture generation, ABX harness
tests/fixtures/       Raw f32 fixture buffers for native regression tests
vendor/keller-jsfx/   Keller's reference JSFX source (non-commercial license)
docs/                 Current notes, research references, ABX notes
```

## Build

```bash
make native
make native-test
```

This builds:

- `native/bin/nilamp_render`
- `native/bin/nilamp_taps_render`
- `native/bin/test_native`

Regenerate generated native tables after table-generator changes:

```bash
python3 tools/gen_5e3_tables.py
```

Run a render:

```bash
native/bin/nilamp_render --input in.wav --output out.wav
```

Run ABX comparison against JSFX:

```bash
python3 tools/abx_compare.py input.wav
```

## License

MIT — see `LICENSE`.

**Exception**: `vendor/keller-jsfx/` contains Helmut Keller's JSFX source as
reference material, licensed for non-commercial use only. See
`vendor/keller-jsfx/NOTICE.md`. The MIT license does **not** apply to that
directory.

## Author

Mason Austin Green
GitHub: [@greenm01](https://github.com/greenm01)
