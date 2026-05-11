// SPDX-License-Identifier: MIT
#include "nilamp_dsp.h"
#include "nilamp_gui.h"
#include "nilamp_host.h"

#if !defined(__APPLE__)
#include "base/source/timer.h"
#endif
#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstpresetkeys.h"
#include "public.sdk/source/common/pluginview.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#ifndef NILAMP_VST3_NAME
#define NILAMP_VST3_NAME "nilamp TWD MKII"
#endif

#ifndef NILAMP_RELEASE_VERSION
#define NILAMP_RELEASE_VERSION "1.0.2"
#endif

namespace NilampVst3 {

using namespace Steinberg;
using namespace Steinberg::Vst;

static const FUID ProcessorUID(0xb0494b81, 0xb2385c29, 0x8c4d5734, 0x6d0b1222);
static const FUID ControllerUID(0x66e72a3a, 0x9187500d, 0xafa4d86a, 0x88935c65);
#if !defined(__APPLE__)
static constexpr uint32 kEditorTimerMs = 33;
#endif
static constexpr double kEditorFrameIntervalSeconds = 1.0 / 30.0;
static constexpr std::array<CtrlNumber, 6> kFrontFaceMidiCcs = {
    kCtrlGPC1, kCtrlGPC2, kCtrlGPC3, kCtrlGPC4, kCtrlGPC5, kCtrlGPC6,
};

static bool iidEqual(const TUID a, const TUID b)
{
    return std::memcmp(a, b, sizeof(TUID)) == 0;
}

static double normalizedToPlain(const NilampControlSpec *spec, double normalized)
{
    if (!spec) {
        return 0.0;
    }
    if (!std::isfinite(normalized)) {
        normalized = 0.0;
    }
    if (normalized < 0.0) {
        normalized = 0.0;
    } else if (normalized > 1.0) {
        normalized = 1.0;
    }
    const double value = spec->min_value + normalized * (spec->max_value - spec->min_value);
    if (spec->display == NILAMP_CONTROL_DISPLAY_ENUM && spec->step > 0.0f) {
        const double steps = std::round((value - spec->min_value) / spec->step);
        return nilamp_host_clamp_param(spec->min_value + steps * spec->step, spec);
    }
    return nilamp_host_clamp_param(value, spec);
}

static double plainToNormalized(const NilampControlSpec *spec, double plain)
{
    if (!spec || spec->max_value <= spec->min_value) {
        return 0.0;
    }
    const double clamped = nilamp_host_clamp_param(plain, spec);
    return (clamped - spec->min_value) / (spec->max_value - spec->min_value);
}

static void copyAsciiToTChar(String128 dst, const char *text)
{
    UString(dst, 128).fromAscii(text ? text : "");
}

static bool writeStream(void *user, const void *data, uint64_t size)
{
    IBStream *stream = static_cast<IBStream *>(user);
    if (!stream || !data) {
        return false;
    }
    const uint8_t *cursor = static_cast<const uint8_t *>(data);
    while (size > 0) {
        int32 written = 0;
        const uint32_t chunk = size > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(size);
        if (stream->write(const_cast<uint8_t *>(cursor), static_cast<int32>(chunk), &written) !=
                kResultOk ||
            written <= 0) {
            return false;
        }
        cursor += static_cast<uint32_t>(written);
        size -= static_cast<uint32_t>(written);
    }
    return true;
}

static bool readStream(void *user, void *data, uint64_t size)
{
    IBStream *stream = static_cast<IBStream *>(user);
    if (!stream || !data) {
        return false;
    }
    uint8_t *cursor = static_cast<uint8_t *>(data);
    while (size > 0) {
        int32 bytesRead = 0;
        const uint32_t chunk = size > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(size);
        if (stream->read(cursor, static_cast<int32>(chunk), &bytesRead) != kResultOk ||
            bytesRead <= 0) {
            return false;
        }
        cursor += static_cast<uint32_t>(bytesRead);
        size -= static_cast<uint32_t>(bytesRead);
    }
    return true;
}

class Processor final : public AudioEffect {
public:
    Processor()
    {
        nilamp_host_core_init(&core);
        setControllerClass(ControllerUID);
    }

    ~Processor() override { nilamp_host_core_deinit(&core); }

