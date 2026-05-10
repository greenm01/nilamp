# Keller Performance Benchmark

`make native-perf-bench` runs `tools/benchmark_keller_perf.py`, using Keller's
staged JSFX under `ysfx` as the baseline and comparing it to nilamp's loaded
CLAP and VST3 plugins.

The headline comparison is `steady_plugin_process`: each driver loads and warms
the effect first, then times only in-memory audio processing blocks. This is the
runtime number that matters for plugin use.

This benchmark intentionally excludes DAW editor cost, host CPU-meter behavior,
and host-specific bus scheduling. Use
`docs/notes/reaper-carla-host-validation.md` for local Reaper/Carla checks that
cover those host-visible effects.

The harness also reports:

- `plugin_lifecycle`: effect/plugin load, initialize, activate, deactivate, and
  destroy.
- `reload`: repeated same-process effect/plugin reload work. For ysfx this
  includes JSFX load/compile/JIT.

The VST3 lifecycle/reload numbers are useful for relative warm-path regressions,
but cold host startup should be validated in a real host or a subprocess-based
driver because a single process can retain SDK/module state between runs.

Quick smoke:

```bash
make native-perf-bench PERF_BENCH_ARGS="--quick --runs 1 --warmups 0 --duration 0.25"
```

Full default:

```bash
make native-perf-bench
```

Useful JSON output:

```bash
make native-perf-bench PERF_BENCH_ARGS="--json-out /tmp/nilamp_perf/results.json"
```
