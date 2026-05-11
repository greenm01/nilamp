// SPDX-License-Identifier: MIT
#include "nilamp_host.h"

#include "nilamp_compat.h"
#include "nilamp_cpu.h"

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NILAMP_HOST_STATE_VERSION_1 1u
#define NILAMP_HOST_STATE_VERSION_1_PARAM_COUNT 6u
#define NILAMP_HOST_STATE_VERSION_2 2u
#define NILAMP_HOST_STATE_VERSION_2_PARAM_COUNT 17u
#define NILAMP_HOST_STATE_VERSION_3 3u
#define NILAMP_HOST_STATE_VERSION_3_PARAM_COUNT 19u

typedef struct NilampHostStateBlob {
    uint32_t magic;
    uint32_t version;
    float values[NILAMP_PARAM_COUNT];
} NilampHostStateBlob;

static const NilampControlSpec *const nilamp_param_specs = NULL;

static const NilampControlSpec *nilamp_host_specs(void)
{
    return nilamp_param_specs ? nilamp_param_specs : nilamp_control_specs(NULL);
}

static void nilamp_host_destroy_engines(NilampHostCore *core)
{
    if (!core) {
        return;
    }
    for (uint32_t i = 0; i < 2; i++) {
        nilamp_engine_destroy(core->engines[i]);
        core->engines[i] = NULL;
    }
}

void nilamp_host_core_init(NilampHostCore *core)
{
    if (!core) {
        return;
    }
    memset(core, 0, sizeof(*core));
    core->params = nilamp_default_params();
    nilamp_host_core_load_values(core);
}

void nilamp_host_core_deinit(NilampHostCore *core)
{
    nilamp_host_core_deactivate(core);
}

bool nilamp_host_core_activate(NilampHostCore *core, double sample_rate)
{
    if (!core || !isfinite(sample_rate) || sample_rate <= 0.0) {
        return false;
    }

    nilamp_cpu_enable_realtime_float_mode();
    nilamp_host_destroy_engines(core);
    core->engines[0] = nilamp_engine_create(sample_rate);
    core->engines[1] = nilamp_engine_create(sample_rate);
    if (!core->engines[0] || !core->engines[1]) {
        nilamp_host_destroy_engines(core);
        return false;
    }

    core->sample_rate = sample_rate;
    core->active = true;
    nilamp_host_core_apply_params(core);
    return true;
}

void nilamp_host_core_deactivate(NilampHostCore *core)
{
    if (!core) {
        return;
    }
    core->active = false;
    nilamp_host_destroy_engines(core);
}

void nilamp_host_core_reset(NilampHostCore *core)
{
    if (!core) {
        return;
    }
    for (uint32_t i = 0; i < 2; i++) {
        if (core->engines[i]) {
            nilamp_engine_reset(core->engines[i]);
        }
    }
    nilamp_host_core_apply_params(core);
}

void nilamp_host_core_apply_params(NilampHostCore *core)
{
    if (!core) {
        return;
    }
    for (uint32_t i = 0; i < 2; i++) {
        if (core->engines[i]) {
            nilamp_engine_set_params(core->engines[i], &core->params);
        }
    }
}

const NilampControlSpec *nilamp_host_find_param(uint32_t id)
{
    const NilampControlSpec *specs = nilamp_host_specs();
    for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
        if (specs[i].id == id) {
            return &specs[i];
        }
    }
    return NULL;
}

uint32_t nilamp_host_param_index(uint32_t id)
{
    const NilampControlSpec *specs = nilamp_host_specs();
    for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
        if (specs[i].id == id) {
            return i;
        }
    }
    return NILAMP_PARAM_COUNT;
}

double nilamp_host_clamp_param(double value, const NilampControlSpec *spec)
{
    if (!spec) {
        return 0.0;
    }
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

double nilamp_host_get_param_value(const NilampParams *params, uint32_t id)
{
    if (!params) {
        return 0.0;
    }
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
    case NILAMP_PARAM_BYPASS:
        return params->bypass;
    case NILAMP_PARAM_COUNT:
    default:
        return 0.0;
    }
}