    tresult PLUGIN_API initialize(FUnknown *context) SMTG_OVERRIDE
    {
        const tresult result = AudioEffect::initialize(context);
        if (result != kResultOk) {
            return result;
        }
        addAudioInput(STR("Audio In"), SpeakerArr::kMono);
        addAudioOutput(STR("Audio Out"), SpeakerArr::kMono);
        addEventInput(STR("MIDI In"), 16);
        return kResultOk;
    }

    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE
    {
        if (state) {
            return nilamp_host_core_activate(&core, processSetup.sampleRate) ? kResultOk :
                                                                       kResultFalse;
        }
        nilamp_host_core_deactivate(&core);
        return kResultOk;
    }

    tresult PLUGIN_API setState(IBStream *state) SMTG_OVERRIDE
    {
        return nilamp_host_load_state(&core, readStream, state) ? kResultOk : kResultFalse;
    }

    tresult PLUGIN_API getState(IBStream *state) SMTG_OVERRIDE
    {
        return nilamp_host_save_state(&core, writeStream, state) ? kResultOk : kResultFalse;
    }

    tresult PLUGIN_API setBusArrangements(SpeakerArrangement *inputs, int32 numIns,
                                          SpeakerArrangement *outputs,
                                          int32 numOuts) SMTG_OVERRIDE
    {
        if (!inputs || !outputs || numIns != 1 || numOuts != 1) {
            return kResultFalse;
        }
        const int32 inputChannels = SpeakerArr::getChannelCount(inputs[0]);
        const int32 outputChannels = SpeakerArr::getChannelCount(outputs[0]);
        if ((inputChannels != 1 && inputChannels != 2) ||
            (outputChannels != 1 && outputChannels != 2)) {
            return kResultFalse;
        }
        getAudioInput(0)->setArrangement(inputs[0]);
        getAudioOutput(0)->setArrangement(outputs[0]);
        return kResultTrue;
    }

    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) SMTG_OVERRIDE
    {
        return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API setProcessing(TBool) SMTG_OVERRIDE
    {
        return kResultOk;
    }

    tresult PLUGIN_API process(ProcessData &data) SMTG_OVERRIDE
    {
        if (data.numSamples <= 0) {
            return kResultOk;
        }
        handleParameterChanges(data.inputParameterChanges, data.numSamples);

        NilampHostAudioBlock block = {};
        if (data.numInputs > 0 && data.inputs && data.inputs[0].channelBuffers32) {
            block.input_channels = static_cast<uint32_t>(data.inputs[0].numChannels);
            for (uint32_t ch = 0; ch < block.input_channels && ch < 2u; ch++) {
                block.inputs[ch] = data.inputs[0].channelBuffers32[ch];
            }
            block.input_constant_mask = data.inputs[0].silenceFlags;
        }
        if (data.numOutputs > 0 && data.outputs && data.outputs[0].channelBuffers32) {
            block.output_channels = static_cast<uint32_t>(data.outputs[0].numChannels);
            for (uint32_t ch = 0; ch < block.output_channels && ch < 2u; ch++) {
                block.outputs[ch] = data.outputs[0].channelBuffers32[ch];
            }
        }

        uint32_t cursor = 0;
        for (uint32_t i = 0; i < eventCount; i++) {
            const uint32_t offset = events[i].offset < static_cast<uint32_t>(data.numSamples) ?
                                        events[i].offset :
                                        static_cast<uint32_t>(data.numSamples);
            if (!nilamp_host_process_segment(&core, &block, cursor, offset)) {
                return kResultFalse;
            }
            (void)nilamp_host_core_set_param(&core, events[i].id, events[i].plain);
            cursor = offset;
        }
        if (!nilamp_host_process_segment(&core, &block, cursor,
                                         static_cast<uint32_t>(data.numSamples))) {
            return kResultFalse;
        }
        return kResultOk;
    }

private:
    struct ParamEvent {
        uint32_t offset;
        uint32_t id;
        double plain;
    };

    void handleParameterChanges(IParameterChanges *changes, int32 sampleCount)
    {
        eventCount = 0;
        if (!changes) {
            return;
        }
        const int32 queueCount = changes->getParameterCount();
        for (int32 queueIndex = 0; queueIndex < queueCount; queueIndex++) {
            IParamValueQueue *queue = changes->getParameterData(queueIndex);
            if (!queue) {
                continue;
            }
            const ParamID id = queue->getParameterId();
            const NilampControlSpec *spec = nilamp_host_find_param(id);
            if (!spec) {
                continue;
            }
            const int32 pointCount = queue->getPointCount();
            for (int32 pointIndex = 0; pointIndex < pointCount && eventCount < events.size();
                 pointIndex++) {
                int32 offset = 0;
                ParamValue normalized = 0.0;
                if (queue->getPoint(pointIndex, offset, normalized) != kResultOk) {
                    continue;
                }
                if (offset < 0) {
                    offset = 0;
                } else if (offset > sampleCount) {
                    offset = sampleCount;
                }
                events[eventCount++] = {
                    static_cast<uint32_t>(offset),
                    static_cast<uint32_t>(id),
                    normalizedToPlain(spec, normalized),
                };
            }
        }
        std::sort(events.begin(), events.begin() + static_cast<ptrdiff_t>(eventCount),
                  [](const ParamEvent &a, const ParamEvent &b) {
                      return a.offset < b.offset;
                  });
    }

    NilampHostCore core = {};
    std::array<ParamEvent, 256> events = {};
    uint32_t eventCount = 0;
};

class Controller;
class Editor;

#if defined(__linux__)
class LinuxEditorTimer final : public Linux::ITimerHandler {
public:
    explicit LinuxEditorTimer(Editor *ownerIn) : owner(ownerIn) {}

