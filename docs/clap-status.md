# CLAP Status

nilamp implements a focused CLAP audio-effect subset, not the full optional CLAP
API surface. The current shell is appropriate for a guitar amp plugin: one main
audio input, one main audio output, host automation, state save/load, mono and
stereo audio configurations, and an embedded editor when CLAP GUI support is
enabled.

The CLAP validator currently passes with `21 tests run, 16 passed, 0 failed, 5
skipped`. The skipped validator cases are for optional preset discovery and note
ports that nilamp does not implement.

## Implemented

- CLAP plugin factory and descriptor metadata, including audio-effect,
  distortion, mono, and stereo feature tags.
- Main audio input/output ports.
- Mono and stereo audio port configurations via `clap.audio-ports-config` and
  config-info compatibility support.
- 32-bit floating-point audio processing.
- Host-visible parameter registration, modules, host names, text conversion,
  automation, GUI gesture/value events, enum/stepped flags, and bypass flag.
- Shared CLAP/VST3 bypass parameter and pass-through processing.
- Sample-offset parameter changes inside `process()`.
- Versioned state save/load with compatibility for older nilamp state blobs.
- Hardware-controller pages through `clap.remote-controls/2`, exposing bypass
  plus the default-screen amp-face knobs.
- Embedded GUI support for the supported platform parent API.
- GUI timer support through `clap.timer-support` when CLAP GUI support is
  enabled.

## Guitar-Amp-Relevant Backlog

1. Consider MIDI CC-style control only if a CLAP host workflow needs it.
   VST3 handles this with `IMidiMapping`, where the host turns MIDI CC input
   into normal parameter automation. CLAP remote controls are now implemented,
   so only add note-port MIDI event handling if a target CLAP host cannot map
   hardware controls another way.

2. Consider `clap.param-indication/4` as later GUI polish.
   This could let hosts mark mapped or automated parameters in the custom GUI,
   but it only matters if target hosts expose useful indication data.

3. Consider `clap.state-context/2` after presets exist.
   It could initially delegate to the existing state implementation, but its
   main value is distinguishing project, duplicate, and preset load semantics.

4. Consider `clap.latency` and `clap.tail` as low-value compatibility polish.
   nilamp is currently zero-latency and does not expose a meaningful effect tail,
   so these extensions would mostly make that explicit.

## Low-Value For This Plugin

- Direct MIDI note/CC processing, note ports, note names, note expression,
  voice info, tuning, and triggers unless target-host testing proves remote
  controls insufficient.
- Factory preset support until nilamp has real presets to ship. A complete CLAP
  preset story would include `clap.preset-load/2` plus the preset discovery
  factory, but the API is only useful once preset names, values, locations, and
  metadata exist.
- Surround, ambisonic, sidechain, configurable arbitrary ports, and audio-port
  activation unless a future feature needs them.
- Render mode, thread pool, scratch memory, transport control, track info,
  context menu, project/resource directory, undo, webview, and other specialized
  or draft extensions without a concrete host requirement.
