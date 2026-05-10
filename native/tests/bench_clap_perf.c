// SPDX-License-Identifier: MIT
#include "bench_perf_common.h"

#include <clap/clap.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/stat.h>
#endif

#include <stdbool.h>

#define NILAMP_PLUGIN_ID "dev.niltempus.nilamp"

typedef struct {
    NilampBenchArgs common;
    const char *plugin_path;
} Args;

#if defined(_WIN32)
typedef HMODULE NilampModule;

static NilampModule nilamp_module_open(const char *path) { return LoadLibraryA(path); }
static void *nilamp_module_symbol(NilampModule module, const char *name)
{
    return module ? (void *)GetProcAddress(module, name) : NULL;
}
static void nilamp_module_close(NilampModule module)
{
    if (module) FreeLibrary(module);
}
static void nilamp_module_print_error(const char *path)
{
    fprintf(stderr, "error: load %s: Windows error %lu\n", path, (unsigned long)GetLastError());
}
#else
typedef void *NilampModule;

static NilampModule nilamp_module_open(const char *path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
static void *nilamp_module_symbol(NilampModule module, const char *name)
{
    return module ? dlsym(module, name) : NULL;
}
static void nilamp_module_close(NilampModule module)
{
    if (module) dlclose(module);
}
static void nilamp_module_print_error(const char *path)
{
    fprintf(stderr, "error: load %s: %s\n", path, dlerror());
}
#endif

typedef struct {
    const clap_event_header_t **events;
    uint32_t count;
} EventList;

typedef struct {
    char library_path[4096];
    const char *load_path;
    NilampModule module;
    const clap_plugin_entry_t *entry;
    const clap_plugin_factory_t *factory;
    const clap_plugin_t *plugin;
} ClapBench;

static uint32_t evt_size(const clap_input_events_t *list)
{
    return ((const EventList *)list->ctx)->count;
}

static const clap_event_header_t *evt_get(const clap_input_events_t *list, uint32_t i)
{
    const EventList *events = (const EventList *)list->ctx;
    return i < events->count ? events->events[i] : NULL;
}

static bool evt_try_push(const clap_output_events_t *list, const clap_event_header_t *event)
{
    (void)list;
    (void)event;
    return true;
}

static const void *host_get_extension(const clap_host_t *host, const char *id)
{
    (void)host;
    (void)id;
    return NULL;
}

static void host_request_restart(const clap_host_t *host) { (void)host; }
static void host_request_process(const clap_host_t *host) { (void)host; }
static void host_request_callback(const clap_host_t *host) { (void)host; }

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

static void usage(FILE *f)
{
    fprintf(f,
            "bench_clap_perf --plugin PLUGIN.clap [options]\n"
            "\n"
            "Options match bench_ysfx_perf common benchmark options.\n");
}

static int parse_args(int argc, char **argv, Args *args)
{
    memset(args, 0, sizeof(*args));
    nilamp_bench_args_init(&args->common);

    for (int i = 1; i < argc; i++) {
        const char *name = argv[i];
        if (strcmp(name, "-h") == 0 || strcmp(name, "--help") == 0) {
            usage(stdout);
            exit(0);
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "error: missing value for %s\n", name);
            return -1;
        }
        const char *value = argv[++i];
        if (strcmp(name, "--plugin") == 0) {
            args->plugin_path = value;
            continue;
        }
        const int parsed = nilamp_bench_parse_common_arg(&args->common, name, value);
        if (parsed == 0) {
            continue;
        }
        if (parsed < 0) {
            return -1;
        }
        fprintf(stderr, "error: unknown argument %s\n", name);
        return -1;
    }
    if (!args->plugin_path) {
        usage(stderr);
        return -1;
    }
    if (args->common.duration_s <= 0.0) {
        fprintf(stderr, "error: --duration must be positive\n");
        return -1;
    }
    return 0;
}

static void parse_param_event(clap_event_param_value_t *event, clap_id id, double value)
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

