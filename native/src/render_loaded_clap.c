// SPDX-License-Identifier: MIT
//
// Diagnostic CLAP host: dlopen a CLAP plugin and render a mono float32 WAV
// through it under several stereo-port presentation modes. Used to isolate
// whether the plugin itself emits clean output before involving REAPER.
//
// This is a diagnostic tool, not a test. It deliberately does not assert
// anything; the comparison and pass/fail logic lives in the Python wrapper.

#include "nilamp_dsp.h"

#include <clap/clap.h>

#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NILAMP_PLUGIN_ID "dev.niltempus.nilamp"

typedef enum {
    MODE_SHARED = 0,
    MODE_DISTINCT = 1,
    MODE_MONO_INPUT = 2,
    MODE_INPLACE_MONO = 3,
} Mode;

typedef struct {
    const char *plugin_path;
    const char *input_path;
    const char *output_l_path;
    const char *output_r_path;
    Mode mode;
    uint32_t block;
    int vary_block;
    uint32_t vary_seed;
    double sample_rate;
    float gain_db;
    float volume_pct;
    float bass_pct;
    float mid_pct;
    float treble_pct;
    float sag_pct;
} Args;

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8u);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static void wr16(FILE *f, uint16_t v)
{
    fputc((int)(v & 0xffu), f);
    fputc((int)((v >> 8u) & 0xffu), f);
}

static void wr32(FILE *f, uint32_t v)
{
    fputc((int)(v & 0xffu), f);
    fputc((int)((v >> 8u) & 0xffu), f);
    fputc((int)((v >> 16u) & 0xffu), f);
    fputc((int)((v >> 24u) & 0xffu), f);
}

static int read_file(const char *path, uint8_t **data, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "render_loaded_clap: open %s: %s\n", path, strerror(errno));
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) { fclose(f); return -1; }
    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf); fclose(f); return -1;
    }
    fclose(f);
    *data = buf;
    *size = (size_t)len;
    return 0;
}

static int load_mono_wav_f32(const char *path, float **samples, size_t *frames,
                             uint32_t *sample_rate)
{
    uint8_t *file = NULL;
    size_t size = 0;
    if (read_file(path, &file, &size) != 0) return -1;
    if (size < 12 || memcmp(file, "RIFF", 4) != 0 || memcmp(file + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "render_loaded_clap: not a RIFF/WAVE: %s\n", path);
        free(file);
        return -1;
    }

    uint16_t format_tag = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint32_t sr = 0;
    const uint8_t *data = NULL;
    size_t data_size = 0;

    for (size_t pos = 12; pos + 8 <= size;) {
        const uint8_t *chunk = file + pos;
        const uint32_t chunk_size = rd32(chunk + 4);
        const size_t body = pos + 8;
        if (body + chunk_size > size) break;
        if (memcmp(chunk, "fmt ", 4) == 0 && chunk_size >= 16) {
            format_tag = rd16(file + body);
            channels = rd16(file + body + 2);
            sr = rd32(file + body + 4);
            bits = rd16(file + body + 14);
        } else if (memcmp(chunk, "data", 4) == 0) {
            data = file + body;
            data_size = chunk_size;
        }
        pos = body + chunk_size + (chunk_size & 1u);
    }

    if (!data || channels == 0 || sr == 0 || format_tag != 3 || bits != 32) {
        fprintf(stderr, "render_loaded_clap: require mono float32 WAV\n");
        free(file);
        return -1;
    }
    if (channels != 1) {
        fprintf(stderr, "render_loaded_clap: input must be mono, got %u channels\n",
                channels);
        free(file);
        return -1;
    }

    const size_t n = data_size / sizeof(float);
    float *out = (float *)malloc(n * sizeof(float));
    if (!out) { free(file); return -1; }
    memcpy(out, data, n * sizeof(float));
    free(file);

    *samples = out;
    *frames = n;
    *sample_rate = sr;
    return 0;
}

