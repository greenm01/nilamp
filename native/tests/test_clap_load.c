// SPDX-License-Identifier: MIT
#include "nilamp_dsp.h"

#include <clap/clap.h>
#include <clap/ext/gui.h>
#include <clap/ext/timer-support.h>

#include <dlfcn.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#define NILAMP_HOST_OUTPUT_LIMIT 1.0f
#define NILAMP_STRESS_SAMPLE_RATE 48000.0f

typedef struct TestEvents {
    const clap_event_header_t **events;
    uint32_t count;
} TestEvents;

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

static void fill_input(float *left, float *right, uint32_t frames)
{
    for (uint32_t i = 0; i < frames; i++) {
        const float t = (float)i / (float)frames;
        left[i] = 0.06f * sinf(17.0f * t) + 0.015f * cosf(43.0f * t);
        right[i] = 0.04f * cosf(11.0f * t) - 0.02f * sinf(29.0f * t);
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

static void render_engine_pair(const float *in_l, const float *in_r, float *ref_l,
                               float *ref_r, uint32_t frames, bool mono_to_stereo)
{
    NilampEngine *engine_l = nilamp_engine_create(48000.0);
    NilampEngine *engine_r = nilamp_engine_create(48000.0);
    check(engine_l && engine_r, "direct engine create failed");
    nilamp_engine_process(engine_l, in_l, ref_l, frames);
    nilamp_engine_process(engine_r, mono_to_stereo ? in_l : in_r, ref_r, frames);
    nilamp_engine_destroy(engine_l);
    nilamp_engine_destroy(engine_r);
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
    float in_l[Frames];
    float in_r[Frames];
    float ref_l[Frames];
    float ref_r[Frames];
    float out_l[Frames];
    float out_r[Frames];
    fill_input(in_l, in_r, Frames);

    plugin->reset(plugin);
    memset(out_l, 0, sizeof(out_l));
    memset(out_r, 0, sizeof(out_r));
    render_engine_pair(in_l, in_r, ref_l, ref_r, Frames, false);
    float *stereo_inputs[2] = {in_l, in_r};
    float *stereo_outputs[2] = {out_l, out_r};
    clap_audio_buffer_t input = {
        .data32 = stereo_inputs,
        .data64 = NULL,
        .channel_count = 2,
        .latency = 0,
        .constant_mask = 0,
    };
    clap_audio_buffer_t output = {
        .data32 = stereo_outputs,
        .data64 = NULL,
        .channel_count = 2,
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
    compare_output(out_l, ref_l, Frames, "stereo left");
    compare_output(out_r, ref_r, Frames, "stereo right");

    plugin->reset(plugin);
    float inplace_l[Frames];
    float inplace_r[Frames];
    memcpy(inplace_l, in_l, sizeof(inplace_l));
    memcpy(inplace_r, in_r, sizeof(inplace_r));
    float *stereo_inplace[2] = {inplace_l, inplace_r};
    input.data32 = stereo_inplace;
    output.data32 = stereo_inplace;
    run_process_chunks(plugin, process, Frames);
    compare_output(inplace_l, ref_l, Frames, "stereo in-place left");
    compare_output(inplace_r, ref_r, Frames, "stereo in-place right");

    plugin->reset(plugin);
    float mono_inplace[Frames];
    float mono_r[Frames];
    memcpy(mono_inplace, in_l, sizeof(mono_inplace));
    memset(mono_r, 0, sizeof(mono_r));
    render_engine_pair(in_l, in_l, ref_l, ref_r, Frames, true);
    float *mono_inputs[1] = {mono_inplace};
    float *mono_outputs[2] = {mono_inplace, mono_r};
    input.data32 = mono_inputs;
    input.channel_count = 1;
    output.data32 = mono_outputs;
    output.channel_count = 2;
    run_process_chunks(plugin, process, Frames);
    compare_output(mono_inplace, ref_l, Frames, "mono in-place left");
    compare_output(mono_r, ref_r, Frames, "mono in-place right");

    // Mono content presented on a stereo port (REAPER mono-track default):
    // both input channels point at the same buffer. The plugin must run a
    // single engine and duplicate the output so L and R are bit-identical
    // instead of decorrelating through two independent nonlinear engines.
    plugin->reset(plugin);
    float mono_shared[Frames];
    float mono_stereo_out_l[Frames];
    float mono_stereo_out_r[Frames];
    float mono_stereo_ref[Frames];
    memcpy(mono_shared, in_l, sizeof(mono_shared));
    memset(mono_stereo_out_l, 0, sizeof(mono_stereo_out_l));
    memset(mono_stereo_out_r, 0, sizeof(mono_stereo_out_r));
    {
        NilampEngine *engine = nilamp_engine_create(48000.0);
        check(engine != NULL, "mono-on-stereo engine create failed");
        nilamp_engine_process(engine, mono_shared, mono_stereo_ref, Frames);
        nilamp_engine_destroy(engine);
    }
    float *mono_shared_inputs[2] = {mono_shared, mono_shared};
    float *mono_stereo_outputs[2] = {mono_stereo_out_l, mono_stereo_out_r};
    input.data32 = mono_shared_inputs;
    input.channel_count = 2;
    input.constant_mask = 0u;
    output.data32 = mono_stereo_outputs;
    output.channel_count = 2;
    run_process_chunks(plugin, process, Frames);
    compare_output(mono_stereo_out_l, mono_stereo_ref, Frames, "mono-on-stereo left");
    compare_output(mono_stereo_out_r, mono_stereo_ref, Frames, "mono-on-stereo right");
    for (uint32_t i = 0; i < Frames; i++) {
        if (mono_stereo_out_l[i] != mono_stereo_out_r[i]) {
            fprintf(stderr,
                    "test_clap_load: mono-on-stereo L/R diverge at %u: L=%g R=%g\n",
                    i, mono_stereo_out_l[i], mono_stereo_out_r[i]);
            exit(1);
        }
    }

    plugin->reset(plugin);
    float constant = 0.025f;
    float constant_in[1] = {constant};
    float constant_ref_input[Frames];
    for (uint32_t i = 0; i < Frames; i++) {
        constant_ref_input[i] = constant;
        out_l[i] = 0.0f;
        out_r[i] = 0.0f;
    }
    render_engine_pair(constant_ref_input, constant_ref_input, ref_l, ref_r, Frames, true);
    float *constant_inputs[1] = {constant_in};
    output.data32 = stereo_outputs;
    output.channel_count = 2;
    input.data32 = constant_inputs;
    input.channel_count = 1;
    input.constant_mask = 1u;
    process->frames_count = Frames;
    check(plugin->process(plugin, process) == CLAP_PROCESS_CONTINUE,
          "constant process returned failure");
    compare_output(out_l, ref_l, Frames, "constant left");
    compare_output(out_r, ref_r, Frames, "constant right");
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
        check(strcmp(info.name, spec->name) == 0, "param metadata name mismatch");
        check(strcmp(info.module, spec->module) == 0, "param metadata module mismatch");
        check(fabs(info.min_value - spec->min_value) < 0.000001,
              "param metadata minimum mismatch");
        check(fabs(info.max_value - spec->max_value) < 0.000001,
              "param metadata maximum mismatch");
        check(fabs(info.default_value - spec->default_value) < 0.000001,
              "param metadata default mismatch");

        if (spec->display == NILAMP_CONTROL_DISPLAY_ENUM) {
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
    static float in_l[Frames];
    static float in_r[Frames];
    static float out_l[Frames];
    static float out_r[Frames];

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
        in_l[i] = 0.15f * sinf(2.0f * 3.14159265358979323846f * 220.0f * t);
        in_r[i] = 0.10f * sinf(2.0f * 3.14159265358979323846f * 330.0f * t);
        out_l[i] = 0.0f;
        out_r[i] = 0.0f;
    }

    float *input_channels[2] = {in_l, in_r};
    float *output_channels[2] = {out_l, out_r};
    clap_audio_buffer_t input = {
        .data32 = input_channels,
        .data64 = NULL,
        .channel_count = 2,
        .latency = 0,
        .constant_mask = 0,
    };
    clap_audio_buffer_t output = {
        .data32 = output_channels,
        .data64 = NULL,
        .channel_count = 2,
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
        check(isfinite(out_l[i]) && isfinite(out_r[i]), "stress output is non-finite");
        peak = fmaxf(peak, fabsf(out_l[i]));
        peak = fmaxf(peak, fabsf(out_r[i]));
    }
    check(peak > 1.0e-8f, "stress output is silent");
    check(peak <= NILAMP_HOST_OUTPUT_LIMIT + 0.000001f,
          "stress output exceeds host safety limit");
}

int main(int argc, char **argv)
{
    const char *plugin_path = argc > 1 ? argv[1] : "native/bin/nilamp-twd-mkii.clap";
    void *handle = dlopen(plugin_path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "test_clap_load: dlopen failed: %s\n", dlerror());
        return 1;
    }

    const clap_plugin_entry_t *entry =
        (const clap_plugin_entry_t *)dlsym(handle, "clap_entry");
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
    check(port_info.channel_count == 2, "input port is not stereo");
    check(audio_ports->get(plugin, 0, false, &port_info), "output port info failed");
    check(port_info.channel_count == 2, "output port is not stereo");

    const clap_plugin_params_t *params =
        (const clap_plugin_params_t *)plugin->get_extension(plugin, CLAP_EXT_PARAMS);
    check(params != NULL, "missing params extension");
    check_param_metadata(plugin, params);
    clap_param_info_t param_info = {0};
    check(params->get_info(plugin, NILAMP_PARAM_GAIN_DB, &param_info),
          "gain info read failed");
    check(fabs(param_info.min_value + 12.0) < 0.000001,
          "unexpected minimum gain info");
    check(fabs(param_info.max_value - 12.0) < 0.000001,
          "unexpected maximum gain info");
    check(fabs(param_info.default_value) < 0.000001,
          "unexpected default gain info");
    check(params->get_info(plugin, NILAMP_PARAM_OUTPUT_GAIN_DB, &param_info),
          "output gain info read failed");
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
    const char *preferred_api = NULL;
    bool preferred_floating = true;
    check(gui->get_preferred_api(plugin, &preferred_api, &preferred_floating),
          "gui preferred api failed");
    check(preferred_api && strcmp(preferred_api, NILAMP_EXPECT_CLAP_WINDOW_API) == 0 &&
              !preferred_floating,
          "unexpected gui preferred api");
    check(timer && timer->on_timer, "missing timer support extension");
#else
    check(gui == NULL, "unexpected gui extension");
    check(timer == NULL, "unexpected timer support extension");
#endif

    check(plugin->activate(plugin, 48000.0, 1, 64), "activate failed");
    check(plugin->start_processing(plugin), "start processing failed");

    enum { Frames = 64 };
    float in_l[Frames];
    float in_r[Frames];
    float out_l[Frames];
    float out_r[Frames];
    for (uint32_t i = 0; i < Frames; i++) {
        in_l[i] = (float)i / (float)Frames * 0.05f;
        in_r[i] = -in_l[i];
        out_l[i] = 0.0f;
        out_r[i] = 0.0f;
    }

    float *input_channels[2] = {in_l, in_r};
    float *output_channels[2] = {out_l, out_r};
    clap_audio_buffer_t input = {
        .data32 = input_channels,
        .data64 = NULL,
        .channel_count = 2,
        .latency = 0,
        .constant_mask = 0,
    };
    clap_audio_buffer_t output = {
        .data32 = output_channels,
        .data64 = NULL,
        .channel_count = 2,
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
        check(isfinite(out_l[i]) && isfinite(out_r[i]), "non-finite output");
    }

    plugin->reset(plugin);
    float mono_inplace[Frames];
    float mono_out_r[Frames];
    for (uint32_t i = 0; i < Frames; i++) {
        mono_inplace[i] = (float)i / (float)Frames * 0.05f;
        mono_out_r[i] = 0.0f;
    }
    float *mono_input_channels[1] = {mono_inplace};
    float *mono_output_channels[2] = {mono_inplace, mono_out_r};
    input.data32 = mono_input_channels;
    input.channel_count = 1;
    output.data32 = mono_output_channels;
    output.channel_count = 2;
    check(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
          "mono in-place process returned failure");
    for (uint32_t i = 0; i < Frames; i++) {
        check(isfinite(mono_inplace[i]) && isfinite(mono_out_r[i]),
              "non-finite mono in-place output");
    }

    input.data32 = input_channels;
    input.channel_count = 2;
    output.data32 = output_channels;
    output.channel_count = 2;
    run_clap_engine_compare(plugin, &process, &in_events, &out_events);
    input.data32 = input_channels;
    input.channel_count = 2;
    input.constant_mask = 0;
    output.data32 = output_channels;
    output.channel_count = 2;
    process.audio_inputs = &input;
    process.audio_outputs = &output;
    process.audio_inputs_count = 1;
    process.audio_outputs_count = 1;
    process.frames_count = Frames;
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

    run_clap_output_safety_test(plugin, params, &process, &out_events);

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(handle);

    return 0;
}
