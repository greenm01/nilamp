# GUI Development Notes

Future plugin UI work should stay in the C toolchain and avoid adding Rust,
Nim, Python, or Lua to the plugin runtime.

## Preferred GUI Stack

- Use Pugl for embedded X11 plugin window creation and event handling. Under
  Wayland compositors, the v1 path relies on XWayland.
- Use `sokol_gfx.h` for GPU rendering. Do not use `sokol_app.h` in the plugin;
  the host owns the application/window lifecycle.
- Use Nuklear with `sokol_nuklear.h` for the first custom editor. Keep Nuklear
  vendored and unmodified.
- Keep the UI shape Elm-like in our code: explicit state, small update
  messages, and a render/build function that maps state to Nuklear widgets.
- Keep the CLAP DSP/audio callback independent from the GUI layer.
- Keep GUI allocation, file I/O, and host/UI calls out of `process()`.
- Do not add C++, Dear ImGui/cimgui, NanoVG, Lua/LuaJIT, or KDL parsing to the
  plugin runtime.
- LSP Plugins' renderer is useful as architecture precedent only. Its OpenGL
  implementation is C++ and LGPL/GPL-family code, so do not copy it into this
  MIT C codebase.

## Integration Shape

- Keep `native/src/nilamp_clap.c` focused on CLAP lifecycle, audio ports,
  parameters, state, and extension routing.
- Add GUI code as a separate module rather than growing the audio shell.
- Let the generic host parameter surface remain the baseline even after a
  custom UI lands.
- Keep native Wayland as a later explicit feature, not an implied promise.