static int write_stereo_channels_f32(const char *path, const float *samples,
                                     size_t frames, uint32_t sample_rate)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "render_loaded_clap: create %s: %s\n", path, strerror(errno));
        return -1;
    }
    const uint32_t data_size = (uint32_t)(frames * sizeof(float));
    const uint32_t riff_size = 4u + (8u + 16u) + (8u + data_size);
    fwrite("RIFF", 1, 4, f); wr32(f, riff_size);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); wr32(f, 16u);
    wr16(f, 3u); wr16(f, 1u);
    wr32(f, sample_rate);
    wr32(f, sample_rate * 4u);
    wr16(f, 4u); wr16(f, 32u);
    fwrite("data", 1, 4, f); wr32(f, data_size);
    fwrite(samples, sizeof(float), frames, f);
    int ok = ferror(f) == 0;
    fclose(f);
    return ok ? 0 : -1;
}

// ---- minimal CLAP host ----

typedef struct {
    const clap_event_header_t **events;
    uint32_t count;
} EventList;

static uint32_t evt_size(const clap_input_events_t *list)
{
    return ((const EventList *)list->ctx)->count;
}

static const clap_event_header_t *evt_get(const clap_input_events_t *list, uint32_t i)
{
    const EventList *el = (const EventList *)list->ctx;
    return i < el->count ? el->events[i] : NULL;
}

static bool evt_try_push(const clap_output_events_t *list, const clap_event_header_t *e)
{
    (void)list; (void)e; return true;
}

static const void *host_get_extension(const clap_host_t *host, const char *id)
{
    (void)host; (void)id; return NULL;
}

static void host_request_restart(const clap_host_t *h) { (void)h; }
static void host_request_process(const clap_host_t *h) { (void)h; }
static void host_request_callback(const clap_host_t *h) { (void)h; }

static void parse_param_event(clap_event_param_value_t *e, clap_id id, double v)
{
    memset(e, 0, sizeof(*e));
    e->header.size = sizeof(*e);
    e->header.time = 0;
    e->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e->header.type = CLAP_EVENT_PARAM_VALUE;
    e->param_id = id;
    e->note_id = -1;
    e->port_index = -1;
    e->channel = -1;
    e->key = -1;
    e->value = v;
}

