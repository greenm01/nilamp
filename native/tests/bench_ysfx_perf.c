// SPDX-License-Identifier: MIT
#include "bench_perf_common.h"

#include "ysfx.h"

#include <stdbool.h>

typedef struct {
    NilampBenchArgs common;
    const char *effect_path;
    const char *import_root;
    float ysfx_input_gain;
} Args;

typedef struct {
    ysfx_config_t *config;
    ysfx_t *fx;
} YsfxBench;

static void usage(FILE *f)
{
    fprintf(f,
            "bench_ysfx_perf --effect EFFECT.jsfx [options]\n"
            "\n"
            "Common options:\n"
            "  --phase steady_plugin_process|plugin_lifecycle|reload\n"
            "  --input-kind sine|sweep|silence|di_like\n"
            "  --sample-rate N --duration SEC --block N --runs N --warmups N\n"
            "  --output-raw PATH\n"
            "\n"
            "ysfx options:\n"
            "  --import-root DIR\n"
            "  --ysfx-input-gain X   Input gain before Keller JSFX (default 0.5)\n");
}

static void log_report(intptr_t userdata, ysfx_log_level level, const char *message)
{
    (void)userdata;
    if (level >= ysfx_log_warning) {
        fprintf(stderr, "ysfx %s: %s\n", ysfx_log_level_string(level), message);
    }
}

static int parse_args(int argc, char **argv, Args *args)
{
    memset(args, 0, sizeof(*args));
    nilamp_bench_args_init(&args->common);
    args->ysfx_input_gain = 0.5f;

    for (int i = 1; i < argc; i++) {
        const char *name = argv[i];
        if (strcmp(name, "-h") == 0 || strcmp(name, "--help") == 0) {
            usage(stdout);
            exit(0);
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "error: missing value for %s\n", name);
            return -1;
        }
        const char *value = argv[++i];
        if (strcmp(name, "--effect") == 0) {
            args->effect_path = value;
            continue;
        }
        if (strcmp(name, "--import-root") == 0) {
            args->import_root = value;
            continue;
        }
        if (strcmp(name, "--ysfx-input-gain") == 0) {
            if (nilamp_bench_parse_float(name, value, &args->ysfx_input_gain) != 0) {
                return -1;
            }
            continue;
        }
        const int parsed = nilamp_bench_parse_common_arg(&args->common, name, value);
        if (parsed == 0) {
            continue;
        }
        if (parsed < 0) {
            return -1;
        }
        fprintf(stderr, "error: unknown argument %s\n", name);
        return -1;
    }

    if (!args->effect_path) {
        usage(stderr);
        return -1;
    }
    if (args->common.duration_s <= 0.0) {
        fprintf(stderr, "error: --duration must be positive\n");
        return -1;
    }
    return 0;
}

static int set_slider(ysfx_t *fx, uint32_t index, double value)
{
    const uint32_t zero_based = index - 1u;
    if (!ysfx_slider_exists(fx, zero_based)) {
        fprintf(stderr, "error: slider%u does not exist\n", index);
        return -1;
    }
    ysfx_slider_set_value(fx, zero_based, value, true);
    return 0;
}

static int set_keller_sliders(ysfx_t *fx, const NilampParams *params)
{
    if (set_slider(fx, 1u, params->gain_db) != 0) return -1;
    if (set_slider(fx, 2u, params->tube1) != 0) return -1;
    if (set_slider(fx, 3u, params->volume_pct) != 0) return -1;
    if (set_slider(fx, 4u, params->bass_pct) != 0) return -1;
    if (set_slider(fx, 5u, params->mid_pct) != 0) return -1;
    if (set_slider(fx, 6u, params->treble_pct) != 0) return -1;
    if (set_slider(fx, 7u, params->tone_fmid_dbhz) != 0) return -1;
    if (set_slider(fx, 8u, params->tone_qmid_db) != 0) return -1;
    if (set_slider(fx, 9u, params->phase_splitter) != 0) return -1;
    if (set_slider(fx, 10u, params->spk_res_gain1_db) != 0) return -1;
    if (set_slider(fx, 11u, params->spk_res_gain2_db) != 0) return -1;
    if (set_slider(fx, 12u, params->spk_res_fres_dbhz) != 0) return -1;
    if (set_slider(fx, 13u, params->spk_res_qts_db) != 0) return -1;
    if (set_slider(fx, 14u, params->spk_ind_gain1_db) != 0) return -1;
    if (set_slider(fx, 15u, params->spk_ind_gain2_db) != 0) return -1;
    if (set_slider(fx, 16u, params->spk_ind_find_dbhz) != 0) return -1;
    if (set_slider(fx, 17u, params->output_gain_db) != 0) return -1;
    if (set_slider(fx, 18u, params->gain_comp) != 0) return -1;
    return 0;
}

static void free_ysfx(YsfxBench *bench)
{
    if (!bench) {
        return;
    }
    ysfx_free(bench->fx);
    ysfx_config_free(bench->config);
    memset(bench, 0, sizeof(*bench));
}

