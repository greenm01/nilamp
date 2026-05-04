# dsp/

Faust source files (`.dsp`).

Empty for now. Eventually:

- `nilamp.dsp` — top-level signal flow per amp
- `tubes.dsp` — tube stage classes (CC, CD, CF, LTP) ported from Keller's `HK_LIB_TUBE.jsfx-inc`
- `filters.dsp` — filter primitives (most map to Faust's stdlib `fi.lowpass`, `fi.tf2`, `fi.svf`)
- `adaa.dsp` — antiderivative anti-aliasing runtime, ported from Keller's `HK_LIB_ADNL.jsfx-inc`
- `tables/` — generated lookup tables (gitignored; produced by `tools/gen_tables.py`)
- `amps/` — per-amp parameter sets and topology variants

Build: `faust -lang rust -o ../src/dsp.rs nilamp.dsp` (target path TBD).
