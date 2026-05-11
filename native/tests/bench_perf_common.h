// SPDX-License-Identifier: MIT
#ifndef NILAMP_BENCH_PERF_COMMON_H
#define NILAMP_BENCH_PERF_COMMON_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#include "nilamp_dsp.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/resource.h>
#endif

#define NILAMP_BENCH_PI 3.14159265358979323846

typedef enum {
    NILAMP_BENCH_PHASE_STEADY = 0,
    NILAMP_BENCH_PHASE_LIFECYCLE = 1,
    NILAMP_BENCH_PHASE_RELOAD = 2,
} NilampBenchPhase;

typedef enum {
    NILAMP_BENCH_INPUT_SINE = 0,
    NILAMP_BENCH_INPUT_SWEEP = 1,
    NILAMP_BENCH_INPUT_SILENCE = 2,
    NILAMP_BENCH_INPUT_DI_LIKE = 3,
} NilampBenchInputKind;

typedef struct {
    double wall_s;
    double user_s;
    double sys_s;
    long max_rss_kb;
} NilampBenchUsage;

typedef struct {
    const char *phase_name;
    NilampBenchPhase phase;
    const char *input_name;
    NilampBenchInputKind input_kind;
    const char *output_raw_path;
    uint32_t sample_rate;
    uint32_t block;
    uint32_t runs;
    uint32_t warmups;
    double duration_s;
    float input_scale;
    NilampParams params;
} NilampBenchArgs;

static inline double nilamp_bench_time_now(void)
{
#if defined(_WIN32)
    LARGE_INTEGER freq;
    LARGE_INTEGER ticks;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&ticks);
    return (double)ticks.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

static inline NilampBenchUsage nilamp_bench_usage_now(void)
{
    NilampBenchUsage usage;
    memset(&usage, 0, sizeof(usage));
#if defined(_WIN32)
    usage.wall_s = nilamp_bench_time_now();
#else
    struct rusage ru;
    memset(&ru, 0, sizeof(ru));
    getrusage(RUSAGE_SELF, &ru);
    usage.wall_s = nilamp_bench_time_now();
    usage.user_s = (double)ru.ru_utime.tv_sec + (double)ru.ru_utime.tv_usec * 1e-6;
    usage.sys_s = (double)ru.ru_stime.tv_sec + (double)ru.ru_stime.tv_usec * 1e-6;
#if defined(__APPLE__)
    usage.max_rss_kb = (long)(ru.ru_maxrss / 1024);
#else
    usage.max_rss_kb = ru.ru_maxrss;
#endif
#endif
    return usage;
}

static inline NilampBenchUsage nilamp_bench_usage_diff(NilampBenchUsage start,
                                                       NilampBenchUsage end)
{
    NilampBenchUsage diff;
    diff.wall_s = end.wall_s - start.wall_s;
    diff.user_s = end.user_s - start.user_s;
    diff.sys_s = end.sys_s - start.sys_s;
    diff.max_rss_kb = end.max_rss_kb;
    return diff;
}

static inline double nilamp_bench_cpu_pct(NilampBenchUsage usage)
{
    return usage.wall_s > 0.0 ? (usage.user_s + usage.sys_s) / usage.wall_s * 100.0 : 0.0;
}

static inline int nilamp_bench_parse_u32(const char *name, const char *value,
                                         uint32_t min_value, uint32_t max_value,
                                         uint32_t *out)
{
    char *end = NULL;
    errno = 0;
    const unsigned long parsed = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed < min_value ||
        parsed > max_value) {
        fprintf(stderr, "error: %s: invalid value '%s'\n", name, value);
        return -1;
    }
    *out = (uint32_t)parsed;
    return 0;
}

static inline int nilamp_bench_parse_double(const char *name, const char *value,
                                            double *out)
{
    char *end = NULL;
    errno = 0;
    const double parsed = strtod(value, &end);
    if (errno != 0 || end == value || *end != '\0' || !isfinite(parsed)) {
        fprintf(stderr, "error: %s: invalid value '%s'\n", name, value);
        return -1;
    }
    *out = parsed;
    return 0;
}

static inline int nilamp_bench_parse_float(const char *name, const char *value,
                                           float *out)
{
    double parsed = 0.0;
    if (nilamp_bench_parse_double(name, value, &parsed) != 0) {
        return -1;
    }
    *out = (float)parsed;
    return 0;
}