    tresult PLUGIN_API queryInterface(const TUID queryIid, void **obj) SMTG_OVERRIDE
    {
        if (!obj) {
            return kInvalidArgument;
        }
        if (iidEqual(queryIid, Linux::ITimerHandler::iid) ||
            iidEqual(queryIid, FUnknown::iid)) {
            *obj = static_cast<Linux::ITimerHandler *>(this);
            addRef();
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef() SMTG_OVERRIDE { return ++refs; }

    uint32 PLUGIN_API release() SMTG_OVERRIDE
    {
        const uint32 next = --refs;
        if (next == 0) {
            delete this;
        }
        return next;
    }

    void PLUGIN_API onTimer() SMTG_OVERRIDE;
    void detach() { owner = nullptr; }

private:
    Editor *owner = nullptr;
    uint32 refs = 1;
};
#endif

class Editor final : public CPluginView
    , public IPlugViewContentScaleSupport
#if !defined(__APPLE__)
    , public ITimerCallback
#endif
{
public:
    explicit Editor(Controller *controller);
    ~Editor() override;

    uint32 PLUGIN_API addRef() SMTG_OVERRIDE { return CPluginView::addRef(); }
    uint32 PLUGIN_API release() SMTG_OVERRIDE { return CPluginView::release(); }
    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) SMTG_OVERRIDE;
    tresult PLUGIN_API queryInterface(const TUID queryIid, void **obj) SMTG_OVERRIDE;
    tresult PLUGIN_API attached(void *parent, FIDString type) SMTG_OVERRIDE;
    tresult PLUGIN_API removed() SMTG_OVERRIDE;
    tresult PLUGIN_API getSize(ViewRect *size) SMTG_OVERRIDE;
    tresult PLUGIN_API onSize(ViewRect *newSize) SMTG_OVERRIDE;
    tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) SMTG_OVERRIDE;
#if defined(__linux__)
    tresult PLUGIN_API setFrame(IPlugFrame *frame) SMTG_OVERRIDE;
#endif
#if !defined(__APPLE__)
    void onTimer(Timer *timer) SMTG_OVERRIDE;
#endif
    void hostParamsChanged();

private:
    static float getParam(void *user, uint32_t id);
    static void beginParamGesture(void *user, uint32_t id);
    static void setParam(void *user, uint32_t id, float value);
    static void endParamGesture(void *user, uint32_t id);
    static const char *modelName(void *user);
    void updateScaledRect();
    void startTimer();
    void stopTimer();
    void tick();
#if defined(__linux__)
    friend class LinuxEditorTimer;
#endif

