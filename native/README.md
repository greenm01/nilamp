# Native C Path

`native/` contains the current runtime implementation for nilamp.

## Boundaries

- C owns realtime DSP, offline rendering, shared plugin host glue, and the
  custom GUI runtime. The CLAP shell stays C; the VST3 shell uses a small
  C++/Objective-C++ ABI layer around the C core.
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

On macOS this is a bundle containing
`Contents/MacOS/nilamp-twd-mkii`; on Linux it is a shared library file.

The VST3 plugin bundle is:

```bash
native/bin/nilamp-twd-mkii.vst3
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
re-signs the copied bundle.

Install the VST3 to the user plugin path with:

```bash
make install-vst3-user
```

This writes `~/Library/Audio/Plug-Ins/VST3/nilamp-twd-mkii.vst3` on macOS and
`~/.vst3/nilamp-twd-mkii.vst3` on Linux. The macOS install also re-signs the
copied bundle.

On Windows, build from an x64 Native Tools prompt with:

```bat
nmake /f Makefile.msvc native
nmake /f Makefile.msvc native-host-test
nmake /f Makefile.msvc install-clap-user
nmake /f Makefile.msvc install-vst3-user
```

The Windows user install target copies to `%LOCALAPPDATA%\Programs\Common\CLAP`.
The Windows VST3 user install target copies to
`%LOCALAPPDATA%\Programs\Common\VST3`.
The system install target, `install-clap`, copies to
`C:\Program Files\Common Files\CLAP`; run the prompt elevated or set
`CLAP_INSTALL_DIR=...`.

Build Linux and macOS release packages with:

```bash
make package-linux-release
make package-macos-release
```

The Linux tarball includes `install.sh` for per-user CLAP and VST3
installation. The macOS ZIP includes `install.command` for per-user CLAP and
VST3 installation. The Windows ZIP includes `install.cmd` for per-user CLAP
and VST3 installation. Release packaging writes GPG signatures and checksums in
`dist/`.

Quick native throughput benchmark:

```bash
make native-bench
```

The CLAP and CLI render paths enable x86 FTZ/DAZ floating-point mode when
available. Non-x86 builds compile the helper away.

`make native-test` runs the DSP fixture tests, a small CLAP loader smoke test,
and a VST3 loader smoke test where the native VST3 target is enabled. The
plugin smoke tests scan the plugin, activate it, process audio, restore state,
and apply automation.

`make native-host-test` is REAPER-free. It runs the native CLAP/VST3 loaders
and optional external validators when installed (`clap-validator`, `validator`,
or `vst3validator`). The old REAPER smoke test remains available as
`make native-reaper-host-test` for manual host checks.

`tools/abx_compare.py` defaults to `native/bin/nilamp_render` for comparison
against the canonical Keller JSFX render.

```bash
python3 tools/abx_compare.py --preset sine
python3 tools/abx_compare.py --preset sweep
```
