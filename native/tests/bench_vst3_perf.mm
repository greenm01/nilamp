// SPDX-License-Identifier: MIT
#include "bench_perf_common.h"

#include "nilamp_host.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define NILAMP_VST3_PROCESSOR_UID INLINE_UID(0xb0494b81, 0xb2385c29, 0x8c4d5734, 0x6d0b1222)
#ifndef NILAMP_TEST_VST3_LINUX_BINARY
#define NILAMP_TEST_VST3_LINUX_BINARY "x86_64-linux/nilamp-twd-mkii.so"
#endif
#ifndef NILAMP_TEST_VST3_WINDOWS_BINARY
#define NILAMP_TEST_VST3_WINDOWS_BINARY "x86_64-win\\nilamp-twd-mkii.vst3"
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

typedef struct {
    NilampBenchArgs common;
    const char *plugin_path;
} Args;

static bool iidEqual(const TUID a, const TUID b)
{
    return std::memcmp(a, b, sizeof(TUID)) == 0;
}

class MemoryStream final : public IBStream {
public:
    void setBytes(const void *data, size_t size)
    {
        bytes.assign(static_cast<const uint8_t *>(data),
                     static_cast<const uint8_t *>(data) + size);
        offset = 0;
    }

    tresult PLUGIN_API queryInterface(const TUID queryIid, void **obj) SMTG_OVERRIDE
    {
        if (!obj) return kInvalidArgument;
        if (iidEqual(queryIid, IBStream::iid) || iidEqual(queryIid, FUnknown::iid)) {
            *obj = static_cast<IBStream *>(this);
            addRef();
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef() SMTG_OVERRIDE { return ++refs; }
    uint32 PLUGIN_API release() SMTG_OVERRIDE { return --refs; }

    tresult PLUGIN_API read(void *buffer, int32 numBytes, int32 *numBytesRead) SMTG_OVERRIDE
    {
        if (!buffer || numBytes < 0) return kInvalidArgument;
        const size_t remaining = bytes.size() - offset;
        const size_t requested = static_cast<size_t>(numBytes);
        const size_t count = requested < remaining ? requested : remaining;
        if (count > 0) {
            std::memcpy(buffer, bytes.data() + offset, count);
            offset += count;
        }
        if (numBytesRead) *numBytesRead = static_cast<int32>(count);
        return kResultOk;
    }

    tresult PLUGIN_API write(void *buffer, int32 numBytes, int32 *numBytesWritten) SMTG_OVERRIDE
    {
        if (!buffer || numBytes < 0) return kInvalidArgument;
        const uint8_t *src = static_cast<const uint8_t *>(buffer);
        if (offset + static_cast<size_t>(numBytes) > bytes.size()) {
            bytes.resize(offset + static_cast<size_t>(numBytes));
        }
        std::memcpy(bytes.data() + offset, src, static_cast<size_t>(numBytes));
        offset += static_cast<size_t>(numBytes);
        if (numBytesWritten) *numBytesWritten = numBytes;
        return kResultOk;
    }

    tresult PLUGIN_API seek(int64 pos, int32 mode, int64 *result) SMTG_OVERRIDE
    {
        int64 next = 0;
        if (mode == kIBSeekSet) {
            next = pos;
        } else if (mode == kIBSeekCur) {
            next = static_cast<int64>(offset) + pos;
        } else if (mode == kIBSeekEnd) {
            next = static_cast<int64>(bytes.size()) + pos;
        } else {
            return kInvalidArgument;
        }
        if (next < 0 || static_cast<uint64_t>(next) > bytes.size()) {
            return kResultFalse;
        }
        offset = static_cast<size_t>(next);
        if (result) *result = static_cast<int64>(offset);
        return kResultOk;
    }

    tresult PLUGIN_API tell(int64 *pos) SMTG_OVERRIDE
    {
        if (!pos) return kInvalidArgument;
        *pos = static_cast<int64>(offset);
        return kResultOk;
    }

private:
    std::vector<uint8_t> bytes;
    size_t offset = 0;
    uint32 refs = 1;
};

typedef struct {
#if defined(__APPLE__)
    CFBundleRef bundle;
    bool (*module_entry)(CFBundleRef);
    bool (*module_exit)(void);
#elif defined(_WIN32)
    HMODULE module;
    bool (*module_entry)(void);
    bool (*module_exit)(void);
#else
    void *module;
    bool (*module_entry)(void *);
    bool (*module_exit)(void);
#endif
    IPluginFactory *factory;
    IComponent *component;
    IAudioProcessor *processor;
} Vst3Bench;

static void usage(FILE *f)
{
    std::fprintf(f,
                 "bench_vst3_perf --plugin PLUGIN.vst3 [options]\n"
                 "\n"
                 "Options match bench_ysfx_perf common benchmark options.\n");
}

static int parse_args(int argc, char **argv, Args *args)
{
    std::memset(args, 0, sizeof(*args));
    nilamp_bench_args_init(&args->common);

    for (int i = 1; i < argc; i++) {
        const char *name = argv[i];
        if (std::strcmp(name, "-h") == 0 || std::strcmp(name, "--help") == 0) {
            usage(stdout);
            std::exit(0);
        }
        if (i + 1 >= argc) {
            std::fprintf(stderr, "error: missing value for %s\n", name);
            return -1;
        }
        const char *value = argv[++i];
        if (std::strcmp(name, "--plugin") == 0) {
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
        std::fprintf(stderr, "error: unknown argument %s\n", name);
        return -1;
    }
    if (!args->plugin_path) {
        usage(stderr);
        return -1;
    }
    if (args->common.duration_s <= 0.0) {
        std::fprintf(stderr, "error: --duration must be positive\n");
        return -1;
    }
    return 0;
}

static void fill_state_stream(MemoryStream &stream, const NilampParams *params)
{
    struct StateBlob {
        uint32_t magic;
        uint32_t version;
        float values[NILAMP_PARAM_COUNT];
    } blob = {};
    blob.magic = NILAMP_HOST_STATE_MAGIC;
    blob.version = NILAMP_HOST_STATE_VERSION;
    const NilampControlSpec *specs = nilamp_control_specs(nullptr);
    for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
        blob.values[i] =
            static_cast<float>(nilamp_bench_param_value(params, static_cast<NilampParamId>(specs[i].id)));
    }
    stream.setBytes(&blob, sizeof(blob));
}

static void destroy_vst3(Vst3Bench *bench)
{
    if (!bench) return;
    if (bench->processor) {
        bench->processor->setProcessing(false);
    }
    if (bench->component) {
        bench->component->setActive(false);
        bench->component->terminate();
    }
    if (bench->processor) {
        bench->processor->release();
    }
    if (bench->component) {
        bench->component->release();
    }
    if (bench->factory) {
        bench->factory->release();
    }
#if defined(__APPLE__)
    if (bench->module_exit) bench->module_exit();
    if (bench->bundle) CFRelease(bench->bundle);
#elif defined(_WIN32)
    if (bench->module_exit) bench->module_exit();
    if (bench->module) FreeLibrary(bench->module);
#else
    if (bench->module_exit) bench->module_exit();
    if (bench->module) dlclose(bench->module);
#endif
    std::memset(bench, 0, sizeof(*bench));
}

static int load_vst3(const Args *args, Vst3Bench *bench)
{
    std::memset(bench, 0, sizeof(*bench));
    using GetFactoryFn = IPluginFactory *(*)();
    GetFactoryFn getFactory = nullptr;

#if defined(__APPLE__)
    CFStringRef path =
        CFStringCreateWithCString(kCFAllocatorDefault, args->plugin_path, kCFStringEncodingUTF8);
    if (!path) {
        std::fprintf(stderr, "error: failed to create VST3 path string\n");
        return -1;
    }
    CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, path,
                                                 kCFURLPOSIXPathStyle, true);
    CFRelease(path);
    if (!url) {
        std::fprintf(stderr, "error: failed to create VST3 bundle URL\n");
        return -1;
    }
    bench->bundle = CFBundleCreate(kCFAllocatorDefault, url);
    CFRelease(url);
    if (!bench->bundle || !CFBundleLoadExecutable(bench->bundle)) {
        std::fprintf(stderr, "error: failed to load VST3 bundle executable\n");
        destroy_vst3(bench);
        return -1;
    }
    bench->module_entry = reinterpret_cast<bool (*)(CFBundleRef)>(
        CFBundleGetFunctionPointerForName(bench->bundle, CFSTR("bundleEntry")));
    bench->module_exit = reinterpret_cast<bool (*)(void)>(
        CFBundleGetFunctionPointerForName(bench->bundle, CFSTR("bundleExit")));
    getFactory = reinterpret_cast<GetFactoryFn>(
        CFBundleGetFunctionPointerForName(bench->bundle, CFSTR("GetPluginFactory")));
    if (!bench->module_entry || !bench->module_exit || !getFactory ||
        !bench->module_entry(bench->bundle)) {
        std::fprintf(stderr, "error: failed to initialize VST3 bundle\n");
        destroy_vst3(bench);
        return -1;
    }
#elif defined(_WIN32)
    char libraryPath[4096] = {};
    const int written = std::snprintf(libraryPath, sizeof(libraryPath), "%s\\Contents\\%s",
                                      args->plugin_path, NILAMP_TEST_VST3_WINDOWS_BINARY);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(libraryPath)) {
        std::fprintf(stderr, "error: VST3 library path is too long\n");
        return -1;
    }
    bench->module = LoadLibraryA(libraryPath);
    if (!bench->module) {
        std::fprintf(stderr, "error: failed to load VST3 module: %lu\n",
                     static_cast<unsigned long>(GetLastError()));
        return -1;
    }
    bench->module_entry = reinterpret_cast<bool (*)(void)>(GetProcAddress(bench->module, "InitDll"));
    bench->module_exit = reinterpret_cast<bool (*)(void)>(GetProcAddress(bench->module, "ExitDll"));
    getFactory = reinterpret_cast<GetFactoryFn>(GetProcAddress(bench->module, "GetPluginFactory"));
    if (!bench->module_entry || !bench->module_exit || !getFactory || !bench->module_entry()) {
        std::fprintf(stderr, "error: failed to initialize VST3 module\n");
        destroy_vst3(bench);
        return -1;
    }
#else
    char libraryPath[4096] = {};
    const int written = std::snprintf(libraryPath, sizeof(libraryPath), "%s/Contents/%s",
                                      args->plugin_path, NILAMP_TEST_VST3_LINUX_BINARY);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(libraryPath)) {
        std::fprintf(stderr, "error: VST3 library path is too long\n");
        return -1;
    }
    bench->module = dlopen(libraryPath, RTLD_NOW | RTLD_LOCAL);
    if (!bench->module) {
        std::fprintf(stderr, "error: failed to load VST3 module: %s\n", dlerror());
        return -1;
    }
    bench->module_entry = reinterpret_cast<bool (*)(void *)>(dlsym(bench->module, "ModuleEntry"));
    bench->module_exit = reinterpret_cast<bool (*)(void)>(dlsym(bench->module, "ModuleExit"));
    getFactory = reinterpret_cast<GetFactoryFn>(dlsym(bench->module, "GetPluginFactory"));
    if (!bench->module_entry || !bench->module_exit || !getFactory ||
        !bench->module_entry(bench->module)) {
        std::fprintf(stderr, "error: failed to initialize VST3 module\n");
        destroy_vst3(bench);
        return -1;
    }
