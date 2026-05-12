// SPDX-License-Identifier: MIT
#include "nilamp_dsp.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports-config.h>
#include <clap/ext/gui.h>
#include <clap/ext/latency.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/param-indication.h>
#include <clap/ext/remote-controls.h>
#include <clap/ext/state-context.h>
#include <clap/ext/tail.h>
#include <clap/ext/timer-support.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#ifndef NILAMP_EXPECT_CLAP_GUI
#define NILAMP_EXPECT_CLAP_GUI 1
#endif

#if NILAMP_EXPECT_CLAP_GUI
#if defined(__APPLE__)
#define NILAMP_EXPECT_CLAP_WINDOW_API CLAP_WINDOW_API_COCOA
#define NILAMP_EXPECT_CLAP_FLOATING 1
#elif defined(_WIN32)
#define NILAMP_EXPECT_CLAP_WINDOW_API CLAP_WINDOW_API_WIN32
#define NILAMP_EXPECT_CLAP_FLOATING 0
#else
#define NILAMP_EXPECT_CLAP_WINDOW_API CLAP_WINDOW_API_X11
#define NILAMP_EXPECT_CLAP_FLOATING 0
#endif
#endif

#define NILAMP_PLUGIN_ID "dev.niltempus.nilamp"
#ifndef NILAMP_EXPECT_CLAP_NAME
#define NILAMP_EXPECT_CLAP_NAME "nilamp"
#endif
#ifndef NILAMP_RELEASE_VERSION
#define NILAMP_RELEASE_VERSION "1.0.2"
#endif