    Controller *controller = nullptr;
    NilampGui *gui = nullptr;
#if defined(__APPLE__)
    NSTimer *timer = nil;
#else
    Timer *timer = nullptr;
#endif
#if defined(__linux__)
    FUnknownPtr<Linux::IRunLoop> runLoop;
    LinuxEditorTimer *runLoopTimer = nullptr;
    bool runLoopTimerRegistered = false;
#endif
    double contentScale = 1.0;
};

static UnitID findUnitIdForModule(const char *module,
                                  const std::array<const char *, NILAMP_PARAM_COUNT> &modules,
                                  uint32_t moduleCount)
{
    for (uint32_t i = 0; i < moduleCount; i++) {
        if (module && modules[i] && std::strcmp(module, modules[i]) == 0) {
            return static_cast<UnitID>(i + 1u);
        }
    }
    return kRootUnitId;
}

class Controller final : public EditControllerEx1, public IMidiMapping {
public:
    Controller() { nilamp_host_core_init(&core); }
    ~Controller() override { nilamp_host_core_deinit(&core); }

    uint32 PLUGIN_API addRef() SMTG_OVERRIDE { return EditControllerEx1::addRef(); }
    uint32 PLUGIN_API release() SMTG_OVERRIDE { return EditControllerEx1::release(); }

    tresult PLUGIN_API queryInterface(const TUID queryIid, void **obj) SMTG_OVERRIDE
    {
        if (!obj) {
            return kInvalidArgument;
        }
        if (iidEqual(queryIid, IMidiMapping::iid)) {
            *obj = static_cast<IMidiMapping *>(this);
            addRef();
            return kResultOk;
        }
        return EditControllerEx1::queryInterface(queryIid, obj);
    }

    tresult PLUGIN_API initialize(FUnknown *context) SMTG_OVERRIDE
    {
        const tresult result = EditControllerEx1::initialize(context);
        if (result != kResultOk) {
            return result;
        }
#if defined(__linux__)
        runLoop = context;
#endif

        const NilampControlSpec *specs = nilamp_control_specs(NULL);
        String128 rootName = {};
        copyAsciiToTChar(rootName, NILAMP_VST3_NAME);
        addUnit(new Unit(rootName, kRootUnitId, kNoParentUnitId, kNoProgramListId));

        std::array<const char *, NILAMP_PARAM_COUNT> modules = {};
        uint32_t moduleCount = 0;
        for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
            const char *module = specs[i].module;
            if (!module || module[0] == '\0') {
                continue;
            }
            if (findUnitIdForModule(module, modules, moduleCount) != kRootUnitId) {
                continue;
            }
            modules[moduleCount] = module;
            String128 unitName = {};
            copyAsciiToTChar(unitName, module);
            addUnit(new Unit(unitName, static_cast<UnitID>(moduleCount + 1u), kRootUnitId,
                             kNoProgramListId));
            moduleCount++;
        }

        for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
            const NilampControlSpec *spec = &specs[i];
            String128 title = {};
            String128 units = {};
            copyAsciiToTChar(title, spec->host_name ? spec->host_name : spec->name);
            copyAsciiToTChar(units, spec->unit);
            const double normalizedDefault = plainToNormalized(spec, spec->default_value);
            const int32 stepCount = spec->display == NILAMP_CONTROL_DISPLAY_ENUM ?
                                        static_cast<int32>(spec->enum_count - 1u) :
                                        0;
            const int32 flags = ParameterInfo::kCanAutomate |
                                (spec->display == NILAMP_CONTROL_DISPLAY_ENUM ?
                                     ParameterInfo::kIsList :
                                     0) |
                                (spec->id == NILAMP_PARAM_BYPASS ?
                                     ParameterInfo::kIsBypass :
                                     0);
            const UnitID unitId = findUnitIdForModule(spec->module, modules, moduleCount);
            if (spec->display == NILAMP_CONTROL_DISPLAY_ENUM && spec->enum_names) {
                auto *param = new StringListParameter(title, spec->id, units, flags, unitId);
                for (uint32_t item = 0; item < spec->enum_count; item++) {
                    String128 itemName = {};
                    copyAsciiToTChar(itemName, spec->enum_names[item]);
                    param->appendString(itemName);
                }
                param->setNormalized(normalizedDefault);
                parameters.addParameter(param);
            } else {
                auto *param = new RangeParameter(title, spec->id, units, spec->min_value,
                                                 spec->max_value, spec->default_value,
                                                 stepCount, flags, unitId);
                param->setNormalized(normalizedDefault);
                parameters.addParameter(param);
            }
        }
        buildMidiMappings();
        return kResultOk;
    }

    tresult PLUGIN_API getMidiControllerAssignment(int32 busIndex, int16 channel,
                                                   CtrlNumber midiControllerNumber,
                                                   ParamID &id) SMTG_OVERRIDE
    {
        if (busIndex != 0 || channel < 0 || channel >= 16) {
            return kResultFalse;
        }
        for (uint32_t i = 0; i < midiMappingCount; i++) {
            if (midiMappings[i].controller == midiControllerNumber) {
                id = midiMappings[i].id;
                return kResultTrue;
            }
        }
        return kResultFalse;
    }

    tresult PLUGIN_API setComponentState(IBStream *state) SMTG_OVERRIDE
    {
        if (!state || !nilamp_host_load_state(&core, readStream, state)) {
            return kResultFalse;
        }
        syncParamsFromCore();
        notifyEditorParamsChanged();
        return kResultOk;
    }

    tresult PLUGIN_API setState(IBStream *state) SMTG_OVERRIDE
    {
        return setComponentState(state);
    }

    tresult PLUGIN_API getState(IBStream *state) SMTG_OVERRIDE
    {
        return nilamp_host_save_state(&core, writeStream, state) ? kResultOk : kResultFalse;
    }

    tresult PLUGIN_API getParamStringByValue(ParamID tag, ParamValue valueNormalized,
                                             String128 string) SMTG_OVERRIDE
    {
        const NilampControlSpec *spec = nilamp_host_find_param(tag);
        if (!spec || !string) {
            return kResultFalse;
        }
        char text[64] = {};
        if (!nilamp_host_param_value_to_text(tag, normalizedToPlain(spec, valueNormalized),
                                             text, sizeof(text))) {
            return kResultFalse;
        }
        copyAsciiToTChar(string, text);
        return kResultTrue;
    }

    tresult PLUGIN_API getParamValueByString(ParamID tag, TChar *string,
                                             ParamValue &valueNormalized) SMTG_OVERRIDE
    {
        const NilampControlSpec *spec = nilamp_host_find_param(tag);
        if (!spec || !string) {
            return kResultFalse;
        }
        char text[128] = {};
        UString(string, 128).toAscii(text, sizeof(text));
        double plain = 0.0;
        if (!nilamp_host_param_text_to_value(tag, text, &plain)) {
            return kResultFalse;
        }
        valueNormalized = plainToNormalized(spec, plain);
        return kResultTrue;
    }

    ParamValue PLUGIN_API normalizedParamToPlain(ParamID tag,
                                                 ParamValue valueNormalized) SMTG_OVERRIDE
    {
        return normalizedToPlain(nilamp_host_find_param(tag), valueNormalized);
    }

    ParamValue PLUGIN_API plainParamToNormalized(ParamID tag,
                                                 ParamValue plainValue) SMTG_OVERRIDE
    {
        return plainToNormalized(nilamp_host_find_param(tag), plainValue);
    }

    tresult PLUGIN_API setParamNormalized(ParamID tag, ParamValue value) SMTG_OVERRIDE
    {
        const tresult result = EditControllerEx1::setParamNormalized(tag, value);
        const NilampControlSpec *spec = nilamp_host_find_param(tag);
        if (result == kResultOk && spec) {
            (void)nilamp_host_core_set_param(&core, tag, normalizedToPlain(spec, value));
            if (!editorEditActive) {
                notifyEditorParamsChanged();
            }
        }
        return result;
    }

    IPlugView *PLUGIN_API createView(FIDString name) SMTG_OVERRIDE
    {
        if (name && std::strcmp(name, ViewType::kEditor) == 0) {
            return new Editor(this);
        }
        return nullptr;
    }

    float getRawParam(uint32_t id) const
    {
        return static_cast<float>(nilamp_host_core_get_param(&core, id));
    }

    void setRawParamFromEditor(uint32_t id, float value)
    {
        const NilampControlSpec *spec = nilamp_host_find_param(id);
        if (!spec) {
            return;
        }
        const ParamValue normalized = plainToNormalized(spec, value);
        editorEditActive = true;
        (void)setParamNormalized(id, normalized);
        editorEditActive = false;
        performEdit(id, normalized);
    }

    void beginRawParamGesture(uint32_t id)
    {
        if (nilamp_host_find_param(id)) {
            beginEdit(id);
        }
    }

    void endRawParamGesture(uint32_t id)
    {
        if (nilamp_host_find_param(id)) {
            endEdit(id);
        }
    }

    void registerEditor(Editor *editor)
    {
        activeEditor = editor;
    }

    void unregisterEditor(Editor *editor)
    {
        if (activeEditor == editor) {
            activeEditor = nullptr;
        }
    }