#endif

    bench->factory = getFactory();
    if (!bench->factory) {
        std::fprintf(stderr, "error: VST3 factory unavailable\n");
        destroy_vst3(bench);
        return -1;
    }

    TUID processorUid = NILAMP_VST3_PROCESSOR_UID;
    if (bench->factory->createInstance(processorUid, IComponent::iid,
                                       reinterpret_cast<void **>(&bench->component)) != kResultOk ||
        !bench->component ||
        bench->component->queryInterface(IAudioProcessor::iid,
                                        reinterpret_cast<void **>(&bench->processor)) != kResultOk ||
        !bench->processor) {
        std::fprintf(stderr, "error: failed to create VST3 processor\n");
        destroy_vst3(bench);
        return -1;
    }
    if (bench->component->initialize(nullptr) != kResultOk) {
        std::fprintf(stderr, "error: VST3 component initialize failed\n");
        destroy_vst3(bench);
        return -1;
    }

    MemoryStream state;
    fill_state_stream(state, &args->common.params);
    if (bench->component->setState(&state) != kResultOk) {
        std::fprintf(stderr, "error: VST3 setState failed\n");
        destroy_vst3(bench);
        return -1;
    }

    SpeakerArrangement inputArrangement = SpeakerArr::kMono;
    SpeakerArrangement outputArrangement = SpeakerArr::kMono;
    if (bench->processor->setBusArrangements(&inputArrangement, 1, &outputArrangement, 1) !=
        kResultTrue) {
        std::fprintf(stderr, "error: VST3 mono bus arrangement failed\n");
        destroy_vst3(bench);
        return -1;
    }
    ProcessSetup setup = {};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = static_cast<int32>(args->common.block);
    setup.sampleRate = static_cast<double>(args->common.sample_rate);
    if (bench->processor->setupProcessing(setup) != kResultOk ||
        bench->component->setActive(true) != kResultOk ||
        bench->processor->setProcessing(true) != kResultOk) {
        std::fprintf(stderr, "error: VST3 activation failed\n");
        destroy_vst3(bench);
        return -1;
    }
    return 0;
}

