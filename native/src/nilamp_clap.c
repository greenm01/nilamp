// SPDX-License-Identifier: MIT
#include "nilamp_dsp.h"

#include <clap/clap.h>

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NILAMP_PLUGIN_ID "dev.niltempus.nilamp"
#define NILAMP_STATE_MAGIC 0x4e4c4150u
#define NILAMP_STATE_VERSION 1u

typedef enum NilampParamId {
    NILAMP_PARAM_GAIN_DB = 0,
    NILAMP_PARAM_VOLUME_PCT = 1,
    NILAMP_PARAM_BASS_PCT = 2,
    NILAMP_PARAM_MID_PCT = 3,
    NILAMP_PARAM_TREBLE_PCT = 4,
    NILAMP_PARAM_SAG_PCT = 5,
    NILAMP_PARAM_COUNT = 6,
} NilampParamId;

typedef struct NilampParamSpec {
    clap_id id;
    const char *name;
    const char *module;
    const char *unit;
    double min_value;
    double max_value;
    double default_value;
} NilampParamSpec;

typedef struct NilampClap {
    clap_plugin_t plugin;
    const clap_host_t *host;
    const clap_host_params_t *host_params;
    NilampEngine *engines[2];
    NilampParams params;
    double sample_rate;
    bool active;
} NilampClap;

typedef struct NilampStateBlob {
    uint32_t magic;
    uint32_t version;
    float values[NILAMP_PARAM_COUNT];
} NilampStateBlob;

static const NilampParamSpec nilamp_param_specs[NILAMP_PARAM_COUNT] = {
    {NILAMP_PARAM_GAIN_DB, "Gain", "Amp", "dB", 0.0, 24.0, 0.0},
    {NILAMP_PARAM_VOLUME_PCT, "Volume", "Amp", "%", 0.0, 100.0, 50.0},
    {NILAMP_PARAM_BASS_PCT, "Bass", "Tone", "%", 0.0, 100.0, 50.0},
    {NILAMP_PARAM_MID_PCT, "Mid", "Tone", "%", 0.0, 100.0, 50.0},
    {NILAMP_PARAM_TREBLE_PCT, "Treble", "Tone", "%", 0.0, 100.0, 50.0},
    {NILAMP_PARAM_SAG_PCT, "Sag", "Power", "%", 0.0, 100.0, 50.0},
};

static const char *const nilamp_features[] = {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_DISTORTION,
    CLAP_PLUGIN_FEATURE_STEREO,
    NULL,
};

static const clap_plugin_descriptor_t nilamp_descriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = NILAMP_PLUGIN_ID,
    .name = "nilamp",
    .vendor = "niltempus",
    .url = "",
    .manual_url = "",
    .support_url = "",
    .version = "0.1.0",
    .description = "Native C guitar amp model",
    .features = nilamp_features,
};

static NilampClap *nilamp_from_plugin(const clap_plugin_t *plugin)
{
    return plugin ? (NilampClap *)plugin->plugin_data : NULL;
}

static double nilamp_clamp(double value, const NilampParamSpec *spec)
{
    if (!isfinite(value)) {
        return spec->default_value;
    }
    if (value < spec->min_value) {
        return spec->min_value;
    }
    if (value > spec->max_value) {
        return spec->max_value;
    }
    return value;
}

static const NilampParamSpec *nilamp_find_param(clap_id id)
{
    for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
        if (nilamp_param_specs[i].id == id) {
            return &nilamp_param_specs[i];
        }
    }
    return NULL;
}

static double nilamp_get_param_value(const NilampParams *params, clap_id id)
{
    switch ((NilampParamId)id) {
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
    case NILAMP_PARAM_COUNT:
    default:
        return 0.0;
    }
}

static bool nilamp_set_param_value(NilampParams *params, clap_id id, double value)
{
    const NilampParamSpec *spec = nilamp_find_param(id);
    if (!spec) {
        return false;
    }

    const float clamped = (float)nilamp_clamp(value, spec);
    switch ((NilampParamId)id) {
    case NILAMP_PARAM_GAIN_DB:
        params->gain_db = clamped;
        return true;
    case NILAMP_PARAM_VOLUME_PCT:
        params->volume_pct = clamped;
        return true;
    case NILAMP_PARAM_BASS_PCT:
        params->bass_pct = clamped;
        return true;
    case NILAMP_PARAM_MID_PCT:
        params->mid_pct = clamped;
        return true;
    case NILAMP_PARAM_TREBLE_PCT:
        params->treble_pct = clamped;
        return true;
    case NILAMP_PARAM_SAG_PCT:
        params->sag_pct = clamped;
        return true;
    case NILAMP_PARAM_COUNT:
    default:
        return false;
    }
}

