// SPDX-License-Identifier: MIT

#include "nilamp_dsp.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

static int test_filter_lp(void)
{
    size_t n_in = 0;
    size_t n_exp = 0;
    float *input = read_f32_fixture("tests/fixtures/sine_1k_amp05_48k_4800.f32", &n_in);
    float *expected = read_f32_fixture("tests/fixtures/filter_lp_8800_sine05_48k.f32", &n_exp);
    if (input == NULL || expected == NULL || n_in != n_exp) {
        free(input);
        free(expected);
        return -1;
    }
    float *actual = calloc(n_in, sizeof(float));
    nilamp_test_flt_ii1_lp(8800.0f, 48000.0, input, actual, n_in);
    const int rc = compare("flt_ii1_lp", actual, expected, n_in, 1e-6f, 1e-7f);
    free(input);
    free(expected);
    free(actual);
    return rc;
}

static int test_filter_hp(void)
{
    size_t n_in = 0;
    size_t n_exp = 0;
    float *input = read_f32_fixture("tests/fixtures/sine_1k_amp05_48k_4800.f32", &n_in);
    float *expected = read_f32_fixture("tests/fixtures/filter_hp_10_sine05_48k.f32", &n_exp);
    if (input == NULL || expected == NULL || n_in != n_exp) {
        free(input);
        free(expected);
        return -1;
    }
    float *actual = calloc(n_in, sizeof(float));
    nilamp_test_flt_ii1_hp(10.0f, 48000.0, input, actual, n_in);
    const int rc = compare("flt_ii1_hp", actual, expected, n_in, 1e-6f, 1e-7f);
    free(input);
    free(expected);
    free(actual);
    return rc;
}

static int test_pss(void)
{
    size_t n_in = 0;
    size_t n_dvs = 0;
    size_t n_s = 0;
    float *input = read_f32_fixture("tests/fixtures/sine_1k_amp05_48k_4800.f32", &n_in);
    float *expected_dvs = read_f32_fixture("tests/fixtures/pss_dvs_sine05_48k.f32", &n_dvs);
    float *expected_s = read_f32_fixture("tests/fixtures/pss_s_sine05_48k.f32", &n_s);
    if (input == NULL || expected_dvs == NULL || expected_s == NULL || n_in != n_dvs || n_in != n_s) {
        free(input);
        free(expected_dvs);
        free(expected_s);
        return -1;
    }
    float *actual_dvs = calloc(n_in, sizeof(float));
    float *actual_s = calloc(n_in, sizeof(float));
    nilamp_test_pss(22000.0f, 0.05f, 48000.0, input, actual_dvs, actual_s, n_in);
    int rc = compare("tube_pss_dvs", actual_dvs, expected_dvs, n_in, 1e-2f, 1e-3f);
    if (rc == 0) {
        rc = compare("tube_pss_s", actual_s, expected_s, n_in, 1e-3f, 1e-4f);
    }
    free(input);
    free(expected_dvs);
    free(expected_s);
    free(actual_dvs);
    free(actual_s);
    return rc;
}

int main(void)
{
    int rc = 0;
    if (test_filter_lp() != 0) rc = 1;
    if (test_filter_hp() != 0) rc = 1;
    if (test_pss() != 0) rc = 1;
    return rc;
}
