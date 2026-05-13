// SPDX-License-Identifier: MIT

#include "nilamp_dsp.h"
#include "nilamp_gui.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE 48000.0

typedef struct {
    const char *label;
    const char *fixture;
    float max_abs_limit;
    float rms_limit;
} FixtureSpec;

static float *read_f32_fixture(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        perror(path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    const long len = ftell(f);
    if (len < 0 || len % 4 != 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    float *buf = malloc((size_t)len);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *n = (size_t)len / 4u;
    return buf;
}

static int compare(const char *label, const float *actual, const float *expected, size_t n, float max_abs_limit, float rms_limit)
{
    float max_abs = 0.0f;
    double sum_sq = 0.0;
    for (size_t i = 0; i < n; i++) {
        const float d = fabsf(actual[i] - expected[i]);
        if (d > max_abs) {
            max_abs = d;
        }
        sum_sq += (double)d * (double)d;
    }
    const float rms = (float)sqrt(sum_sq / (double)n);
    printf("[%s] max_abs=%.6e rms=%.6e n=%zu\n", label, (double)max_abs, (double)rms, n);
    if (max_abs > max_abs_limit || rms > rms_limit) {
        fprintf(stderr,
                "%s exceeded tolerance: max_abs %.6e > %.6e or rms %.6e > %.6e\n",
                label, (double)max_abs, (double)max_abs_limit, (double)rms, (double)rms_limit);
        return -1;
    }
    return 0;
}

static float *alloc_zero(size_t n)
{
    return calloc(n, sizeof(float));
}

static int load_input(const char *path, float **input, size_t *n)
{
    *input = read_f32_fixture(path, n);
    return *input == NULL ? -1 : 0;
}

static int compare_fixture(const char *label, const float *actual, size_t n, const char *fixture, float max_abs_limit, float rms_limit)
{
    size_t n_exp = 0;
    float *expected = read_f32_fixture(fixture, &n_exp);
    if (expected == NULL || n_exp != n) {
        free(expected);
        return -1;
    }
    const int rc = compare(label, actual, expected, n, max_abs_limit, rms_limit);
    free(expected);
    return rc;
}

static float rms_diff_after(const float *a, const float *b, size_t n, size_t skip)
{
    if (skip >= n) {
        skip = 0;
    }
    double sum_sq = 0.0;
    for (size_t i = skip; i < n; i++) {
        const double d = (double)a[i] - (double)b[i];
        sum_sq += d * d;
    }
    return (float)sqrt(sum_sq / (double)(n - skip));
}

static float peak_abs(const float *x, size_t n)
{
    float peak = 0.0f;
    for (size_t i = 0; i < n; i++) {
        const float v = fabsf(x[i]);
        if (v > peak) {
            peak = v;
        }
    }
    return peak;
}

static void fill_sine(float *out, size_t n, float freq, float amp)
{
    for (size_t i = 0; i < n; i++) {
        const float t = (float)i / (float)SAMPLE_RATE;
        out[i] = amp * sinf(6.28318530717958647692f * freq * t);
    }
}

static int render_params(const float *input, size_t n,
                         const NilampParams *params, float *output)
{
    NilampEngine *engine = nilamp_engine_create(SAMPLE_RATE);
    if (!engine) {
        return -1;
    }
    nilamp_engine_set_params(engine, params);
    nilamp_engine_process(engine, input, output, (uint32_t)n);
    nilamp_engine_destroy(engine);
    return 0;
}

static int check_render_delta(const char *label, const float *input, size_t n,
                              const NilampParams *a, const NilampParams *b,
                              float min_rms_diff)
{
    float *out_a = alloc_zero(n);
    float *out_b = alloc_zero(n);
    if (!out_a || !out_b || render_params(input, n, a, out_a) != 0 ||
        render_params(input, n, b, out_b) != 0) {
        free(out_a);
        free(out_b);
        return -1;
    }

    const float diff = rms_diff_after(out_a, out_b, n, n / 10u);
    const float peak_a = peak_abs(out_a, n);
    const float peak_b = peak_abs(out_b, n);
    printf("[%s] rms_diff=%.6e peak_a=%.6e peak_b=%.6e n=%zu\n",
           label, (double)diff, (double)peak_a, (double)peak_b, n);
    const int rc = diff >= min_rms_diff && peak_a > 1e-8f && peak_b > 1e-8f ? 0 : -1;
    if (rc != 0) {
        fprintf(stderr, "%s did not produce required audio delta %.6e >= %.6e\n",
                label, (double)diff, (double)min_rms_diff);
    }
    free(out_a);
    free(out_b);
    return rc;
}

static int test_pkd(void)
{
    float *input = NULL;
    size_t n = 0;
    if (load_input("tests/fixtures/sine_1k_amp05_48k_4800.f32", &input, &n) != 0) {
        return -1;
    }
    float *actual = alloc_zero(n);
    const float k1 = 1.0f - expf(-1.0f / (1e-3f * (float)SAMPLE_RATE));
    const float k2 = expf(-1.0f / (50e-3f * (float)SAMPLE_RATE));
    nilamp_test_pkd(0.0f, 0.001f, k1, k2, input, actual, n);
    const int rc = compare_fixture("pkd", actual, n, "tests/fixtures/pkd_baseline_48k.f32", 1e-3f, 1e-4f);
    free(input);
    free(actual);
    return rc;
}

static int test_filters(void)
{
    float *input = NULL;
    size_t n = 0;
    int rc = 0;
    if (load_input("tests/fixtures/sine_1k_amp05_48k_4800.f32", &input, &n) != 0) {
        return -1;
    }

    float *actual = alloc_zero(n);
    nilamp_test_flt_ii1_lp(8800.0f, SAMPLE_RATE, input, actual, n);
    if (compare_fixture("flt_ii1_lp", actual, n, "tests/fixtures/filter_lp_8800_sine05_48k.f32", 1e-6f, 1e-7f) != 0) rc = 1;

    nilamp_test_flt_ii1_hp(10.0f, SAMPLE_RATE, input, actual, n);
    if (compare_fixture("flt_ii1_hp", actual, n, "tests/fixtures/filter_hp_10_sine05_48k.f32", 1e-6f, 1e-7f) != 0) rc = 1;

    nilamp_test_flt_df2_lp_keller(SAMPLE_RATE, input, actual, n);
    if (compare_fixture("flt_df2_lp_keller", actual, n, "tests/fixtures/filter_df2_lp_10k_keller_sine05_48k.f32", 1e-5f, 1e-6f) != 0) rc = 1;

    nilamp_test_flt_sv2_tst(SAMPLE_RATE, input, actual, n);
    if (compare_fixture("flt_sv2_tst", actual, n, "tests/fixtures/filter_svf_tst_sine05_48k.f32", 1e-3f, 1e-4f) != 0) rc = 1;

    float *backend[NILAMP_TEST_NUM_BACKEND_FILTERS] = { 0 };
    for (size_t i = 0; i < NILAMP_TEST_NUM_BACKEND_FILTERS; i++) {
        backend[i] = alloc_zero(n);
    }
    nilamp_test_filter_backend(SAMPLE_RATE, input, backend, n);
    const FixtureSpec specs[NILAMP_TEST_NUM_BACKEND_FILTERS] = {
        { "filter_backend_hp3", "tests/fixtures/filter_backend_hp3_sine05_48k.f32", 1e-3f, 1e-4f },
        { "filter_backend_hp4", "tests/fixtures/filter_backend_hp4_sine05_48k.f32", 1e-3f, 1e-4f },
        { "filter_backend_peq1", "tests/fixtures/filter_backend_peq1_sine05_48k.f32", 1e-3f, 1e-4f },
        { "filter_backend_hs1", "tests/fixtures/filter_backend_hs1_sine05_48k.f32", 1e-3f, 1e-4f },
        { "filter_backend_peq1_hs1", "tests/fixtures/filter_backend_peq1_hs1_sine05_48k.f32", 1e-3f, 1e-4f },
        { "filter_backend_t4_pre_chain", "tests/fixtures/filter_backend_t4_pre_chain_sine05_48k.f32", 1e-3f, 1e-4f },
        { "filter_backend_t5_pre_chain", "tests/fixtures/filter_backend_t5_pre_chain_sine05_48k.f32", 1e-3f, 1e-4f },
    };
    for (size_t i = 0; i < NILAMP_TEST_NUM_BACKEND_FILTERS; i++) {
        if (compare_fixture(specs[i].label, backend[i], n, specs[i].fixture, specs[i].max_abs_limit, specs[i].rms_limit) != 0) rc = 1;
        free(backend[i]);
    }

    free(actual);
    free(input);
    return rc;
}

static int test_adnl(void)
{
    int rc = 0;
    const struct {
        NilampTestAdnlTable table;
        const char *short_name;
    } tables[] = {
        { NILAMP_TEST_ADNL_T1_12AX7, "t1_12ax7" },
        { NILAMP_TEST_ADNL_T2_12AX7, "t2_12ax7" },
        { NILAMP_TEST_ADNL_T3_CD, "t3_cd" },
        { NILAMP_TEST_ADNL_T4_6V6, "t4_6v6" },
        { NILAMP_TEST_ADNL_T5_6V6, "t5_6v6" },
    };

    for (size_t i = 0; i < sizeof(tables) / sizeof(tables[0]); i++) {
        for (int large = 0; large < 2; large++) {
            const char *input_path = large ? "tests/fixtures/sine_200_amp20_48k_4800.f32" : "tests/fixtures/sine_1k_amp05_48k_4800.f32";
            char fixture[128];
            char label[64];
            snprintf(fixture, sizeof(fixture), "tests/fixtures/adnl_%s_%s_48k.f32", tables[i].short_name, large ? "sine20" : "sine05");
            snprintf(label, sizeof(label), "adnl_%s_%s", tables[i].short_name, large ? "large" : "small");
            float *input = NULL;
            size_t n = 0;
            if (load_input(input_path, &input, &n) != 0) {
                return -1;
            }
            float *actual = alloc_zero(n);
            nilamp_test_adnl(tables[i].table, input, actual, n);
            if (compare_fixture(label, actual, n, fixture, 1e-3f, 1e-4f) != 0) rc = 1;
            free(input);
            free(actual);
        }
    }
    return rc;
}

static int test_tubes(void)
{
    float *input = NULL;
    size_t n = 0;
    int rc = 0;
    if (load_input("tests/fixtures/sine_1k_amp05_48k_4800.f32", &input, &n) != 0) {
        return -1;
    }
    float *v = alloc_zero(n);
    float *vk = alloc_zero(n);
    float *dia = alloc_zero(n);

    nilamp_test_tube_ck_t2(SAMPLE_RATE, input, v, dia, n);
    if (compare_fixture("tube_ck_t2_v", v, n, "tests/fixtures/tube_ck_t2_v_sine05_48k.f32", 0.5f, 5e-2f) != 0) rc = 1;
    if (compare_fixture("tube_ck_t2_dia", dia, n, "tests/fixtures/tube_ck_t2_dia_sine05_48k.f32", 5e-6f, 5e-7f) != 0) rc = 1;

    nilamp_test_tube_ck_t5(SAMPLE_RATE, input, v, dia, n);
    if (compare_fixture("tube_ck_t5_v", v, n, "tests/fixtures/tube_ck_t5_v_sine05_48k.f32", 0.5f, 5e-2f) != 0) rc = 1;
    if (compare_fixture("tube_ck_t5_dia", dia, n, "tests/fixtures/tube_ck_t5_dia_sine05_48k.f32", 7e-5f, 2e-6f) != 0) rc = 1;

    nilamp_test_tube_ck_t2_dz(SAMPLE_RATE, input, v, dia, n);
    if (compare_fixture("tube_ck_t2_dz_v", v, n, "tests/fixtures/tube_ck_t2_dz_v_sine05_48k.f32", 0.5f, 5e-2f) != 0) rc = 1;
    if (compare_fixture("tube_ck_t2_dz_dia", dia, n, "tests/fixtures/tube_ck_t2_dz_dia_sine05_48k.f32", 5e-6f, 5e-7f) != 0) rc = 1;

    nilamp_test_tube_cd_t3(SAMPLE_RATE, input, v, vk, dia, n);
    if (compare_fixture("tube_cd_t3_v", v, n, "tests/fixtures/tube_cd_t3_v_sine05_48k.f32", 0.5f, 5e-2f) != 0) rc = 1;
    if (compare_fixture("tube_cd_t3_vk", vk, n, "tests/fixtures/tube_cd_t3_vk_sine05_48k.f32", 0.5f, 5e-2f) != 0) rc = 1;
    if (compare_fixture("tube_cd_t3_dia", dia, n, "tests/fixtures/tube_cd_t3_dia_sine05_48k.f32", 5e-6f, 5e-7f) != 0) rc = 1;

    nilamp_test_tube_cd_t3_dz(SAMPLE_RATE, input, v, vk, dia, n);
    if (compare_fixture("tube_cd_t3_dz_v", v, n, "tests/fixtures/tube_cd_t3_dz_v_sine05_48k.f32", 0.5f, 1e-1f) != 0) rc = 1;
    if (compare_fixture("tube_cd_t3_dz_vk", vk, n, "tests/fixtures/tube_cd_t3_dz_vk_sine05_48k.f32", 0.5f, 1e-1f) != 0) rc = 1;
    if (compare_fixture("tube_cd_t3_dz_dia", dia, n, "tests/fixtures/tube_cd_t3_dz_dia_sine05_48k.f32", 5e-6f, 2e-6f) != 0) rc = 1;

    free(input);
    free(v);
    free(vk);
    free(dia);
    return rc;
}

static int test_power_pair(void)
{
    size_t n_v = 0;
    size_t n_vk = 0;
    int rc = 0;
    float *t3_v = read_f32_fixture("tests/fixtures/power_pair_t3_v_48k.f32", &n_v);
    float *t3_vk = read_f32_fixture("tests/fixtures/power_pair_t3_vk_48k.f32", &n_vk);
    if (t3_v == NULL || t3_vk == NULL || n_v != n_vk) {
        free(t3_v);
        free(t3_vk);
        return -1;
    }
    float *outputs[NILAMP_TEST_NUM_POWER_PAIR_OUTPUTS] = { 0 };
    for (size_t i = 0; i < NILAMP_TEST_NUM_POWER_PAIR_OUTPUTS; i++) {
        outputs[i] = alloc_zero(n_v);
    }
    nilamp_test_power_pair(SAMPLE_RATE, t3_v, t3_vk, outputs, n_v);
    const FixtureSpec specs[NILAMP_TEST_NUM_POWER_PAIR_OUTPUTS] = {
        { "power_pair_t4_v", "tests/fixtures/power_pair_t4_v_48k.f32", 0.55f, 5e-2f },
        { "power_pair_t5_v", "tests/fixtures/power_pair_t5_v_48k.f32", 0.5f, 5e-2f },
        { "power_pair_post_pp", "tests/fixtures/power_pair_post_pp_48k.f32", 0.75f, 7.5e-2f },
        { "power_pair_total_dia", "tests/fixtures/power_pair_total_dia_48k.f32", 2.5e-4f, 2e-5f },
    };
    for (size_t i = 0; i < NILAMP_TEST_NUM_POWER_PAIR_OUTPUTS; i++) {
        if (compare_fixture(specs[i].label, outputs[i], n_v, specs[i].fixture, specs[i].max_abs_limit, specs[i].rms_limit) != 0) rc = 1;
        free(outputs[i]);
    }
    free(t3_v);
    free(t3_vk);
    return rc;
}

static int test_pss(void)
{
    float *input = NULL;
    size_t n = 0;
    if (load_input("tests/fixtures/sine_1k_amp05_48k_4800.f32", &input, &n) != 0) {
        return -1;
    }
    float *actual_dvs = alloc_zero(n);
    float *actual_s = alloc_zero(n);
    nilamp_test_pss(22000.0f, 0.05f, SAMPLE_RATE, input, actual_dvs, actual_s, n);
    int rc = compare_fixture("tube_pss_dvs", actual_dvs, n, "tests/fixtures/pss_dvs_sine05_48k.f32", 1e-2f, 1e-3f);
    if (compare_fixture("tube_pss_s", actual_s, n, "tests/fixtures/pss_s_sine05_48k.f32", 1e-3f, 1e-4f) != 0) rc = 1;
    free(input);
    free(actual_dvs);
    free(actual_s);
    return rc;
}

static int test_nilamp_taps(void)
{
    float *input = NULL;
    size_t n = 0;
    int rc = 0;
    if (load_input("tests/fixtures/sine_1k_amp05_48k_4800.f32", &input, &n) != 0) {
        return -1;
    }
    NilampEngine *engine = nilamp_engine_create(SAMPLE_RATE);
    NilampParams params = nilamp_default_params();
    params.phase_splitter = 0.0f;
    params.sag_pct = 35.355339f;
    nilamp_engine_set_params(engine, &params);
    float *outputs[NILAMP_NUM_TAPS] = { 0 };
    for (size_t i = 0; i < NILAMP_NUM_TAPS; i++) {
        outputs[i] = alloc_zero(n);
    }
    nilamp_engine_process_taps(engine, input, outputs, (uint32_t)n);
    const FixtureSpec specs[NILAMP_NUM_TAPS] = {
        { "nilamp_taps_v_out", "tests/fixtures/nilamp_taps_v_out_48k.f32", 0.25f, 5e-2f },
        { "nilamp_taps_res1_v", "tests/fixtures/nilamp_taps_res1_v_48k.f32", 0.5f, 5e-2f },
        { "nilamp_taps_res3_v", "tests/fixtures/nilamp_taps_res3_v_48k.f32", 1.25f, 7e-2f },
        { "nilamp_taps_res4_v", "tests/fixtures/nilamp_taps_res4_v_48k.f32", 0.5f, 5e-2f },
        { "nilamp_taps_drive_t4", "tests/fixtures/nilamp_taps_drive_t4_48k.f32", 0.5f, 5e-2f },
        { "nilamp_taps_res5_v", "tests/fixtures/nilamp_taps_res5_v_48k.f32", 1.0f, 7.5e-2f },
        { "nilamp_taps_res_t5_v", "tests/fixtures/nilamp_taps_res_t5_v_48k.f32", 1.0f, 7.5e-2f },
        { "nilamp_taps_dvs2", "tests/fixtures/nilamp_taps_dvs2_48k.f32", 1e-2f, 1e-3f },
        { "nilamp_taps_dvs3", "tests/fixtures/nilamp_taps_dvs3_48k.f32", 1e-2f, 1e-3f },
        { "nilamp_taps_p2_s", "tests/fixtures/nilamp_taps_p2_s_48k.f32", 1e-4f, 1e-5f },
        { "nilamp_taps_p3_s", "tests/fixtures/nilamp_taps_p3_s_48k.f32", 1e-4f, 1e-5f },
        { "nilamp_taps_drive_t5", "tests/fixtures/nilamp_taps_drive_t5_48k.f32", 0.5f, 5e-2f },
        { "nilamp_taps_post_pp", "tests/fixtures/nilamp_taps_post_pp_48k.f32", 1.0f, 7.5e-2f },
        { "nilamp_taps_post_peq3", "tests/fixtures/nilamp_taps_post_peq3_48k.f32", 1.0f, 7.5e-2f },
        { "nilamp_taps_post_hs3", "tests/fixtures/nilamp_taps_post_hs3_48k.f32", 1.25f, 7.5e-2f },
        { "nilamp_taps_post_hp5", "tests/fixtures/nilamp_taps_post_hp5_48k.f32", 1.25f, 7.5e-2f },
        { "nilamp_taps_t4_advk_in", "tests/fixtures/nilamp_taps_t4_advk_in_48k.f32", 0.25f, 2.5e-2f },
        { "nilamp_taps_t5_advk_in", "tests/fixtures/nilamp_taps_t5_advk_in_48k.f32", 0.25f, 2.5e-2f },
        { "nilamp_taps_t4_dia", "tests/fixtures/nilamp_taps_t4_dia_48k.f32", 1e-3f, 1e-4f },
        { "nilamp_taps_t5_dia", "tests/fixtures/nilamp_taps_t5_dia_48k.f32", 1e-3f, 1e-4f },
        { "nilamp_taps_t4_advk_out", "tests/fixtures/nilamp_taps_t4_advk_out_48k.f32", 0.25f, 2.5e-2f },
        { "nilamp_taps_t5_advk_out", "tests/fixtures/nilamp_taps_t5_advk_out_48k.f32", 0.25f, 2.5e-2f },
        { "nilamp_taps_dia1_next", "tests/fixtures/nilamp_taps_dia1_next_48k.f32", 2e-3f, 2e-4f },
    };
    for (size_t i = 0; i < NILAMP_NUM_TAPS; i++) {
        if (compare_fixture(specs[i].label, outputs[i], n, specs[i].fixture, specs[i].max_abs_limit, specs[i].rms_limit) != 0) rc = 1;
        free(outputs[i]);
    }
    nilamp_engine_destroy(engine);
    free(input);
    return rc;
}

static int test_control_audio_impact(void)
{
    enum { N = 48000 };
    float *input = alloc_zero(N);
    if (!input) {
        return -1;
    }
    fill_sine(input, N, 220.0f, 0.15f);

    int rc = 0;
    NilampParams base = nilamp_default_params();
    base.gain_db = 12.0f;
    base.volume_pct = 100.0f;

    NilampParams sag0 = base;
    NilampParams sag100 = base;
    sag0.sag_pct = 0.0f;
    sag100.sag_pct = 100.0f;
    if (check_render_delta("control_sag_0_vs_100", input, N, &sag0, &sag100,
                           5e-4f) != 0) {
        rc = 1;
    }

    NilampParams tube_ay7 = base;
    NilampParams tube_ax7 = base;
    tube_ay7.tube1 = 0.0f;
    tube_ax7.tube1 = 1.0f;
    if (check_render_delta("control_tube1_12ay7_vs_12ax7", input, N,
                           &tube_ay7, &tube_ax7, 1e-3f) != 0) {
        rc = 1;
    }

    for (int mode = 0; mode <= 4; mode++) {
        if (mode == 2) {
            continue;
        }
        NilampParams mode_params = base;
        mode_params.phase_splitter = (float)mode;
        char label[64];
        snprintf(label, sizeof(label), "control_splitter_default_vs_%d", mode);
        if (check_render_delta(label, input, N, &base, &mode_params, 1e-3f) != 0) {
            rc = 1;
        }
    }

    NilampParams comp_off = base;
    NilampParams comp_both = base;
    comp_off.gain_comp = 0.0f;
    comp_both.gain_comp = 3.0f;
    if (check_render_delta("control_gain_comp_off_vs_both", input, N,
                           &comp_off, &comp_both, 1e-3f) != 0) {
        rc = 1;
    }

    free(input);
    return rc;
}

static int test_model_identity(void)
{
    NilampEngine *engine = nilamp_engine_create(SAMPLE_RATE);
    if (engine == NULL) {
        return -1;
    }
    NilampEngine *invalid = nilamp_engine_create_model(SAMPLE_RATE, (NilampModelId)99);
    const int ok = nilamp_engine_model_id(engine) == NILAMP_MODEL_KELLER_TWD_DLX_II &&
                   nilamp_model_name(NILAMP_MODEL_KELLER_TWD_DLX_II)[0] != '\0' &&
                   invalid == NULL;
    nilamp_engine_destroy(invalid);
    nilamp_engine_destroy(engine);
    return ok ? 0 : 1;
}

static int test_model_gui_about_versions(void)
{
    const NilampGuiLayoutSpec *layout = nilamp_model_gui_layout(NILAMP_MODEL_KELLER_TWD_DLX_II);
    if (!layout) {
        return 1;
    }

    char nilamp_version[64];
    snprintf(nilamp_version, sizeof(nilamp_version), "nilamp %s", NILAMP_RELEASE_VERSION);
    int saw_nilamp_version = 0;
    int saw_keller_reference = 0;
    for (uint32_t screen_index = 0; screen_index < layout->screen_count; screen_index++) {
        const NilampGuiScreenSpec *screen = &layout->screens[screen_index];
        if (screen->id != NILAMP_GUI_SCREEN_ID_ABOUT) {
            continue;
        }
        for (uint32_t widget_index = 0; widget_index < screen->widget_count; widget_index++) {
            const NilampGuiWidgetSpec *widget = &screen->widgets[widget_index];
            if (widget->type != NILAMP_GUI_WIDGET_TEXT || !widget->label) {
                continue;
            }
            if (strcmp(widget->label, nilamp_version) == 0) {
                saw_nilamp_version = 1;
            }
            if (strcmp(widget->label, "Keller JSFX Version 1.0.4") == 0) {
                saw_keller_reference = 1;
            }
        }
    }
    if (!saw_nilamp_version || !saw_keller_reference) {
        fprintf(stderr, "about screen missing nilamp version or Keller JSFX reference\n");
        return 1;
    }
    return 0;
}

static int test_model_gui_preferred_size(void)
{
    const NilampGuiLayoutSpec *layout = nilamp_model_gui_layout(NILAMP_MODEL_KELLER_TWD_DLX_II);
    if (!layout) {
        return 1;
    }
    uint32_t width = 0u;
    uint32_t height = 0u;
    if (!nilamp_gui_preferred_size(layout, &width, &height) ||
        width != 750u || height != 510u) {
        fprintf(stderr, "unexpected preferred GUI size: %ux%u\n", width, height);
        return 1;
    }
    return 0;
}

int main(void)
{
    int rc = 0;
    if (test_model_identity() != 0) rc = 1;
    if (test_model_gui_about_versions() != 0) rc = 1;
    if (test_model_gui_preferred_size() != 0) rc = 1;
    if (test_pkd() != 0) rc = 1;
    if (test_filters() != 0) rc = 1;
    if (test_adnl() != 0) rc = 1;
    if (test_tubes() != 0) rc = 1;
    if (test_power_pair() != 0) rc = 1;
    if (test_pss() != 0) rc = 1;
    if (test_nilamp_taps() != 0) rc = 1;
    if (test_control_audio_impact() != 0) rc = 1;
    return rc;
}