static void nilamp_apply_params(NilampClap *plug)
{
    for (uint32_t i = 0; i < 2; i++) {
        if (plug->engines[i]) {
            nilamp_engine_set_params(plug->engines[i], &plug->params);
        }
    }
}

static void nilamp_destroy_engines(NilampClap *plug)
{
    for (uint32_t i = 0; i < 2; i++) {
        nilamp_engine_destroy(plug->engines[i]);
        plug->engines[i] = NULL;
    }
}

static bool nilamp_create_engines(NilampClap *plug, double sample_rate)
{
    nilamp_destroy_engines(plug);

    plug->engines[0] = nilamp_engine_create(sample_rate);
    plug->engines[1] = nilamp_engine_create(sample_rate);
    if (!plug->engines[0] || !plug->engines[1]) {
        nilamp_destroy_engines(plug);
        return false;
    }

    plug->sample_rate = sample_rate;
    nilamp_apply_params(plug);
    return true;
}

static void nilamp_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    (void)snprintf(dst, dst_size, "%s", src);
}

static bool nilamp_init(const clap_plugin_t *plugin)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug || !plug->host || !clap_version_is_compatible(plug->host->clap_version)) {
        return false;
    }

    if (plug->host->get_extension) {
        plug->host_params =
            (const clap_host_params_t *)plug->host->get_extension(plug->host, CLAP_EXT_PARAMS);
    }
    return true;
}

static void nilamp_destroy(const clap_plugin_t *plugin)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug) {
        return;
    }
    nilamp_destroy_engines(plug);
    free(plug);
}

static bool nilamp_activate(const clap_plugin_t *plugin, double sample_rate,
                            uint32_t min_frames_count, uint32_t max_frames_count)
{
    (void)min_frames_count;
    (void)max_frames_count;

    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug || !isfinite(sample_rate) || sample_rate <= 0.0) {
        return false;
    }

    if (!nilamp_create_engines(plug, sample_rate)) {
        return false;
    }
    plug->active = true;
    return true;
}

static void nilamp_deactivate(const clap_plugin_t *plugin)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug) {
        return;
    }
    plug->active = false;
    nilamp_destroy_engines(plug);
}

static bool nilamp_start_processing(const clap_plugin_t *plugin)
{
    return nilamp_from_plugin(plugin) != NULL;
}

static void nilamp_stop_processing(const clap_plugin_t *plugin)
{
    (void)plugin;
}

static void nilamp_reset(const clap_plugin_t *plugin)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug) {
        return;
    }

    for (uint32_t i = 0; i < 2; i++) {
        if (plug->engines[i]) {
            nilamp_engine_reset(plug->engines[i]);
            nilamp_engine_set_params(plug->engines[i], &plug->params);
        }
    }
}

static void nilamp_zero_channel(float *output, uint32_t offset, uint32_t nframes)
{
    if (output && nframes > 0) {
        memset(output + offset, 0, sizeof(float) * nframes);
    }
}

static void nilamp_process_channel(NilampEngine *engine, const clap_audio_buffer_t *input,
                                   uint32_t input_channel, clap_audio_buffer_t *output,
                                   uint32_t output_channel, uint32_t offset,
                                   uint32_t nframes)
{
    float *out = output->data32[output_channel];
    if (!out) {
        return;
    }
    if (!engine) {
        nilamp_zero_channel(out, offset, nframes);
        return;
    }

    const bool has_input = input && input->data32 && input_channel < input->channel_count &&
                           input->data32[input_channel];
    const bool input_is_constant =
        has_input && ((input->constant_mask & (UINT64_C(1) << input_channel)) != 0u);

    if (has_input && !input_is_constant) {
        nilamp_engine_process(engine, input->data32[input_channel] + offset, out + offset, nframes);
        return;
    }

    const float zero = 0.0f;
    const float *sample = has_input ? input->data32[input_channel] : &zero;
    for (uint32_t i = 0; i < nframes; i++) {
        nilamp_engine_process(engine, sample, out + offset + i, 1);
    }
}

