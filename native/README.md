# Native C Path

`native/` contains the current runtime implementation for nilamp.

## Boundaries

- C owns realtime DSP, offline rendering, and the CLAP plugin shell.
- Lua may be used for build-time codegen/config helpers when it is useful.
- Python remains the numerical oracle, fixture generator, and ABX analysis
  layer.

Lua and Python are not linked into the renderer or DSP engine, and neither
should run in a future audio callback.

## Build

```bash
make native
make native-test
make native-host-test
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

It writes 23 float32 channels:
`v_out, res1_v, res3_v, res4_v, drive_t4, res5_v, res_t5_v, dvs2, dvs3,
p2_s, p3_s, drive_t5, post_pp, post_peq3, post_hs3, post_hp5,
t4_advk_in, t5_advk_in, t4_dia, t5_dia, t4_advk_out, t5_advk_out,
dia1_next`.

The no-GUI CLAP plugin is:

```bash
native/bin/nilamp.clap
```

Quick native throughput benchmark:

```bash
make native-bench
```

The CLAP and CLI render paths enable x86 FTZ/DAZ floating-point mode when
available. Non-x86 builds compile the helper away.

`make native-test` runs both the DSP fixture tests and a small CLAP loader
smoke test that scans the plugin, activates it, processes audio, and applies
one automation event.

`make native-host-test` runs the no-GUI CLAP through REAPER with temporary
`CLAP_PATH` discovery. It is intentionally separate from `make native-test`
because it depends on a local host and graphical/audio environment.

`tools/abx_compare.py` defaults to `native/bin/nilamp_render` for comparison
against the canonical Keller JSFX render.

```bash
python3 tools/abx_compare.py --preset sine
python3 tools/abx_compare.py --preset sweep
```