static int load_ysfx(const Args *args, YsfxBench *bench)
{
    memset(bench, 0, sizeof(*bench));
    bench->config = ysfx_config_new();
    if (!bench->config) {
        fprintf(stderr, "error: ysfx_config_new failed\n");
        return -1;
    }
    ysfx_set_log_reporter(bench->config, log_report);
    if (args->import_root) {
        ysfx_set_import_root(bench->config, args->import_root);
    } else {
        ysfx_guess_file_roots(bench->config, args->effect_path);
    }
    ysfx_register_builtin_audio_formats(bench->config);

    bench->fx = ysfx_new(bench->config);
    if (!bench->fx) {
        fprintf(stderr, "error: ysfx_new failed\n");
        free_ysfx(bench);
        return -1;
    }
    if (!ysfx_load_file(bench->fx, args->effect_path, 0)) {
        fprintf(stderr, "error: ysfx_load_file failed: %s\n", args->effect_path);
        free_ysfx(bench);
        return -1;
    }
    if (!ysfx_compile(bench->fx, ysfx_compile_no_gfx | ysfx_compile_no_serialize)) {
        fprintf(stderr, "error: ysfx_compile failed: %s\n", args->effect_path);
        free_ysfx(bench);
        return -1;
    }
    ysfx_set_sample_rate(bench->fx, (ysfx_real)args->common.sample_rate);
    ysfx_set_block_size(bench->fx, args->common.block);
    if (set_keller_sliders(bench->fx, &args->common.params) != 0) {
        free_ysfx(bench);
        return -1;
    }
    ysfx_init(bench->fx);
    return 0;
}

static void process_ysfx(YsfxBench *bench, const Args *args, const float *input,
                         float *output, uint32_t frames)
{
    memset(output, 0, sizeof(float) * frames);
    size_t pos = 0;
    while (pos < frames) {
        const uint32_t n = args->common.block < frames - pos ?
                               args->common.block :
                               (uint32_t)(frames - pos);
        const float *ins[1] = { input + pos };
        float *outs[1] = { output + pos };
        ysfx_time_info_t time_info;
        memset(&time_info, 0, sizeof(time_info));
        time_info.tempo = 120.0;
        time_info.playback_state = ysfx_playback_playing;
        time_info.time_position = (ysfx_real)pos / (ysfx_real)args->common.sample_rate;
        time_info.beat_position = time_info.time_position * 2.0;
        time_info.time_signature[0] = 4;
        time_info.time_signature[1] = 4;
        ysfx_set_time_info(bench->fx, &time_info);
        ysfx_process_float(bench->fx, ins, outs, 1, 1, n);
        pos += n;
    }
}

static int run_lifecycle(const Args *args)
{
    for (uint32_t i = 0; i < args->common.warmups + args->common.runs; i++) {
        NilampBenchUsage start = nilamp_bench_usage_now();
        YsfxBench bench;
        const int rc = load_ysfx(args, &bench);
        free_ysfx(&bench);
        NilampBenchUsage usage = nilamp_bench_usage_diff(start, nilamp_bench_usage_now());
        if (rc != 0) {
            return 1;
        }
        if (i >= args->common.warmups) {
            nilamp_bench_print_result("keller_ysfx", &args->common,
                                      i - args->common.warmups, 0u, usage, NULL);
        }
    }
    return 0;
}

static int run_steady(const Args *args)
{
    const uint32_t frames = (uint32_t)llround(args->common.duration_s *
                                             (double)args->common.sample_rate);
    float *input = (float *)calloc(frames, sizeof(float));
    float *output = (float *)calloc(frames, sizeof(float));
    if (!input || !output) {
        fprintf(stderr, "error: allocation failed\n");
        free(input);
        free(output);
        return 1;
    }

    nilamp_bench_fill_input(input, frames, args->common.sample_rate,
                            args->common.input_kind,
                            args->common.input_scale * args->ysfx_input_gain);

    YsfxBench bench;
    if (load_ysfx(args, &bench) != 0) {
        free(input);
        free(output);
        return 1;
    }

    for (uint32_t i = 0; i < args->common.warmups + args->common.runs; i++) {
        ysfx_init(bench.fx);
        NilampBenchUsage start = nilamp_bench_usage_now();
        process_ysfx(&bench, args, input, output, frames);
        NilampBenchUsage usage = nilamp_bench_usage_diff(start, nilamp_bench_usage_now());
        if (i >= args->common.warmups) {
            nilamp_bench_print_result("keller_ysfx", &args->common,
                                      i - args->common.warmups, frames, usage, output);
        }
    }

    int rc = nilamp_bench_write_raw(args->common.output_raw_path, output, frames);
    free_ysfx(&bench);
    free(input);
    free(output);
    return rc == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    Args args;
    if (parse_args(argc, argv, &args) != 0) {
        return 2;
    }
    if (args.common.phase == NILAMP_BENCH_PHASE_STEADY) {
        return run_steady(&args);
    }
    return run_lifecycle(&args);
}