static bool nilamp_process_segment(NilampClap *plug, const clap_process_t *process,
                                   uint32_t start, uint32_t end)
{
    if (end <= start) {
        return true;
    }
    if (!process || process->audio_outputs_count == 0 || !process->audio_outputs) {
        return false;
    }

    const clap_audio_buffer_t *input =
        (process->audio_inputs_count > 0 && process->audio_inputs) ? &process->audio_inputs[0] : NULL;
    clap_audio_buffer_t *output = &process->audio_outputs[0];
    if (!output->data32) {
        return false;
    }

    const uint32_t frames = end - start;
    const uint32_t output_channels = output->channel_count;
    const uint32_t process_channels = output_channels < 2u ? output_channels : 2u;
    const uint32_t input_channels = input ? input->channel_count : 0u;

    if (process_channels >= 2u && input && input->data32 && input_channels == 1u &&
        input->data32[0] && output->data32[0] == input->data32[0]) {
        nilamp_process_channel(plug->engines[1], input, 0u, output, 1u, start, frames);
        nilamp_process_channel(plug->engines[0], input, 0u, output, 0u, start, frames);
    } else {
        for (uint32_t ch = 0; ch < process_channels; ch++) {
            const uint32_t input_channel = input && input_channels > 1u ? ch : 0u;
            nilamp_process_channel(plug->engines[ch], input, input_channel, output, ch, start, frames);
        }
    }

    for (uint32_t ch = 2; ch < output_channels; ch++) {
        if (output->data32[ch]) {
            nilamp_zero_channel(output->data32[ch], start, frames);
        }
    }
    return true;
}

static void nilamp_handle_event(NilampClap *plug, const clap_event_header_t *event)
{
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID ||
        event->type != CLAP_EVENT_PARAM_VALUE ||
        event->size < sizeof(clap_event_param_value_t)) {
        return;
    }

    const clap_event_param_value_t *param_event = (const clap_event_param_value_t *)event;
    if (nilamp_set_param_value(&plug->params, param_event->param_id, param_event->value)) {
        nilamp_apply_params(plug);
    }
}

static clap_process_status nilamp_process(const clap_plugin_t *plugin,
                                          const clap_process_t *process)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug || !process || process->frames_count == 0) {
        return CLAP_PROCESS_CONTINUE;
    }

    uint32_t cursor = 0;
    const uint32_t event_count =
        process->in_events ? process->in_events->size(process->in_events) : 0u;

    for (uint32_t i = 0; i < event_count; i++) {
        const clap_event_header_t *event = process->in_events->get(process->in_events, i);
        const uint32_t event_time =
            event && event->time < process->frames_count ? event->time : process->frames_count;
        if (!nilamp_process_segment(plug, process, cursor, event_time)) {
            return CLAP_PROCESS_ERROR;
        }
        nilamp_handle_event(plug, event);
        cursor = event_time;
    }

    if (!nilamp_process_segment(plug, process, cursor, process->frames_count)) {
        return CLAP_PROCESS_ERROR;
    }
    return CLAP_PROCESS_CONTINUE;
}

static const void *nilamp_get_extension(const clap_plugin_t *plugin, const char *id);

static void nilamp_on_main_thread(const clap_plugin_t *plugin)
{
    (void)plugin;
}

static uint32_t nilamp_audio_ports_count(const clap_plugin_t *plugin, bool is_input)
{
    (void)plugin;
    (void)is_input;
    return 1;
}

