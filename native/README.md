# Native C Path

`native/` contains the current runtime implementation for nilamp.

## Boundaries

- C owns realtime DSP and offline rendering.
- Lua may be used for build-time codegen/config helpers when it is useful.
- Python remains the numerical oracle, fixture generator, and ABX analysis
  layer.

Lua and Python are not linked into the renderer or DSP engine, and neither
should run in a future audio callback.

## Build

```bash
make native
make native-test
```

Generated ADNL tables live under `native/generated/` and are produced by:

```bash
python3 tools/gen_5e3_tables.py
```

The main renderer is:

```bash
native/bin/nilamp_render --input in.wav --output out.wav
```

The tap renderer is:

```bash
native/bin/nilamp_taps_render --input in.wav --output taps.wav
```

`tools/abx_compare.py` defaults to `native/bin/nilamp_render` for comparison
against the canonical Keller JSFX render.
