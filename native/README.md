# Native C/Lua migration path

This directory contains the side-by-side replacement path for the current
Faust/Rust stack.

## Boundaries

- C owns realtime DSP and offline rendering.
- Lua is build-time codegen/config only.
- Python remains the numerical oracle, fixture generator, and ABX analysis
  layer.

Lua is not linked into the renderer or DSP engine, and must not run in the
future audio callback.

## Build

```bash
make native
make native-test
```

The Makefile runs `native/scripts/gen_tables.lua` to parse the existing
`dsp/5e3_tables.lib` waveform definitions into generated C arrays under
`native/build/`. Those generated files are disposable build artifacts.

The renderer is:

```bash
native/bin/nilamp_render --input in.wav --output out.wav
```

The CLI intentionally mirrors the existing Rust renderer so
`tools/abx_compare.py --nilamp-render native/bin/nilamp_render ...` can point
at the native path.
