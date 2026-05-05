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

enum {
    NILAMP_NUM_TAPS = 9,
    NILAMP_TEST_NUM_BACKEND_FILTERS = 7,
    NILAMP_TEST_NUM_POWER_PAIR_OUTPUTS = 4,
};

typedef enum NilampTestAdnlTable {
    NILAMP_TEST_ADNL_T1_12AX7,
    NILAMP_TEST_ADNL_T2_12AX7,
    NILAMP_TEST_ADNL_T3_CD,
    NILAMP_TEST_ADNL_T4_6V6,
    NILAMP_TEST_ADNL_T5_6V6,
} NilampTestAdnlTable;

NilampEngine *nilamp_engine_create(double sample_rate);
void nilamp_engine_destroy(NilampEngine *engine);
void nilamp_engine_reset(NilampEngine *engine);
void nilamp_engine_set_params(NilampEngine *engine, const NilampParams *params);
void nilamp_engine_process(NilampEngine *engine, const float *input, float *output, uint32_t nframes);
void nilamp_engine_process_taps(NilampEngine *engine, const float *input, float *outputs[NILAMP_NUM_TAPS], uint32_t nframes);

NilampParams nilamp_default_params(void);

#ifdef NILAMP_ENABLE_TEST_API
void nilamp_test_flt_ii1_lp(float f, double sample_rate, const float *input, float *output, size_t n);
void nilamp_test_flt_ii1_hp(float f, double sample_rate, const float *input, float *output, size_t n);
void nilamp_test_flt_sv2_tst(double sample_rate, const float *input, float *output, size_t n);
void nilamp_test_pkd(float xth, float xdiode, float k1, float k2, const float *input, float *output, size_t n);
void nilamp_test_adnl(NilampTestAdnlTable table, const float *input, float *output, size_t n);
void nilamp_test_filter_backend(double sample_rate, const float *input, float *outputs[NILAMP_TEST_NUM_BACKEND_FILTERS], size_t n);
void nilamp_test_tube_ck_t2(double sample_rate, const float *input, float *v_out, float *dia, size_t n);
void nilamp_test_tube_ck_t5(double sample_rate, const float *input, float *v_out, float *dia, size_t n);
void nilamp_test_tube_ck_t2_dz(double sample_rate, const float *input, float *v_out, float *dia, size_t n);
void nilamp_test_tube_cd_t3(double sample_rate, const float *input, float *v_out, float *vk_out, float *dia, size_t n);
void nilamp_test_tube_cd_t3_dz(double sample_rate, const float *input, float *v_out, float *vk_out, float *dia, size_t n);
void nilamp_test_power_pair(double sample_rate, const float *t3_v, const float *t3_vk, float *outputs[NILAMP_TEST_NUM_POWER_PAIR_OUTPUTS], size_t n);
void nilamp_test_pss(float r, float tau, double sample_rate, const float *dia, float *dvs, float *s, size_t n);
#endif

#endif
