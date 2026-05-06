# nilamp

A native C guitar amp model based on Helmut Keller's "A Tube Amp Modeling
Project," extended toward a multi-amp platform. For fun.

The name is "no amp" — `nil` + `amp`.

## Status

The active implementation is a native C DSP engine with Make-built offline
renderers and a first-pass C CLAP plugin with an embedded GPU GUI shell. The
current milestone is continuing ysfx-backed JSFX parity work while hardening
the plugin host and editor surfaces.

## Goals

- Native CLAP plugin with no YSFX wrapper dependency
- Multi-amp platform: 5E3 -> Bassman -> Plexi -> AC30 -> Twin -> ...
- Realtime tweakable amp parameters
- External IR loader for cab simulation

## Tech stack

- **C** for realtime DSP and offline rendering
- **ysfx** for headless Keller JSFX reference renders
- **KDL 2** for build-time amp model data
- **Pugl + sokol_gfx + Nuklear** for the C-only embedded plugin editor
- **Python** with NumPy/SciPy for table generation, oracle fixtures, and ABX
  analysis, plus KDL-to-C model generation
- **Make** as the build system
- **JSFX** reference renders from Keller's source for equivalence checks

KDL parsing, Lua, Python, allocation, file I/O, and locks are not allowed in
future audio callbacks.

## Repository layout

```
native/               C engine, renderers, generated ADNL tables, native tests
third_party/clap/     Vendored official CLAP C headers
tools/                Python oracle, table/fixture generation, ABX harness
tests/fixtures/       Raw f32 fixture buffers for native regression tests
vendor/keller-jsfx/   Keller's reference JSFX source (non-commercial license)
docs/                 Current notes, research references, ABX notes
```

## Build

```bash
make native
make native-test
make native-host-test
make setup-python
make native-jsfx-test
```

This builds:

- `native/bin/nilamp_render`
- `native/bin/nilamp_taps_render`
- `native/bin/nilamp.clap`
- `native/bin/ysfx_render` when `YSFX_ROOT` points at a ready checkout
- `native/bin/test_native`
- `native/bin/test_clap_load`

`native/bin/ysfx_render` is linked against the maintained ysfx checkout at
`~/src/ysfx` by default. Override with `YSFX_ROOT=/path/to/ysfx` if needed.
Initialize that checkout's submodules if the build reports missing
`thirdparty/dr_libs` headers:

```bash
git -C ~/src/ysfx submodule update --init
```

The JSFX/parity tools use Python with NumPy/SciPy. Bootstrap a local virtualenv
with:

```bash
make setup-python
```

`make native-jsfx-test` and the other Python validation targets prefer
`./.venv/bin/python3` when it exists.

## macOS notes

- `make native`, `make native-test`, `make native-host-test`, and
  `make native-jsfx-test` work on macOS.
- The embedded CLAP editor supports Cocoa on macOS and X11/XWayland on Linux.
  Native Wayland and Windows editor support are future work.

Regenerate generated native tables after table-generator changes:

```bash
python3 tools/gen_5e3_tables.py
```

Regenerate generated native amp model data after KDL model changes:

```bash
python3 tools/gen_amp_models.py native/generated/nilamp_models.inc models/amps/keller_twd_dlx_ii.kdl
```

Run a render:

```bash
native/bin/nilamp_render --input in.wav --output out.wav
```

Run ABX comparison against JSFX:

```bash
python3 tools/abx_compare.py input.wav
python3 tools/abx_compare.py --preset sine
python3 tools/abx_compare.py --preset sweep
```

`make native-host-test` is REAPER-free: it runs the native CLAP loader and
optional `clap-validator` when that tool is installed. The old REAPER smoke
test remains available as `make native-reaper-host-test` for manual host checks.

## License

MIT — see `LICENSE`.

**Exception**: `vendor/keller-jsfx/` contains Helmut Keller's JSFX source as
reference material, licensed for non-commercial use only. See
`vendor/keller-jsfx/NOTICE.md`. The MIT license does **not** apply to that
directory.

## Author

niltempus
