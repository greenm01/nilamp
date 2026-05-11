# CLAP Status

nilamp implements a focused CLAP audio-effect subset, not the full optional CLAP
API surface. The current shell is appropriate for a guitar amp plugin: one main
audio input, one main audio output, MIDI CC control, host automation, state
save/load, mono and stereo audio configurations, and an embedded editor when
CLAP GUI support is enabled.

The CLAP validator currently passes with `21 tests run, 18 passed, 0 failed, 3
skipped`. The skipped validator cases are for optional preset discovery that
nilamp does not implement.

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
- MIDI CC-style parameter control through one `clap.note-ports` MIDI input,
  matching the VST3 CC map for bypass and amp-face knobs.
- Host-provided GUI mapping and automation indicators through
  `clap.param-indication/4` when CLAP GUI support is enabled.
- Context-aware state save/load through `clap.state-context/2`, delegating to
  the versioned state implementation for project, duplicate, and preset
  contexts.
- Explicit zero latency and zero tail declarations through `clap.latency` and
  `clap.tail`.
- Embedded GUI support for the supported platform parent API.
- GUI timer support through `clap.timer-support` when CLAP GUI support is
  enabled.

## Guitar-Amp-Relevant Backlog

No remaining CLAP API gaps are currently worth implementing for nilamp's guitar
amp scope without a concrete host requirement.

## Low-Value For This Plugin

- MIDI learn, MIDI2, MPE, sysex, note names, note expression, voice info,
  tuning, and triggers.
- Factory preset support until nilamp has real presets to ship. A complete CLAP
  preset story would include `clap.preset-load/2` plus the preset discovery
  factory, but the API is only useful once preset names, values, locations, and
  metadata exist.
- Surround, ambisonic, sidechain, configurable arbitrary ports, and audio-port
  activation unless a future feature needs them.
- Render mode, thread pool, scratch memory, transport control, track info,
  context menu, project/resource directory, undo, webview, and other specialized
  or draft extensions without a concrete host requirement.