static bool nilamp_audio_ports_get(const clap_plugin_t *plugin, uint32_t index,
                                   bool is_input, clap_audio_port_info_t *info)
{
    (void)plugin;
    if (index != 0 || !info) {
        return false;
    }

    info->id = is_input ? 0u : 1u;
    nilamp_copy_text(info->name, sizeof(info->name), is_input ? "Audio In" : "Audio Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = is_input ? 1u : 0u;
    return true;
}

static const clap_plugin_audio_ports_t nilamp_audio_ports_ext = {
    .count = nilamp_audio_ports_count,
    .get = nilamp_audio_ports_get,
};

static uint32_t nilamp_params_count(const clap_plugin_t *plugin)
{
    (void)plugin;
    return NILAMP_PARAM_COUNT;
}

static bool nilamp_params_get_info(const clap_plugin_t *plugin, uint32_t param_index,
                                   clap_param_info_t *param_info)
{
    (void)plugin;
    if (!param_info || param_index >= NILAMP_PARAM_COUNT) {
        return false;
    }

    const NilampParamSpec *spec = &nilamp_param_specs[param_index];
    memset(param_info, 0, sizeof(*param_info));
    param_info->id = spec->id;
    param_info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS;
    param_info->cookie = (void *)spec;
    nilamp_copy_text(param_info->name, sizeof(param_info->name), spec->name);
    nilamp_copy_text(param_info->module, sizeof(param_info->module), spec->module);
    param_info->min_value = spec->min_value;
    param_info->max_value = spec->max_value;
    param_info->default_value = spec->default_value;
    return true;
}

static bool nilamp_params_get_value(const clap_plugin_t *plugin, clap_id param_id,
                                    double *out_value)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug || !out_value || !nilamp_find_param(param_id)) {
        return false;
    }
    *out_value = nilamp_get_param_value(&plug->params, param_id);
    return true;
}

static bool nilamp_params_value_to_text(const clap_plugin_t *plugin, clap_id param_id,
                                        double value, char *out_buffer,
                                        uint32_t out_buffer_capacity)
{
    (void)plugin;
    const NilampParamSpec *spec = nilamp_find_param(param_id);
    if (!spec || !out_buffer || out_buffer_capacity == 0) {
        return false;
    }

    const double clamped = nilamp_clamp(value, spec);
    const int written = snprintf(out_buffer, out_buffer_capacity, "%.3f %s", clamped, spec->unit);
    return written >= 0 && (uint32_t)written < out_buffer_capacity;
}

static bool nilamp_params_text_to_value(const clap_plugin_t *plugin, clap_id param_id,
                                        const char *param_value_text, double *out_value)
{
    (void)plugin;
    const NilampParamSpec *spec = nilamp_find_param(param_id);
    if (!spec || !param_value_text || !out_value) {
        return false;
    }

    errno = 0;
    char *end = NULL;
    const double parsed = strtod(param_value_text, &end);
    if (end == param_value_text || errno == ERANGE || !isfinite(parsed)) {
        return false;
    }
    *out_value = nilamp_clamp(parsed, spec);
    return true;
}

static void nilamp_params_flush(const clap_plugin_t *plugin,
                                const clap_input_events_t *in,
                                const clap_output_events_t *out)
{
    (void)out;
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug || !in) {
        return;
    }

    const uint32_t event_count = in->size(in);
    for (uint32_t i = 0; i < event_count; i++) {
        nilamp_handle_event(plug, in->get(in, i));
    }
}

static const clap_plugin_params_t nilamp_params_ext = {
    .count = nilamp_params_count,
    .get_info = nilamp_params_get_info,
    .get_value = nilamp_params_get_value,
    .value_to_text = nilamp_params_value_to_text,
    .text_to_value = nilamp_params_text_to_value,
    .flush = nilamp_params_flush,
};

static bool nilamp_write_all(const clap_ostream_t *stream, const void *data, uint64_t size)
{
    const uint8_t *cursor = (const uint8_t *)data;
    while (size > 0) {
        const int64_t written = stream->write(stream, cursor, size);
        if (written <= 0) {
            return false;
        }
        cursor += (uint64_t)written;
        size -= (uint64_t)written;
    }
    return true;
}

static bool nilamp_read_all(const clap_istream_t *stream, void *data, uint64_t size)
{
    uint8_t *cursor = (uint8_t *)data;
    while (size > 0) {
        const int64_t bytes_read = stream->read(stream, cursor, size);
        if (bytes_read <= 0) {
            return false;
        }
        cursor += (uint64_t)bytes_read;
        size -= (uint64_t)bytes_read;
    }
    return true;
}

static void nilamp_params_to_values(const NilampParams *params,
                                    float values[NILAMP_PARAM_COUNT])
{
    for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
        values[i] = (float)nilamp_get_param_value(params, nilamp_param_specs[i].id);
    }
}

