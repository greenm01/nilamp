// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L

#include "nilamp_cpu.h"
#include "nilamp_dsp.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define SAMPLE_RATE 48000.0
#define BENCH_FRAMES (48000u * 8u)
#define ADNL_REPEATS 64u
#define NILAMP_PI 3.14159265358979323846

static double seconds_since(struct timespec start, struct timespec end)
{
    const time_t sec = end.tv_sec - start.tv_sec;
    const long nsec = end.tv_nsec - start.tv_nsec;
    return (double)sec + (double)nsec * 1e-9;
}

static double now_elapsed_from(struct timespec start)
{
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    return seconds_since(start, end);
}

static void fill_sine(float *buf, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        buf[i] = 0.1f * sinf((float)(2.0 * NILAMP_PI * 440.0 * (double)i / SAMPLE_RATE));
    }
}

static double consume_peak(const float *buf, size_t n)
{
    double peak = 0.0;
    for (size_t i = 0; i < n; i++) {
        const double x = fabs((double)buf[i]);
        if (x > peak) {
            peak = x;
        }
    }
    return peak;
}

static int bench_adnl(float *input, float *output, size_t n)
{
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (uint32_t i = 0; i < ADNL_REPEATS; i++) {
        nilamp_test_adnl(NILAMP_TEST_ADNL_T4_6V6, input, output, n);
    }
    const double elapsed = now_elapsed_from(start);
    const double samples = (double)n * (double)ADNL_REPEATS;
    printf("adnl_t4_6v6: %.2f ns/sample, %.2f Msamples/s, peak %.6e\n",
           elapsed * 1e9 / samples,
           samples / elapsed / 1e6,
           consume_peak(output, n));
    return elapsed > 0.0 ? 0 : 1;
}

static int bench_engine(const char *label, const float *input, float *output, size_t n)
{
    NilampEngine *engine = nilamp_engine_create(SAMPLE_RATE);
    if (engine == NULL) {
        return 1;
    }
    NilampParams params = nilamp_default_params();
    params.sag_pct = 100.0f;
    nilamp_engine_set_params(engine, &params);

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    nilamp_engine_process(engine, input, output, (uint32_t)n);
    const double elapsed = now_elapsed_from(start);
    printf("%s: %.2f ns/sample, %.2fx realtime, peak %.6e\n",
           label,
           elapsed * 1e9 / (double)n,
           ((double)n / SAMPLE_RATE) / elapsed,
           consume_peak(output, n));

    nilamp_engine_destroy(engine);
    return elapsed > 0.0 ? 0 : 1;
}

int main(void)
{
    nilamp_cpu_enable_realtime_float_mode();

    float *input = calloc(BENCH_FRAMES, sizeof(float));
    float *silence = calloc(BENCH_FRAMES, sizeof(float));
    float *output = calloc(BENCH_FRAMES, sizeof(float));
    if (input == NULL || silence == NULL || output == NULL) {
        free(input);
        free(silence);
        free(output);
        return 1;
    }

    fill_sine(input, BENCH_FRAMES);
    int rc = 0;
    if (bench_adnl(input, output, BENCH_FRAMES) != 0) rc = 1;
    if (bench_engine("engine_sine", input, output, BENCH_FRAMES) != 0) rc = 1;
    if (bench_engine("engine_silence", silence, output, BENCH_FRAMES) != 0) rc = 1;

    free(input);
    free(silence);
    free(output);
    return rc;
}
