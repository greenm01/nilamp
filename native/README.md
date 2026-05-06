# Native C Path

`native/` contains the current runtime implementation for nilamp.

## Boundaries

- C owns realtime DSP, offline rendering, the CLAP plugin shell, and the custom
  GUI runtime.
- Lua may be used for build-time codegen/config helpers when it is useful.
- Python remains the numerical oracle, fixture generator, and ABX analysis
  layer.

KDL, Lua, and Python are not linked into the renderer, plugin, or DSP engine,
and none of them should run in a future audio callback.

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

The CLAP plugin is:

```bash
native/bin/nilamp-twd-mkii.clap
```

Its custom editor is an embedded GPU GUI built in C with Pugl, `sokol_gfx`,
and Nuklear: X11 on Linux, Cocoa on macOS, and Win32 on Windows. Wayland
sessions use the Linux path through XWayland for now.

Install the CLAP to the user plugin path with:

```bash
make install-clap-user
```

This writes `~/Library/Audio/Plug-Ins/CLAP/nilamp-twd-mkii.clap` on macOS and
`~/.clap/nilamp-twd-mkii.clap` on Linux by default. The macOS install also
re-signs the copied dylib.

On Windows, build from an x64 Native Tools prompt with:

```bat
nmake /f Makefile.msvc native
nmake /f Makefile.msvc native-host-test
nmake /f Makefile.msvc install-clap-user
```

The Windows user install target copies to `%LOCALAPPDATA%\Programs\Common\CLAP`.
The system install target, `install-clap`, copies to
`C:\Program Files\Common Files\CLAP`; run the prompt elevated or set
`CLAP_INSTALL_DIR=...`.

Build Linux and macOS release packages with:

```bash
make package-linux-release
make package-macos-release
```

The Linux tarball includes `install.sh` for `~/.clap` installation. The macOS
ZIP includes `install.command` for per-user CLAP installation. Both write GPG
signatures and checksums in `dist/`.

Quick native throughput benchmark:

```bash
make native-bench
```

The CLAP and CLI render paths enable x86 FTZ/DAZ floating-point mode when
available. Non-x86 builds compile the helper away.

`make native-test` runs both the DSP fixture tests and a small CLAP loader
smoke test that scans the plugin, activates it, processes audio, and applies
one automation event.

`make native-host-test` is REAPER-free. It runs the native CLAP loader and
optional `clap-validator` when that tool is installed. The old REAPER smoke
test remains available as `make native-reaper-host-test` for manual host
checks.

`tools/abx_compare.py` defaults to `native/bin/nilamp_render` for comparison
against the canonical Keller JSFX render.

```bash
python3 tools/abx_compare.py --preset sine
python3 tools/abx_compare.py --preset sweep
```
