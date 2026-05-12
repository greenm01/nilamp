// SPDX-License-Identifier: MIT
//
// Env-gated per-block process() timing for the CLAP and VST3 plug-ins.
//
// Set NILAMP_PROCESS_LOG to a path prefix to enable. The plug-in writes
// `<prefix>.<plugin_kind>.txt` on destroy, containing the block count,
// min/max/mean ns-per-frame, p50 and p95, plus the last N raw samples.
// When the env var is unset every entry/exit pair is a single NULL check.

#ifndef NILAMP_PROCESS_LOG_H
#define NILAMP_PROCESS_LOG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NilampProcessLog NilampProcessLog;

// Creates a logger if NILAMP_PROCESS_LOG is set in the environment. Returns
// NULL otherwise. plugin_kind is appended to the output path so the same
// REAPER session can capture CLAP and VST3 to separate files.
NilampProcessLog *nilamp_process_log_create(const char *plugin_kind);

// Flushes a summary to the configured path and frees the logger.
void nilamp_process_log_destroy(NilampProcessLog *log);

// Returns true if the logger is recording. Callers can use this to skip the
// nilamp_process_log_now()/record() pair entirely when disabled.
bool nilamp_process_log_enabled(const NilampProcessLog *log);

// Returns a monotonic timestamp in nanoseconds, or 0 if log is disabled.
uint64_t nilamp_process_log_now(const NilampProcessLog *log);

// Records one process() invocation. Cheap: writes into a preallocated ring
// buffer and updates a small set of accumulators. Safe to call from the
// realtime audio thread when the logger is enabled. If start_ns is 0 (when
// recording is disabled) the call returns immediately.
void nilamp_process_log_record(NilampProcessLog *log,
                               uint64_t start_ns,
                               uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif
