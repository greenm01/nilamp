// SPDX-License-Identifier: MIT
#include "nilamp_dsp.h"
#include "nilamp_host.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define NILAMP_VST3_PROCESSOR_UID INLINE_UID(0xb0494b81, 0xb2385c29, 0x8c4d5734, 0x6d0b1222)
#define NILAMP_VST3_CONTROLLER_UID INLINE_UID(0x66e72a3a, 0x9187500d, 0xafa4d86a, 0x88935c65)
#ifndef NILAMP_TEST_VST3_LINUX_BINARY
#define NILAMP_TEST_VST3_LINUX_BINARY "x86_64-linux/nilamp-twd-mkii.so"
#endif
#ifndef NILAMP_TEST_VST3_WINDOWS_BINARY
#define NILAMP_TEST_VST3_WINDOWS_BINARY "x86_64-win\\nilamp-twd-mkii.vst3"
#endif
#ifndef NILAMP_RELEASE_VERSION
#define NILAMP_RELEASE_VERSION "1.0.2"
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

static void fail(const char *message)
{
    std::fprintf(stderr, "test_vst3_load: %s\n", message);
    std::exit(1);
}

static void check(bool condition, const char *message)
{
    if (!condition) {
        fail(message);
    }
}

static bool iidEqual(const TUID a, const TUID b)
{
    return std::memcmp(a, b, sizeof(TUID)) == 0;
}

static void checkDefaultMonoBusses(IComponent *component, IAudioProcessor *processor)
{
    check(component->getBusCount(kAudio, kInput) == 1, "unexpected VST3 input bus count");
    check(component->getBusCount(kAudio, kOutput) == 1, "unexpected VST3 output bus count");

    BusInfo bus = {};
    check(component->getBusInfo(kAudio, kInput, 0, bus) == kResultOk,
          "input bus info failed");
    check(bus.channelCount == 1, "default input bus is not mono");
    check(bus.busType == kMain, "default input bus is not main");
    check((bus.flags & BusInfo::kDefaultActive) != 0, "default input bus is not active");

    bus = {};
    check(component->getBusInfo(kAudio, kOutput, 0, bus) == kResultOk,
          "output bus info failed");
    check(bus.channelCount == 1, "default output bus is not mono");
    check(bus.busType == kMain, "default output bus is not main");
    check((bus.flags & BusInfo::kDefaultActive) != 0, "default output bus is not active");

    SpeakerArrangement arrangement = 0;
    check(processor->getBusArrangement(kInput, 0, arrangement) == kResultTrue,
          "input bus arrangement read failed");
    check(arrangement == SpeakerArr::kMono, "default input arrangement is not mono");
    check(processor->getBusArrangement(kOutput, 0, arrangement) == kResultTrue,
          "output bus arrangement read failed");
    check(arrangement == SpeakerArr::kMono, "default output arrangement is not mono");
}

class MemoryStream final : public IBStream {
public:
    tresult PLUGIN_API queryInterface(const TUID queryIid, void **obj) SMTG_OVERRIDE
    {
        if (!obj) {
            return kInvalidArgument;
        }
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
        if (!buffer || numBytes < 0) {
            return kInvalidArgument;
        }
        const uint64_t remaining = size - offset;
        const uint64_t requested = static_cast<uint64_t>(numBytes);
        const uint64_t count = requested < remaining ? requested : remaining;
        if (count > 0) {
            std::memcpy(buffer, data + offset, static_cast<size_t>(count));
            offset += count;
        }
        if (numBytesRead) {
            *numBytesRead = static_cast<int32>(count);
        }
        return kResultOk;
    }

    tresult PLUGIN_API write(void *buffer, int32 numBytes, int32 *numBytesWritten) SMTG_OVERRIDE
    {
        if (!buffer || numBytes < 0) {
            return kInvalidArgument;
        }
        const uint64_t requested = static_cast<uint64_t>(numBytes);
        if (offset + requested > sizeof(data)) {
            return kResultFalse;
        }
        std::memcpy(data + offset, buffer, static_cast<size_t>(requested));
        offset += requested;
        if (offset > size) {
            size = offset;
        }
        if (numBytesWritten) {
            *numBytesWritten = numBytes;
        }
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
            next = static_cast<int64>(size) + pos;
        } else {
            return kInvalidArgument;
        }
        if (next < 0 || static_cast<uint64_t>(next) > size) {
            return kResultFalse;
        }
        offset = static_cast<uint64_t>(next);
        if (result) {
            *result = static_cast<int64>(offset);
        }
        return kResultOk;
    }

    tresult PLUGIN_API tell(int64 *pos) SMTG_OVERRIDE
    {
        if (!pos) {
            return kInvalidArgument;
        }
        *pos = static_cast<int64>(offset);
        return kResultOk;
    }

    void rewind() { offset = 0; }

