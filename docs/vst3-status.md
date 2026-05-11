# VST3 Status

nilamp implements a focused VST3 audio-effect subset, not the full optional VST3
API surface. The current shell is appropriate for a guitar amp plugin: one main
audio input, one main audio output, host automation, state save/load, mono and
stereo bus arrangements, and an embedded editor.

The Steinberg validator currently passes with `47 tests passed, 0 tests failed`.

## Implemented

- Split processor/controller VST3 classes.
- 32-bit floating-point audio processing.
- Main mono/stereo audio bus arrangements.
- Host-visible parameter registration, text conversion, and automation.
- Parameter units/groups via `IUnitInfo`, derived from shared control module
  metadata.
- VST3 MIDI CC mapping for bypass and default-screen amp-face knobs via
  `IMidiMapping`.
- Shared CLAP/VST3 bypass parameter and pass-through processing.
- Sample-offset parameter changes inside `process()`.
- Component and controller state save/load.
- Embedded editor creation for the supported platform parent type.
- VST3 editor content-scale support.
- SDK base-class interfaces including component, audio processor, edit
  controller, edit controller 2, connection point, and process-context
  requirements.

## Guitar-Amp-Relevant Backlog

1. Add factory presets/program lists only when nilamp has real presets to ship.
   Program-list API support without an actual preset story adds complexity but
   little value.

2. Consider 64-bit audio processing as a compatibility/polish item, not a
   performance optimization.
   Double-precision VST3 processing mainly reduces plugin I/O rounding and lets
   hosts exercise their 64-bit processing path. It is usually neutral to slower
   than 32-bit for this kind of DSP, and conversion-buffer support would add
   overhead unless the native host path becomes genuinely double-capable.

## Low-Value For This Plugin

- Direct MIDI note/CC processing, MIDI learn, note expression, and keyswitches.
- Aux, sidechain, or surround buses unless a future feature needs external
  control audio or multichannel processing.
- Data exchange, prefetch, channel context, physical UI, or other specialized
  optional VST3 interfaces without a concrete host requirement.
