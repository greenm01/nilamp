// SPDX-License-Identifier: MIT
#include <clap/clap.h>
#include <clap/ext/gui.h>

#include <dlfcn.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NILAMP_PLUGIN_ID "dev.niltempus.nilamp"

typedef struct TestEvents {
    const clap_event_header_t **events;
    uint32_t count;
} TestEvents;

typedef struct MemoryStream {
    uint8_t data[128];
    uint64_t size;
    uint64_t offset;
} MemoryStream;

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
    (void)extension_id;
    return NULL;
}

static void host_request_restart(const clap_host_t *host)
{
    (void)host;
}

static void host_request_process(const clap_host_t *host)
{
    (void)host;
}

static void host_request_callback(const clap_host_t *host)
{
    (void)host;
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

int main(int argc, char **argv)
{
    const char *plugin_path = argc > 1 ? argv[1] : "native/bin/nilamp.clap";
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

    clap_host_t host = {
        .clap_version = CLAP_VERSION_INIT,
        .host_data = NULL,
        .name = "nilamp smoke host",
        .vendor = "niltempus",
        .url = "",
        .version = "0.1.0",
        .get_extension = host_get_extension,
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
    check(params->count(plugin) == 6, "unexpected parameter count");
    double gain = -1.0;
    check(params->get_value(plugin, 0, &gain), "gain read failed");
    check(fabs(gain) < 0.000001, "unexpected default gain");

    const clap_plugin_state_t *state =
        (const clap_plugin_state_t *)plugin->get_extension(plugin, CLAP_EXT_STATE);
    check(state != NULL, "missing state extension");

    const clap_plugin_gui_t *gui =
        (const clap_plugin_gui_t *)plugin->get_extension(plugin, CLAP_EXT_GUI);
    check(gui != NULL, "missing gui extension");
    check(gui->is_api_supported(plugin, CLAP_WINDOW_API_X11, false),
          "gui does not support embedded X11");
    check(!gui->is_api_supported(plugin, CLAP_WINDOW_API_X11, true),
          "gui unexpectedly supports floating X11");
    check(!gui->is_api_supported(plugin, CLAP_WINDOW_API_WAYLAND, false),
          "gui unexpectedly supports embedded Wayland");
    const char *preferred_api = NULL;
    bool preferred_floating = true;
    check(gui->get_preferred_api(plugin, &preferred_api, &preferred_floating),
          "gui preferred api failed");
    check(preferred_api && strcmp(preferred_api, CLAP_WINDOW_API_X11) == 0 && !preferred_floating,
          "unexpected gui preferred api");

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
        .value = 6.0,
    };
    const clap_event_header_t *event_ptrs[1] = {&gain_event.header};
    TestEvents automation_events = {.events = event_ptrs, .count = 1};
    in_events.ctx = &automation_events;
    check(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
          "automation process returned failure");
    check(params->get_value(plugin, 0, &gain), "gain reread failed");
    check(fabs(gain - 6.0) < 0.000001, "automation gain was not applied");

    MemoryStream memory = {0};
    clap_ostream_t ostream = {
        .ctx = &memory,
        .write = stream_write,
    };
    check(state->save(plugin, &ostream), "state save failed");

    gain_event.value = 12.0;
    check(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
          "second automation process returned failure");
    check(params->get_value(plugin, 0, &gain), "gain second reread failed");
    check(fabs(gain - 12.0) < 0.000001, "second automation gain was not applied");

    memory.offset = 0;
    clap_istream_t istream = {
        .ctx = &memory,
        .read = stream_read,
    };
    check(state->load(plugin, &istream), "state load failed");
    check(params->get_value(plugin, 0, &gain), "gain state reread failed");
    check(fabs(gain - 6.0) < 0.000001, "state did not restore gain");

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(handle);

    return 0;
}