static inline int nilamp_bench_parse_enum_arg(const char *name, const char *value,
                                              NilampParamId id, float *out)
{
    const NilampControlSpec *spec = nilamp_control_spec((uint32_t)id);
    if (!spec || spec->display != NILAMP_CONTROL_DISPLAY_ENUM || !spec->enum_names) {
        fprintf(stderr, "error: %s: enum spec unavailable\n", name);
        return -1;
    }
    for (uint32_t i = 0; i < spec->enum_count; i++) {
        if (strcmp(value, spec->enum_names[i]) == 0) {
            *out = (float)i;
            return 0;
        }
    }
    uint32_t parsed = 0;
    if (nilamp_bench_parse_u32(name, value, (uint32_t)spec->min_value,
                               (uint32_t)spec->max_value, &parsed) != 0) {
        return -1;
    }
    *out = (float)parsed;
    return 0;
}

static inline const char *nilamp_bench_phase_name(NilampBenchPhase phase)
{
    switch (phase) {
    case NILAMP_BENCH_PHASE_STEADY:
        return "steady_plugin_process";
    case NILAMP_BENCH_PHASE_LIFECYCLE:
        return "plugin_lifecycle";
    case NILAMP_BENCH_PHASE_RELOAD:
        return "reload";
    }
    return "unknown";
}

static inline int nilamp_bench_set_phase(NilampBenchArgs *args, const char *value)
{
    if (strcmp(value, "steady_plugin_process") == 0 || strcmp(value, "steady") == 0) {
        args->phase = NILAMP_BENCH_PHASE_STEADY;
    } else if (strcmp(value, "plugin_lifecycle") == 0 || strcmp(value, "lifecycle") == 0) {
        args->phase = NILAMP_BENCH_PHASE_LIFECYCLE;
    } else if (strcmp(value, "reload") == 0) {
        args->phase = NILAMP_BENCH_PHASE_RELOAD;
    } else {
        fprintf(stderr, "error: --phase: unknown phase '%s'\n", value);
        return -1;
    }
    args->phase_name = nilamp_bench_phase_name(args->phase);
    return 0;
}

static inline int nilamp_bench_set_input_kind(NilampBenchArgs *args, const char *value)
{
    if (strcmp(value, "sine") == 0) {
        args->input_kind = NILAMP_BENCH_INPUT_SINE;
    } else if (strcmp(value, "sweep") == 0) {
        args->input_kind = NILAMP_BENCH_INPUT_SWEEP;
    } else if (strcmp(value, "silence") == 0) {
        args->input_kind = NILAMP_BENCH_INPUT_SILENCE;
    } else if (strcmp(value, "di_like") == 0 || strcmp(value, "di-like") == 0) {
        args->input_kind = NILAMP_BENCH_INPUT_DI_LIKE;
    } else {
        fprintf(stderr, "error: --input-kind: unknown input '%s'\n", value);
        return -1;
    }
    args->input_name = value;
    return 0;
}

static inline void nilamp_bench_args_init(NilampBenchArgs *args)
{
    memset(args, 0, sizeof(*args));
    args->phase = NILAMP_BENCH_PHASE_STEADY;
    args->phase_name = nilamp_bench_phase_name(args->phase);
    args->input_kind = NILAMP_BENCH_INPUT_SINE;
    args->input_name = "sine";
    args->sample_rate = 48000u;
    args->block = 64u;
    args->runs = 5u;
    args->warmups = 1u;
    args->duration_s = 8.0;
    args->input_scale = 1.0f;
    args->params = nilamp_default_params();
}

static inline double nilamp_bench_param_value(const NilampParams *params, NilampParamId id)
{
    switch (id) {
    case NILAMP_PARAM_GAIN_DB:
        return params->gain_db;
    case NILAMP_PARAM_VOLUME_PCT:
        return params->volume_pct;
    case NILAMP_PARAM_BASS_PCT:
        return params->bass_pct;
    case NILAMP_PARAM_MID_PCT:
        return params->mid_pct;
    case NILAMP_PARAM_TREBLE_PCT:
        return params->treble_pct;
    case NILAMP_PARAM_SAG_PCT:
        return params->sag_pct;
    case NILAMP_PARAM_OUTPUT_GAIN_DB:
        return params->output_gain_db;
    case NILAMP_PARAM_TONE_FMID_DBHZ:
        return params->tone_fmid_dbhz;
    case NILAMP_PARAM_TONE_QMID_DB:
        return params->tone_qmid_db;
    case NILAMP_PARAM_SPK_RES_GAIN1_DB:
        return params->spk_res_gain1_db;
    case NILAMP_PARAM_SPK_RES_GAIN2_DB:
        return params->spk_res_gain2_db;
    case NILAMP_PARAM_SPK_RES_FRES_DBHZ:
        return params->spk_res_fres_dbhz;
    case NILAMP_PARAM_SPK_RES_QTS_DB:
        return params->spk_res_qts_db;
    case NILAMP_PARAM_SPK_IND_GAIN1_DB:
        return params->spk_ind_gain1_db;
    case NILAMP_PARAM_SPK_IND_GAIN2_DB:
        return params->spk_ind_gain2_db;
    case NILAMP_PARAM_SPK_IND_FIND_DBHZ:
        return params->spk_ind_find_dbhz;
    case NILAMP_PARAM_GAIN_COMP:
        return params->gain_comp;
    case NILAMP_PARAM_TUBE1:
        return params->tube1;
    case NILAMP_PARAM_PHASE_SPLITTER:
        return params->phase_splitter;
    case NILAMP_PARAM_COUNT:
    default:
        return 0.0;
    }
}

