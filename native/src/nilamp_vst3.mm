// SPDX-License-Identifier: MIT
#include "nilamp_dsp.h"
#include "nilamp_gui.h"
#include "nilamp_host.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstpresetkeys.h"
#include "public.sdk/source/common/pluginview.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#ifndef NILAMP_VST3_NAME
#define NILAMP_VST3_NAME "nilamp TWD MKII"
#endif

#define NILAMP_VST3_VERSION "1.0.0"

namespace NilampVst3 {

using namespace Steinberg;
using namespace Steinberg::Vst;

static const FUID ProcessorUID(0xb0494b81, 0xb2385c29, 0x8c4d5734, 0x6d0b1222);
static const FUID ControllerUID(0x66e72a3a, 0x9187500d, 0xafa4d86a, 0x88935c65);

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
    if (spec->step > 0.0f) {
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
        addAudioInput(STR("Audio In"), SpeakerArr::kStereo);
        addAudioOutput(STR("Audio Out"), SpeakerArr::kStereo);
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

class Editor final : public CPluginView {
public:
    explicit Editor(Controller *controller);
    ~Editor() override;

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) SMTG_OVERRIDE;
    tresult PLUGIN_API attached(void *parent, FIDString type) SMTG_OVERRIDE;
    tresult PLUGIN_API removed() SMTG_OVERRIDE;
    tresult PLUGIN_API onSize(ViewRect *newSize) SMTG_OVERRIDE;

private:
    static float getParam(void *user, uint32_t id);
    static void setParam(void *user, uint32_t id, float value);
    static const char *modelName(void *user);
    void tick();

    Controller *controller = nullptr;
    NilampGui *gui = nullptr;
    NSTimer *timer = nil;
};

class Controller final : public EditController {
public:
    Controller() { nilamp_host_core_init(&core); }
    ~Controller() override { nilamp_host_core_deinit(&core); }

    tresult PLUGIN_API initialize(FUnknown *context) SMTG_OVERRIDE
    {
        const tresult result = EditController::initialize(context);
        if (result != kResultOk) {
            return result;
        }

        const NilampControlSpec *specs = nilamp_control_specs(NULL);
        for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
            const NilampControlSpec *spec = &specs[i];
            String128 title = {};
            String128 units = {};
            copyAsciiToTChar(title, spec->name);
            copyAsciiToTChar(units, spec->unit);
            const double normalizedDefault = plainToNormalized(spec, spec->default_value);
            const int32 stepCount = spec->display == NILAMP_CONTROL_DISPLAY_ENUM ?
                                        static_cast<int32>(spec->enum_count - 1u) :
                                        0;
            const int32 flags = ParameterInfo::kCanAutomate |
                                (spec->display == NILAMP_CONTROL_DISPLAY_ENUM ?
                                     ParameterInfo::kIsList :
                                     0);
            if (spec->display == NILAMP_CONTROL_DISPLAY_ENUM && spec->enum_names) {
                auto *param = new StringListParameter(title, spec->id, units, flags);
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
                                                 stepCount, flags);
                param->setNormalized(normalizedDefault);
                parameters.addParameter(param);
            }
        }
        return kResultOk;
    }

    tresult PLUGIN_API setComponentState(IBStream *state) SMTG_OVERRIDE
    {
        if (!state || !nilamp_host_load_state(&core, readStream, state)) {
            return kResultFalse;
        }
        syncParamsFromCore();
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
        const tresult result = EditController::setParamNormalized(tag, value);
        const NilampControlSpec *spec = nilamp_host_find_param(tag);
        if (result == kResultOk && spec) {
            (void)nilamp_host_core_set_param(&core, tag, normalizedToPlain(spec, value));
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
        (void)setParamNormalized(id, normalized);
        beginEdit(id);
        performEdit(id, normalized);
        endEdit(id);
    }

private:
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

    NilampHostCore core = {};
};

Editor::Editor(Controller *controllerIn) : CPluginView(nullptr), controller(controllerIn)
{
    if (controller) {
        controller->addRef();
    }
    rect = ViewRect(0, 0, 500, 340);
}

Editor::~Editor()
{
    if (timer) {
        [timer invalidate];
        timer = nil;
    }
    nilamp_gui_destroy(gui);
    gui = nullptr;
    if (controller) {
        controller->release();
    }
}

tresult PLUGIN_API Editor::isPlatformTypeSupported(FIDString type)
{
    return type && std::strcmp(type, kPlatformTypeNSView) == 0 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Editor::attached(void *parent, FIDString type)
{
    if (!parent || isPlatformTypeSupported(type) != kResultTrue || !controller) {
        return kResultFalse;
    }
    const NilampGuiCallbacks callbacks = {
        this,
        getParam,
        setParam,
        modelName,
    };
    gui = nilamp_gui_create(&callbacks, (const NilampGuiParamSpec *)nilamp_control_specs(NULL),
                            NILAMP_PARAM_COUNT, nilamp_model_gui_layout(NILAMP_MODEL_DEFAULT),
                            NILAMP_GUI_API_COCOA, false);
    const NilampGuiParent guiParent = {NILAMP_GUI_API_COCOA, (uintptr_t)parent};
    if (!gui ||
        !nilamp_gui_set_parent(gui, guiParent) ||
        !nilamp_gui_show(gui)) {
        nilamp_gui_destroy(gui);
        gui = nullptr;
        return kResultFalse;
    }
    systemWindow = parent;
    timer = [NSTimer scheduledTimerWithTimeInterval:0.033
                                            repeats:YES
                                              block:^(NSTimer *) {
                                                  this->tick();
                                              }];
    return kResultOk;
}

tresult PLUGIN_API Editor::removed()
{
    if (timer) {
        [timer invalidate];
        timer = nil;
    }
    if (gui) {
        (void)nilamp_gui_hide(gui);
        nilamp_gui_destroy(gui);
        gui = nullptr;
    }
    systemWindow = nullptr;
    return kResultOk;
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

float Editor::getParam(void *user, uint32_t id)
{
    Editor *self = static_cast<Editor *>(user);
    return self && self->controller ? self->controller->getRawParam(id) : 0.0f;
}

void Editor::setParam(void *user, uint32_t id, float value)
{
    Editor *self = static_cast<Editor *>(user);
    if (self && self->controller) {
        self->controller->setRawParamFromEditor(id, value);
    }
}

const char *Editor::modelName(void *user)
{
    (void)user;
    return nilamp_model_name(NILAMP_MODEL_DEFAULT);
}

void Editor::tick()
{
    if (gui) {
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
           NILAMP_VST3_VERSION,
           kVstVersionString,
           createProcessorInstance)
DEF_CLASS2(INLINE_UID(0x66e72a3a, 0x9187500d, 0xafa4d86a, 0x88935c65),
           PClassInfo::kManyInstances,
           kVstComponentControllerClass,
           NILAMP_VST3_NAME " Controller",
           0,
           "",
           NILAMP_VST3_VERSION,
           kVstVersionString,
           createControllerInstance)
END_FACTORY