static int reset_vst3(const Args *args, Vst3Bench *bench)
{
    if (bench->processor->setProcessing(false) != kResultOk ||
        bench->component->setActive(false) != kResultOk) {
        return -1;
    }
    MemoryStream state;
    fill_state_stream(state, &args->common.params);
    if (bench->component->setState(&state) != kResultOk ||
        bench->component->setActive(true) != kResultOk ||
        bench->processor->setProcessing(true) != kResultOk) {
        return -1;
    }
    return 0;
}

static int process_vst3(const Vst3Bench *bench, const Args *args, const float *input,
                        float *output, uint32_t frames)
{
    std::memset(output, 0, sizeof(float) * frames);
    size_t pos = 0;
    while (pos < frames) {
        const uint32_t n = args->common.block < frames - pos ?
                               args->common.block :
                               static_cast<uint32_t>(frames - pos);
        float *inputChannels[1] = { const_cast<float *>(input + pos) };
        float *outputChannels[1] = { output + pos };
        AudioBusBuffers in = {};
        in.numChannels = 1;
        in.silenceFlags = args->common.input_kind == NILAMP_BENCH_INPUT_SILENCE ? 1u : 0u;
        in.channelBuffers32 = inputChannels;
        AudioBusBuffers out = {};
        out.numChannels = 1;
        out.channelBuffers32 = outputChannels;
        ProcessData data = {};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = static_cast<int32>(n);
        data.numInputs = 1;
        data.numOutputs = 1;
        data.inputs = &in;
        data.outputs = &out;
        if (bench->processor->process(data) != kResultOk) {
            std::fprintf(stderr, "error: VST3 process failed at frame %zu\n", pos);
            return -1;
        }
        pos += n;
    }
    return 0;
}