bool nilamp_host_set_param_value(NilampParams *params, uint32_t id, double value)
{
    const NilampControlSpec *spec = nilamp_host_find_param(id);
    if (!params || !spec) {
        return false;
    }

    const float clamped = (float)nilamp_host_clamp_param(value, spec);
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
    case NILAMP_PARAM_OUTPUT_GAIN_DB:
        params->output_gain_db = clamped;
        return true;
    case NILAMP_PARAM_TONE_FMID_DBHZ:
        params->tone_fmid_dbhz = clamped;
        return true;
    case NILAMP_PARAM_TONE_QMID_DB:
        params->tone_qmid_db = clamped;
        return true;
    case NILAMP_PARAM_SPK_RES_GAIN1_DB:
        params->spk_res_gain1_db = clamped;
        return true;
    case NILAMP_PARAM_SPK_RES_GAIN2_DB:
        params->spk_res_gain2_db = clamped;
        return true;
    case NILAMP_PARAM_SPK_RES_FRES_DBHZ:
        params->spk_res_fres_dbhz = clamped;
        return true;
    case NILAMP_PARAM_SPK_RES_QTS_DB:
        params->spk_res_qts_db = clamped;
        return true;
    case NILAMP_PARAM_SPK_IND_GAIN1_DB:
        params->spk_ind_gain1_db = clamped;
        return true;
    case NILAMP_PARAM_SPK_IND_GAIN2_DB:
        params->spk_ind_gain2_db = clamped;
        return true;
    case NILAMP_PARAM_SPK_IND_FIND_DBHZ:
        params->spk_ind_find_dbhz = clamped;
        return true;
    case NILAMP_PARAM_GAIN_COMP:
        params->gain_comp = clamped;
        return true;
    case NILAMP_PARAM_TUBE1:
        params->tube1 = clamped;
        return true;
    case NILAMP_PARAM_PHASE_SPLITTER:
        params->phase_splitter = clamped;
        return true;
    case NILAMP_PARAM_BYPASS:
        params->bypass = clamped;
        return true;
    case NILAMP_PARAM_COUNT:
    default:
        return false;
    }
}

bool nilamp_host_core_set_param(NilampHostCore *core, uint32_t id, double value)
{
    if (!core || !nilamp_host_set_param_value(&core->params, id, value)) {
        return false;
    }
    const uint32_t index = nilamp_host_param_index(id);
    if (index < NILAMP_PARAM_COUNT) {
        core->values[index] = (float)nilamp_host_get_param_value(&core->params, id);
    }
    nilamp_host_core_apply_params(core);
    return true;
}

double nilamp_host_core_get_param(const NilampHostCore *core, uint32_t id)
{
    return core ? nilamp_host_get_param_value(&core->params, id) : 0.0;
}

void nilamp_host_core_load_values(NilampHostCore *core)
{
    if (!core) {
        return;
    }
    const NilampControlSpec *specs = nilamp_host_specs();
    for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
        core->values[i] = (float)nilamp_host_get_param_value(&core->params, specs[i].id);
    }
}

void nilamp_host_core_store_values(NilampHostCore *core)
{
    if (!core) {
        return;
    }
    const NilampControlSpec *specs = nilamp_host_specs();
    core->params = nilamp_default_params();
    for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
        (void)nilamp_host_set_param_value(&core->params, specs[i].id, core->values[i]);
    }
}

bool nilamp_host_param_value_to_text(uint32_t id, double value, char *dst, uint32_t dst_size)
{
    const NilampControlSpec *spec = nilamp_host_find_param(id);
    if (!spec || !dst || dst_size == 0) {
        return false;
    }

    const double clamped = nilamp_host_clamp_param(value, spec);
    if (spec->display == NILAMP_CONTROL_DISPLAY_ENUM && spec->enum_names &&
        spec->enum_count > 0u) {
        const int index = (int)lround(clamped);
        const char *name = (index >= 0 && (uint32_t)index < spec->enum_count) ?
                               spec->enum_names[index] :
                               spec->enum_names[(uint32_t)lround(spec->default_value)];
        const int written = snprintf(dst, dst_size, "%s", name);
        return written >= 0 && (uint32_t)written < dst_size;
    }

    const double display = nilamp_control_display_value(spec, (float)clamped);
    const int written = snprintf(dst, dst_size, "%.3g %s", display, spec->unit);
    return written >= 0 && (uint32_t)written < dst_size;
}

bool nilamp_host_param_text_to_value(uint32_t id, const char *text, double *out_value)
{
    const NilampControlSpec *spec = nilamp_host_find_param(id);
    if (!spec || !text || !out_value) {
        return false;
    }

    if (spec->display == NILAMP_CONTROL_DISPLAY_ENUM && spec->enum_names &&
        spec->enum_count > 0u) {
        for (uint32_t i = 0; i < spec->enum_count; i++) {
            if (nilamp_stricmp(text, spec->enum_names[i]) == 0) {
                *out_value = (double)i;
                return true;
            }
        }
        if (spec->id == NILAMP_PARAM_GAIN_COMP && nilamp_stricmp(text, "tube1") == 0) {
            *out_value = 1.0;
            return true;
        }
    }

    errno = 0;
    char *end = NULL;
    double parsed = strtod(text, &end);
    if (end == text || errno == ERANGE || !isfinite(parsed)) {
        return false;
    }
    if (spec->display == NILAMP_CONTROL_DISPLAY_ISO266) {
        if (parsed <= 0.0) {
            return false;
        }
        parsed = 20.0 * log10(parsed);
    }
    *out_value = nilamp_host_clamp_param(parsed, spec);
    return true;
}

