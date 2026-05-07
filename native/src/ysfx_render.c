// SPDX-License-Identifier: MIT

#include "ysfx.h"

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint16_t format_tag;
    uint8_t *data;
    size_t data_size;
} Wav;

typedef struct {
    uint32_t index;
    double value;
} SliderArg;

typedef struct {
    const char *input_path;
    const char *output_path;
    const char *effect_path;
    const char *import_root;
    uint32_t channels;
    uint32_t block;
    float input_gain;
    SliderArg sliders[256];
    size_t slider_count;
} Args;

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8u);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
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
    if (f == NULL) {
        fprintf(stderr, "error: opening %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    const long len = ftell(f);
    if (len < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    uint8_t *buf = malloc((size_t)len);
    if (buf == NULL) {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    *data = buf;
    *size = (size_t)len;
    return 0;
}

static int parse_wav(const char *path, Wav *wav)
{
    uint8_t *file = NULL;
    size_t size = 0;
    if (read_file(path, &file, &size) != 0) {
        return -1;
    }
    if (size < 12 || memcmp(file, "RIFF", 4) != 0 || memcmp(file + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "error: %s is not a RIFF/WAVE file\n", path);
        free(file);
        return -1;
    }

    memset(wav, 0, sizeof(*wav));
    for (size_t pos = 12; pos + 8 <= size;) {
        const uint8_t *chunk = file + pos;
        const uint32_t chunk_size = rd32(chunk + 4);
        const size_t body_pos = pos + 8;
        if (body_pos + chunk_size > size) {
            break;
        }
        if (memcmp(chunk, "fmt ", 4) == 0 && chunk_size >= 16) {
            wav->format_tag = rd16(file + body_pos);
            wav->channels = rd16(file + body_pos + 2);
            wav->sample_rate = rd32(file + body_pos + 4);
            wav->bits_per_sample = rd16(file + body_pos + 14);
            if (wav->format_tag == 0xfffeu && chunk_size >= 26) {
                wav->format_tag = rd16(file + body_pos + 24);
            }
        } else if (memcmp(chunk, "data", 4) == 0) {
            wav->data_size = chunk_size;
            wav->data = malloc(chunk_size);
            if (wav->data == NULL) {
                free(file);
                return -1;
            }
            memcpy(wav->data, file + body_pos, chunk_size);
        }
        pos = body_pos + chunk_size + (chunk_size & 1u);
    }
    free(file);

    if (wav->data == NULL || wav->channels == 0 || wav->sample_rate == 0) {
        fprintf(stderr, "error: %s is missing fmt or data chunk\n", path);
        return -1;
    }
    if (!((wav->format_tag == 3u && wav->bits_per_sample == 32u) ||
          (wav->format_tag == 1u && (wav->bits_per_sample == 16u || wav->bits_per_sample == 24u || wav->bits_per_sample == 32u)))) {
        fprintf(stderr, "error: unsupported wav format tag=%u bits=%u\n", wav->format_tag, wav->bits_per_sample);
        return -1;
    }
    return 0;
}

static void free_wav(Wav *wav)
{
    free(wav->data);
    memset(wav, 0, sizeof(*wav));
}

static int32_t sign_extend24(uint32_t x)
{
    if ((x & 0x800000u) != 0u) {
        x |= 0xff000000u;
    }
    return (int32_t)x;
}

static int wav_to_mono(const Wav *wav, float **mono, size_t *frames)
{
    const size_t bytes_per_sample = wav->bits_per_sample / 8u;
    const size_t frame_bytes = bytes_per_sample * wav->channels;
    if (frame_bytes == 0 || wav->data_size % frame_bytes != 0) {
        return -1;
    }
    *frames = wav->data_size / frame_bytes;
    *mono = calloc(*frames, sizeof(float));
    if (*mono == NULL) {
        return -1;
    }

    for (size_t f = 0; f < *frames; f++) {
        double acc = 0.0;
        for (uint16_t ch = 0; ch < wav->channels; ch++) {
            const uint8_t *p = wav->data + f * frame_bytes + (size_t)ch * bytes_per_sample;
            float sample;
            if (wav->format_tag == 3u) {
                memcpy(&sample, p, sizeof(sample));
            } else if (wav->bits_per_sample == 16u) {
                sample = (float)(int16_t)rd16(p) / 32768.0f;
            } else if (wav->bits_per_sample == 24u) {
                const uint32_t raw = (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u);
                sample = (float)sign_extend24(raw) / 8388608.0f;
            } else {
                sample = (float)(int32_t)rd32(p) / 2147483648.0f;
            }
            acc += sample;
        }
        (*mono)[f] = (float)(acc / wav->channels);
    }
    return 0;
}

static int write_wav_f32(const char *path, const float *samples, size_t frames, uint16_t channels, uint32_t sample_rate)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "error: creating %s: %s\n", path, strerror(errno));
        return -1;
    }
    const uint32_t data_size = (uint32_t)(frames * (size_t)channels * sizeof(float));
    const uint32_t riff_size = 4u + (8u + 16u) + (8u + data_size);
    fwrite("RIFF", 1, 4, f);
    wr32(f, riff_size);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    wr32(f, 16u);
    wr16(f, 3u);
    wr16(f, channels);
    wr32(f, sample_rate);
    wr32(f, sample_rate * (uint32_t)channels * 4u);
    wr16(f, (uint16_t)(channels * 4u));
    wr16(f, 32u);
    fwrite("data", 1, 4, f);
    wr32(f, data_size);
    fwrite(samples, sizeof(float), frames * (size_t)channels, f);
    const int ok = ferror(f) == 0;
    fclose(f);
    return ok ? 0 : -1;
}