static int run_lifecycle(const Args *args)
{
    for (uint32_t i = 0; i < args->common.warmups + args->common.runs; i++) {
        NilampBenchUsage start = nilamp_bench_usage_now();
        Vst3Bench bench;
        const int rc = load_vst3(args, &bench);
        destroy_vst3(&bench);
        NilampBenchUsage usage = nilamp_bench_usage_diff(start, nilamp_bench_usage_now());
        if (rc != 0) {
            return 1;
        }
        if (i >= args->common.warmups) {
            nilamp_bench_print_result("nilamp_vst3", &args->common,
                                      i - args->common.warmups, 0u, usage, nullptr);
        }
    }
    return 0;
}

static int run_steady(const Args *args)
{
    const uint32_t frames = static_cast<uint32_t>(llround(args->common.duration_s *
                                                         static_cast<double>(args->common.sample_rate)));
    float *input = static_cast<float *>(std::calloc(frames, sizeof(float)));
    float *output = static_cast<float *>(std::calloc(frames, sizeof(float)));
    if (!input || !output) {
        std::fprintf(stderr, "error: allocation failed\n");
        std::free(input);
        std::free(output);
        return 1;
    }
    nilamp_bench_fill_input(input, frames, args->common.sample_rate,
                            args->common.input_kind, args->common.input_scale);

    Vst3Bench bench;
    if (load_vst3(args, &bench) != 0) {
        std::free(input);
        std::free(output);
        return 1;
    }
    int rc = 0;
    for (uint32_t i = 0; i < args->common.warmups + args->common.runs; i++) {
        if (reset_vst3(args, &bench) != 0) {
            std::fprintf(stderr, "error: VST3 reset failed\n");
            rc = 1;
            break;
        }
        NilampBenchUsage start = nilamp_bench_usage_now();
        if (process_vst3(&bench, args, input, output, frames) != 0) {
            rc = 1;
            break;
        }
        NilampBenchUsage usage = nilamp_bench_usage_diff(start, nilamp_bench_usage_now());
        if (i >= args->common.warmups) {
            nilamp_bench_print_result("nilamp_vst3", &args->common,
                                      i - args->common.warmups, frames, usage, output);
        }
    }
    if (rc == 0 && nilamp_bench_write_raw(args->common.output_raw_path, output, frames) != 0) {
        rc = 1;
    }
    destroy_vst3(&bench);
    std::free(input);
    std::free(output);
    return rc;
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