private:
    uint8_t data[256] = {};
    uint64_t size = 0;
    uint64_t offset = 0;
    uint32 refs = 1;
};

class ParamQueue final : public IParamValueQueue {
public:
    ParamQueue(ParamID paramId, int32 sampleOffset, ParamValue paramValue)
        : id(paramId), offset(sampleOffset), value(paramValue)
    {
    }

    tresult PLUGIN_API queryInterface(const TUID queryIid, void **obj) SMTG_OVERRIDE
    {
        if (!obj) {
            return kInvalidArgument;
        }
        if (iidEqual(queryIid, IParamValueQueue::iid) || iidEqual(queryIid, FUnknown::iid)) {
            *obj = static_cast<IParamValueQueue *>(this);
            addRef();
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef() SMTG_OVERRIDE { return ++refs; }
    uint32 PLUGIN_API release() SMTG_OVERRIDE { return --refs; }
    ParamID PLUGIN_API getParameterId() SMTG_OVERRIDE { return id; }
    int32 PLUGIN_API getPointCount() SMTG_OVERRIDE { return 1; }

    tresult PLUGIN_API getPoint(int32 index, int32 &sampleOffset,
                                ParamValue &paramValue) SMTG_OVERRIDE
    {
        if (index != 0) {
            return kInvalidArgument;
        }
        sampleOffset = offset;
        paramValue = value;
        return kResultOk;
    }

    tresult PLUGIN_API addPoint(int32, ParamValue, int32 &) SMTG_OVERRIDE
    {
        return kNotImplemented;
    }

private:
    ParamID id;
    int32 offset;
    ParamValue value;
    uint32 refs = 1;
};

class ParameterChanges final : public IParameterChanges {
public:
    explicit ParameterChanges(ParamQueue *paramQueue) : queue(paramQueue) {}

    tresult PLUGIN_API queryInterface(const TUID queryIid, void **obj) SMTG_OVERRIDE
    {
        if (!obj) {
            return kInvalidArgument;
        }
        if (iidEqual(queryIid, IParameterChanges::iid) || iidEqual(queryIid, FUnknown::iid)) {
            *obj = static_cast<IParameterChanges *>(this);
            addRef();
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef() SMTG_OVERRIDE { return ++refs; }
    uint32 PLUGIN_API release() SMTG_OVERRIDE { return --refs; }
    int32 PLUGIN_API getParameterCount() SMTG_OVERRIDE { return queue ? 1 : 0; }
    IParamValueQueue *PLUGIN_API getParameterData(int32 index) SMTG_OVERRIDE
    {
        return index == 0 ? queue : nullptr;
    }
    IParamValueQueue *PLUGIN_API addParameterData(const ParamID &, int32 &) SMTG_OVERRIDE
    {
        return nullptr;
    }

private:
    ParamQueue *queue;
    uint32 refs = 1;
};

static double normalized(uint32_t id, double plain)
{
    const NilampControlSpec *spec = nilamp_control_spec(id);
    check(spec != nullptr, "missing parameter spec");
    return (plain - spec->min_value) / (spec->max_value - spec->min_value);
}

static void copyTitleAscii(const String128 title, char *dst, size_t dstSize)
{
    if (!dst || dstSize == 0) {
        return;
    }
    size_t i = 0;
    for (; i + 1 < dstSize && i < 128 && title[i] != 0; i++) {
        dst[i] = static_cast<char>(title[i]);
    }
    dst[i] = '\0';
}

static void fillInput(float *left, float *right, uint32_t frames)
{
    for (uint32_t i = 0; i < frames; i++) {
        const float t = static_cast<float>(i) / static_cast<float>(frames);
        left[i] = 0.06f * std::sin(17.0f * t) + 0.015f * std::cos(43.0f * t);
        right[i] = 0.04f * std::cos(11.0f * t) - 0.02f * std::sin(29.0f * t);
    }
}

static void sanitize(float *buffer, uint32_t frames)
{
    for (uint32_t i = 0; i < frames; i++) {
        buffer[i] = nilamp_host_sanitize_sample(buffer[i]);
    }
}

static void compareOutput(const float *actual, const float *expected, uint32_t frames,
                          const char *label)
{
    for (uint32_t i = 0; i < frames; i++) {
        const float diff = std::fabs(actual[i] - expected[i]);
        if (!std::isfinite(actual[i]) || diff > 0.00001f) {
            std::fprintf(stderr,
                         "test_vst3_load: %s mismatch at %u: actual=%g expected=%g diff=%g\n",
                         label, i, actual[i], expected[i], diff);
            std::exit(1);
        }
    }
}

static void renderReference(float gainDb, const float *inL, const float *inR,
                            float *outL, float *outR, uint32_t frames)
{
    NilampEngine *engineL = nilamp_engine_create(48000.0);
    NilampEngine *engineR = nilamp_engine_create(48000.0);
    check(engineL != nullptr && engineR != nullptr, "failed to create reference engines");
    NilampParams params = nilamp_default_params();
    params.gain_db = gainDb;
    nilamp_engine_set_params(engineL, &params);
    nilamp_engine_set_params(engineR, &params);
    nilamp_engine_process(engineL, inL, outL, frames);
    nilamp_engine_process(engineR, inR, outR, frames);
    sanitize(outL, frames);
    sanitize(outR, frames);
    nilamp_engine_destroy(engineL);
    nilamp_engine_destroy(engineR);
}

static void renderReferenceMono(float gainDb, const float *input, float *output,
                                uint32_t frames)
{
    NilampEngine *engine = nilamp_engine_create(48000.0);
    check(engine != nullptr, "failed to create mono reference engine");
    NilampParams params = nilamp_default_params();
    params.gain_db = gainDb;
    nilamp_engine_set_params(engine, &params);
    nilamp_engine_process(engine, input, output, frames);
    sanitize(output, frames);
    nilamp_engine_destroy(engine);
}

static void renderReferenceAfterWarmup(float gainDb, const float *inL, const float *inR,
                                       float *outL, float *outR, uint32_t frames)
{
    NilampEngine *engineL = nilamp_engine_create(48000.0);
    NilampEngine *engineR = nilamp_engine_create(48000.0);
    check(engineL != nullptr && engineR != nullptr, "failed to create warm reference engines");
    NilampParams params = nilamp_default_params();
    float warmL[128] = {};
    float warmR[128] = {};
    nilamp_engine_process(engineL, inL, warmL, frames);
    nilamp_engine_process(engineR, inR, warmR, frames);
    params.gain_db = gainDb;
    nilamp_engine_set_params(engineL, &params);
    nilamp_engine_set_params(engineR, &params);
    nilamp_engine_process(engineL, inL, outL, frames);
    nilamp_engine_process(engineR, inR, outR, frames);
    sanitize(outL, frames);
    sanitize(outR, frames);
    nilamp_engine_destroy(engineL);
    nilamp_engine_destroy(engineR);
}

int main(int argc, char **argv)
{
    check(argc == 2, "usage: test_vst3_load /path/to/plugin.vst3");

#if defined(__APPLE__)
    CFStringRef path = CFStringCreateWithCString(kCFAllocatorDefault, argv[1],
                                                 kCFStringEncodingUTF8);
    check(path != nullptr, "failed to create path string");
    CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, path,
                                                 kCFURLPOSIXPathStyle, true);
    CFRelease(path);
    check(url != nullptr, "failed to create bundle URL");
    CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, url);
    CFRelease(url);
    check(bundle != nullptr, "failed to create bundle");
    check(CFBundleLoadExecutable(bundle), "failed to load bundle executable");

    using BundleEntryFn = bool (*)(CFBundleRef);
    using BundleExitFn = bool (*)(void);
    using GetFactoryFn = IPluginFactory *(*)();
    auto bundleEntry = reinterpret_cast<BundleEntryFn>(
        CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleEntry")));
    auto bundleExit = reinterpret_cast<BundleExitFn>(
        CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleExit")));
    auto getFactory = reinterpret_cast<GetFactoryFn>(
        CFBundleGetFunctionPointerForName(bundle, CFSTR("GetPluginFactory")));
    check(bundleEntry != nullptr && bundleExit != nullptr && getFactory != nullptr,
          "missing VST3 entry points");
    check(bundleEntry(bundle), "bundleEntry failed");
#elif defined(_WIN32)
    char libraryPath[4096] = {};
    const int written = std::snprintf(libraryPath, sizeof(libraryPath),
                                      "%s\\Contents\\%s", argv[1],
                                      NILAMP_TEST_VST3_WINDOWS_BINARY);
    check(written > 0 && static_cast<size_t>(written) < sizeof(libraryPath),
          "VST3 library path is too long");
    HMODULE module = LoadLibraryA(libraryPath);
    if (!module) {
        std::fprintf(stderr, "test_vst3_load: failed to load VST3 module: %lu\n",
                     static_cast<unsigned long>(GetLastError()));
        return 1;
    }

    using ModuleEntryFn = bool (*)();
    using ModuleExitFn = bool (*)();
    using GetFactoryFn = IPluginFactory *(*)();
    auto moduleEntry =
        reinterpret_cast<ModuleEntryFn>(GetProcAddress(module, "InitDll"));
    auto moduleExit =
        reinterpret_cast<ModuleExitFn>(GetProcAddress(module, "ExitDll"));
    auto getFactory =
        reinterpret_cast<GetFactoryFn>(GetProcAddress(module, "GetPluginFactory"));
    check(moduleEntry != nullptr && moduleExit != nullptr && getFactory != nullptr,
          "missing VST3 entry points");
    check(moduleEntry(), "InitDll failed");
#else
    char libraryPath[4096] = {};
    const int written = std::snprintf(libraryPath, sizeof(libraryPath),
                                      "%s/Contents/%s", argv[1],
                                      NILAMP_TEST_VST3_LINUX_BINARY);
    check(written > 0 && static_cast<size_t>(written) < sizeof(libraryPath),
          "VST3 library path is too long");
    void *module = dlopen(libraryPath, RTLD_NOW | RTLD_LOCAL);
    if (!module) {
        std::fprintf(stderr, "test_vst3_load: failed to load VST3 module: %s\n", dlerror());
        return 1;
    }

    using ModuleEntryFn = bool (*)(void *);
    using ModuleExitFn = bool (*)(void);
    using GetFactoryFn = IPluginFactory *(*)();
    auto moduleEntry = reinterpret_cast<ModuleEntryFn>(dlsym(module, "ModuleEntry"));
    auto moduleExit = reinterpret_cast<ModuleExitFn>(dlsym(module, "ModuleExit"));
    auto getFactory = reinterpret_cast<GetFactoryFn>(dlsym(module, "GetPluginFactory"));
    check(moduleEntry != nullptr && moduleExit != nullptr && getFactory != nullptr,
          "missing VST3 entry points");
    check(moduleEntry(module), "ModuleEntry failed");
#endif

    IPluginFactory *factory = getFactory();
    check(factory != nullptr, "missing plugin factory");
    check(factory->countClasses() == 2, "unexpected factory class count");
    IPluginFactory2 *factory2 = nullptr;
    check(factory->queryInterface(IPluginFactory2::iid, (void **)&factory2) == kResultOk,
          "factory2 query failed");
    PClassInfo2 classInfo = {};
    check(factory2->getClassInfo2(0, &classInfo) == kResultOk,
          "processor class info2 failed");
    check(std::strcmp(classInfo.version, NILAMP_RELEASE_VERSION) == 0,
          "unexpected VST3 processor version");
    classInfo = {};
    check(factory2->getClassInfo2(1, &classInfo) == kResultOk,
          "controller class info2 failed");
    check(std::strcmp(classInfo.version, NILAMP_RELEASE_VERSION) == 0,
          "unexpected VST3 controller version");
    factory2->release();

    TUID processorUid = NILAMP_VST3_PROCESSOR_UID;
    TUID controllerUid = NILAMP_VST3_CONTROLLER_UID;
    IComponent *component = nullptr;
    IAudioProcessor *processor = nullptr;
    IEditController *controller = nullptr;
    check(factory->createInstance(processorUid, IComponent::iid, (void **)&component) == kResultOk,
          "processor component create failed");
    check(component->queryInterface(IAudioProcessor::iid, (void **)&processor) == kResultOk,
          "audio processor query failed");
    check(factory->createInstance(controllerUid, IEditController::iid, (void **)&controller) ==
              kResultOk,
          "controller create failed");

    check(component->initialize(nullptr) == kResultOk, "component initialize failed");
    check(controller->initialize(nullptr) == kResultOk, "controller initialize failed");
    checkDefaultMonoBusses(component, processor);
    check(controller->getParameterCount() == NILAMP_PARAM_COUNT,
          "unexpected VST3 parameter count");
    ParameterInfo info = {};
    check(controller->getParameterInfo(NILAMP_PARAM_GAIN_DB, info) == kResultOk,
          "gain parameter info failed");
    check(info.id == NILAMP_PARAM_GAIN_DB, "unexpected gain parameter id");
    char title[128] = {};
    copyTitleAscii(info.title, title, sizeof(title));
    check(std::strcmp(title, "Input Gain") == 0, "unexpected VST3 input gain title");
    check(controller->getParameterInfo(NILAMP_PARAM_OUTPUT_GAIN_DB, info) == kResultOk,
          "output gain parameter info failed");
    copyTitleAscii(info.title, title, sizeof(title));
    check(std::strcmp(title, "Output Gain") == 0, "unexpected VST3 output gain title");
    check(controller->getParameterInfo(NILAMP_PARAM_BYPASS, info) == kResultOk,
          "bypass parameter info failed");
    copyTitleAscii(info.title, title, sizeof(title));
    check(std::strcmp(title, "Bypass") == 0, "unexpected VST3 bypass title");
    check(info.stepCount == 1, "VST3 bypass is not a toggle");
    check((info.flags & ParameterInfo::kIsBypass) != 0,
          "VST3 bypass flag is missing");
    check(std::fabs(info.defaultNormalizedValue) < 0.000001,
          "unexpected VST3 bypass default");

    IPlugView *view = controller->createView(ViewType::kEditor);
    check(view != nullptr, "editor view create failed");
    IPlugViewContentScaleSupport *scaleSupport = nullptr;
    check(view->queryInterface(IPlugViewContentScaleSupport::iid,
                               (void **)&scaleSupport) == kResultOk,
          "editor scale support query failed");
    ViewRect viewSize = {};
    check(view->getSize(&viewSize) == kResultTrue, "editor getSize failed");
    check(viewSize.getWidth() == 500 && viewSize.getHeight() == 340,
          "unexpected default editor size");
    check(scaleSupport->setContentScaleFactor(1.5f) == kResultTrue,
          "editor scale set failed");
    viewSize = {};
    check(view->getSize(&viewSize) == kResultTrue, "scaled editor getSize failed");
    check(viewSize.getWidth() == 750 && viewSize.getHeight() == 510,
          "unexpected scaled editor size");
    scaleSupport->release();
    view->release();

    const ParamValue smoothVolume =
        controller->normalizedParamToPlain(NILAMP_PARAM_VOLUME_PCT, 0.513);
    check(std::fabs(smoothVolume - 51.3) < 0.000001,
          "VST3 continuous parameter conversion is stepped");
    const ParamValue discreteTube = controller->normalizedParamToPlain(NILAMP_PARAM_TUBE1, 0.51);
    check(std::fabs(discreteTube - 1.0) < 0.000001,
          "VST3 enum parameter conversion is not discrete");

    SpeakerArrangement inputArrangement = SpeakerArr::kMono;
    SpeakerArrangement outputArrangement = SpeakerArr::kMono;
    check(processor->setBusArrangements(&inputArrangement, 1, &outputArrangement, 1) ==
              kResultTrue,
          "mono bus arrangement failed");
    ProcessSetup setup = {};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 128;
    setup.sampleRate = 48000.0;
    check(processor->setupProcessing(setup) == kResultOk, "setupProcessing failed");
    check(component->setActive(true) == kResultOk, "setActive failed");
    check(processor->setProcessing(true) == kResultOk, "setProcessing failed");

    constexpr uint32_t Frames = 128;
    float inL[Frames] = {};
    float inR[Frames] = {};
    float outL[Frames] = {};
    float outR[Frames] = {};
    float refL[Frames] = {};
    float refR[Frames] = {};
    fillInput(inL, inR, Frames);

    float monoOut[Frames] = {};
    float monoRef[Frames] = {};
    float *monoInputChannels[1] = {inL};
    float *monoOutputChannels[1] = {monoOut};
    AudioBusBuffers monoInput = {};
    AudioBusBuffers monoOutput = {};
    monoInput.numChannels = 1;
    monoInput.channelBuffers32 = monoInputChannels;
    monoOutput.numChannels = 1;
    monoOutput.channelBuffers32 = monoOutputChannels;
    ProcessData monoData = {};
    monoData.processMode = kRealtime;
    monoData.symbolicSampleSize = kSample32;
    monoData.numSamples = Frames;
    monoData.numInputs = 1;
    monoData.numOutputs = 1;
    monoData.inputs = &monoInput;
    monoData.outputs = &monoOutput;
    check(processor->process(monoData) == kResultOk, "mono process failed");
    renderReferenceMono(0.0f, inL, monoRef, Frames);
    compareOutput(monoOut, monoRef, Frames, "mono default");
    processor->setProcessing(false);
    component->setActive(false);

    inputArrangement = SpeakerArr::kStereo;
    outputArrangement = SpeakerArr::kStereo;
    check(processor->setBusArrangements(&inputArrangement, 1, &outputArrangement, 1) ==
              kResultTrue,
          "stereo bus arrangement failed");
    check(component->setActive(true) == kResultOk, "stereo setActive failed");
    check(processor->setProcessing(true) == kResultOk, "stereo setProcessing failed");

    float *inputChannels[2] = {inL, inR};
    float *outputChannels[2] = {outL, outR};
    AudioBusBuffers input = {};
    AudioBusBuffers output = {};
    input.numChannels = 2;
    input.channelBuffers32 = inputChannels;
    output.numChannels = 2;
    output.channelBuffers32 = outputChannels;
    ProcessData data = {};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = Frames;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &input;
    data.outputs = &output;

    check(processor->process(data) == kResultOk, "default process failed");
    renderReference(0.0f, inL, inR, refL, refR, Frames);
    compareOutput(outL, refL, Frames, "default L");
    compareOutput(outR, refR, Frames, "default R");

    MemoryStream state;
    check(component->getState(&state) == kResultOk, "state save failed");

    std::memset(outL, 0, sizeof(outL));
    std::memset(outR, 0, sizeof(outR));
    ParamQueue gainQueue(NILAMP_PARAM_GAIN_DB, 0, normalized(NILAMP_PARAM_GAIN_DB, 6.0));
    ParameterChanges changes(&gainQueue);
    data.inputParameterChanges = &changes;
    check(processor->process(data) == kResultOk, "automation process failed");
    renderReferenceAfterWarmup(6.0f, inL, inR, refL, refR, Frames);
    compareOutput(outL, refL, Frames, "automation L");
    compareOutput(outR, refR, Frames, "automation R");

    state.rewind();
    check(component->setState(&state) == kResultOk, "state restore failed");
    check(component->setActive(false) == kResultOk, "state restore deactivate failed");
    check(component->setActive(true) == kResultOk, "state restore reactivate failed");
    data.inputParameterChanges = nullptr;
    std::memset(outL, 0, sizeof(outL));
    std::memset(outR, 0, sizeof(outR));
    check(processor->process(data) == kResultOk, "restored process failed");
    renderReference(0.0f, inL, inR, refL, refR, Frames);
    compareOutput(outL, refL, Frames, "restored L");
    compareOutput(outR, refR, Frames, "restored R");

    std::memset(outL, 0, sizeof(outL));
    std::memset(outR, 0, sizeof(outR));
    ParamQueue bypassOnQueue(NILAMP_PARAM_BYPASS, 0, normalized(NILAMP_PARAM_BYPASS, 1.0));
    ParameterChanges bypassOnChanges(&bypassOnQueue);
    data.inputParameterChanges = &bypassOnChanges;
    check(processor->process(data) == kResultOk, "bypass process failed");
    compareOutput(outL, inL, Frames, "bypass L");
    compareOutput(outR, inR, Frames, "bypass R");

    processor->setProcessing(false);
    component->setActive(false);
    controller->terminate();
    component->terminate();
    controller->release();
    processor->release();
    component->release();
    factory->release();
#if defined(__APPLE__)
    check(bundleExit(), "bundleExit failed");
    CFRelease(bundle);
#elif defined(_WIN32)
    check(moduleExit(), "ExitDll failed");
    check(FreeLibrary(module), "FreeLibrary failed");
#else
    check(moduleExit(), "ModuleExit failed");
    check(dlclose(module) == 0, "dlclose failed");
#endif
    return 0;
}
