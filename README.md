# nilamp

nilamp is a native C guitar amp plugin for CLAP and VST3, based on Helmut Keller's
["A Tube Amp Modeling Project"](https://www.helmutkelleraudio.de/). The current
model is Keller TWD DLX II, modeled after a Fender Tweed Deluxe with a more
versatile tone stack.

The code arcitecture is designed for more amps later.

The name means "no amp": `nil` + `amp`.

## Screenshots

![nilamp TWD DLX MKII main screen](docs/images/nilamp-twd-dlx-mkii-main.png)

![nilamp TWD DLX MKII options screen](docs/images/nilamp-twd-dlx-mkii-options.png)

## Status

The active build is a native C DSP engine, Make-built offline renderers, and
native CLAP/VST3 plugin shells with an embedded GPU editor. Linux, macOS, and
Windows are supported native targets. Release packages are available from this
repository's GitHub Releases for supported desktop platforms. Linux and macOS
packages include CLAP and VST3; the Windows package now includes both formats.

At Keller TWD DLX II defaults, nilamp's full input-to-output sweep render
matches Keller's JSFX reference with a `-80.7 dB` residual under ysfx.

Current editor backends are X11/XWayland on Linux, Cocoa on macOS, and Win32 on
Windows. Native Wayland support is future work.

## Compatibility

nilamp ships as both CLAP and VST3. Current checks:

| Platform | Host | CLAP | VST3 |
| --- | --- | --- | --- |
| macOS | REAPER | Works | Works |
| macOS | Kushview Element | Works | Works |
| Windows | REAPER | Works | Works |
| Windows | Kushview Element | Works | Works |
| Linux | Carla | Works | Works |
| Linux | REAPER | Works | Works |

On Linux, the editor uses X11/XWayland. Native Wayland is not available yet.

## Goals

- Native CLAP and VST3 plugins with no YSFX wrapper dependency
- Multi-amp platform: 5E3 -> Bassman -> Plexi -> AC30 -> Twin -> ...
- Realtime tweakable amp parameters
- External IR loader for cab simulation

## Tech stack

- **C** for realtime DSP and offline rendering
- **ysfx** for headless Keller JSFX reference renders
- **KDL 2** for build-time amp model data
- **Pugl + sokol_gfx + Nuklear** for the C-only embedded plugin editor
- **Python** with NumPy/SciPy for table generation, oracle fixtures, ABX
  analysis, and KDL-to-C model generation
- **Make** as the build system
- **JSFX** reference renders from Keller's source for equivalence checks

KDL parsing, Lua, Python, allocation, file I/O, and locks do not belong in
audio callbacks.

## Dependencies

The native plugin build requires:

- C11 compiler, C++ compiler, `make`, and `git`
- `cmake` for building the external ysfx reference runner
- Python 3 with `venv`/`pip`; `make setup-python` installs NumPy and SciPy into
  `./.venv`
- Linux only: X11/Xrandr/Xcursor/Xext and OpenGL development headers
- macOS only: Xcode Command Line Tools; Cocoa, CoreVideo, and OpenGL come
  from the macOS SDK
- Windows only: Visual Studio 2022 Build Tools with the MSVC x64 toolchain and
  Windows SDK; use an x64 Native Tools prompt or run `vcvars64.bat`

Install system packages:

```bash
# Void Linux
sudo xbps-install -S base-devel git cmake python3 python3-pip python3-virtualenv \
  libX11-devel libXrandr-devel libXcursor-devel libXext-devel MesaLib-devel

# Arch Linux
sudo pacman -S --needed base-devel git cmake python python-pip \
  libx11 libxrandr libxcursor libxext mesa

# Debian / Ubuntu
sudo apt install build-essential git cmake python3 python3-venv python3-pip \
  libx11-dev libxrandr-dev libxcursor-dev libxext-dev libgl1-mesa-dev

# macOS
xcode-select --install
brew install cmake python git

# Windows
# Install Visual Studio 2022 Build Tools with "Desktop development with C++".
```

The JSFX parity path uses Joep Vanlier's maintained ysfx checkout.

```bash
git clone https://github.com/JoepVanlier/ysfx.git ~/src/ysfx
git -C ~/src/ysfx submodule update --init --recursive
```

To use a different checkout, set `YSFX_ROOT=/path/to/ysfx`.

## Repository layout

```
native/               C engine, renderers, generated ADNL tables, native tests
third_party/clap/     Vendored official CLAP C headers
third_party/vst3sdk/  Vendored Steinberg VST3 SDK subset
tools/                Python oracle, table/fixture generation, ABX harness
tests/fixtures/       Raw f32 fixture buffers for native regression tests
vendor/keller-jsfx/   Keller's reference JSFX source (non-commercial license)
docs/                 Current notes, research references, ABX notes
```

## Build

Linux and macOS use the main Makefile:

```bash
make native
make native-test
make native-host-test
make install-clap-user
make install-vst3-user
make setup-python
make native-jsfx-test
```

Windows uses MSVC/NMake from an x64 Native Tools prompt:

```bat
nmake /f Makefile.msvc native
nmake /f Makefile.msvc native-test
nmake /f Makefile.msvc native-host-test
nmake /f Makefile.msvc install-clap-user
nmake /f Makefile.msvc install-vst3-user
```

This builds:

- `native/bin/nilamp_render`
- `native/bin/nilamp_taps_render`
- `native/bin/nilamp-twd-mkii.clap`
- `native/bin/nilamp-twd-mkii.vst3` where the native VST3 target is enabled
- `native/bin/ysfx_render` when `YSFX_ROOT` points at a ready checkout
- `native/bin/test_native`
- `native/bin/test_clap_load`
- `native/bin/test_vst3_load` where the native VST3 target is enabled

`native/bin/ysfx_render` links against
`https://github.com/JoepVanlier/ysfx.git` at `~/src/ysfx` by default. Set
`YSFX_ROOT=/path/to/ysfx` to use another checkout. If the build reports missing
`thirdparty/dr_libs` headers, initialize the ysfx submodules:

```bash
git -C ~/src/ysfx submodule update --init
```

The JSFX parity tools use Python with NumPy and SciPy. Bootstrap a local
virtualenv with:

```bash
make setup-python
```

`make native-jsfx-test` and the other Python validation targets prefer
`./.venv/bin/python3` when it exists.

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

Run the Keller/ysfx-vs-native performance benchmark:

```bash
make native-perf-bench
make native-perf-bench PERF_BENCH_ARGS="--quick --runs 1 --warmups 0"
```

The headline table compares steady-state in-memory plugin processing after
Keller/ysfx JIT and native plugin activation are complete. Separate lifecycle
and reload sections track ysfx load/compile/JIT cost against native CLAP/VST3
load, instantiate, activate, and destroy cost.

For host-visible REAPER CPU measurements with editor/UI cost included, use the
scripted workflow in `tools/reaper_perf/` rather than reading REAPER's
Performance Meter by eye.

Build Linux and macOS release packages with:

```bash
make package-linux-release
make package-macos-release
```

Build the Windows release package from an x64 Native Tools prompt with:

```bat
nmake /f Makefile.msvc package-windows-release
```

## License

MIT. See `LICENSE`.

**Exception**: `vendor/keller-jsfx/` contains Helmut Keller's JSFX source as
reference material, licensed for non-commercial use only. See
`vendor/keller-jsfx/NOTICE.md`. The MIT license does **not** apply to that
directory.

`third_party/fonts/0xproto/` contains 0xProto, licensed under the SIL Open
Font License 1.1. See `third_party/fonts/0xproto/LICENSE`.

## Author

niltempus
