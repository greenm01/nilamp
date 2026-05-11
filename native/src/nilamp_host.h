// SPDX-License-Identifier: MIT
#ifndef NILAMP_HOST_H
#define NILAMP_HOST_H

#include "nilamp_dsp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NILAMP_PLUGIN_ID "dev.niltempus.nilamp"
#define NILAMP_HOST_STATE_MAGIC 0x4e4c4150u
#define NILAMP_HOST_STATE_VERSION 4u
#define NILAMP_HOST_OUTPUT_LIMIT 1.0f

typedef struct NilampHostCore {
    NilampEngine *engines[2];
    NilampParams params;
    float values[NILAMP_PARAM_COUNT];
    double sample_rate;
    bool active;
} NilampHostCore;

typedef struct NilampHostAudioBlock {
    const float *inputs[2];
    float *outputs[2];
    uint32_t input_channels;
    uint32_t output_channels;
    uint64_t input_constant_mask;
} NilampHostAudioBlock;

typedef bool (*NilampHostReadFn)(void *user, void *data, uint64_t size);
typedef bool (*NilampHostWriteFn)(void *user, const void *data, uint64_t size);

void nilamp_host_core_init(NilampHostCore *core);
void nilamp_host_core_deinit(NilampHostCore *core);
bool nilamp_host_core_activate(NilampHostCore *core, double sample_rate);
void nilamp_host_core_deactivate(NilampHostCore *core);
void nilamp_host_core_reset(NilampHostCore *core);
void nilamp_host_core_apply_params(NilampHostCore *core);

const NilampControlSpec *nilamp_host_find_param(uint32_t id);
uint32_t nilamp_host_param_index(uint32_t id);
double nilamp_host_clamp_param(double value, const NilampControlSpec *spec);
double nilamp_host_get_param_value(const NilampParams *params, uint32_t id);
bool nilamp_host_set_param_value(NilampParams *params, uint32_t id, double value);
bool nilamp_host_core_set_param(NilampHostCore *core, uint32_t id, double value);
double nilamp_host_core_get_param(const NilampHostCore *core, uint32_t id);
void nilamp_host_core_load_values(NilampHostCore *core);
void nilamp_host_core_store_values(NilampHostCore *core);

bool nilamp_host_param_value_to_text(uint32_t id, double value, char *dst, uint32_t dst_size);
bool nilamp_host_param_text_to_value(uint32_t id, const char *text, double *out_value);

bool nilamp_host_save_state(const NilampHostCore *core, NilampHostWriteFn write, void *user);
bool nilamp_host_load_state(NilampHostCore *core, NilampHostReadFn read, void *user);

bool nilamp_host_process_segment(NilampHostCore *core, const NilampHostAudioBlock *block,
                                 uint32_t start, uint32_t end);
float nilamp_host_sanitize_sample(float sample);

#ifdef __cplusplus
}
#endif

#endif
