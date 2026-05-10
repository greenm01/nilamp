# Reaper/Carla Host Validation

Use this pass for host-visible behavior that the in-memory benchmark does not
cover: plugin calibration, editor interaction, host automation refresh, and
editor-open CPU trends.

## Build And Install

```bash
make native-test
make native-host-test
make native-jsfx-test
make install-clap-user
make install-vst3-user
```

Restart or rescan the host after installing.

## Reaper

Reaper is the primary local host because it can run Keller's JSFX and nilamp in
the same project.

1. Put the same mono DI item on separate tracks.
2. Insert Keller `TWD DLX II` JSFX on one track and nilamp VST3 or CLAP on the
   other.
3. Set visible controls to matching values: Gain `0 dB`, Output Gain `0 dB`,
   Volume/Bass/Mid/Treble/Sag `50%`, Tube 1 `ECC83`, Splitter `LTP 3`,
   compensation `Splitter`.
4. Compare rendered peak/RMS or null-test gain. Record any fixed offset; do not
   change native calibration unless the Reaper render confirms the reported
   `6 dB` delta.
5. With the nilamp editor open, verify mouse wheel changes each knob/value
   control by one parameter step per wheel tick.
6. Move a Reaper parameter control or automation envelope while the nilamp
   editor is open but unfocused. The editor should update without needing focus.
7. Compare Reaper's CPU trend with editor closed and editor open, and record
   buffer size, sample rate, bus layout, and plugin format.

## Carla

Carla is a secondary sanity host for plugin behavior, not the performance
authority.

1. Load nilamp CLAP and VST3.
2. Confirm mouse-wheel editing works in the embedded editor.
3. Change parameters from Carla's host controls while the editor is visible.
   The GUI should update immediately.
4. Check CPU trend with editor closed and open. Treat differences from Reaper as
   host-specific until reproduced.

## Performance Notes

Use `make native-perf-bench` for isolated steady-state processing and YSFX
JIT/reload timing. Use Reaper/Carla only for host-observed behavior, especially
editor cost and bus-layout effects.