static bool nilamp_values_to_params(const float values[NILAMP_PARAM_COUNT],
                                    NilampParams *params)
{
    for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
        if (!nilamp_set_param_value(params, nilamp_param_specs[i].id, values[i])) {
            return false;
        }
    }
    return true;
}

static bool nilamp_state_save(const clap_plugin_t *plugin, const clap_ostream_t *stream)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug || !stream || !stream->write) {
        return false;
    }

    NilampStateBlob blob = {
        .magic = NILAMP_STATE_MAGIC,
        .version = NILAMP_STATE_VERSION,
    };
    nilamp_params_to_values(&plug->params, blob.values);
    return nilamp_write_all(stream, &blob, sizeof(blob));
}

static bool nilamp_state_load(const clap_plugin_t *plugin, const clap_istream_t *stream)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug || !stream || !stream->read) {
        return false;
    }

    NilampStateBlob blob = {0};
    if (!nilamp_read_all(stream, &blob, sizeof(blob)) || blob.magic != NILAMP_STATE_MAGIC ||
        blob.version != NILAMP_STATE_VERSION ||
        !nilamp_values_to_params(blob.values, &plug->params)) {
        return false;
    }
    nilamp_apply_params(plug);
    if (plug->host_params && plug->host_params->rescan) {
        plug->host_params->rescan(plug->host, CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);
    }
    return true;
}

static const clap_plugin_state_t nilamp_state_ext = {
    .save = nilamp_state_save,
    .load = nilamp_state_load,
};

static const void *nilamp_get_extension(const clap_plugin_t *plugin, const char *id)
{
    (void)plugin;
    if (!id) {
        return NULL;
    }
    if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
        return &nilamp_audio_ports_ext;
    }
    if (strcmp(id, CLAP_EXT_PARAMS) == 0) {
        return &nilamp_params_ext;
    }
    if (strcmp(id, CLAP_EXT_STATE) == 0) {
        return &nilamp_state_ext;
    }
    return NULL;
}

static const clap_plugin_t *nilamp_create_plugin(const clap_plugin_factory_t *factory,
                                                 const clap_host_t *host,
                                                 const char *plugin_id)
{
    (void)factory;
    if (!host || !plugin_id || strcmp(plugin_id, NILAMP_PLUGIN_ID) != 0) {
        return NULL;
    }

    NilampClap *plug = (NilampClap *)calloc(1, sizeof(*plug));
    if (!plug) {
        return NULL;
    }

    plug->host = host;
    plug->params = nilamp_default_params();
    plug->plugin.desc = &nilamp_descriptor;
    plug->plugin.plugin_data = plug;
    plug->plugin.init = nilamp_init;
    plug->plugin.destroy = nilamp_destroy;
    plug->plugin.activate = nilamp_activate;
    plug->plugin.deactivate = nilamp_deactivate;
    plug->plugin.start_processing = nilamp_start_processing;
    plug->plugin.stop_processing = nilamp_stop_processing;
    plug->plugin.reset = nilamp_reset;
    plug->plugin.process = nilamp_process;
    plug->plugin.get_extension = nilamp_get_extension;
    plug->plugin.on_main_thread = nilamp_on_main_thread;

    return &plug->plugin;
}

static uint32_t nilamp_factory_get_plugin_count(const clap_plugin_factory_t *factory)
{
    (void)factory;
    return 1;
}

static const clap_plugin_descriptor_t *
nilamp_factory_get_plugin_descriptor(const clap_plugin_factory_t *factory, uint32_t index)
{
    (void)factory;
    return index == 0 ? &nilamp_descriptor : NULL;
}

static const clap_plugin_factory_t nilamp_plugin_factory = {
    .get_plugin_count = nilamp_factory_get_plugin_count,
    .get_plugin_descriptor = nilamp_factory_get_plugin_descriptor,
    .create_plugin = nilamp_create_plugin,
};

static bool nilamp_entry_init(const char *plugin_path)
{
    (void)plugin_path;
    return true;
}

static void nilamp_entry_deinit(void) {}

static const void *nilamp_entry_get_factory(const char *factory_id)
{
    if (factory_id && strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) {
        return &nilamp_plugin_factory;
    }
    return NULL;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init = nilamp_entry_init,
    .deinit = nilamp_entry_deinit,
    .get_factory = nilamp_entry_get_factory,
};