#if defined(__linux__)
    Linux::IRunLoop *getRunLoop() const { return runLoop; }
#endif

private:
    struct MidiMapping {
        CtrlNumber controller;
        ParamID id;
    };

    void buildMidiMappings()
    {
        midiMappingCount = 0;
        addMidiMapping(kCtrlSustainOnOff, NILAMP_PARAM_BYPASS);

        const NilampGuiLayoutSpec *layout = nilamp_model_gui_layout(NILAMP_MODEL_DEFAULT);
        if (!layout) {
            return;
        }
        for (uint32_t screenIndex = 0; screenIndex < layout->screen_count; screenIndex++) {
            const NilampGuiScreenSpec *screen = &layout->screens[screenIndex];
            if (screen->id != layout->default_screen) {
                continue;
            }
            for (uint32_t widgetIndex = 0; widgetIndex < screen->widget_count; widgetIndex++) {
                const NilampGuiWidgetSpec *widget = &screen->widgets[widgetIndex];
                if (widget->type != NILAMP_GUI_WIDGET_KNOB ||
                    widget->param_id >= NILAMP_PARAM_COUNT) {
                    continue;
                }
                const uint32_t ccIndex = midiMappingCount - 1u;
                if (ccIndex >= kFrontFaceMidiCcs.size()) {
                    return;
                }
                addMidiMapping(kFrontFaceMidiCcs[ccIndex], widget->param_id);
            }
            return;
        }
    }

    void addMidiMapping(CtrlNumber controller, ParamID id)
    {
        if (midiMappingCount < midiMappings.size()) {
            midiMappings[midiMappingCount++] = {controller, id};
        }
    }

    void syncParamsFromCore()
    {
        const NilampControlSpec *specs = nilamp_control_specs(NULL);
        for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
            const NilampControlSpec *spec = &specs[i];
            if (auto *param = parameters.getParameter(spec->id)) {
                param->setNormalized(
                    plainToNormalized(spec, nilamp_host_core_get_param(&core, spec->id)));
            }
        }
    }

    void notifyEditorParamsChanged()
    {
        if (activeEditor) {
            activeEditor->hostParamsChanged();
        }
    }

    NilampHostCore core = {};
    std::array<MidiMapping, kFrontFaceMidiCcs.size() + 1u> midiMappings = {};
    uint32_t midiMappingCount = 0;
    Editor *activeEditor = nullptr;
    bool editorEditActive = false;
#if defined(__linux__)
    FUnknownPtr<Linux::IRunLoop> runLoop;
#endif
};

Editor::Editor(Controller *controllerIn) : CPluginView(nullptr), controller(controllerIn)
{
    if (controller) {
        controller->addRef();
    }
    updateScaledRect();
}

Editor::~Editor()
{
    stopTimer();
    if (controller) {
        controller->unregisterEditor(this);
    }
    nilamp_gui_destroy(gui);
    gui = nullptr;
    if (controller) {
        controller->release();
    }
}

tresult PLUGIN_API Editor::isPlatformTypeSupported(FIDString type)
{
#if defined(__APPLE__)
    return type && std::strcmp(type, kPlatformTypeNSView) == 0 ? kResultTrue : kResultFalse;
#elif defined(_WIN32)
    return type && std::strcmp(type, kPlatformTypeHWND) == 0 ? kResultTrue : kResultFalse;
#else
    return type && std::strcmp(type, kPlatformTypeX11EmbedWindowID) == 0 ? kResultTrue :
                                                                          kResultFalse;
#endif
}

tresult PLUGIN_API Editor::queryInterface(const TUID queryIid, void **obj)
{
    if (!obj) {
        return kInvalidArgument;
    }
    if (iidEqual(queryIid, IPlugViewContentScaleSupport::iid)) {
        *obj = static_cast<IPlugViewContentScaleSupport *>(this);
        addRef();
        return kResultOk;
    }
    return CPluginView::queryInterface(queryIid, obj);
}

