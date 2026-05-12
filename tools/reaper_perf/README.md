# REAPER Host-Visible CPU Measurement

This workflow replaces visual readings from REAPER's Performance Meter with a
repeatable host-visible measurement. It samples the REAPER process while a
ReaScript drives matching nilamp and Keller/ysfx scenarios.

The result is intentionally different from `make native-perf-bench`:

- `native-perf-bench` measures isolated in-memory FX/audio processing.
- This workflow measures REAPER process CPU with transport, host scheduling,
  FX wrapper cost, and editor UI cost included.

## Project Setup

Open a REAPER project with two comparable tracks:

- one track containing the nilamp CLAP instance
- one track containing the nilamp VST3 instance

Name the tracks with recognizable words such as `nilamp clap` and
`nilamp vst3`. The Lua script first searches track names and then FX names.

The script mutes only the two comparison tracks it finds. Other project tracks
are left in their current mute state.

## Run

On macOS:

```bash
python3 tools/reaper_perf/measure_reaper_cpu.py
```

To sample the values displayed in REAPER's own Performance Meter instead, use:

```bash
python3 tools/reaper_perf/measure_reaper_perf_meter.py
```

That sampler reads the visible `CPU` and `FX CPU` text from REAPER through
macOS Accessibility. Grant the terminal app Accessibility permission in System
Settings first, and keep the REAPER Performance Meter window available. The
sampler writes a temporary config file that the Lua scenario driver reads, so
it works even when REAPER was launched from Finder rather than from the shell.

On Windows, from PowerShell:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\reaper_perf\measure_reaper_cpu.ps1
```

Then, in REAPER, run:

```text
tools/reaper_perf/run_reaper_perf_scenarios.lua
```

The default run measures three passes of:

- `nilamp_vst3_editor_closed`
- `nilamp_vst3_editor_open`
- `keller_ysfx_editor_closed`
- `keller_ysfx_editor_open`

Set `NILAMP_REAPER_PERF_SURFACES=nilamp_clap,nilamp_vst3` to compare only the
native plugin formats.

Outputs are written under `dist/reaper-perf/`, which is ignored by Git.

## Tuning

The Lua script reads these optional environment variables:

- `NILAMP_REAPER_PERF_SECONDS`: measured seconds per scenario, default `30`
- `NILAMP_REAPER_PERF_SETTLE_SECONDS`: pre-measurement settle time, default `3`
- `NILAMP_REAPER_PERF_REPEATS`: repeat count, default `3`
- `NILAMP_REAPER_PERF_SURFACES`: comma-separated surfaces, default
  `nilamp_clap,nilamp_vst3,keller_ysfx`
- `NILAMP_REAPER_PERF_MARKERS`: marker JSONL path
- `NILAMP_REAPER_PERF_CONFIG`: Lua config table path. If unset, the script
  checks `nilamp_reaper_perf_config.lua` in the system temp directory.

The macOS Performance Meter sampler accepts:

```bash
python3 tools/reaper_perf/measure_reaper_perf_meter.py \
  --reaper-pid 12345 \
  --scenario-seconds 15 \
  --settle-seconds 3 \
  --repeats 3 \
  --surfaces nilamp_clap,nilamp_vst3
```

Use `--reaper-pid` when more than one REAPER instance is running.

The PowerShell script accepts:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\reaper_perf\measure_reaper_cpu.ps1 `
  -ReaperPid 12345 `
  -PollMs 250 `
  -WarmupTrimSeconds 2 `
  -CooldownTrimSeconds 1 `
  -OutDir dist\reaper-perf
```

Use `-ReaperPid` if more than one REAPER process is running.

## Interpreting Results

Use aggregate rows with `median_cpu_pct_total_capacity` and
`p95_cpu_pct_total_capacity` as the headline values. Avoid single-sample maxima
unless you are hunting spikes.

The sampler reports two CPU forms:

- `cpu_pct_total_capacity`: process CPU normalized by all logical processors,
  roughly comparable to Windows process CPU percentage.
- `cpu_pct_one_core`: process CPU relative to one fully busy logical processor.

The `measure_reaper_perf_meter.py` sampler reports `reaper_cpu_pct` and
`reaper_fx_cpu_pct` exactly as exposed by REAPER's Performance Meter UI on
macOS. It is useful for matching what a user sees in REAPER, but it is more
host/UI-state dependent than `make native-perf-bench`.