static inline int nilamp_bench_parse_common_arg(NilampBenchArgs *args,
                                                const char *name,
                                                const char *value)
{
    if (strcmp(name, "--phase") == 0) {
        return nilamp_bench_set_phase(args, value);
    }
    if (strcmp(name, "--input-kind") == 0) {
        return nilamp_bench_set_input_kind(args, value);
    }
    if (strcmp(name, "--sample-rate") == 0) {
        return nilamp_bench_parse_u32(name, value, 1u, 384000u, &args->sample_rate);
    }
    if (strcmp(name, "--block") == 0) {
        return nilamp_bench_parse_u32(name, value, 1u, 8192u, &args->block);
    }
    if (strcmp(name, "--runs") == 0) {
        return nilamp_bench_parse_u32(name, value, 1u, 100000u, &args->runs);
    }
    if (strcmp(name, "--warmups") == 0) {
        return nilamp_bench_parse_u32(name, value, 0u, 100000u, &args->warmups);
    }
    if (strcmp(name, "--duration") == 0) {
        return nilamp_bench_parse_double(name, value, &args->duration_s);
    }
    if (strcmp(name, "--input-scale") == 0) {
        return nilamp_bench_parse_float(name, value, &args->input_scale);
    }
    if (strcmp(name, "--output-raw") == 0) {
        args->output_raw_path = value;
        return 0;
    }
    if (strcmp(name, "--gain") == 0) {
        return nilamp_bench_parse_float(name, value, &args->params.gain_db);
    }
    if (strcmp(name, "--volume") == 0) {
        return nilamp_bench_parse_float(name, value, &args->params.volume_pct);
    }
    if (strcmp(name, "--bass") == 0) {
        return nilamp_bench_parse_float(name, value, &args->params.bass_pct);
    }
    if (strcmp(name, "--mid") == 0) {
        return nilamp_bench_parse_float(name, value, &args->params.mid_pct);
    }
    if (strcmp(name, "--treble") == 0) {
        return nilamp_bench_parse_float(name, value, &args->params.treble_pct);
    }
    if (strcmp(name, "--sag") == 0) {
        return nilamp_bench_parse_float(name, value, &args->params.sag_pct);
    }
    if (strcmp(name, "--output-gain") == 0) {
        return nilamp_bench_parse_float(name, value, &args->params.output_gain_db);
    }
    if (strcmp(name, "--fmid") == 0) {
        return nilamp_bench_parse_float(name, value, &args->params.tone_fmid_dbhz);
    }
    if (strcmp(name, "--qmid") == 0) {
        return nilamp_bench_parse_float(name, value, &args->params.tone_qmid_db);
    }
    if (strcmp(name, "--res-gain1") == 0) {
        return nilamp_bench_parse_float(name, value, &args->params.spk_res_gain1_db);
    }
    if (strcmp(name, "--res-gain2") == 0) {
        return nilamp_bench_parse_float(name, value, &args->params.spk_res_gain2_db);
    }
    if (strcmp(name, "--res-fres") == 0) {
        return nilamp_bench_parse_float(name, value, &args->params.spk_res_fres_dbhz);
    }
    if (strcmp(name, "--res-qts") == 0) {
        return nilamp_bench_parse_float(name, value, &args->params.spk_res_qts_db);
    }
    if (strcmp(name, "--ind-gain1") == 0) {
        return nilamp_bench_parse_float(name, value, &args->params.spk_ind_gain1_db);
    }
    if (strcmp(name, "--ind-gain2") == 0) {
        return nilamp_bench_parse_float(name, value, &args->params.spk_ind_gain2_db);
    }
    if (strcmp(name, "--ind-find") == 0) {
        return nilamp_bench_parse_float(name, value, &args->params.spk_ind_find_dbhz);
    }
    if (strcmp(name, "--gcomp") == 0) {
        return nilamp_bench_parse_enum_arg(name, value, NILAMP_PARAM_GAIN_COMP,
                                           &args->params.gain_comp);
    }
    if (strcmp(name, "--tube1") == 0) {
        return nilamp_bench_parse_enum_arg(name, value, NILAMP_PARAM_TUBE1,
                                           &args->params.tube1);
    }
    if (strcmp(name, "--splitter") == 0) {
        return nilamp_bench_parse_enum_arg(name, value, NILAMP_PARAM_PHASE_SPLITTER,
                                           &args->params.phase_splitter);
    }
    return 1;
}

