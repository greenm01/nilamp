// SPDX-License-Identifier: MIT

#include "nilamp_dsp.h"

#include <errno.h>
#include <math.h>
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
    const char *input_path;
    const char *output_path;
    NilampParams params;
    uint32_t block;
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
#ifdef NILAMP_TAPS_RENDER
    const char *program = "nilamp_taps_render";
    const char *extra = "\nWrites 9 float32 channels: v_out, res1_v, res3_v, res4_v, drive_t4, res5_v, res_t5_v, dvs2, dvs3.\n";
#else
    const char *program = "nilamp_render";
    const char *extra = "";
#endif
    fprintf(f,
            "%s --input IN.wav --output OUT.wav [params]\n"
            "%s"
            "\n"
            "Params:\n"
            "  --gain    dB      Input gain (default 0)\n"
            "  --volume  pct     Volume 0..100 (default 50)\n"
            "  --bass    pct     Bass 0..100 (default 50)\n"
            "  --mid     pct     Mid 0..100 (default 50)\n"
            "  --treble  pct     Treble 0..100 (default 50)\n"
            "  --sag     pct     Sag 0..100 (default 50)\n"
            "  --block   n       Processing block size\n",
            program,
            extra);
}

static int parse_float_arg(const char *name, const char *value, float *out)
{
    char *end = NULL;
    const float v = strtof(value, &end);
    if (end == value || *end != '\0' || !isfinite(v)) {
        fprintf(stderr, "error: %s: invalid float '%s'\n", name, value);
        return -1;
    }
    *out = v;
    return 0;
}

static int parse_args(int argc, char **argv, Args *args)
{
    memset(args, 0, sizeof(*args));
    args->params = nilamp_default_params();
    args->block = 64;
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
        } else if (strcmp(a, "--gain") == 0) {
            if (parse_float_arg(a, v, &args->params.gain_db) != 0) return -1;
        } else if (strcmp(a, "--volume") == 0) {
            if (parse_float_arg(a, v, &args->params.volume_pct) != 0) return -1;
        } else if (strcmp(a, "--bass") == 0) {
            if (parse_float_arg(a, v, &args->params.bass_pct) != 0) return -1;
        } else if (strcmp(a, "--mid") == 0) {
            if (parse_float_arg(a, v, &args->params.mid_pct) != 0) return -1;
        } else if (strcmp(a, "--treble") == 0) {
            if (parse_float_arg(a, v, &args->params.treble_pct) != 0) return -1;
        } else if (strcmp(a, "--sag") == 0) {
            if (parse_float_arg(a, v, &args->params.sag_pct) != 0) return -1;
        } else if (strcmp(a, "--block") == 0) {
            char *end = NULL;
            const unsigned long block = strtoul(v, &end, 10);
            if (end == v || *end != '\0' || block == 0ul || block > UINT32_MAX) {
                fprintf(stderr, "error: --block: invalid value '%s'\n", v);
                return -1;
            }
            args->block = (uint32_t)block;
        } else {
            fprintf(stderr, "error: unknown argument: %s\n", a);
            return -1;
        }
    }
    if (args->input_path == NULL || args->output_path == NULL) {
        usage(stderr);
        return -1;
    }
    return 0;
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

    NilampEngine *engine = nilamp_engine_create(wav.sample_rate);
    float *out = NULL;
#ifdef NILAMP_TAPS_RENDER
    float *tap_channels[NILAMP_NUM_TAPS] = { 0 };
    for (size_t ch = 0; ch < NILAMP_NUM_TAPS; ch++) {
        tap_channels[ch] = calloc(frames, sizeof(float));
        if (tap_channels[ch] == NULL) {
            fprintf(stderr, "error: allocation failed\n");
            for (size_t j = 0; j < NILAMP_NUM_TAPS; j++) {
                free(tap_channels[j]);
            }
            free(mono);
            nilamp_engine_destroy(engine);
            free_wav(&wav);
            return 1;
        }
    }
    out = calloc(frames * NILAMP_NUM_TAPS, sizeof(float));
#else
    out = calloc(frames, sizeof(float));
#endif
    if (out == NULL || engine == NULL) {
        fprintf(stderr, "error: allocation failed\n");
#ifdef NILAMP_TAPS_RENDER
        for (size_t ch = 0; ch < NILAMP_NUM_TAPS; ch++) {
            free(tap_channels[ch]);
        }
#endif
        free(out);
        free(mono);
        nilamp_engine_destroy(engine);
        free_wav(&wav);
        return 1;
    }
    nilamp_engine_set_params(engine, &args.params);

    size_t pos = 0;
    while (pos < frames) {
        const size_t n = args.block < frames - pos ? args.block : frames - pos;
#ifdef NILAMP_TAPS_RENDER
        float *views[NILAMP_NUM_TAPS];
        for (size_t ch = 0; ch < NILAMP_NUM_TAPS; ch++) {
            views[ch] = tap_channels[ch] + pos;
        }
        nilamp_engine_process_taps(engine, mono + pos, views, (uint32_t)n);
#else
        nilamp_engine_process(engine, mono + pos, out + pos, (uint32_t)n);
#endif
        pos += n;
    }

#ifdef NILAMP_TAPS_RENDER
    for (size_t f = 0; f < frames; f++) {
        for (size_t ch = 0; ch < NILAMP_NUM_TAPS; ch++) {
            out[f * NILAMP_NUM_TAPS + ch] = tap_channels[ch][f];
        }
    }
    const int rc = write_wav_f32(args.output_path, out, frames, NILAMP_NUM_TAPS, wav.sample_rate);
#else
    const int rc = write_wav_f32(args.output_path, out, frames, 1u, wav.sample_rate);
#endif
    if (rc == 0) {
        fprintf(stderr,
                "rendered %zu frames @ %u Hz, %u ch -> %s\n",
                frames,
                wav.sample_rate,
#ifdef NILAMP_TAPS_RENDER
                (unsigned)NILAMP_NUM_TAPS,
#else
                1u,
#endif
                args.output_path);
    }
    nilamp_engine_destroy(engine);
#ifdef NILAMP_TAPS_RENDER
    for (size_t ch = 0; ch < NILAMP_NUM_TAPS; ch++) {
        free(tap_channels[ch]);
    }
#endif
    free(out);
    free(mono);
    free_wav(&wav);
    return rc == 0 ? 0 : 1;
}
