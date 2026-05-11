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

- one track containing the nilamp VST3 instance
- one track containing Keller's JSFX loaded through ysfx

Name the tracks with recognizable words such as `nilamp` and `keller` or
`ysfx`. The Lua script first searches track names and then FX names. If your
project uses different names, edit the `surfaces` table at the top of
`run_reaper_perf_scenarios.lua`.

The script mutes only the two comparison tracks it finds. Other project tracks
are left in their current mute state.

## Run

From PowerShell:

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

Outputs are written under `dist/reaper-perf/`, which is ignored by Git.

## Tuning

The Lua script reads these optional environment variables:

- `NILAMP_REAPER_PERF_SECONDS`: measured seconds per scenario, default `30`
- `NILAMP_REAPER_PERF_SETTLE_SECONDS`: pre-measurement settle time, default `3`
- `NILAMP_REAPER_PERF_REPEATS`: repeat count, default `3`
- `NILAMP_REAPER_PERF_MARKERS`: marker JSONL path

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

This workflow does not scrape REAPER's `FX CPU` meter. REAPER's public
ReaScript API controls FX/editor state, but does not provide a stable
Performance Meter CPU/FX CPU data API. For FX-only cost, use
`make native-perf-bench`.