static int flush_params(const ClapBench *bench, const NilampParams *params)
{
    const clap_plugin_params_t *param_ext =
        (const clap_plugin_params_t *)bench->plugin->get_extension(bench->plugin,
                                                                   CLAP_EXT_PARAMS);
    if (!param_ext || !param_ext->flush) {
        return 0;
    }
    clap_event_param_value_t events[NILAMP_PARAM_COUNT];
    const clap_event_header_t *ptrs[NILAMP_PARAM_COUNT];
    const NilampControlSpec *specs = nilamp_control_specs(NULL);
    for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
        parse_param_event(&events[i], specs[i].id,
                          nilamp_bench_param_value(params, (NilampParamId)specs[i].id));
        ptrs[i] = &events[i].header;
    }
    EventList list = { .events = ptrs, .count = NILAMP_PARAM_COUNT };
    clap_input_events_t in_events = { .ctx = &list, .size = evt_size, .get = evt_get };
    clap_output_events_t out_events = { .ctx = NULL, .try_push = evt_try_push };
    param_ext->flush(bench->plugin, &in_events, &out_events);
    return 0;
}

static void destroy_clap(ClapBench *bench)
{
    if (!bench) {
        return;
    }
    if (bench->plugin) {
        bench->plugin->stop_processing(bench->plugin);
        bench->plugin->deactivate(bench->plugin);
        bench->plugin->destroy(bench->plugin);
    }
    if (bench->entry) {
        bench->entry->deinit();
    }
    nilamp_module_close(bench->module);
    memset(bench, 0, sizeof(*bench));
}

static int load_clap(const Args *args, ClapBench *bench)
{
    memset(bench, 0, sizeof(*bench));
    bench->load_path = clap_library_path(args->plugin_path, bench->library_path,
                                         sizeof(bench->library_path));
    bench->module = nilamp_module_open(bench->load_path);
    if (!bench->module) {
        nilamp_module_print_error(bench->load_path);
        return -1;
    }
    bench->entry =
        (const clap_plugin_entry_t *)nilamp_module_symbol(bench->module, "clap_entry");
    if (!bench->entry || !bench->entry->init || !bench->entry->init(args->plugin_path)) {
        fprintf(stderr, "error: clap_entry init failed\n");
        destroy_clap(bench);
        return -1;
    }
    bench->factory =
        (const clap_plugin_factory_t *)bench->entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    if (!bench->factory) {
        fprintf(stderr, "error: CLAP factory unavailable\n");
        destroy_clap(bench);
        return -1;
    }

    clap_host_t host = {
        .clap_version = CLAP_VERSION_INIT,
        .host_data = NULL,
        .name = "bench_clap_perf",
        .vendor = "niltempus",
        .url = "",
        .version = "0.1.0",
        .get_extension = host_get_extension,
        .request_restart = host_request_restart,
        .request_process = host_request_process,
        .request_callback = host_request_callback,
    };
    bench->plugin = bench->factory->create_plugin(bench->factory, &host, NILAMP_PLUGIN_ID);
    if (!bench->plugin || !bench->plugin->init(bench->plugin)) {
        fprintf(stderr, "error: create/init CLAP plugin failed\n");
        destroy_clap(bench);
        return -1;
    }
    if (!bench->plugin->activate(bench->plugin, (double)args->common.sample_rate, 1,
                                 args->common.block)) {
        fprintf(stderr, "error: CLAP activate failed\n");
        destroy_clap(bench);
        return -1;
    }
    if (!bench->plugin->start_processing(bench->plugin)) {
        fprintf(stderr, "error: CLAP start_processing failed\n");
        destroy_clap(bench);
        return -1;
    }
    (void)flush_params(bench, &args->common.params);
    if (bench->plugin->reset) {
        bench->plugin->reset(bench->plugin);
    }
    return 0;
}

