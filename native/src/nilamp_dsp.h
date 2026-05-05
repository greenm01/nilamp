// SPDX-License-Identifier: MIT
#ifndef NILAMP_DSP_H
#define NILAMP_DSP_H

#include <stddef.h>
#include <stdint.h>

typedef struct NilampEngine NilampEngine;

typedef struct NilampParams {
    float gain_db;
    float volume_pct;
    float bass_pct;
    float mid_pct;
    float treble_pct;
    float sag_pct;
} NilampParams;

NilampEngine *nilamp_engine_create(double sample_rate);
void nilamp_engine_destroy(NilampEngine *engine);
void nilamp_engine_reset(NilampEngine *engine);
void nilamp_engine_set_params(NilampEngine *engine, const NilampParams *params);
void nilamp_engine_process(NilampEngine *engine, const float *input, float *output, uint32_t nframes);

NilampParams nilamp_default_params(void);

#ifdef NILAMP_ENABLE_TEST_API
void nilamp_test_flt_ii1_lp(float f, double sample_rate, const float *input, float *output, size_t n);
void nilamp_test_flt_ii1_hp(float f, double sample_rate, const float *input, float *output, size_t n);
void nilamp_test_pss(float r, float tau, double sample_rate, const float *dia, float *dvs, float *s, size_t n);
#endif

#endif