static const char *clap_library_path(const char *plugin_path, char *buffer, size_t buffer_size)
{
#if defined(_WIN32)
    (void)buffer;
    (void)buffer_size;
    return plugin_path;
#else
    struct stat st;
    if (stat(plugin_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return plugin_path;
    }

    const char *name = strrchr(plugin_path, '/');
    name = name ? name + 1 : plugin_path;
    size_t name_len = strlen(name);
    if (name_len > 5 && strcmp(name + name_len - 5, ".clap") == 0) {
        name_len -= 5;
    }

    snprintf(buffer, buffer_size, "%s/Contents/MacOS/%.*s", plugin_path, (int)name_len, name);
    return buffer;
#endif
}
#define NILAMP_HOST_OUTPUT_LIMIT 1.0f
#define NILAMP_STRESS_SAMPLE_RATE 48000.0f

typedef struct TestEvents {
    const clap_event_header_t **events;
    uint32_t count;
} TestEvents;

typedef struct TestOutputEvents {
    clap_event_param_value_t params[32];
    uint32_t count;
} TestOutputEvents;

typedef struct MemoryStream {
    uint8_t data[128];
    uint64_t size;
    uint64_t offset;
} MemoryStream;

typedef struct TestHostData {
    uint32_t request_callback_count;
    uint32_t request_process_count;
    clap_id next_timer_id;
    clap_id active_timer_id;
    uint32_t active_timer_period_ms;
    bool timer_registered;
} TestHostData;

#if defined(_WIN32)
typedef HMODULE NilampModule;

static NilampModule nilamp_module_open(const char *path)
{
    return LoadLibraryA(path);
}

static void *nilamp_module_symbol(NilampModule module, const char *name)
{
    return module ? (void *)GetProcAddress(module, name) : NULL;
}

static void nilamp_module_close(NilampModule module)
{
    if (module) {
        FreeLibrary(module);
    }
}

static void nilamp_module_print_error(const char *prefix)
{
    fprintf(stderr, "test_clap_load: %s failed: Windows error %lu\n",
            prefix, (unsigned long)GetLastError());
}
#else
typedef void *NilampModule;

static NilampModule nilamp_module_open(const char *path)
{
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

static void *nilamp_module_symbol(NilampModule module, const char *name)
{
    return module ? dlsym(module, name) : NULL;
}

static void nilamp_module_close(NilampModule module)
{
    if (module) {
        dlclose(module);
    }
}

static void nilamp_module_print_error(const char *prefix)
{
    fprintf(stderr, "test_clap_load: %s failed: %s\n", prefix, dlerror());
}
#endif

static void fail(const char *message)
{
    fprintf(stderr, "test_clap_load: %s\n", message);
    exit(1);
}

static void check(bool condition, const char *message)
{
    if (!condition) {
        fail(message);
    }
}

static const void *host_get_extension(const clap_host_t *host, const char *extension_id)
{
    (void)host;
    if (extension_id && strcmp(extension_id, CLAP_EXT_TIMER_SUPPORT) == 0) {
        static const clap_host_timer_support_t timer_support = {
            .register_timer = NULL,
            .unregister_timer = NULL,
        };
        return &timer_support;
    }
    return NULL;
}

static void host_request_restart(const clap_host_t *host)
{
    (void)host;
}

static void host_request_process(const clap_host_t *host)
{
    TestHostData *data = (TestHostData *)host->host_data;
    if (data) {
        data->request_process_count++;
    }
}

static void host_request_callback(const clap_host_t *host)
{
    TestHostData *data = (TestHostData *)host->host_data;
    if (data) {
        data->request_callback_count++;
    }
}

static bool host_register_timer(const clap_host_t *host, uint32_t period_ms,
                                clap_id *timer_id)
{
    TestHostData *data = (TestHostData *)host->host_data;
    if (!data || !timer_id || data->timer_registered) {
        return false;
    }
    data->active_timer_id = data->next_timer_id++;
    data->active_timer_period_ms = period_ms;
    data->timer_registered = true;
    *timer_id = data->active_timer_id;
    return true;
}

static bool host_unregister_timer(const clap_host_t *host, clap_id timer_id)
{
    TestHostData *data = (TestHostData *)host->host_data;
    if (!data || !data->timer_registered || timer_id != data->active_timer_id) {
        return false;
    }
    data->timer_registered = false;
    data->active_timer_id = CLAP_INVALID_ID;
    data->active_timer_period_ms = 0;
    return true;
}

static const void *host_get_extension_with_timer(const clap_host_t *host,
                                                 const char *extension_id)
{
    if (extension_id && strcmp(extension_id, CLAP_EXT_TIMER_SUPPORT) == 0) {
        static const clap_host_timer_support_t timer_support = {
            .register_timer = host_register_timer,
            .unregister_timer = host_unregister_timer,
        };
        return &timer_support;
    }
    return host_get_extension(host, extension_id);
}

static uint32_t events_size(const clap_input_events_t *list)
{
    const TestEvents *events = (const TestEvents *)list->ctx;
    return events->count;
}

static const clap_event_header_t *events_get(const clap_input_events_t *list, uint32_t index)
{
    const TestEvents *events = (const TestEvents *)list->ctx;
    return index < events->count ? events->events[index] : NULL;
}

static bool events_try_push(const clap_output_events_t *list, const clap_event_header_t *event)
{
    (void)list;
    (void)event;
    return false;
}

static bool events_capture_try_push(const clap_output_events_t *list,
                                    const clap_event_header_t *event)
{
    TestOutputEvents *events = (TestOutputEvents *)list->ctx;
    if (!events || !event || event->type != CLAP_EVENT_PARAM_VALUE ||
        event->size < sizeof(clap_event_param_value_t) ||
        events->count >= sizeof(events->params) / sizeof(events->params[0])) {
        return false;
    }
    memcpy(&events->params[events->count], event, sizeof(events->params[events->count]));
    events->count++;
    return true;
}

static int64_t stream_write(const clap_ostream_t *stream, const void *buffer, uint64_t size)
{
    MemoryStream *memory = (MemoryStream *)stream->ctx;
    const uint64_t remaining = sizeof(memory->data) - memory->offset;
    const uint64_t count = size < remaining ? size : remaining;
    if (count == 0) {
        return -1;
    }
    memcpy(memory->data + memory->offset, buffer, count);
    memory->offset += count;
    if (memory->offset > memory->size) {
        memory->size = memory->offset;
    }
    return (int64_t)count;
}

static int64_t stream_read(const clap_istream_t *stream, void *buffer, uint64_t size)
{
    MemoryStream *memory = (MemoryStream *)stream->ctx;
    const uint64_t remaining = memory->size - memory->offset;
    const uint64_t count = size < remaining ? size : remaining;
    if (count == 0) {
        return 0;
    }
    memcpy(buffer, memory->data + memory->offset, count);
    memory->offset += count;
    return (int64_t)count;
}

static void fill_input(float *signal, uint32_t frames)
{
    for (uint32_t i = 0; i < frames; i++) {
        const float t = (float)i / (float)frames;
        signal[i] = 0.06f * sinf(17.0f * t) + 0.015f * cosf(43.0f * t);
    }
}

static void compare_output(const float *actual, const float *expected, uint32_t frames,
                           const char *label)
{
    for (uint32_t i = 0; i < frames; i++) {
        const float diff = fabsf(actual[i] - expected[i]);
        if (!isfinite(actual[i]) || diff > 0.00001f) {
            fprintf(stderr,
                    "test_clap_load: %s mismatch at %u: actual=%g expected=%g diff=%g\n",
                    label, i, actual[i], expected[i], diff);
            exit(1);
        }
    }
}

static void render_engine_mono(const float *in, float *ref, uint32_t frames)
{
    NilampEngine *engine = nilamp_engine_create(48000.0);
    check(engine != NULL, "direct engine create failed");
    nilamp_engine_process(engine, in, ref, frames);
    nilamp_engine_destroy(engine);
}

static void run_process_chunks(const clap_plugin_t *plugin, clap_process_t *process,
                               uint32_t total_frames)
{
    static const uint32_t chunks[] = {7u, 13u, 31u, 5u, 64u, 3u, 97u};
    clap_audio_buffer_t *input = (clap_audio_buffer_t *)process->audio_inputs;
    clap_audio_buffer_t *output = process->audio_outputs;
    float *input_base[8] = {0};
    float *output_base[8] = {0};
    const uint32_t input_channels = input->channel_count < 8u ? input->channel_count : 8u;
    const uint32_t output_channels = output->channel_count < 8u ? output->channel_count : 8u;
    for (uint32_t ch = 0; ch < input_channels; ch++) {
        input_base[ch] = input->data32[ch];
    }
    for (uint32_t ch = 0; ch < output_channels; ch++) {
        output_base[ch] = output->data32[ch];
    }

    uint32_t cursor = 0;
    uint32_t chunk_index = 0;
    while (cursor < total_frames) {
        uint32_t frames = chunks[chunk_index % (sizeof(chunks) / sizeof(chunks[0]))];
        if (frames > total_frames - cursor) {
            frames = total_frames - cursor;
        }
        process->frames_count = frames;
        check(plugin->process(plugin, process) == CLAP_PROCESS_CONTINUE,
              "chunked process returned failure");

        for (uint32_t ch = 0; ch < input->channel_count; ch++) {
            if (input->data32[ch]) {
                input->data32[ch] += frames;
            }
        }
        for (uint32_t ch = 0; ch < output->channel_count; ch++) {
            if (output->data32 != input->data32 && output->data32[ch]) {
                output->data32[ch] += frames;
            }
        }
        cursor += frames;
        chunk_index++;
    }

    for (uint32_t ch = 0; ch < input_channels; ch++) {
        input->data32[ch] = input_base[ch];
    }
    for (uint32_t ch = 0; ch < output_channels; ch++) {
        if (output->data32 != input->data32) {
            output->data32[ch] = output_base[ch];
        }
    }
}

static void run_clap_engine_compare(const clap_plugin_t *plugin,
                                    clap_process_t *process,
                                    clap_input_events_t *in_events,
                                    clap_output_events_t *out_events)
{
    enum { Frames = 257 };
    float in_buf[Frames];
    float ref[Frames];
    float out_buf[Frames];
    fill_input(in_buf, Frames);

    // Mono in / mono out is the only shape we accept (matches Keller's JSFX
    // `in_pin: mono input`, `out_pin: mono output`). Streaming, in-place,
    // and constant-input scenarios all flow through the single engine.
    plugin->reset(plugin);
    memset(out_buf, 0, sizeof(out_buf));
    render_engine_mono(in_buf, ref, Frames);

    float *mono_input_only[1] = {in_buf};
    float *mono_output_only[1] = {out_buf};
    clap_audio_buffer_t input = {
        .data32 = mono_input_only,
        .data64 = NULL,
        .channel_count = 1,
        .latency = 0,
        .constant_mask = 0,
    };
    clap_audio_buffer_t output = {
        .data32 = mono_output_only,
        .data64 = NULL,
        .channel_count = 1,
        .latency = 0,
        .constant_mask = 0,
    };
    process->audio_inputs = &input;
    process->audio_outputs = &output;
    process->audio_inputs_count = 1;
    process->audio_outputs_count = 1;
    process->in_events = in_events;
    process->out_events = out_events;
    run_process_chunks(plugin, process, Frames);
    compare_output(out_buf, ref, Frames, "mono in/out");

    plugin->reset(plugin);
    float mono_inplace[Frames];
    memcpy(mono_inplace, in_buf, sizeof(mono_inplace));
    float *mono_inplace_channels[1] = {mono_inplace};
    input.data32 = mono_inplace_channels;
    output.data32 = mono_inplace_channels;
    run_process_chunks(plugin, process, Frames);
    compare_output(mono_inplace, ref, Frames, "mono in-place");

    plugin->reset(plugin);
    float constant = 0.025f;
    float constant_in[1] = {constant};
    float constant_ref_input[Frames];
    for (uint32_t i = 0; i < Frames; i++) {
        constant_ref_input[i] = constant;
        out_buf[i] = 0.0f;
    }
    render_engine_mono(constant_ref_input, ref, Frames);
    float *constant_inputs[1] = {constant_in};
    input.data32 = constant_inputs;
    input.channel_count = 1;
    input.constant_mask = 1u;
    output.data32 = mono_output_only;
    output.channel_count = 1;
    process->frames_count = Frames;
    check(plugin->process(plugin, process) == CLAP_PROCESS_CONTINUE,
          "constant process returned failure");
    compare_output(out_buf, ref, Frames, "constant");
    input.constant_mask = 0u;
}

static void init_param_event(clap_event_param_value_t *event, clap_id id, double value)
{
    memset(event, 0, sizeof(*event));
    event->header.size = sizeof(*event);
    event->header.time = 0;
    event->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event->header.type = CLAP_EVENT_PARAM_VALUE;
    event->param_id = id;
    event->note_id = -1;
    event->port_index = -1;
    event->channel = -1;
    event->key = -1;
    event->value = value;
}

static void init_midi_cc_event(clap_event_midi_t *event, uint32_t time,
                               uint16_t port_index, uint8_t channel,
                               uint8_t controller, uint8_t value)
{
    memset(event, 0, sizeof(*event));
    event->header.size = sizeof(*event);
    event->header.time = time;
    event->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event->header.type = CLAP_EVENT_MIDI;
    event->header.flags = CLAP_EVENT_IS_LIVE;
    event->port_index = port_index;
    event->data[0] = (uint8_t)(0xb0u | (channel & 0x0fu));
    event->data[1] = controller;
    event->data[2] = value;
}

static void init_midi_note_on_event(clap_event_midi_t *event, uint32_t time)
{
    memset(event, 0, sizeof(*event));
    event->header.size = sizeof(*event);
    event->header.time = time;
    event->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event->header.type = CLAP_EVENT_MIDI;
    event->header.flags = CLAP_EVENT_IS_LIVE;
    event->port_index = 0;
    event->data[0] = 0x90u;
    event->data[1] = 60u;
    event->data[2] = 100u;
}

static double midi_cc_plain_value(clap_id id, uint8_t value)
{
    const NilampControlSpec *spec = nilamp_control_specs(NULL);
    check(id < NILAMP_PARAM_COUNT, "invalid MIDI expected param id");
    if (id == NILAMP_PARAM_BYPASS) {
        return value >= 64u ? 1.0 : 0.0;
    }
    return spec[id].min_value +
           ((double)value / 127.0) * (spec[id].max_value - spec[id].min_value);
}

static void reset_clap_params_to_defaults(const clap_plugin_t *plugin,
                                          const clap_plugin_params_t *params,
                                          clap_output_events_t *out_events)
{
    uint32_t spec_count = 0;
    const NilampControlSpec *specs = nilamp_control_specs(&spec_count);
    clap_event_param_value_t param_events[NILAMP_PARAM_COUNT];
    const clap_event_header_t *event_ptrs[NILAMP_PARAM_COUNT];

    check(spec_count == NILAMP_PARAM_COUNT, "default reset spec count mismatch");
    for (uint32_t i = 0; i < spec_count; i++) {
        init_param_event(&param_events[i], specs[i].id, specs[i].default_value);
        event_ptrs[i] = &param_events[i].header;
    }

    TestEvents events = {.events = event_ptrs, .count = spec_count};
    clap_input_events_t input = {
        .ctx = &events,
        .size = events_size,
        .get = events_get,
    };
    params->flush(plugin, &input, out_events);
    plugin->reset(plugin);
}

static void check_param_metadata(const clap_plugin_t *plugin,
                                 const clap_plugin_params_t *params)
{
    uint32_t spec_count = 0;
    const NilampControlSpec *specs = nilamp_control_specs(&spec_count);
    check(specs != NULL, "missing control specs");
    check(spec_count == NILAMP_PARAM_COUNT, "unexpected control spec count");
    check(params->count(plugin) == spec_count, "CLAP param count does not match specs");

    for (uint32_t i = 0; i < spec_count; i++) {
        clap_param_info_t info = {0};
        const NilampControlSpec *spec = &specs[i];
        check(params->get_info(plugin, i, &info), "param metadata read failed");
        check(info.id == spec->id, "param metadata id mismatch");
        const char *expected_name = spec->host_name ? spec->host_name : spec->name;
        check(strcmp(info.name, expected_name) == 0, "param metadata name mismatch");
        check(strcmp(info.module, spec->module) == 0, "param metadata module mismatch");
        check(fabs(info.min_value - spec->min_value) < 0.000001,
              "param metadata minimum mismatch");
        check(fabs(info.max_value - spec->max_value) < 0.000001,
              "param metadata maximum mismatch");
        check(fabs(info.default_value - spec->default_value) < 0.000001,
              "param metadata default mismatch");

        if (spec->display == NILAMP_CONTROL_DISPLAY_ENUM) {
            check((info.flags & CLAP_PARAM_IS_STEPPED) != 0,
                  "enum param is not marked stepped");
            check((info.flags & CLAP_PARAM_IS_ENUM) != 0,
                  "enum param is not marked enum");
            check(spec->enum_names != NULL && spec->enum_count > 0u,
                  "enum spec is missing labels");
            for (uint32_t value = 0; value < spec->enum_count; value++) {
                char text[64];
                double parsed = -1.0;
                check(params->value_to_text(plugin, spec->id, (double)value,
                                            text, sizeof(text)),
                      "enum value_to_text failed");
                check(strcmp(text, spec->enum_names[value]) == 0,
                      "enum value_to_text label mismatch");
                check(params->text_to_value(plugin, spec->id, spec->enum_names[value],
                                            &parsed),
                      "enum text_to_value failed");
                check(fabs(parsed - (double)value) < 0.000001,
                      "enum text_to_value mismatch");
            }
        }
        if (spec->id == NILAMP_PARAM_BYPASS) {
            check((info.flags & CLAP_PARAM_IS_BYPASS) != 0,
                  "bypass param is not marked bypass");
            check((info.flags & CLAP_PARAM_IS_STEPPED) != 0,
                  "bypass param is not marked stepped");
            check(fabs(info.min_value) < 0.000001 &&
                      fabs(info.max_value - 1.0) < 0.000001 &&
                      fabs(info.default_value) < 0.000001,
                  "unexpected bypass range/default");
        }
    }
}

static double alternate_param_value(const NilampControlSpec *spec)
{
    if (spec->display == NILAMP_CONTROL_DISPLAY_ENUM) {
        return spec->max_value;
    }
    double value = spec->default_value + spec->step;
    if (value > spec->max_value) {
        value = spec->min_value;
    }
    if (fabs(value - spec->default_value) < 0.000001 && spec->max_value > spec->min_value) {
        value = spec->max_value;
    }
    return value;
}

static void run_all_param_automation_test(const clap_plugin_t *plugin,
                                          const clap_plugin_params_t *params,
                                          clap_process_t *process,
                                          clap_output_events_t *out_events)
{
    uint32_t spec_count = 0;
    const NilampControlSpec *specs = nilamp_control_specs(&spec_count);
    clap_event_param_value_t events[NILAMP_PARAM_COUNT];
    const clap_event_header_t *event_ptrs[NILAMP_PARAM_COUNT];
    double expected[NILAMP_PARAM_COUNT];

    check(spec_count == NILAMP_PARAM_COUNT, "automation spec count mismatch");
    for (uint32_t i = 0; i < spec_count; i++) {
        expected[i] = alternate_param_value(&specs[i]);
        init_param_event(&events[i], specs[i].id, expected[i]);
        event_ptrs[i] = &events[i].header;
    }

    TestEvents all_events = {.events = event_ptrs, .count = spec_count};
    clap_input_events_t input_events = {
        .ctx = &all_events,
        .size = events_size,
        .get = events_get,
    };
    params->flush(plugin, &input_events, out_events);
    for (uint32_t i = 0; i < spec_count; i++) {
        double actual = NAN;
        check(params->get_value(plugin, specs[i].id, &actual),
              "automated param read failed");
        check(fabs(actual - expected[i]) < 0.000001,
              "automated param value mismatch");
    }

    plugin->reset(plugin);
    check(plugin->process(plugin, process) == CLAP_PROCESS_CONTINUE,
          "all-param automation process returned failure");
    clap_audio_buffer_t *output = process->audio_outputs;
    for (uint32_t ch = 0; ch < output->channel_count; ch++) {
        if (!output->data32[ch]) {
            continue;
        }
        for (uint32_t i = 0; i < process->frames_count; i++) {
            check(isfinite(output->data32[ch][i]), "all-param automation output is non-finite");
        }
    }
}

static void run_clap_output_safety_test(const clap_plugin_t *plugin,
                                        const clap_plugin_params_t *params,
                                        clap_process_t *process,
                                        clap_output_events_t *out_events)
{
    enum { Frames = 48000 };
    static float in_buf[Frames];
    static float out_buf[Frames];

    clap_event_param_value_t param_events[6];
    const double values[6] = {6.0, 80.0, 30.0, 60.0, 70.0, 100.0};
    const clap_event_header_t *event_ptrs[6];
    for (uint32_t i = 0; i < 6u; i++) {
        init_param_event(&param_events[i], i, values[i]);
        event_ptrs[i] = &param_events[i].header;
    }
    TestEvents parameter_events = {.events = event_ptrs, .count = 6u};
    clap_input_events_t parameter_input = {
        .ctx = &parameter_events,
        .size = events_size,
        .get = events_get,
    };
    params->flush(plugin, &parameter_input, out_events);
    plugin->reset(plugin);

    for (uint32_t i = 0; i < Frames; i++) {
        const float t = (float)i / NILAMP_STRESS_SAMPLE_RATE;
        in_buf[i] = 0.15f * sinf(2.0f * 3.14159265358979323846f * 220.0f * t);
        out_buf[i] = 0.0f;
    }

    float *input_channels[1] = {in_buf};
    float *output_channels[1] = {out_buf};
    clap_audio_buffer_t input = {
        .data32 = input_channels,
        .data64 = NULL,
        .channel_count = 1,
        .latency = 0,
        .constant_mask = 0,
    };
    clap_audio_buffer_t output = {
        .data32 = output_channels,
        .data64 = NULL,
        .channel_count = 1,
        .latency = 0,
        .constant_mask = 0,
    };
    TestEvents empty_events = {.events = NULL, .count = 0};
    clap_input_events_t empty_input = {
        .ctx = &empty_events,
        .size = events_size,
        .get = events_get,
    };

    process->audio_inputs = &input;
    process->audio_outputs = &output;
    process->audio_inputs_count = 1;
    process->audio_outputs_count = 1;
    process->in_events = &empty_input;
    process->out_events = out_events;
    run_process_chunks(plugin, process, Frames);

    float peak = 0.0f;
    for (uint32_t i = 0; i < Frames; i++) {
        check(isfinite(out_buf[i]), "stress output is non-finite");
        peak = fmaxf(peak, fabsf(out_buf[i]));
    }
    check(peak > 1.0e-8f, "stress output is silent");
    check(peak <= NILAMP_HOST_OUTPUT_LIMIT + 0.000001f,
          "stress output exceeds host safety limit");
}

static void run_clap_bypass_test(const clap_plugin_t *plugin,
                                 const clap_plugin_params_t *params,
                                 clap_process_t *process,
                                 clap_output_events_t *out_events)
{
    enum { Frames = 64 };
    float in_buf[Frames];
    float out_buf[Frames];
    fill_input(in_buf, Frames);
    memset(out_buf, 0, sizeof(out_buf));

    clap_event_param_value_t bypass_event;
    init_param_event(&bypass_event, NILAMP_PARAM_BYPASS, 1.0);
    const clap_event_header_t *event_ptrs[1] = {&bypass_event.header};
    TestEvents bypass_events = {.events = event_ptrs, .count = 1u};
    clap_input_events_t bypass_input = {
        .ctx = &bypass_events,
        .size = events_size,
        .get = events_get,
    };
    params->flush(plugin, &bypass_input, out_events);

    float *input_channels[1] = {in_buf};
    float *output_channels[1] = {out_buf};
    clap_audio_buffer_t input = {
        .data32 = input_channels,
        .data64 = NULL,
        .channel_count = 1,
        .latency = 0,
        .constant_mask = 0,
    };
    clap_audio_buffer_t output = {
        .data32 = output_channels,
        .data64 = NULL,
        .channel_count = 1,
        .latency = 0,
        .constant_mask = 0,
    };
    TestEvents empty_events = {.events = NULL, .count = 0};
    clap_input_events_t empty_input = {
        .ctx = &empty_events,
        .size = events_size,
        .get = events_get,
    };

    process->audio_inputs = &input;
    process->audio_outputs = &output;
    process->audio_inputs_count = 1;
    process->audio_outputs_count = 1;
    process->in_events = &empty_input;
    process->out_events = out_events;
    process->frames_count = Frames;
    check(plugin->process(plugin, process) == CLAP_PROCESS_CONTINUE,
          "bypass process returned failure");
    compare_output(out_buf, in_buf, Frames, "bypass");

    bypass_event.value = 0.0;
    params->flush(plugin, &bypass_input, out_events);
}

static void run_clap_midi_test(const clap_plugin_t *plugin,
                               const clap_plugin_params_t *params,
                               clap_process_t *process,
                               clap_output_events_t *scratch_out_events)
{
    enum { Frames = 64 };
    const clap_process_t original_process = *process;
    static float in_buf[Frames];
    static float out_buf[Frames];

    reset_clap_params_to_defaults(plugin, params, scratch_out_events);
    fill_input(in_buf, Frames);
    memset(out_buf, 0, sizeof(out_buf));

    float *input_channels[1] = {in_buf};
    float *output_channels[1] = {out_buf};
    clap_audio_buffer_t input = {
        .data32 = input_channels,
        .data64 = NULL,
        .channel_count = 1,
        .latency = 0,
        .constant_mask = 0,
    };
    clap_audio_buffer_t output = {
        .data32 = output_channels,
        .data64 = NULL,
        .channel_count = 1,
        .latency = 0,
        .constant_mask = 0,
    };

    const clap_id mapped_ids[] = {
        NILAMP_PARAM_BYPASS,
        NILAMP_PARAM_GAIN_DB,
        NILAMP_PARAM_OUTPUT_GAIN_DB,
        NILAMP_PARAM_VOLUME_PCT,
        NILAMP_PARAM_BASS_PCT,
        NILAMP_PARAM_MID_PCT,
        NILAMP_PARAM_TREBLE_PCT,
    };
    const uint8_t mapped_ccs[] = {64u, 16u, 17u, 18u, 19u, 80u, 81u};
    const uint8_t mapped_values[] = {127u, 127u, 0u, 64u, 32u, 96u, 48u};
    clap_event_midi_t midi_events[sizeof(mapped_ids) / sizeof(mapped_ids[0])];
    const clap_event_header_t *event_ptrs[sizeof(mapped_ids) / sizeof(mapped_ids[0])];
    for (uint32_t i = 0; i < sizeof(mapped_ids) / sizeof(mapped_ids[0]); i++) {
        init_midi_cc_event(&midi_events[i], i, 0u, (uint8_t)i, mapped_ccs[i],
                           mapped_values[i]);
        event_ptrs[i] = &midi_events[i].header;
    }

    TestEvents input_events = {
        .events = event_ptrs,
        .count = sizeof(mapped_ids) / sizeof(mapped_ids[0]),
    };
    clap_input_events_t midi_input = {
        .ctx = &input_events,
        .size = events_size,
        .get = events_get,
    };
    TestOutputEvents captured = {0};
    clap_output_events_t captured_output = {
        .ctx = &captured,
        .try_push = events_capture_try_push,
    };

    process->audio_inputs = &input;
    process->audio_outputs = &output;
    process->audio_inputs_count = 1;
    process->audio_outputs_count = 1;
    process->in_events = &midi_input;
    process->out_events = &captured_output;
    process->frames_count = Frames;
    check(plugin->process(plugin, process) == CLAP_PROCESS_CONTINUE,
          "MIDI mapped process returned failure");
    check(captured.count == sizeof(mapped_ids) / sizeof(mapped_ids[0]),
          "unexpected MIDI output param event count");
    for (uint32_t i = 0; i < sizeof(mapped_ids) / sizeof(mapped_ids[0]); i++) {
        const double expected = midi_cc_plain_value(mapped_ids[i], mapped_values[i]);
        double actual = NAN;
        check(params->get_value(plugin, mapped_ids[i], &actual),
              "MIDI mapped param read failed");
        check(fabs(actual - expected) < 0.00001, "MIDI mapped param value mismatch");
        check(captured.params[i].header.time == i, "MIDI output event time mismatch");
        check((captured.params[i].header.flags & CLAP_EVENT_DONT_RECORD) != 0u,
              "MIDI output event is recordable");
        check((captured.params[i].header.flags & CLAP_EVENT_IS_LIVE) != 0u,
              "MIDI output event is not live");
        check(captured.params[i].param_id == mapped_ids[i],
              "MIDI output event param id mismatch");
        check(fabs(captured.params[i].value - expected) < 0.00001,
              "MIDI output event value mismatch");
    }

    clap_event_midi_t ignored_events[3];
    init_midi_cc_event(&ignored_events[0], 0u, 1u, 0u, 16u, 0u);
    init_midi_note_on_event(&ignored_events[1], 1u);
    init_midi_cc_event(&ignored_events[2], 2u, 0u, 0u, 82u, 127u);
    const clap_event_header_t *ignored_ptrs[3] = {
        &ignored_events[0].header,
        &ignored_events[1].header,
        &ignored_events[2].header,
    };
    TestEvents ignored_input_events = {.events = ignored_ptrs, .count = 3u};
    midi_input.ctx = &ignored_input_events;
    captured.count = 0u;
    check(plugin->process(plugin, process) == CLAP_PROCESS_CONTINUE,
          "MIDI ignored process returned failure");
    check(captured.count == 0u, "ignored MIDI produced output param events");
    double gain = NAN;
    check(params->get_value(plugin, NILAMP_PARAM_GAIN_DB, &gain),
          "MIDI ignored gain read failed");
    check(fabs(gain - midi_cc_plain_value(NILAMP_PARAM_GAIN_DB, 127u)) < 0.00001,
          "ignored MIDI changed mapped gain");

    reset_clap_params_to_defaults(plugin, params, scratch_out_events);
    fill_input(in_buf, Frames);
    float ref[Frames];
    render_engine_mono(in_buf, ref, Frames);
    memset(out_buf, 0, sizeof(out_buf));
    clap_event_midi_t bypass_event;
    init_midi_cc_event(&bypass_event, 32u, 0u, 0u, 64u, 127u);
    const clap_event_header_t *bypass_ptrs[1] = {&bypass_event.header};
    TestEvents bypass_input_events = {.events = bypass_ptrs, .count = 1u};
    midi_input.ctx = &bypass_input_events;
    captured.count = 0u;
    check(plugin->process(plugin, process) == CLAP_PROCESS_CONTINUE,
          "MIDI bypass split process returned failure");
    compare_output(out_buf, ref, 32u, "MIDI pre-bypass");
    compare_output(out_buf + 32u, in_buf + 32u, Frames - 32u, "MIDI post-bypass");

    reset_clap_params_to_defaults(plugin, params, scratch_out_events);
    *process = original_process;
}

int main(int argc, char **argv)
{
    const char *plugin_path = argc > 1 ? argv[1] : "native/bin/nilamp-twd-mkii.clap";
    char library_path[4096];
    const char *load_path = clap_library_path(plugin_path, library_path, sizeof(library_path));
    NilampModule handle = nilamp_module_open(load_path);
    if (!handle) {
        nilamp_module_print_error("module open");
        return 1;
    }

    const clap_plugin_entry_t *entry =
        (const clap_plugin_entry_t *)nilamp_module_symbol(handle, "clap_entry");
    check(entry != NULL, "missing clap_entry");
    check(entry->init(plugin_path), "entry init failed");

    const clap_plugin_factory_t *factory =
        (const clap_plugin_factory_t *)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    check(factory != NULL, "missing plugin factory");
    check(factory->get_plugin_count(factory) == 1, "unexpected plugin count");

    const clap_plugin_descriptor_t *descriptor =
        factory->get_plugin_descriptor(factory, 0);
    check(descriptor != NULL, "missing descriptor");
    check(strcmp(descriptor->id, NILAMP_PLUGIN_ID) == 0, "unexpected plugin id");
    check(descriptor->name != NULL &&
              strcmp(descriptor->name, NILAMP_EXPECT_CLAP_NAME) == 0,
          "unexpected plugin name");
    check(descriptor->version != NULL &&
              strcmp(descriptor->version, NILAMP_RELEASE_VERSION) == 0,
          "unexpected plugin version");

    TestHostData host_data = {
        .next_timer_id = 1u,
        .active_timer_id = CLAP_INVALID_ID,
    };
    clap_host_t host = {
        .clap_version = CLAP_VERSION_INIT,
        .host_data = &host_data,
        .name = "nilamp smoke host",
        .vendor = "niltempus",
        .url = "",
        .version = "0.1.0",
        .get_extension = host_get_extension_with_timer,
        .request_restart = host_request_restart,
        .request_process = host_request_process,
        .request_callback = host_request_callback,
    };

    const clap_plugin_t *plugin =
        factory->create_plugin(factory, &host, NILAMP_PLUGIN_ID);
    check(plugin != NULL, "create plugin failed");
    check(plugin->init(plugin), "plugin init failed");

    const clap_plugin_audio_ports_t *audio_ports =
        (const clap_plugin_audio_ports_t *)plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS);
    check(audio_ports != NULL, "missing audio ports extension");
    clap_audio_port_info_t port_info = {0};
    check(audio_ports->count(plugin, true) == 1, "unexpected input port count");
    check(audio_ports->count(plugin, false) == 1, "unexpected output port count");
    check(audio_ports->get(plugin, 0, true, &port_info), "input port info failed");
    check(port_info.channel_count == 1, "input port is not mono");
    check(port_info.port_type && strcmp(port_info.port_type, CLAP_PORT_MONO) == 0,
          "input port type is not mono");
    check(audio_ports->get(plugin, 0, false, &port_info), "output port info failed");
    check(port_info.channel_count == 1, "output port is not mono");
    check(port_info.port_type && strcmp(port_info.port_type, CLAP_PORT_MONO) == 0,
          "output port type is not mono");

    // Guitar amp: mono in / mono out only. The audio-ports-config and
    // audio-ports-config-info extensions are intentionally not advertised
    // because there is no alternative configuration to switch to.
    check(plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS_CONFIG) == NULL,
          "audio ports config extension should not be advertised");
    check(plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO) == NULL,
          "audio ports config info extension should not be advertised");

    const clap_plugin_params_t *params =
        (const clap_plugin_params_t *)plugin->get_extension(plugin, CLAP_EXT_PARAMS);
    check(params != NULL, "missing params extension");
    check_param_metadata(plugin, params);
    clap_param_info_t param_info = {0};
    check(params->get_info(plugin, NILAMP_PARAM_GAIN_DB, &param_info),
          "gain info read failed");
    check(strcmp(param_info.name, "Input Gain") == 0, "unexpected input gain host name");
    check(fabs(param_info.min_value + 12.0) < 0.000001,
          "unexpected minimum gain info");
    check(fabs(param_info.max_value - 12.0) < 0.000001,
          "unexpected maximum gain info");
    check(fabs(param_info.default_value) < 0.000001,
          "unexpected default gain info");
    check(params->get_info(plugin, NILAMP_PARAM_OUTPUT_GAIN_DB, &param_info),
          "output gain info read failed");
    check(strcmp(param_info.name, "Output Gain") == 0, "unexpected output gain host name");
    check(fabs(param_info.min_value + 12.0) < 0.000001,
          "unexpected minimum output gain info");
    check(fabs(param_info.max_value - 12.0) < 0.000001,
          "unexpected maximum output gain info");
    check(fabs(param_info.default_value) < 0.000001,
          "unexpected default output gain info");
    const clap_id preamp_ids[] = {
        NILAMP_PARAM_VOLUME_PCT,
        NILAMP_PARAM_BASS_PCT,
        NILAMP_PARAM_MID_PCT,
        NILAMP_PARAM_TREBLE_PCT,
    };
    for (uint32_t i = 0; i < sizeof(preamp_ids) / sizeof(preamp_ids[0]); i++) {
        check(params->get_info(plugin, preamp_ids[i], &param_info),
              "preamp info read failed");
        check(fabs(param_info.default_value - 50.0) < 0.000001,
              "unexpected default preamp info");
    }
    double gain = -1.0;
    check(params->get_value(plugin, NILAMP_PARAM_GAIN_DB, &gain), "gain read failed");
    check(fabs(gain) < 0.000001, "unexpected default gain");
    double output_gain = -99.0;
    check(params->get_value(plugin, NILAMP_PARAM_OUTPUT_GAIN_DB, &output_gain),
          "output gain read failed");
    check(fabs(output_gain) < 0.000001, "unexpected default output gain");
    for (uint32_t i = 0; i < sizeof(preamp_ids) / sizeof(preamp_ids[0]); i++) {
        double value = -1.0;
        check(params->get_value(plugin, preamp_ids[i], &value),
              "preamp value read failed");
        check(fabs(value - 50.0) < 0.000001, "unexpected default preamp value");
    }
    double fmid = 0.0;
    check(params->get_value(plugin, NILAMP_PARAM_TONE_FMID_DBHZ, &fmid),
          "Fmid read failed");
    check(fabs(fmid - 56.0) < 0.000001, "unexpected default Fmid");
    check(params->get_info(plugin, NILAMP_PARAM_TUBE1, &param_info),
          "tube1 info read failed");
    check(fabs(param_info.default_value - 1.0) < 0.000001,
          "unexpected default tube1 info");
    check(params->get_info(plugin, NILAMP_PARAM_PHASE_SPLITTER, &param_info),
          "splitter info read failed");
    check(fabs(param_info.default_value - 2.0) < 0.000001,
          "unexpected default splitter info");
    double tube1 = -1.0;
    double splitter = -1.0;
    check(params->get_value(plugin, NILAMP_PARAM_TUBE1, &tube1), "tube1 read failed");
    check(params->get_value(plugin, NILAMP_PARAM_PHASE_SPLITTER, &splitter),
          "splitter read failed");
    check(fabs(tube1 - 1.0) < 0.000001, "unexpected default tube1");
    check(fabs(splitter - 2.0) < 0.000001, "unexpected default splitter");
    check(params->get_info(plugin, NILAMP_PARAM_BYPASS, &param_info),
          "bypass info read failed");
    check(strcmp(param_info.name, "Bypass") == 0, "unexpected bypass host name");
    double bypass = -1.0;
    check(params->get_value(plugin, NILAMP_PARAM_BYPASS, &bypass), "bypass read failed");
    check(fabs(bypass) < 0.000001, "unexpected default bypass");
    char text[32];
    check(params->value_to_text(plugin, NILAMP_PARAM_TUBE1, 0.0, text, sizeof(text)) &&
              strcmp(text, "12AY7") == 0,
          "tube1 value_to_text failed");
    check(params->value_to_text(plugin, NILAMP_PARAM_PHASE_SPLITTER, 4.0, text, sizeof(text)) &&
              strcmp(text, "LTP 3") == 0,
          "splitter value_to_text failed");
    check(params->text_to_value(plugin, NILAMP_PARAM_PHASE_SPLITTER, "CD BAL", &splitter) &&
              fabs(splitter - 1.0) < 0.000001,
          "splitter text_to_value failed");

    const clap_plugin_state_t *state =
        (const clap_plugin_state_t *)plugin->get_extension(plugin, CLAP_EXT_STATE);
    check(state != NULL, "missing state extension");

    const clap_plugin_state_context_t *state_context =
        (const clap_plugin_state_context_t *)plugin->get_extension(plugin,
                                                                   CLAP_EXT_STATE_CONTEXT);
    check(state_context != NULL, "missing state context extension");
    const clap_plugin_latency_t *latency =
        (const clap_plugin_latency_t *)plugin->get_extension(plugin, CLAP_EXT_LATENCY);
    check(latency != NULL, "missing latency extension");
    check(latency->get(plugin) == 0u, "unexpected CLAP latency");
    const clap_plugin_tail_t *tail =
        (const clap_plugin_tail_t *)plugin->get_extension(plugin, CLAP_EXT_TAIL);
    check(tail != NULL, "missing tail extension");
    check(tail->get(plugin) == 0u, "unexpected CLAP tail");