static void usage(FILE *f)
{
    fprintf(f,
            "ysfx_render --input IN.wav --output OUT.wav --effect EFFECT.jsfx --import-root Effects [options]\n"
            "\n"
            "Options:\n"
            "  --channels n       Output channel count (default 1)\n"
            "  --block n          Processing block size (default 64)\n"
            "  --input-gain x     Multiply input before JSFX processing (default 1)\n"
            "  --slider N=VALUE   Set 1-based JSFX slider N before rendering\n");
}

static int parse_u32_arg(const char *name, const char *value, uint32_t min, uint32_t max, uint32_t *out)
{
    char *end = NULL;
    const unsigned long v = strtoul(value, &end, 10);
    if (end == value || *end != '\0' || v < min || v > max) {
        fprintf(stderr, "error: %s: invalid value '%s'\n", name, value);
        return -1;
    }
    *out = (uint32_t)v;
    return 0;
}

static int parse_slider_arg(const char *value, SliderArg *out)
{
    const char *eq = strchr(value, '=');
    if (eq == NULL || eq == value || eq[1] == '\0') {
        fprintf(stderr, "error: --slider expects N=VALUE, got '%s'\n", value);
        return -1;
    }
    char idx_buf[32];
    const size_t idx_len = (size_t)(eq - value);
    if (idx_len >= sizeof(idx_buf)) {
        fprintf(stderr, "error: --slider index too long: '%s'\n", value);
        return -1;
    }
    memcpy(idx_buf, value, idx_len);
    idx_buf[idx_len] = '\0';
    uint32_t index = 0;
    if (parse_u32_arg("--slider", idx_buf, 1, 256, &index) != 0) {
        return -1;
    }

    char *end = NULL;
    const double slider_value = strtod(eq + 1, &end);
    if (end == eq + 1 || *end != '\0' || !isfinite(slider_value)) {
        fprintf(stderr, "error: --slider: invalid value '%s'\n", eq + 1);
        return -1;
    }
    out->index = index;
    out->value = slider_value;
    return 0;
}

static int parse_args(int argc, char **argv, Args *args)
{
    memset(args, 0, sizeof(*args));
    args->channels = 1;
    args->block = 64;
    args->input_gain = 1.0f;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(stdout);
            exit(0);
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "error: missing value for %s\n", a);
            return -1;
        }
        const char *v = argv[++i];
        if (strcmp(a, "--input") == 0) {
            args->input_path = v;
        } else if (strcmp(a, "--output") == 0) {
            args->output_path = v;
        } else if (strcmp(a, "--effect") == 0) {
            args->effect_path = v;
        } else if (strcmp(a, "--import-root") == 0) {
            args->import_root = v;
        } else if (strcmp(a, "--channels") == 0) {
            if (parse_u32_arg(a, v, 1, ysfx_max_channels, &args->channels) != 0) return -1;
        } else if (strcmp(a, "--block") == 0) {
            if (parse_u32_arg(a, v, 1, 8192, &args->block) != 0) return -1;
        } else if (strcmp(a, "--input-gain") == 0) {
            char *end = NULL;
            const float gain = strtof(v, &end);
            if (end == v || *end != '\0' || !isfinite(gain)) {
                fprintf(stderr, "error: --input-gain: invalid value '%s'\n", v);
                return -1;
            }
            args->input_gain = gain;
        } else if (strcmp(a, "--slider") == 0) {
            if (args->slider_count >= sizeof(args->sliders) / sizeof(args->sliders[0])) {
                fprintf(stderr, "error: too many --slider arguments\n");
                return -1;
            }
            if (parse_slider_arg(v, &args->sliders[args->slider_count]) != 0) return -1;
            args->slider_count++;
        } else {
            fprintf(stderr, "error: unknown argument: %s\n", a);
            return -1;
        }
    }
    if (args->input_path == NULL || args->output_path == NULL || args->effect_path == NULL) {
        usage(stderr);
        return -1;
    }
    return 0;
}

static void log_report(intptr_t userdata, ysfx_log_level level, const char *message)
{
    (void)userdata;
    fprintf(stderr, "ysfx %s: %s\n", ysfx_log_level_string(level), message);
}