static inline void nilamp_bench_fill_input(float *dst, uint32_t frames,
                                           uint32_t sample_rate,
                                           NilampBenchInputKind kind,
                                           float scale)
{
    const double sr = (double)sample_rate;
    const double duration_s = (double)frames / sr;
    for (uint32_t i = 0; i < frames; i++) {
        const double t = (double)i / sr;
        double x = 0.0;
        if (kind == NILAMP_BENCH_INPUT_SINE) {
            x = 0.15 * sin(2.0 * NILAMP_BENCH_PI * 440.0 * t);
        } else if (kind == NILAMP_BENCH_INPUT_SWEEP) {
            const double f0 = 40.0;
            const double f1 = 12000.0;
            const double ratio = f1 / f0;
            const double phase = 2.0 * NILAMP_BENCH_PI * f0 * duration_s /
                                 log(ratio) *
                                 (exp(t / duration_s * log(ratio)) - 1.0);
            x = 0.08 * sin(phase);
        } else if (kind == NILAMP_BENCH_INPUT_DI_LIKE) {
            const double env = exp(-1.4 * fmod(t, 0.75));
            x = env * (0.055 * sin(2.0 * NILAMP_BENCH_PI * 110.0 * t) +
                       0.040 * sin(2.0 * NILAMP_BENCH_PI * 220.0 * t + 0.35) +
                       0.025 * sin(2.0 * NILAMP_BENCH_PI * 330.0 * t + 0.90) +
                       0.012 * sin(2.0 * NILAMP_BENCH_PI * 660.0 * t));
        }
        dst[i] = (float)(x * (double)scale);
    }
}

static inline double nilamp_bench_peak(const float *samples, uint32_t frames)
{
    double peak = 0.0;
    for (uint32_t i = 0; i < frames; i++) {
        const double x = fabs((double)samples[i]);
        if (x > peak) {
            peak = x;
        }
    }
    return peak;
}

static inline double nilamp_bench_rms(const float *samples, uint32_t frames)
{
    if (frames == 0) {
        return 0.0;
    }
    double sum = 0.0;
    for (uint32_t i = 0; i < frames; i++) {
        const double x = (double)samples[i];
        sum += x * x;
    }
    return sqrt(sum / (double)frames);
}

static inline int nilamp_bench_write_raw(const char *path, const float *samples,
                                         uint32_t frames)
{
    if (!path) {
        return 0;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "error: creating %s: %s\n", path, strerror(errno));
        return -1;
    }
    const size_t written = fwrite(samples, sizeof(float), frames, f);
    const int ok = written == frames && ferror(f) == 0;
    fclose(f);
    return ok ? 0 : -1;
}

static inline void nilamp_bench_print_result(const char *surface,
                                             const NilampBenchArgs *args,
                                             uint32_t run,
                                             uint32_t frames,
                                             NilampBenchUsage usage,
                                             const float *output)
{
    const double audio_s = args->sample_rate > 0 ?
                               (double)frames / (double)args->sample_rate :
                               0.0;
    const double realtime = usage.wall_s > 0.0 ? audio_s / usage.wall_s : 0.0;
    const double ns_per_frame = frames > 0 ? usage.wall_s * 1e9 / (double)frames : 0.0;
    const double peak = output ? nilamp_bench_peak(output, frames) : 0.0;
    const double rms = output ? nilamp_bench_rms(output, frames) : 0.0;
    printf("{\"surface\":\"%s\",\"phase\":\"%s\",\"run\":%u,"
           "\"input\":\"%s\",\"block\":%u,\"frames\":%u,"
           "\"sample_rate\":%u,\"wall_s\":%.9g,\"user_s\":%.9g,"
           "\"sys_s\":%.9g,\"cpu_pct\":%.9g,\"realtime_factor\":%.9g,"
           "\"ns_per_frame\":%.9g,\"max_rss_kb\":%ld,"
           "\"peak\":%.9g,\"rms\":%.9g}\n",
           surface,
           args->phase_name,
           run,
           args->input_name,
           args->block,
           frames,
           args->sample_rate,
           usage.wall_s,
           usage.user_s,
           usage.sys_s,
           nilamp_bench_cpu_pct(usage),
           realtime,
           ns_per_frame,
           usage.max_rss_kb,
           peak,
           rms);
    fflush(stdout);
}

#endif
