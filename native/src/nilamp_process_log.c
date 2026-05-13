// SPDX-License-Identifier: MIT
#include "nilamp_process_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define NILAMP_PROCESS_LOG_CAPACITY 65536u

struct NilampProcessLog {
    char path[512];
    uint64_t ns_buf[NILAMP_PROCESS_LOG_CAPACITY];
    uint32_t frames_buf[NILAMP_PROCESS_LOG_CAPACITY];
    uint32_t head;
    uint64_t total_calls;
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t sum_ns;
    uint64_t sum_frames;
};

static uint64_t nilamp_process_log_clock_now(void)
{
#if defined(_WIN32)
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    if (!QueryPerformanceCounter(&counter) || !QueryPerformanceFrequency(&frequency) ||
        frequency.QuadPart <= 0) {
        return 0;
    }
    const uint64_t ticks = (uint64_t)counter.QuadPart;
    const uint64_t hz = (uint64_t)frequency.QuadPart;
    return (ticks / hz) * 1000000000ULL + ((ticks % hz) * 1000000000ULL) / hz;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

NilampProcessLog *nilamp_process_log_create(const char *plugin_kind)
{
    const char *prefix = getenv("NILAMP_PROCESS_LOG");
    if (!prefix || !prefix[0]) {
        return NULL;
    }
    NilampProcessLog *log = (NilampProcessLog *)calloc(1, sizeof(*log));
    if (!log) {
        return NULL;
    }
    snprintf(log->path, sizeof(log->path), "%s.%s.txt", prefix,
             plugin_kind && plugin_kind[0] ? plugin_kind : "unknown");
    log->min_ns = UINT64_MAX;
    log->max_ns = 0;
    return log;
}

bool nilamp_process_log_enabled(const NilampProcessLog *log)
{
    return log != NULL;
}

uint64_t nilamp_process_log_now(const NilampProcessLog *log)
{
    if (!log) {
        return 0;
    }
    return nilamp_process_log_clock_now();
}

void nilamp_process_log_record(NilampProcessLog *log,
                               uint64_t start_ns,
                               uint32_t frames)
{
    if (!log || start_ns == 0) {
        return;
    }
    const uint64_t now = nilamp_process_log_clock_now();
    const uint64_t elapsed = now > start_ns ? now - start_ns : 0;

    const uint32_t slot = log->head;
    log->ns_buf[slot] = elapsed;
    log->frames_buf[slot] = frames;
    log->head = (slot + 1u) % NILAMP_PROCESS_LOG_CAPACITY;

    log->total_calls += 1u;
    log->sum_ns += elapsed;
    log->sum_frames += frames;
    if (elapsed < log->min_ns) {
        log->min_ns = elapsed;
    }
    if (elapsed > log->max_ns) {
        log->max_ns = elapsed;
    }
}

static int nilamp_process_log_cmp_u64(const void *a, const void *b)
{
    const uint64_t lhs = *(const uint64_t *)a;
    const uint64_t rhs = *(const uint64_t *)b;
    return lhs < rhs ? -1 : (lhs > rhs ? 1 : 0);
}

static uint64_t nilamp_process_log_percentile(const uint64_t *sorted,
                                              uint32_t count, double pct)
{
    if (count == 0u) {
        return 0;
    }
    double idx = pct * (double)(count - 1u);
    if (idx < 0.0) {
        idx = 0.0;
    }
    if (idx >= (double)(count - 1u)) {
        idx = (double)(count - 1u);
    }
    return sorted[(uint32_t)idx];
}

void nilamp_process_log_destroy(NilampProcessLog *log)
{
    if (!log) {
        return;
    }

    FILE *fp = fopen(log->path, "w");
    if (fp) {
        const uint32_t kept = log->total_calls < NILAMP_PROCESS_LOG_CAPACITY ?
                                  (uint32_t)log->total_calls :
                                  NILAMP_PROCESS_LOG_CAPACITY;

        uint64_t *sorted = NULL;
        if (kept > 0u) {
            sorted = (uint64_t *)malloc((size_t)kept * sizeof(uint64_t));
        }
        if (sorted && kept > 0u) {
            // Latest kept entries live at head-1, head-2, ... wrap-around.
            for (uint32_t i = 0; i < kept; i++) {
                const uint32_t slot =
                    (log->head + NILAMP_PROCESS_LOG_CAPACITY - 1u - i) %
                    NILAMP_PROCESS_LOG_CAPACITY;
                sorted[i] = log->ns_buf[slot];
            }
            qsort(sorted, (size_t)kept, sizeof(uint64_t),
                  nilamp_process_log_cmp_u64);
        }

        const double mean_ns =
            log->total_calls > 0u ?
                (double)log->sum_ns / (double)log->total_calls :
                0.0;
        const double mean_ns_per_frame =
            log->sum_frames > 0u ?
                (double)log->sum_ns / (double)log->sum_frames :
                0.0;

        const uint64_t p50 =
            sorted ? nilamp_process_log_percentile(sorted, kept, 0.50) : 0;
        const uint64_t p95 =
            sorted ? nilamp_process_log_percentile(sorted, kept, 0.95) : 0;
        const uint64_t p99 =
            sorted ? nilamp_process_log_percentile(sorted, kept, 0.99) : 0;

        fprintf(fp, "# nilamp process log\n");
        fprintf(fp, "path=%s\n", log->path);
        fprintf(fp, "calls=%llu\n", (unsigned long long)log->total_calls);
        fprintf(fp, "frames_total=%llu\n",
                (unsigned long long)log->sum_frames);
        fprintf(fp, "ns_total=%llu\n", (unsigned long long)log->sum_ns);
        fprintf(fp, "ns_min=%llu\n",
                (unsigned long long)(log->total_calls > 0u ? log->min_ns : 0u));
        fprintf(fp, "ns_max=%llu\n", (unsigned long long)log->max_ns);
        fprintf(fp, "ns_mean=%.2f\n", mean_ns);
        fprintf(fp, "ns_per_frame_mean=%.3f\n", mean_ns_per_frame);
        fprintf(fp, "ns_p50=%llu\n", (unsigned long long)p50);
        fprintf(fp, "ns_p95=%llu\n", (unsigned long long)p95);
        fprintf(fp, "ns_p99=%llu\n", (unsigned long long)p99);
        fprintf(fp, "samples_kept=%u\n", kept);
        if (sorted && kept > 0u) {
            fprintf(fp, "# raw samples (sorted ascending, latest %u of %llu)\n",
                    kept, (unsigned long long)log->total_calls);
            for (uint32_t i = 0; i < kept; i++) {
                fprintf(fp, "%llu\n", (unsigned long long)sorted[i]);
            }
        }
        free(sorted);
        fclose(fp);
    }

    free(log);
}