#if NILAMP_EXPECT_CLAP_GUI
    const clap_plugin_param_indication_t *param_indication =
        (const clap_plugin_param_indication_t *)plugin->get_extension(
            plugin, CLAP_EXT_PARAM_INDICATION);
    check(param_indication != NULL, "missing param indication extension");
    check(plugin->get_extension(plugin, CLAP_EXT_PARAM_INDICATION_COMPAT) ==
              param_indication,
          "missing param indication compat extension");

    MemoryStream indication_before = {0};
    clap_ostream_t indication_before_out = {
        .ctx = &indication_before,
        .write = stream_write,
    };
    check(state->save(plugin, &indication_before_out),
          "pre-indication state save failed");
    const double gain_before_indication = gain;
    const clap_color_t indication_color = {
        .alpha = 230u,
        .red = 82u,
        .green = 190u,
        .blue = 255u,
    };
    param_indication->set_mapping(plugin, NILAMP_PARAM_GAIN_DB, true,
                                  &indication_color, "GPC1", "mapped");
    param_indication->set_automation(plugin, NILAMP_PARAM_GAIN_DB,
                                     CLAP_PARAM_INDICATION_AUTOMATION_PLAYING,
                                     &indication_color);
    param_indication->set_mapping(plugin, CLAP_INVALID_ID, true, &indication_color,
                                  "bad", "bad");
    param_indication->set_automation(plugin, CLAP_INVALID_ID,
                                     CLAP_PARAM_INDICATION_AUTOMATION_PLAYING,
                                     &indication_color);
    param_indication->set_mapping(plugin, NILAMP_PARAM_GAIN_DB, false, NULL, NULL, NULL);
    param_indication->set_automation(plugin, NILAMP_PARAM_GAIN_DB,
                                     CLAP_PARAM_INDICATION_AUTOMATION_NONE, NULL);
    check(params->get_value(plugin, NILAMP_PARAM_GAIN_DB, &gain),
          "post-indication gain read failed");
    check(fabs(gain - gain_before_indication) < 0.000001,
          "param indication changed gain");
    MemoryStream indication_after = {0};
    clap_ostream_t indication_after_out = {
        .ctx = &indication_after,
        .write = stream_write,
    };
    check(state->save(plugin, &indication_after_out),
          "post-indication state save failed");
    check(indication_before.size == indication_after.size &&
              memcmp(indication_before.data, indication_after.data,
                     (size_t)indication_before.size) == 0,
          "param indication changed state");