tresult PLUGIN_API Editor::attached(void *parent, FIDString type)
{
    if (!parent || isPlatformTypeSupported(type) != kResultTrue || !controller) {
        return kResultFalse;
    }
    const NilampGuiCallbacks callbacks = {
        this,
        getParam,
        beginParamGesture,
        setParam,
        endParamGesture,
        modelName,
    };
    gui = nilamp_gui_create(&callbacks, (const NilampGuiParamSpec *)nilamp_control_specs(NULL),
                            NILAMP_PARAM_COUNT, nilamp_model_gui_layout(NILAMP_MODEL_DEFAULT),
#if defined(__APPLE__)
                            NILAMP_GUI_API_COCOA, false);
    const NilampGuiParent guiParent = {NILAMP_GUI_API_COCOA, (uintptr_t)parent};
#elif defined(_WIN32)
                            NILAMP_GUI_API_WIN32, false);
    const NilampGuiParent guiParent = {NILAMP_GUI_API_WIN32, (uintptr_t)parent};
#else
                            NILAMP_GUI_API_X11, false);
    const NilampGuiParent guiParent = {NILAMP_GUI_API_X11, (uintptr_t)parent};
#endif
    if (!gui ||
        !nilamp_gui_set_scale(gui, contentScale) ||
        !nilamp_gui_set_parent(gui, guiParent) ||
        !nilamp_gui_show(gui)) {
        nilamp_gui_destroy(gui);
        gui = nullptr;
        return kResultFalse;
    }
    controller->registerEditor(this);
    systemWindow = parent;
    (void)nilamp_gui_start_frame_timer(gui, kEditorFrameIntervalSeconds);
    nilamp_gui_on_main_thread(gui);
    startTimer();
    return kResultOk;
}

void Editor::startTimer()
{
#if defined(__APPLE__)
    if (timer) {
        return;
    }
    timer = [NSTimer scheduledTimerWithTimeInterval:kEditorFrameIntervalSeconds
                                            repeats:YES
                                              block:^(NSTimer *) {
                                                  this->tick();
                                              }];
#else
    if (timer) {
        return;
    }
    timer = Timer::create(this, kEditorTimerMs);
#if defined(__linux__)
    if (!timer && controller && !runLoopTimer) {
        if (!runLoop) {
            runLoop = controller->getRunLoop();
        }
        if (runLoop) {
            runLoopTimer = new LinuxEditorTimer(this);
            if (runLoop->registerTimer(runLoopTimer, kEditorTimerMs) == kResultTrue) {
                runLoopTimerRegistered = true;
            } else {
                runLoopTimer->detach();
                runLoopTimer->release();
                runLoopTimer = nullptr;
            }
        }
    }
#endif
#endif
}

void Editor::stopTimer()
{
#if defined(__APPLE__)
    if (timer) {
        [timer invalidate];
        timer = nil;
    }
#else
    if (timer) {
        timer->stop();
        timer->release();
        timer = nullptr;
    }
#if defined(__linux__)
    if (runLoopTimer) {
        if (runLoopTimerRegistered && runLoop) {
            (void)runLoop->unregisterTimer(runLoopTimer);
        }
        runLoopTimerRegistered = false;
        runLoopTimer->detach();
        runLoopTimer->release();
        runLoopTimer = nullptr;
    }
    runLoop = FUnknownPtr<Linux::IRunLoop>();
#endif
#endif
}

#if defined(__linux__)
tresult PLUGIN_API Editor::setFrame(IPlugFrame *frame)
{
    const tresult result = CPluginView::setFrame(frame);
    if (!timer && !runLoopTimer) {
        runLoop = frame;
        if (gui && nilamp_gui_is_visible(gui)) {
            startTimer();
        }
    }
    return result;
}
#endif

tresult PLUGIN_API Editor::removed()
{
    stopTimer();
    if (gui) {
        (void)nilamp_gui_hide(gui);
        nilamp_gui_destroy(gui);
        gui = nullptr;
    }
    if (controller) {
        controller->unregisterEditor(this);
    }
    systemWindow = nullptr;
    return kResultOk;
}

tresult PLUGIN_API Editor::getSize(ViewRect *size)
{
    if (!size) {
        return kInvalidArgument;
    }
    *size = rect;
    return kResultTrue;
}