bool nilamp_host_save_state(const NilampHostCore *core, NilampHostWriteFn write, void *user)
{
    if (!core || !write) {
        return false;
    }

    NilampHostStateBlob blob = {
        .magic = NILAMP_HOST_STATE_MAGIC,
        .version = NILAMP_HOST_STATE_VERSION,
    };
    const NilampControlSpec *specs = nilamp_host_specs();
    for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
        blob.values[i] = (float)nilamp_host_get_param_value(&core->params, specs[i].id);
    }
    return write(user, &blob, sizeof(blob));
}

bool nilamp_host_load_state(NilampHostCore *core, NilampHostReadFn read, void *user)
{
    if (!core || !read) {
        return false;
    }

    struct {
        uint32_t magic;
        uint32_t version;
    } header = {0};
    float values[NILAMP_PARAM_COUNT] = {0};
    NilampParams params = nilamp_default_params();
    const NilampControlSpec *specs = nilamp_host_specs();
    for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
        values[i] = (float)nilamp_host_get_param_value(&params, specs[i].id);
    }

    if (!read(user, &header, sizeof(header)) || header.magic != NILAMP_HOST_STATE_MAGIC) {
        return false;
    }

    uint32_t value_count = 0u;
    if (header.version == NILAMP_HOST_STATE_VERSION_1) {
        value_count = NILAMP_HOST_STATE_VERSION_1_PARAM_COUNT;
    } else if (header.version == NILAMP_HOST_STATE_VERSION_2) {
        value_count = NILAMP_HOST_STATE_VERSION_2_PARAM_COUNT;
    } else if (header.version == NILAMP_HOST_STATE_VERSION_3) {
        value_count = NILAMP_HOST_STATE_VERSION_3_PARAM_COUNT;
    } else if (header.version == NILAMP_HOST_STATE_VERSION) {
        value_count = NILAMP_PARAM_COUNT;
    } else {
        return false;
    }

    if (!read(user, values, sizeof(values[0]) * value_count)) {
        return false;
    }
    for (uint32_t i = 0; i < value_count; i++) {
        if (!nilamp_host_set_param_value(&params, specs[i].id, values[i])) {
            return false;
        }
    }
    if (header.version == NILAMP_HOST_STATE_VERSION_1 ||
        header.version == NILAMP_HOST_STATE_VERSION_2) {
        (void)nilamp_host_set_param_value(&params, NILAMP_PARAM_TUBE1, 1.0);
        (void)nilamp_host_set_param_value(&params, NILAMP_PARAM_PHASE_SPLITTER, 0.0);
    }

    core->params = params;
    nilamp_host_core_load_values(core);
    nilamp_host_core_apply_params(core);
    return true;
}

float nilamp_host_sanitize_sample(float sample)
{
    if (!isfinite(sample)) {
        return 0.0f;
    }
    if (sample != 0.0f && fabsf(sample) < FLT_MIN) {
        return 0.0f;
    }
    if (sample > NILAMP_HOST_OUTPUT_LIMIT) {
        return NILAMP_HOST_OUTPUT_LIMIT;
    }
    if (sample < -NILAMP_HOST_OUTPUT_LIMIT) {
        return -NILAMP_HOST_OUTPUT_LIMIT;
    }
    return sample;
}

static void nilamp_zero_channel(float *output, uint32_t offset, uint32_t nframes)
{
    if (output && nframes > 0) {
        memset(output + offset, 0, sizeof(float) * nframes);
    }
}

static void nilamp_sanitize_channel(float *output, uint32_t offset, uint32_t nframes)
{
    if (!output) {
        return;
    }
    for (uint32_t i = 0; i < nframes; i++) {
        output[offset + i] = nilamp_host_sanitize_sample(output[offset + i]);
    }
}

static void nilamp_process_channel(NilampEngine *engine, const NilampHostAudioBlock *block,
                                   uint32_t input_channel, uint32_t output_channel,
                                   uint32_t offset, uint32_t nframes)
{
    float *out = output_channel < 2u ? block->outputs[output_channel] : NULL;
    if (!out) {
        return;
    }
    if (!engine) {
        nilamp_zero_channel(out, offset, nframes);
        return;
    }

    const bool has_input = input_channel < block->input_channels && input_channel < 2u &&
                           block->inputs[input_channel];
    const bool input_is_constant =
        has_input && ((block->input_constant_mask & (UINT64_C(1) << input_channel)) != 0u);

    if (has_input && !input_is_constant) {
        nilamp_engine_process(engine, block->inputs[input_channel] + offset, out + offset, nframes);
        nilamp_sanitize_channel(out, offset, nframes);
        return;
    }

    const float zero = 0.0f;
    const float *sample = has_input ? block->inputs[input_channel] : &zero;
    for (uint32_t i = 0; i < nframes; i++) {
        nilamp_engine_process(engine, sample, out + offset + i, 1);
        out[offset + i] = nilamp_host_sanitize_sample(out[offset + i]);
    }
}