#else
    check(plugin->get_extension(plugin, CLAP_EXT_PARAM_INDICATION) == NULL,
          "unexpected param indication extension");
#endif

    const clap_plugin_remote_controls_t *remote_controls =
        (const clap_plugin_remote_controls_t *)plugin->get_extension(
            plugin, CLAP_EXT_REMOTE_CONTROLS);
    check(remote_controls != NULL, "missing remote controls extension");
    check(plugin->get_extension(plugin, CLAP_EXT_REMOTE_CONTROLS_COMPAT) == remote_controls,
          "missing remote controls compat extension");
    check(remote_controls->count(plugin) == 1, "unexpected remote controls page count");
    clap_remote_controls_page_t remote_page = {0};
    check(remote_controls->get(plugin, 0, &remote_page), "remote controls page read failed");
    check(remote_page.page_id == 1, "unexpected remote controls page id");
    check(strcmp(remote_page.section_name, "Main") == 0,
          "unexpected remote controls section");
    check(strcmp(remote_page.page_name, "Amp Face") == 0,
          "unexpected remote controls page name");
    check(!remote_page.is_for_preset, "remote controls page is preset-specific");
    const clap_id expected_remote_params[CLAP_REMOTE_CONTROLS_COUNT] = {
        NILAMP_PARAM_BYPASS,
        NILAMP_PARAM_GAIN_DB,
        NILAMP_PARAM_OUTPUT_GAIN_DB,
        NILAMP_PARAM_VOLUME_PCT,
        NILAMP_PARAM_BASS_PCT,
        NILAMP_PARAM_MID_PCT,
        NILAMP_PARAM_TREBLE_PCT,
        CLAP_INVALID_ID,
    };
    for (uint32_t i = 0; i < CLAP_REMOTE_CONTROLS_COUNT; i++) {
        check(remote_page.param_ids[i] == expected_remote_params[i],
              "remote controls param mismatch");
    }
    check(!remote_controls->get(plugin, 1, &remote_page),
          "remote controls invalid page succeeded");

    const clap_plugin_note_ports_t *note_ports =
        (const clap_plugin_note_ports_t *)plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS);
    check(note_ports != NULL, "missing note ports extension");
    check(note_ports->count(plugin, true) == 1, "unexpected input note port count");
    check(note_ports->count(plugin, false) == 0, "unexpected output note port count");
    clap_note_port_info_t note_port = {0};
    check(note_ports->get(plugin, 0, true, &note_port), "note port info read failed");
    check(note_port.id == 0, "unexpected note port id");
    check(strcmp(note_port.name, "MIDI In") == 0, "unexpected note port name");
    check(note_port.supported_dialects == CLAP_NOTE_DIALECT_MIDI,
          "unexpected note port supported dialects");
    check(note_port.preferred_dialect == CLAP_NOTE_DIALECT_MIDI,
          "unexpected note port preferred dialect");
    check(!note_ports->get(plugin, 1, true, &note_port),
          "invalid input note port read succeeded");
    check(!note_ports->get(plugin, 0, false, &note_port),
          "invalid output note port read succeeded");

    const clap_plugin_gui_t *gui =
        (const clap_plugin_gui_t *)plugin->get_extension(plugin, CLAP_EXT_GUI);
    const clap_plugin_timer_support_t *timer =
        (const clap_plugin_timer_support_t *)plugin->get_extension(plugin, CLAP_EXT_TIMER_SUPPORT);