static void process_clap(const ClapBench *bench, const Args *args, const float *input,
                         float *output, uint32_t frames)
{
    EventList empty = { .events = NULL, .count = 0 };
    clap_input_events_t in_events = { .ctx = &empty, .size = evt_size, .get = evt_get };
    clap_output_events_t out_events = { .ctx = NULL, .try_push = evt_try_push };
    memset(output, 0, sizeof(float) * frames);
    size_t pos = 0;
    while (pos < frames) {
        const uint32_t n = args->common.block < frames - pos ?
                               args->common.block :
                               (uint32_t)(frames - pos);
        float *in_channels[1] = { (float *)(input + pos) };
        float *out_channels[1] = { output + pos };
        clap_audio_buffer_t in = {
            .data32 = in_channels,
            .data64 = NULL,
            .channel_count = 1,
            .latency = 0,
            .constant_mask = args->common.input_kind == NILAMP_BENCH_INPUT_SILENCE ? 1u : 0u,
        };
        clap_audio_buffer_t out = {
            .data32 = out_channels,
            .data64 = NULL,
            .channel_count = 1,
            .latency = 0,
            .constant_mask = 0,
        };
        clap_process_t process = {
            .steady_time = (int64_t)pos,
            .frames_count = n,
            .transport = NULL,
            .audio_inputs = &in,
            .audio_outputs = &out,
            .audio_inputs_count = 1,
            .audio_outputs_count = 1,
            .in_events = &in_events,
            .out_events = &out_events,
        };
        if (bench->plugin->process(bench->plugin, &process) == CLAP_PROCESS_ERROR) {
            fprintf(stderr, "error: CLAP process failed at frame %zu\n", pos);
            return;
        }
        pos += n;
    }
}

static int run_lifecycle(const Args *args)
{
    for (uint32_t i = 0; i < args->common.warmups + args->common.runs; i++) {
        NilampBenchUsage start = nilamp_bench_usage_now();
        ClapBench bench;
        const int rc = load_clap(args, &bench);
        destroy_clap(&bench);
        NilampBenchUsage usage = nilamp_bench_usage_diff(start, nilamp_bench_usage_now());
        if (rc != 0) {
            return 1;
        }
        if (i >= args->common.warmups) {
            nilamp_bench_print_result("nilamp_clap", &args->common,
                                      i - args->common.warmups, 0u, usage, NULL);
        }
    }
    return 0;
}

static int run_steady(const Args *args)
{
    const uint32_t frames = (uint32_t)llround(args->common.duration_s *
                                             (double)args->common.sample_rate);
    float *input = (float *)calloc(frames, sizeof(float));
    float *output = (float *)calloc(frames, sizeof(float));
    if (!input || !output) {
        fprintf(stderr, "error: allocation failed\n");
        free(input);
        free(output);
        return 1;
    }
    nilamp_bench_fill_input(input, frames, args->common.sample_rate,
                            args->common.input_kind, args->common.input_scale);

    ClapBench bench;
    if (load_clap(args, &bench) != 0) {
        free(input);
        free(output);
        return 1;
    }
    for (uint32_t i = 0; i < args->common.warmups + args->common.runs; i++) {
        if (bench.plugin->reset) {
            bench.plugin->reset(bench.plugin);
        }
        NilampBenchUsage start = nilamp_bench_usage_now();
        process_clap(&bench, args, input, output, frames);
        NilampBenchUsage usage = nilamp_bench_usage_diff(start, nilamp_bench_usage_now());
        if (i >= args->common.warmups) {
            nilamp_bench_print_result("nilamp_clap", &args->common,
                                      i - args->common.warmups, frames, usage, output);
        }
    }
    const int rc = nilamp_bench_write_raw(args->common.output_raw_path, output, frames);
    destroy_clap(&bench);
    free(input);
    free(output);
    return rc == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    Args args;
    if (parse_args(argc, argv, &args) != 0) {
        return 2;
    }
    if (args.common.phase == NILAMP_BENCH_PHASE_STEADY) {
        return run_steady(&args);
    }
    return run_lifecycle(&args);
}