tresult PLUGIN_API Editor::onSize(ViewRect *newSize)
{
    if (!newSize) {
        return kInvalidArgument;
    }
    rect = *newSize;
    if (gui) {
        (void)nilamp_gui_set_size(gui, static_cast<uint32_t>(rect.getWidth()),
                                  static_cast<uint32_t>(rect.getHeight()));
    }
    return kResultTrue;
}

tresult PLUGIN_API Editor::setContentScaleFactor(ScaleFactor factor)
{
    if (!std::isfinite(factor) || factor <= 0.0f) {
        return kInvalidArgument;
    }
    contentScale = static_cast<double>(factor);
    updateScaledRect();
    if (gui && !nilamp_gui_set_scale(gui, contentScale)) {
        return kResultFalse;
    }
    if (plugFrame) {
        (void)plugFrame->resizeView(this, &rect);
    }
    return kResultTrue;
}

void Editor::updateScaledRect()
{
    const NilampGuiLayoutSpec *layout = nilamp_model_gui_layout(NILAMP_MODEL_DEFAULT);
    const uint32_t designWidth = layout && layout->design_width > 0u ?
                                     layout->design_width :
                                     500u;
    const uint32_t designHeight = layout && layout->design_height > 0u ?
                                      layout->design_height :
                                      340u;
    const int32 width =
        std::max<int32>(1, static_cast<int32>(std::lround(designWidth * contentScale)));
    const int32 height =
        std::max<int32>(1, static_cast<int32>(std::lround(designHeight * contentScale)));
    rect = ViewRect(0, 0, width, height);
}

float Editor::getParam(void *user, uint32_t id)
{
    Editor *self = static_cast<Editor *>(user);
    return self && self->controller ? self->controller->getRawParam(id) : 0.0f;
}

void Editor::beginParamGesture(void *user, uint32_t id)
{
    Editor *self = static_cast<Editor *>(user);
    if (self && self->controller) {
        self->controller->beginRawParamGesture(id);
    }
}

void Editor::setParam(void *user, uint32_t id, float value)
{
    Editor *self = static_cast<Editor *>(user);
    if (self && self->controller) {
        self->controller->setRawParamFromEditor(id, value);
    }
}

void Editor::endParamGesture(void *user, uint32_t id)
{
    Editor *self = static_cast<Editor *>(user);
    if (self && self->controller) {
        self->controller->endRawParamGesture(id);
    }
}

const char *Editor::modelName(void *user)
{
    (void)user;
    return nilamp_model_name(NILAMP_MODEL_DEFAULT);
}

#if defined(__linux__)
void LinuxEditorTimer::onTimer()
{
    if (owner) {
        owner->tick();
    }
}
#endif

#if !defined(__APPLE__)
void Editor::onTimer(Timer *timerIn)
{
    if (timerIn == timer) {
        tick();
    }
}
#endif

void Editor::tick()
{
    if (gui) {
        nilamp_gui_refresh(gui);
        nilamp_gui_on_main_thread(gui);
    }
}

void Editor::hostParamsChanged()
{
    if (gui) {
        nilamp_gui_refresh(gui);
        nilamp_gui_on_main_thread(gui);
    }
}

FUnknown *createProcessorInstance(void *)
{
    return static_cast<IAudioProcessor *>(new Processor);
}

FUnknown *createControllerInstance(void *)
{
    return static_cast<IEditController *>(new Controller);
}

} // namespace NilampVst3

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace NilampVst3;

BEGIN_FACTORY("niltempus", "", "", Vst::kDefaultFactoryFlags)
DEF_CLASS2(INLINE_UID(0xb0494b81, 0xb2385c29, 0x8c4d5734, 0x6d0b1222),
           PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           NILAMP_VST3_NAME,
           Vst::kDistributable,
           "Fx|Distortion",
           NILAMP_RELEASE_VERSION,
           kVstVersionString,
           createProcessorInstance)
DEF_CLASS2(INLINE_UID(0x66e72a3a, 0x9187500d, 0xafa4d86a, 0x88935c65),
           PClassInfo::kManyInstances,
           kVstComponentControllerClass,
           NILAMP_VST3_NAME " Controller",
           0,
           "",
           NILAMP_RELEASE_VERSION,
           kVstVersionString,
           createControllerInstance)
END_FACTORY