#if NILAMP_EXPECT_CLAP_GUI
    check(gui != NULL, "missing gui extension");
    check(gui->is_api_supported(plugin, NILAMP_EXPECT_CLAP_WINDOW_API, false),
          "gui does not support expected embedded api");
    check(gui->is_api_supported(plugin, NILAMP_EXPECT_CLAP_WINDOW_API, true) ==
              (bool)NILAMP_EXPECT_CLAP_FLOATING,
          "unexpected floating gui support");
    check(!gui->is_api_supported(plugin, CLAP_WINDOW_API_WAYLAND, false),
          "gui unexpectedly supports embedded Wayland");
#if defined(_WIN32)
    check(!gui->is_api_supported(plugin, CLAP_WINDOW_API_X11, false),
          "gui unexpectedly supports embedded X11 for non-Element host");
#endif
    const char *preferred_api = NULL;
    bool preferred_floating = true;
    check(gui->get_preferred_api(plugin, &preferred_api, &preferred_floating),
          "gui preferred api failed");
    check(preferred_api && strcmp(preferred_api, NILAMP_EXPECT_CLAP_WINDOW_API) == 0 &&
              !preferred_floating,
          "unexpected gui preferred api");
#if defined(_WIN32)
    clap_host_t element_host = host;
    element_host.name = "Element";
    element_host.vendor = "Kushview";
    const clap_plugin_t *element_plugin =
        factory->create_plugin(factory, &element_host, NILAMP_PLUGIN_ID);
    check(element_plugin != NULL, "create Element-host plugin failed");
    check(element_plugin->init(element_plugin), "Element-host plugin init failed");
    const clap_plugin_gui_t *element_gui =
        (const clap_plugin_gui_t *)element_plugin->get_extension(element_plugin, CLAP_EXT_GUI);
    check(element_gui != NULL, "missing Element-host gui extension");
    check(element_gui->is_api_supported(element_plugin, CLAP_WINDOW_API_X11, false),
          "Element-host x11 compatibility api rejected");
    check(element_gui->is_api_supported(element_plugin, CLAP_WINDOW_API_WIN32, false),
          "Element-host native win32 api rejected");
    element_plugin->destroy(element_plugin);