static int parse_args(int argc, char **argv, Args *args)
{
    memset(args, 0, sizeof(*args));
    args->block = 512;
    args->sample_rate = 48000.0;
    args->gain_db = 0.0f;
    args->volume_pct = 50.0f;
    args->bass_pct = 50.0f;
    args->mid_pct = 50.0f;
    args->treble_pct = 50.0f;
    args->sag_pct = 50.0f;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (i + 1 >= argc) {
            fprintf(stderr, "render_loaded_clap: missing value for %s\n", a);
            return -1;
        }
        const char *v = argv[++i];
        if (strcmp(a, "--plugin") == 0) args->plugin_path = v;
        else if (strcmp(a, "--input") == 0) args->input_path = v;
        else if (strcmp(a, "--output-l") == 0) args->output_l_path = v;
        else if (strcmp(a, "--output-r") == 0) args->output_r_path = v;
        else if (strcmp(a, "--mode") == 0) {
            if (strcmp(v, "shared") == 0) args->mode = MODE_SHARED;
            else if (strcmp(v, "distinct") == 0) args->mode = MODE_DISTINCT;
            else if (strcmp(v, "mono_input") == 0) args->mode = MODE_MONO_INPUT;
            else if (strcmp(v, "inplace_mono") == 0) args->mode = MODE_INPLACE_MONO;
            else { fprintf(stderr, "bad mode '%s'\n", v); return -1; }
        }
        else if (strcmp(a, "--block") == 0) args->block = (uint32_t)strtoul(v, NULL, 10);
        else if (strcmp(a, "--vary-block") == 0) args->vary_block = (int)strtol(v, NULL, 10);
        else if (strcmp(a, "--vary-seed") == 0) args->vary_seed = (uint32_t)strtoul(v, NULL, 10);
        else if (strcmp(a, "--sample-rate") == 0) args->sample_rate = strtod(v, NULL);
        else if (strcmp(a, "--gain") == 0) args->gain_db = strtof(v, NULL);
        else if (strcmp(a, "--volume") == 0) args->volume_pct = strtof(v, NULL);
        else if (strcmp(a, "--bass") == 0) args->bass_pct = strtof(v, NULL);
        else if (strcmp(a, "--mid") == 0) args->mid_pct = strtof(v, NULL);
        else if (strcmp(a, "--treble") == 0) args->treble_pct = strtof(v, NULL);
        else if (strcmp(a, "--sag") == 0) args->sag_pct = strtof(v, NULL);
        else { fprintf(stderr, "render_loaded_clap: unknown arg %s\n", a); return -1; }
    }
    if (!args->plugin_path || !args->input_path || !args->output_l_path ||
        !args->output_r_path) {
        fprintf(stderr,
                "Usage: render_loaded_clap --plugin P --input MONO.wav "
                "--output-l L.wav --output-r R.wav --mode shared|distinct "
                "[--block N] [--sample-rate SR] [--gain dB] [--volume pct] "
                "[--bass pct] [--mid pct] [--treble pct] [--sag pct]\n");
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    Args args;
    if (parse_args(argc, argv, &args) != 0) return 2;

    float *mono = NULL;
    size_t frames = 0;
    uint32_t wav_sr = 0;
    if (load_mono_wav_f32(args.input_path, &mono, &frames, &wav_sr) != 0) return 1;
    if ((uint32_t)args.sample_rate != wav_sr) {
        fprintf(stderr,
                "render_loaded_clap: sample-rate mismatch (cli=%u wav=%u); using wav\n",
                (uint32_t)args.sample_rate, wav_sr);
        args.sample_rate = (double)wav_sr;
    }

    void *handle = dlopen(args.plugin_path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "render_loaded_clap: dlopen %s: %s\n",
                args.plugin_path, dlerror());
        free(mono); return 1;
    }

    const clap_plugin_entry_t *entry =
        (const clap_plugin_entry_t *)dlsym(handle, "clap_entry");
    if (!entry || !entry->init || !entry->init(args.plugin_path)) {
        fprintf(stderr, "render_loaded_clap: clap_entry init failed\n");
        free(mono); return 1;
    }

    const clap_plugin_factory_t *factory =
        (const clap_plugin_factory_t *)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    if (!factory) { fprintf(stderr, "no factory\n"); free(mono); return 1; }

    clap_host_t host = {
        .clap_version = CLAP_VERSION_INIT,
        .host_data = NULL,
        .name = "render_loaded_clap",
        .vendor = "niltempus",
        .url = "",
        .version = "0.1.0",
        .get_extension = host_get_extension,
        .request_restart = host_request_restart,
        .request_process = host_request_process,
        .request_callback = host_request_callback,
    };

    const clap_plugin_t *plugin = factory->create_plugin(factory, &host,
                                                         NILAMP_PLUGIN_ID);
    if (!plugin || !plugin->init(plugin)) {
        fprintf(stderr, "create/init plugin failed\n"); free(mono); return 1;
    }
    if (!plugin->activate(plugin, args.sample_rate, 1, args.block)) {
        fprintf(stderr, "activate failed\n"); free(mono); return 1;
    }
    plugin->start_processing(plugin);

    // Set parameters via params flush.
    const clap_plugin_params_t *params =
        (const clap_plugin_params_t *)plugin->get_extension(plugin, CLAP_EXT_PARAMS);
    if (params && params->flush) {
        clap_event_param_value_t pe[6];
        const double values[6] = {
            args.gain_db, args.volume_pct, args.bass_pct,
            args.mid_pct, args.treble_pct, args.sag_pct,
        };
        const clap_event_header_t *ptrs[6];
        for (uint32_t i = 0; i < 6; i++) {
            parse_param_event(&pe[i], (clap_id)i, values[i]);
            ptrs[i] = &pe[i].header;
        }
        EventList el = { .events = ptrs, .count = 6 };
        clap_input_events_t in_events = { .ctx = &el, .size = evt_size, .get = evt_get };
        clap_output_events_t out_events = { .ctx = NULL, .try_push = evt_try_push };
        params->flush(plugin, &in_events, &out_events);
    }
    plugin->reset(plugin);

    // Allocate output buffers.
    float *out_l = (float *)calloc(frames, sizeof(float));
    float *out_r = (float *)calloc(frames, sizeof(float));
    // Distinct/inplace_mono modes need a second input copy.
    float *in_r = NULL;
    if (args.mode == MODE_DISTINCT) {
        in_r = (float *)malloc(frames * sizeof(float));
        memcpy(in_r, mono, frames * sizeof(float));
    }

    EventList empty = { .events = NULL, .count = 0 };
    clap_input_events_t in_events = { .ctx = &empty, .size = evt_size, .get = evt_get };
    clap_output_events_t out_events = { .ctx = NULL, .try_push = evt_try_push };

    // Tiny LCG for reproducible block-size jitter.
    uint32_t rng = args.vary_seed ? args.vary_seed : 0xC0FFEEu;

    size_t pos = 0;
    while (pos < frames) {
        uint32_t n;
        if (args.vary_block) {
            rng = rng * 1664525u + 1013904223u;
            // Range [1 .. 2*block-1]
            uint32_t span = (args.block * 2u) - 1u;
            n = 1u + (rng % span);
        } else {
            n = args.block;
        }
        if (n > (uint32_t)(frames - pos)) n = (uint32_t)(frames - pos);

        float *in_left_block = mono + pos;
        float *in_right_block = NULL;
        switch (args.mode) {
        case MODE_SHARED:        in_right_block = in_left_block; break;
        case MODE_DISTINCT:      in_right_block = in_r + pos; break;
        case MODE_MONO_INPUT:    in_right_block = NULL; break;
        case MODE_INPLACE_MONO:  in_right_block = NULL; break;
        }

        float *input_channels[2] = { in_left_block, in_right_block };
        float *output_channels[2] = { out_l + pos, out_r + pos };

        // INPLACE_MONO: declare 1-channel input AND alias output[0] to that
        // input buffer, mimicking REAPER's mono-track-on-stereo-bus pattern
        // where the host hands the same scratch to both directions.
        if (args.mode == MODE_INPLACE_MONO) {
            // Copy input into output[0] up front so input/output[0] share
            // memory (the engine reads input then writes output[0]).
            memcpy(out_l + pos, mono + pos, n * sizeof(float));
            input_channels[0] = out_l + pos;
        }

        uint32_t input_ch_count =
            (args.mode == MODE_MONO_INPUT || args.mode == MODE_INPLACE_MONO) ? 1u : 2u;

        clap_audio_buffer_t input = {
            .data32 = input_channels, .data64 = NULL,
            .channel_count = input_ch_count, .latency = 0, .constant_mask = 0,
        };
        clap_audio_buffer_t output = {
            .data32 = output_channels, .data64 = NULL,
            .channel_count = 2, .latency = 0, .constant_mask = 0,
        };

        clap_process_t process = {
            .steady_time = (int64_t)pos,
            .frames_count = n,
            .transport = NULL,
            .audio_inputs = &input,
            .audio_outputs = &output,
            .audio_inputs_count = 1,
            .audio_outputs_count = 1,
            .in_events = &in_events,
            .out_events = &out_events,
        };

        clap_process_status status = plugin->process(plugin, &process);
        if (status == CLAP_PROCESS_ERROR) {
            fprintf(stderr, "process returned ERROR at pos=%zu\n", pos);
            break;
        }
        pos += n;
    }

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(handle);

    int rc = 0;
    if (write_stereo_channels_f32(args.output_l_path, out_l, frames,
                                  (uint32_t)args.sample_rate) != 0) rc = 1;
    if (write_stereo_channels_f32(args.output_r_path, out_r, frames,
                                  (uint32_t)args.sample_rate) != 0) rc = 1;

    const char *mode_str =
        args.mode == MODE_SHARED ? "shared" :
        args.mode == MODE_DISTINCT ? "distinct" :
        args.mode == MODE_MONO_INPUT ? "mono_input" : "inplace_mono";
    fprintf(stderr,
            "render_loaded_clap: mode=%s block=%u%s sr=%u frames=%zu -> %s, %s\n",
            mode_str, args.block, args.vary_block ? "(varying)" : "",
            (uint32_t)args.sample_rate, frames,
            args.output_l_path, args.output_r_path);

    free(out_l); free(out_r); free(in_r); free(mono);
    return rc;
}
