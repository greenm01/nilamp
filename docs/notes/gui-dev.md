# GUI Development Notes

Future plugin UI work should stay in the C toolchain and avoid adding Rust,
Nim, Python, or Lua to the plugin runtime.

## Preferred GUI Stack

- Use Pugl for native plugin window creation, embedding, and event handling.
- Use Sokol headers for lightweight drawing/runtime support where useful.
- Keep the CLAP DSP/audio callback independent from the GUI layer.
- Keep GUI allocation, file I/O, and host/UI calls out of `process()`.
- Vendor headers under `third_party/` when the UI work begins, with upstream
  license files kept next to the imported headers.

## Integration Shape

- Keep `native/src/nilamp_clap.c` focused on CLAP lifecycle, audio ports,
  parameters, state, and extension routing.
- Add GUI code as a separate module rather than growing the audio shell.
- Let the generic host parameter surface remain the baseline even after a
  custom UI lands.