int main(int argc, char **argv)
{
    Args args;
    if (parse_args(argc, argv, &args) != 0) {
        return 2;
    }

    Wav wav;
    if (parse_wav(args.input_path, &wav) != 0) {
        return 1;
    }

    float *mono = NULL;
    size_t frames = 0;
    if (wav_to_mono(&wav, &mono, &frames) != 0) {
        fprintf(stderr, "error: decoding input wav\n");
        free_wav(&wav);
        return 1;
    }

    ysfx_config_t *config = ysfx_config_new();
    ysfx_t *fx = NULL;
    float **outs = NULL;
    float *interleaved = NULL;
    int rc = 1;

    if (config == NULL) {
        fprintf(stderr, "error: ysfx_config_new failed\n");
        goto done;
    }
    if (args.input_gain != 1.0f) {
        for (size_t i = 0; i < frames; i++) {
            mono[i] *= args.input_gain;
        }
    }
    ysfx_set_log_reporter(config, log_report);
    if (args.import_root != NULL) {
        ysfx_set_import_root(config, args.import_root);
    } else {
        ysfx_guess_file_roots(config, args.effect_path);
    }
    ysfx_register_builtin_audio_formats(config);

    fx = ysfx_new(config);
    if (fx == NULL) {
        fprintf(stderr, "error: ysfx_new failed\n");
        goto done;
    }
    if (!ysfx_load_file(fx, args.effect_path, 0)) {
        fprintf(stderr, "error: ysfx_load_file failed: %s\n", args.effect_path);
        goto done;
    }
    if (!ysfx_compile(fx, ysfx_compile_no_gfx | ysfx_compile_no_serialize)) {
        fprintf(stderr, "error: ysfx_compile failed: %s\n", args.effect_path);
        goto done;
    }
    ysfx_set_sample_rate(fx, (ysfx_real)wav.sample_rate);
    ysfx_set_block_size(fx, args.block);

    for (size_t i = 0; i < args.slider_count; i++) {
        const uint32_t zero_based = args.sliders[i].index - 1u;
        if (!ysfx_slider_exists(fx, zero_based)) {
            fprintf(stderr, "error: slider%u does not exist in %s\n", args.sliders[i].index, args.effect_path);
            goto done;
        }
        ysfx_slider_set_value(fx, zero_based, args.sliders[i].value, true);
    }
    ysfx_init(fx);

    outs = calloc(args.channels, sizeof(float *));
    if (outs == NULL) {
        fprintf(stderr, "error: allocation failed\n");
        goto done;
    }
    for (uint32_t ch = 0; ch < args.channels; ch++) {
        outs[ch] = calloc(frames, sizeof(float));
        if (outs[ch] == NULL) {
            fprintf(stderr, "error: allocation failed\n");
            goto done;
        }
    }
    interleaved = calloc(frames * (size_t)args.channels, sizeof(float));
    if (interleaved == NULL) {
        fprintf(stderr, "error: allocation failed\n");
        goto done;
    }

    size_t pos = 0;
    while (pos < frames) {
        const uint32_t n = args.block < frames - pos ? args.block : (uint32_t)(frames - pos);
        const float *ins[1] = { mono + pos };
        float *block_outs[ysfx_max_channels];
        for (uint32_t ch = 0; ch < args.channels; ch++) {
            block_outs[ch] = outs[ch] + pos;
        }
        ysfx_time_info_t time_info;
        memset(&time_info, 0, sizeof(time_info));
        time_info.tempo = 120.0;
        time_info.playback_state = ysfx_playback_playing;
        time_info.time_position = (ysfx_real)pos / (ysfx_real)wav.sample_rate;
        time_info.beat_position = time_info.time_position * 2.0;
        time_info.time_signature[0] = 4;
        time_info.time_signature[1] = 4;
        ysfx_set_time_info(fx, &time_info);
        ysfx_process_float(fx, ins, block_outs, 1, args.channels, n);
        pos += n;
    }

    for (size_t f = 0; f < frames; f++) {
        for (uint32_t ch = 0; ch < args.channels; ch++) {
            interleaved[f * (size_t)args.channels + ch] = outs[ch][f];
        }
    }
    if (write_wav_f32(args.output_path, interleaved, frames, (uint16_t)args.channels, wav.sample_rate) != 0) {
        goto done;
    }
    fprintf(stderr, "ysfx rendered %zu frames @ %u Hz, %u ch -> %s\n", frames, wav.sample_rate, args.channels, args.output_path);
    rc = 0;

done:
    if (outs != NULL) {
        for (uint32_t ch = 0; ch < args.channels; ch++) {
            free(outs[ch]);
        }
    }
    free(outs);
    free(interleaved);
    free(mono);
    ysfx_free(fx);
    ysfx_config_free(config);
    free_wav(&wav);
    return rc;
}