static bool nilamp_stereo_input_is_mono_equivalent(const NilampHostAudioBlock *block)
{
    if (!block || block->input_channels < 2u || !block->inputs[0] || !block->inputs[1]) {
        return false;
    }
    if (block->inputs[0] == block->inputs[1]) {
        return true;
    }
    const bool left_constant = (block->input_constant_mask & UINT64_C(1)) != 0u;
    const bool right_constant = (block->input_constant_mask & (UINT64_C(1) << 1)) != 0u;
    return left_constant && right_constant && block->inputs[0][0] == block->inputs[1][0];
}

static void nilamp_duplicate_channel(float *dst, const float *src, uint32_t offset,
                                     uint32_t nframes)
{
    if (!dst || !src || nframes == 0 || dst == src) {
        return;
    }
    memcpy(dst + offset, src + offset, sizeof(float) * nframes);
}

static void nilamp_passthrough_channel(const NilampHostAudioBlock *block,
                                       uint32_t input_channel, uint32_t output_channel,
                                       uint32_t offset, uint32_t nframes)
{
    if (!block || output_channel >= block->output_channels ||
        output_channel >= 2u || !block->outputs[output_channel]) {
        return;
    }

    float *out = block->outputs[output_channel];
    const bool has_input = input_channel < block->input_channels &&
                           input_channel < 2u && block->inputs[input_channel];
    if (!has_input) {
        nilamp_zero_channel(out, offset, nframes);
        return;
    }

    const float *in = block->inputs[input_channel];
    const bool input_is_constant =
        (block->input_constant_mask & (UINT64_C(1) << input_channel)) != 0u;
    if (input_is_constant) {
        for (uint32_t i = 0; i < nframes; i++) {
            out[offset + i] = nilamp_host_sanitize_sample(in[0]);
        }
    } else if (out != in) {
        memcpy(out + offset, in + offset, sizeof(float) * nframes);
        nilamp_sanitize_channel(out, offset, nframes);
    }
}

static bool nilamp_host_process_bypass_segment(const NilampHostAudioBlock *block,
                                               uint32_t start, uint32_t end)
{
    if (!block || block->output_channels == 0u || !block->outputs[0]) {
        return false;
    }

    const uint32_t frames = end - start;
    const uint32_t process_channels = block->output_channels < 2u ? block->output_channels : 2u;
    for (uint32_t ch = 0; ch < process_channels; ch++) {
        const uint32_t input_channel = block->input_channels > 1u ? ch : 0u;
        nilamp_passthrough_channel(block, input_channel, ch, start, frames);
    }
    return true;
}

bool nilamp_host_process_segment(NilampHostCore *core, const NilampHostAudioBlock *block,
                                 uint32_t start, uint32_t end)
{
    if (end <= start) {
        return true;
    }
    if (!core || !block || block->output_channels == 0 || !block->outputs[0]) {
        return false;
    }

    const uint32_t frames = end - start;
    const uint32_t process_channels = block->output_channels < 2u ? block->output_channels : 2u;

    if (nilamp_host_core_get_param(core, NILAMP_PARAM_BYPASS) >= 0.5) {
        return nilamp_host_process_bypass_segment(block, start, end);
    }

    if (process_channels >= 2u && block->input_channels == 1u && block->inputs[0] &&
        block->outputs[0] == block->inputs[0]) {
        nilamp_process_channel(core->engines[1], block, 0u, 1u, start, frames);
        nilamp_process_channel(core->engines[0], block, 0u, 0u, start, frames);
    } else if (process_channels >= 2u && nilamp_stereo_input_is_mono_equivalent(block) &&
               block->outputs[0] && block->outputs[1] && block->outputs[0] != block->outputs[1]) {
        nilamp_process_channel(core->engines[0], block, 0u, 0u, start, frames);
        nilamp_duplicate_channel(block->outputs[1], block->outputs[0], start, frames);
    } else {
        for (uint32_t ch = 0; ch < process_channels; ch++) {
            const uint32_t input_channel = block->input_channels > 1u ? ch : 0u;
            nilamp_process_channel(core->engines[ch], block, input_channel, ch, start, frames);
        }
    }

    for (uint32_t ch = 2; ch < block->output_channels && ch < 2u; ch++) {
        if (block->outputs[ch]) {
            nilamp_zero_channel(block->outputs[ch], start, frames);
        }
    }
    return true;
}