#endif
    check(timer && timer->on_timer, "missing timer support extension");
#else
    check(gui == NULL, "unexpected gui extension");
    check(timer == NULL, "unexpected timer support extension");
#endif

    check(plugin->activate(plugin, 48000.0, 1, 64), "activate failed");
    check(plugin->start_processing(plugin), "start processing failed");

    enum { Frames = 64 };
    float in_buf[Frames];
    float out_buf[Frames];
    for (uint32_t i = 0; i < Frames; i++) {
        in_buf[i] = (float)i / (float)Frames * 0.05f;
        out_buf[i] = 0.0f;
    }

    float *input_channels[1] = {in_buf};
    float *output_channels[1] = {out_buf};
    clap_audio_buffer_t input = {
        .data32 = input_channels,
        .data64 = NULL,
        .channel_count = 1,
        .latency = 0,
        .constant_mask = 0,
    };
    clap_audio_buffer_t output = {
        .data32 = output_channels,
        .data64 = NULL,
        .channel_count = 1,
        .latency = 0,
        .constant_mask = 0,
    };

    TestEvents empty_events = {.events = NULL, .count = 0};
    clap_input_events_t in_events = {
        .ctx = &empty_events,
        .size = events_size,
        .get = events_get,
    };
    clap_output_events_t out_events = {
        .ctx = NULL,
        .try_push = events_try_push,
    };
    clap_process_t process = {
        .steady_time = 0,
        .frames_count = Frames,
        .transport = NULL,
        .audio_inputs = &input,
        .audio_outputs = &output,
        .audio_inputs_count = 1,
        .audio_outputs_count = 1,
        .in_events = &in_events,
        .out_events = &out_events,
    };

    check(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
          "process returned failure");
    for (uint32_t i = 0; i < Frames; i++) {
        check(isfinite(out_buf[i]), "non-finite output");
    }

    plugin->reset(plugin);
    float mono_inplace[Frames];
    for (uint32_t i = 0; i < Frames; i++) {
        mono_inplace[i] = (float)i / (float)Frames * 0.05f;
    }
    float *mono_inplace_channels[1] = {mono_inplace};
    input.data32 = mono_inplace_channels;
    output.data32 = mono_inplace_channels;
    check(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
          "mono in-place process returned failure");
    for (uint32_t i = 0; i < Frames; i++) {
        check(isfinite(mono_inplace[i]), "non-finite mono in-place output");
    }

    run_clap_engine_compare(plugin, &process, &in_events, &out_events);
    input.data32 = input_channels;
    input.channel_count = 1;
    input.constant_mask = 0;
    output.data32 = output_channels;
    output.channel_count = 1;
    process.audio_inputs = &input;
    process.audio_outputs = &output;
    process.audio_inputs_count = 1;
    process.audio_outputs_count = 1;
    process.frames_count = Frames;
    run_clap_midi_test(plugin, params, &process, &out_events);
    run_all_param_automation_test(plugin, params, &process, &out_events);

    clap_event_param_value_t gain_event = {
        .header = {
            .size = sizeof(gain_event),
            .time = Frames / 2,
            .space_id = CLAP_CORE_EVENT_SPACE_ID,
            .type = CLAP_EVENT_PARAM_VALUE,
            .flags = 0,
        },
        .param_id = 0,
        .cookie = NULL,
        .note_id = -1,
        .port_index = -1,
        .channel = -1,
        .key = -1,
        .value = -6.0,
    };
    const clap_event_header_t *event_ptrs[1] = {&gain_event.header};
    TestEvents automation_events = {.events = event_ptrs, .count = 1};
    in_events.ctx = &automation_events;
    check(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
          "automation process returned failure");
    check(params->get_value(plugin, NILAMP_PARAM_GAIN_DB, &gain), "gain reread failed");
    check(fabs(gain + 6.0) < 0.000001, "negative automation gain was not applied");

    clap_event_param_value_t topology_events_raw[2];
    init_param_event(&topology_events_raw[0], NILAMP_PARAM_TUBE1, 0.0);
    init_param_event(&topology_events_raw[1], NILAMP_PARAM_PHASE_SPLITTER, 4.0);
    const clap_event_header_t *topology_event_ptrs[2] = {
        &topology_events_raw[0].header,
        &topology_events_raw[1].header,
    };
    TestEvents topology_events = {.events = topology_event_ptrs, .count = 2u};
    clap_input_events_t topology_input = {
        .ctx = &topology_events,
        .size = events_size,
        .get = events_get,
    };
    params->flush(plugin, &topology_input, &out_events);
    check(params->get_value(plugin, NILAMP_PARAM_TUBE1, &tube1), "tube1 reread failed");
    check(params->get_value(plugin, NILAMP_PARAM_PHASE_SPLITTER, &splitter),
          "splitter reread failed");
    check(fabs(tube1) < 0.000001, "tube1 automation was not applied");
    check(fabs(splitter - 4.0) < 0.000001, "splitter automation was not applied");

    MemoryStream memory = {0};
    clap_ostream_t ostream = {
        .ctx = &memory,
        .write = stream_write,
    };
    check(state->save(plugin, &ostream), "state save failed");

    gain_event.value = 12.0;
    check(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
          "second automation process returned failure");
    check(params->get_value(plugin, NILAMP_PARAM_GAIN_DB, &gain), "gain second reread failed");
    check(fabs(gain - 12.0) < 0.000001, "second automation gain was not applied");

    memory.offset = 0;
    clap_istream_t istream = {
        .ctx = &memory,
        .read = stream_read,
    };
    check(state->load(plugin, &istream), "state load failed");
    check(params->get_value(plugin, NILAMP_PARAM_GAIN_DB, &gain), "gain state reread failed");
    check(fabs(gain + 6.0) < 0.000001, "state did not restore negative gain");
    check(params->get_value(plugin, NILAMP_PARAM_TUBE1, &tube1), "tube1 state reread failed");
    check(params->get_value(plugin, NILAMP_PARAM_PHASE_SPLITTER, &splitter),
          "splitter state reread failed");
    check(fabs(tube1) < 0.000001, "state did not restore tube1");
    check(fabs(splitter - 4.0) < 0.000001, "state did not restore splitter");
    check(params->get_value(plugin, NILAMP_PARAM_BYPASS, &bypass), "bypass state reread failed");
    check(fabs(bypass - 1.0) < 0.000001, "state did not restore bypass");

    const uint32_t state_context_types[] = {
        CLAP_STATE_CONTEXT_FOR_PRESET,
        CLAP_STATE_CONTEXT_FOR_DUPLICATE,
        CLAP_STATE_CONTEXT_FOR_PROJECT,
    };
    for (uint32_t i = 0; i < sizeof(state_context_types) / sizeof(state_context_types[0]); i++) {
        MemoryStream context_memory = {0};
        clap_ostream_t context_ostream = {
            .ctx = &context_memory,
            .write = stream_write,
        };
        check(state_context->save(plugin, &context_ostream, state_context_types[i]),
              "state context save failed");
        gain_event.value = 12.0;
        check(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
              "state context mutation process returned failure");
        check(params->get_value(plugin, NILAMP_PARAM_GAIN_DB, &gain),
              "state context mutation gain read failed");
        check(fabs(gain - 12.0) < 0.000001,
              "state context mutation gain was not applied");
        context_memory.offset = 0;
        clap_istream_t context_istream = {
            .ctx = &context_memory,
            .read = stream_read,
        };
        check(state_context->load(plugin, &context_istream, state_context_types[i]),
              "state context load failed");
        check(params->get_value(plugin, NILAMP_PARAM_GAIN_DB, &gain),
              "state context gain reread failed");
        check(fabs(gain + 6.0) < 0.000001,
              "state context did not restore negative gain");
    }
    MemoryStream invalid_context_memory = {0};
    clap_ostream_t invalid_context_ostream = {
        .ctx = &invalid_context_memory,
        .write = stream_write,
    };
    check(!state_context->save(plugin, &invalid_context_ostream, 999u),
          "invalid state context save succeeded");
    memory.offset = 0;
    clap_istream_t invalid_context_istream = {
        .ctx = &memory,
        .read = stream_read,
    };
    check(!state_context->load(plugin, &invalid_context_istream, 999u),
          "invalid state context load succeeded");

    struct {
        uint32_t magic;
        uint32_t version;
        float values[6];
    } old_state = {
        .magic = 0x4e4c4150u,
        .version = 1u,
        .values = {3.0f, 60.0f, 40.0f, 50.0f, 70.0f, 25.0f},
    };
    MemoryStream old_memory = {0};
    memcpy(old_memory.data, &old_state, sizeof(old_state));
    old_memory.size = sizeof(old_state);
    clap_istream_t old_istream = {
        .ctx = &old_memory,
        .read = stream_read,
    };
    check(state->load(plugin, &old_istream), "old state load failed");
    check(params->get_value(plugin, NILAMP_PARAM_GAIN_DB, &gain), "old state gain reread failed");
    check(fabs(gain - 3.0) < 0.000001, "old state did not restore gain");
    check(params->get_value(plugin, NILAMP_PARAM_OUTPUT_GAIN_DB, &output_gain),
          "old state output gain reread failed");
    check(fabs(output_gain) < 0.000001, "old state output gain did not default");
    check(params->get_value(plugin, NILAMP_PARAM_TUBE1, &tube1), "old state tube1 reread failed");
    check(params->get_value(plugin, NILAMP_PARAM_PHASE_SPLITTER, &splitter),
          "old state splitter reread failed");
    check(fabs(tube1 - 1.0) < 0.000001, "old state tube1 did not backfill");
    check(fabs(splitter) < 0.000001, "old state splitter did not backfill");
    check(params->get_value(plugin, NILAMP_PARAM_BYPASS, &bypass), "old state bypass reread failed");
    check(fabs(bypass) < 0.000001, "old state bypass did not default");

    struct {
        uint32_t magic;
        uint32_t version;
        float values[17];
    } v2_state = {
        .magic = 0x4e4c4150u,
        .version = 2u,
        .values = {4.0f, 55.0f, 45.0f, 50.0f, 65.0f, 30.0f,
                   -2.0f, 56.0f, -6.0f, 1.0f, 2.0f, 38.0f,
                   6.0f, 3.0f, 3.0f, 62.0f, 3.0f},
    };
    MemoryStream v2_memory = {0};
    memcpy(v2_memory.data, &v2_state, sizeof(v2_state));
    v2_memory.size = sizeof(v2_state);
    clap_istream_t v2_istream = {
        .ctx = &v2_memory,
        .read = stream_read,
    };
    check(state->load(plugin, &v2_istream), "v2 state load failed");
    check(params->get_value(plugin, NILAMP_PARAM_TUBE1, &tube1), "v2 state tube1 reread failed");
    check(params->get_value(plugin, NILAMP_PARAM_PHASE_SPLITTER, &splitter),
          "v2 state splitter reread failed");
    check(fabs(tube1 - 1.0) < 0.000001, "v2 state tube1 did not backfill");
    check(fabs(splitter) < 0.000001, "v2 state splitter did not backfill");

    run_clap_bypass_test(plugin, params, &process, &out_events);
    run_clap_output_safety_test(plugin, params, &process, &out_events);

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    nilamp_module_close(handle);

    return 0;
}
