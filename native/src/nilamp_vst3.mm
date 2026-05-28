// SPDX-License-Identifier: MIT
#include "nilamp_cpu.h"
#include "nilamp_dsp.h"
#include "nilamp_gui.h"
#include "nilamp_host.h"
#include "nilamp_process_log.h"
#include "nilamp_vst3_keys.h"

#if !defined(__APPLE__)
#include "base/source/timer.h"
#endif
#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/keycodes.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstpresetkeys.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "public.sdk/source/common/pluginview.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#elif defined(__linux__)
#include "pluginterfaces/gui/iwaylandframe.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <poll.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#endif

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>

#ifndef NILAMP_VST3_NAME
#define NILAMP_VST3_NAME "nilamp TWD MKII"
#endif

#ifndef NILAMP_RELEASE_VERSION
#define NILAMP_RELEASE_VERSION "1.0.3"
#endif

namespace NilampVst3 {

using namespace Steinberg;
using namespace Steinberg::Vst;

static const FUID ProcessorUID(0xb0494b81, 0xb2385c29, 0x8c4d5734, 0x6d0b1222);
static const FUID ControllerUID(0x66e72a3a, 0x9187500d, 0xafa4d86a, 0x88935c65);
static constexpr uint32_t kEditorIdleTickThreshold = 8u;
static constexpr uint32_t kEditorFastPumpMs = 33u;
static constexpr uint32_t kEditorIdlePumpMs = 67u;
static constexpr std::array<CtrlNumber, 6> kFrontFaceMidiCcs = {
    kCtrlGPC1, kCtrlGPC2, kCtrlGPC3, kCtrlGPC4, kCtrlGPC5, kCtrlGPC6,
};

static void nilamp_vst3_log(const char *fmt, ...)
{
    const char *path = std::getenv("NILAMP_GUI_LOG");
    if (!path || !path[0]) {
        return;
    }

    FILE *fp = std::fopen(path, "a");
    if (!fp) {
        return;
    }

    std::fputs("[nilamp_vst3] ", fp);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(fp, fmt, args);
    va_end(args);
    std::fputc('\n', fp);
    std::fclose(fp);
}

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
        // We never read data.processContext. AudioEffect already inherits
        // IProcessContextRequirements with flags == 0 by default; assign
        // explicitly so the intent is visible at the call site and we are
        // robust against any future base-class default change.
        processContextRequirements = ProcessContextRequirements(0u);
        process_log = nilamp_process_log_create("vst3");
    }

    ~Processor() override
    {
        nilamp_process_log_destroy(process_log);
        process_log = nullptr;
        nilamp_host_core_deinit(&core);
    }

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
        // Guitar amp: mono in / mono out only. Accepting stereo here lets
        // REAPER call us with two distinct buffers carrying the same mono
        // content, which previously doubled our DSP cost. Refusing stereo
        // keeps the host's mono-on-stereo-track adapter on the host side.
        const int32 inputChannels = SpeakerArr::getChannelCount(inputs[0]);
        const int32 outputChannels = SpeakerArr::getChannelCount(outputs[0]);
        if (inputChannels != 1 || outputChannels != 1) {
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
        const uint64_t log_start_ns = nilamp_process_log_now(process_log);
        nilamp_cpu_enable_realtime_float_mode();
        handleParameterChanges(data.inputParameterChanges, data.numSamples);

        // Strict mono I/O — see setBusArrangements. Pick up channel 0 of the
        // single input/output bus when present; ignore any extra channels the
        // host happens to pass.
        NilampHostAudioBlock block = {};
        if (data.numInputs > 0 && data.inputs && data.inputs[0].channelBuffers32 &&
            data.inputs[0].numChannels > 0) {
            block.input = data.inputs[0].channelBuffers32[0];
            block.has_input = block.input != nullptr;
            block.input_is_constant = (data.inputs[0].silenceFlags & 1ull) != 0u;
        }
        if (data.numOutputs > 0 && data.outputs && data.outputs[0].channelBuffers32 &&
            data.outputs[0].numChannels > 0) {
            block.output = data.outputs[0].channelBuffers32[0];
        }

        // Coalesce parameter applies: stage values per event and call
        // nilamp_host_core_apply_params at most once per segment boundary.
        // Events at the same offset (common when REAPER echoes current
        // parameter values at block start) collapse to a single apply.
        uint32_t cursor = 0;
        bool params_dirty = false;
        for (uint32_t i = 0; i < eventCount; i++) {
            const uint32_t offset = events[i].offset < static_cast<uint32_t>(data.numSamples) ?
                                        events[i].offset :
                                        static_cast<uint32_t>(data.numSamples);
            if (offset > cursor) {
                if (params_dirty) {
                    nilamp_host_core_apply_params(&core);
                    params_dirty = false;
                }
                if (!nilamp_host_process_segment(&core, &block, cursor, offset)) {
                    nilamp_process_log_record(process_log, log_start_ns,
                                              static_cast<uint32_t>(data.numSamples));
                    return kResultFalse;
                }
                cursor = offset;
            }
            if (nilamp_host_core_stage_param(&core, events[i].id, events[i].plain)) {
                params_dirty = true;
            }
        }
        if (params_dirty) {
            nilamp_host_core_apply_params(&core);
        }
        if (!nilamp_host_process_segment(&core, &block, cursor,
                                         static_cast<uint32_t>(data.numSamples))) {
            nilamp_process_log_record(process_log, log_start_ns,
                                      static_cast<uint32_t>(data.numSamples));
            return kResultFalse;
        }
        nilamp_process_log_record(process_log, log_start_ns,
                                  static_cast<uint32_t>(data.numSamples));
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
    NilampProcessLog *process_log = nullptr;
};

class Controller;
class Editor;

#if defined(__linux__)
static bool nilamp_wayland_parent_matches_display(wl_surface *parent, wl_display *display)
{
#if defined(WAYLAND_VERSION_MAJOR) && \
    (WAYLAND_VERSION_MAJOR > 1 || (WAYLAND_VERSION_MAJOR == 1 && WAYLAND_VERSION_MINOR >= 23))
    return wl_proxy_get_display(reinterpret_cast<wl_proxy *>(parent)) == display;
#else
    (void)parent;
    (void)display;
    return true;
#endif
}

class WaylandGuiEditor final {
public:
    ~WaylandGuiEditor() { close(); }

    bool attach(IWaylandHost *hostIn,
                void *parentIn,
                const NilampGuiCallbacks &callbacksIn,
                int32 widthIn,
                int32 heightIn,
                double scaleIn)
    {
        if (!hostIn || !parentIn || widthIn <= 0 || heightIn <= 0) {
            return false;
        }

        host = hostIn;
        host->addRef();
        display = host->openWaylandConnection();
        if (!display) {
            close();
            return false;
        }

        parent = static_cast<wl_surface *>(parentIn);
        if (!nilamp_wayland_parent_matches_display(parent, display)) {
            close();
            return false;
        }
        callbacks = callbacksIn;
        callbacksReady = true;
        width = widthIn;
        height = heightIn;
        scale = scaleIn;
        registry = wl_display_get_registry(display);
        if (!registry) {
            close();
            return false;
        }

        wl_registry_add_listener(registry, &registryListener, this);
        wl_display_flush(display);
        nilamp_vst3_log("wayland gui attach pending %dx%d", width, height);
        return true;
    }

    bool resize(int32 widthIn, int32 heightIn)
    {
        if (widthIn <= 0 || heightIn <= 0) {
            return false;
        }
        width = widthIn;
        height = heightIn;
        if (eglWindow) {
            wl_egl_window_resize(eglWindow, width, height, 0, 0);
        }
        if (gui) {
            (void)nilamp_gui_set_size(gui, static_cast<uint32_t>(width),
                                      static_cast<uint32_t>(height));
        }
        if (!gui) {
            return true;
        }
        if (!render()) {
            return false;
        }
        wl_display_flush(display);
        return true;
    }

    void tick()
    {
        const bool hadGui = gui != nullptr;
        pumpWayland();
        if (gui) {
            nilamp_gui_on_main_thread(gui);
        }
        if (hadGui) {
            (void)render();
        }
        wl_display_flush(display);
    }

    void close()
    {
        if (gui) {
            if (eglDisplay != EGL_NO_DISPLAY) {
                eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
            }
            nilamp_gui_unrealize_external_gl(gui);
            nilamp_gui_destroy(gui);
            gui = nullptr;
        }
        if (eglDisplay != EGL_NO_DISPLAY) {
            eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        if (eglSurface != EGL_NO_SURFACE) {
            eglDestroySurface(eglDisplay, eglSurface);
            eglSurface = EGL_NO_SURFACE;
        }
        if (eglContext != EGL_NO_CONTEXT) {
            eglDestroyContext(eglDisplay, eglContext);
            eglContext = EGL_NO_CONTEXT;
        }
        if (eglDisplay != EGL_NO_DISPLAY) {
            eglTerminate(eglDisplay);
            eglDisplay = EGL_NO_DISPLAY;
        }
        if (eglWindow) {
            wl_egl_window_destroy(eglWindow);
            eglWindow = nullptr;
        }
        if (keyboard) {
            wl_keyboard_destroy(keyboard);
            keyboard = nullptr;
        }
        if (pointer) {
            wl_pointer_destroy(pointer);
            pointer = nullptr;
        }
        if (seat) {
            wl_seat_destroy(seat);
            seat = nullptr;
        }
        if (subsurface) {
            wl_subsurface_destroy(subsurface);
            subsurface = nullptr;
        }
        if (surface) {
            wl_surface_destroy(surface);
            surface = nullptr;
        }
        if (subcompositor) {
            wl_subcompositor_destroy(subcompositor);
            subcompositor = nullptr;
        }
        if (compositor) {
            wl_compositor_destroy(compositor);
            compositor = nullptr;
        }
        if (registry) {
            wl_registry_destroy(registry);
            registry = nullptr;
        }
        if (host && display) {
            (void)host->closeWaylandConnection(display);
            display = nullptr;
        }
        if (host) {
            host->release();
            host = nullptr;
        }
        parent = nullptr;
    }

private:
    enum {
        kLinuxBtnLeft = 0x110,
        kLinuxBtnRight = 0x111,
        kLinuxBtnMiddle = 0x112,
    };

    static void registryGlobal(void *data, wl_registry *registryIn, uint32 name,
                               const char *interface, uint32 version)
    {
        WaylandGuiEditor *self = static_cast<WaylandGuiEditor *>(data);
        if (!self || !interface) {
            return;
        }
        const uint32 bindVersion = version > 4 ? 4 : version;
        if (std::strcmp(interface, "wl_compositor") == 0) {
            self->compositor = static_cast<wl_compositor *>(
                wl_registry_bind(registryIn, name, &wl_compositor_interface, bindVersion));
        } else if (std::strcmp(interface, "wl_subcompositor") == 0) {
            self->subcompositor = static_cast<wl_subcompositor *>(
                wl_registry_bind(registryIn, name, &wl_subcompositor_interface, 1));
        } else if (std::strcmp(interface, "wl_seat") == 0) {
            self->seat = static_cast<wl_seat *>(
                wl_registry_bind(registryIn, name, &wl_seat_interface, version > 5 ? 5 : version));
            if (self->seat) {
                wl_seat_add_listener(self->seat, &seatListener, self);
            }
        }
    }

    static void registryGlobalRemove(void *, wl_registry *, uint32) {}

    static void seatCapabilities(void *data, wl_seat *, uint32_t capabilities)
    {
        WaylandGuiEditor *self = static_cast<WaylandGuiEditor *>(data);
        if (!self || !self->seat) {
            return;
        }
        if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !self->pointer) {
            self->pointer = wl_seat_get_pointer(self->seat);
            if (self->pointer) {
                wl_pointer_add_listener(self->pointer, &pointerListener, self);
            }
        } else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && self->pointer) {
            wl_pointer_destroy(self->pointer);
            self->pointer = nullptr;
        }
        if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !self->keyboard) {
            self->keyboard = wl_seat_get_keyboard(self->seat);
            if (self->keyboard) {
                wl_keyboard_add_listener(self->keyboard, &keyboardListener, self);
            }
        } else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && self->keyboard) {
            wl_keyboard_destroy(self->keyboard);
            self->keyboard = nullptr;
        }
    }

    static void seatName(void *, wl_seat *, const char *) {}

    static void pointerEnter(void *data, wl_pointer *, uint32_t, wl_surface *surfaceIn,
                             wl_fixed_t sx, wl_fixed_t sy)
    {
        WaylandGuiEditor *self = static_cast<WaylandGuiEditor *>(data);
        if (!self || surfaceIn != self->surface) {
            return;
        }
        self->pointerX = wl_fixed_to_int(sx);
        self->pointerY = wl_fixed_to_int(sy);
        if (self->gui) {
            (void)nilamp_gui_handle_pointer_motion(self->gui, self->pointerX, self->pointerY);
        }
    }

    static void pointerLeave(void *, wl_pointer *, uint32_t, wl_surface *) {}

    static void pointerMotion(void *data, wl_pointer *, uint32_t, wl_fixed_t sx, wl_fixed_t sy)
    {
        WaylandGuiEditor *self = static_cast<WaylandGuiEditor *>(data);
        if (!self || !self->gui) {
            return;
        }
        self->pointerX = wl_fixed_to_int(sx);
        self->pointerY = wl_fixed_to_int(sy);
        (void)nilamp_gui_handle_pointer_motion(self->gui, self->pointerX, self->pointerY);
    }

    static void pointerButton(void *data, wl_pointer *, uint32_t, uint32_t time,
                              uint32_t button, uint32_t state)
    {
        WaylandGuiEditor *self = static_cast<WaylandGuiEditor *>(data);
        if (!self || !self->gui) {
            return;
        }
        uint32_t guiButton = 3u;
        if (button == kLinuxBtnLeft) {
            guiButton = 0u;
        } else if (button == kLinuxBtnMiddle) {
            guiButton = 1u;
        } else if (button == kLinuxBtnRight) {
            guiButton = 2u;
        }
        if (guiButton >= 3u) {
            return;
        }
        (void)nilamp_gui_handle_pointer_button(self->gui, guiButton,
                                               state == WL_POINTER_BUTTON_STATE_PRESSED,
                                               self->pointerX, self->pointerY,
                                               (double)time / 1000.0);
    }

    static void pointerAxis(void *data, wl_pointer *, uint32_t, uint32_t axis,
                            wl_fixed_t value)
    {
        WaylandGuiEditor *self = static_cast<WaylandGuiEditor *>(data);
        if (!self || !self->gui) {
            return;
        }
        const float delta = -(float)wl_fixed_to_double(value) / 32.0f;
        if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
            (void)nilamp_gui_handle_pointer_scroll(self->gui, 0.0f, delta,
                                                   self->pointerX, self->pointerY);
        } else if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
            (void)nilamp_gui_handle_pointer_scroll(self->gui, delta, 0.0f,
                                                   self->pointerX, self->pointerY);
        }
    }

    static void pointerFrame(void *, wl_pointer *) {}
    static void pointerAxisSource(void *, wl_pointer *, uint32_t) {}
    static void pointerAxisStop(void *, wl_pointer *, uint32_t, uint32_t) {}
    static void pointerAxisDiscrete(void *, wl_pointer *, uint32_t, int32_t) {}
    static void pointerAxisValue120(void *, wl_pointer *, uint32_t, int32_t) {}
    static void pointerAxisRelativeDirection(void *, wl_pointer *, uint32_t, uint32_t) {}

    static void keyboardKeymap(void *, wl_keyboard *, uint32_t, int32_t fd, uint32_t)
    {
        if (fd >= 0) {
            (void)::close(fd);
        }
    }
    static void keyboardEnter(void *, wl_keyboard *, uint32_t, wl_surface *, wl_array *) {}
    static void keyboardLeave(void *, wl_keyboard *, uint32_t, wl_surface *) {}
    static void keyboardKey(void *data, wl_keyboard *, uint32_t, uint32_t, uint32_t key,
                            uint32_t state)
    {
        WaylandGuiEditor *self = static_cast<WaylandGuiEditor *>(data);
        if (!self || !self->gui) {
            return;
        }
        NilampGuiInputKey guiKey = NILAMP_GUI_INPUT_KEY_UNKNOWN;
        switch (key) {
        case 1:
            guiKey = NILAMP_GUI_INPUT_KEY_ESCAPE;
            break;
        case 14:
            guiKey = NILAMP_GUI_INPUT_KEY_BACKSPACE;
            break;
        case 15:
            guiKey = NILAMP_GUI_INPUT_KEY_TAB;
            break;
        case 28:
            guiKey = NILAMP_GUI_INPUT_KEY_ENTER;
            break;
        case 102:
            guiKey = NILAMP_GUI_INPUT_KEY_HOME;
            break;
        case 105:
            guiKey = NILAMP_GUI_INPUT_KEY_LEFT;
            break;
        case 106:
            guiKey = NILAMP_GUI_INPUT_KEY_RIGHT;
            break;
        case 107:
            guiKey = NILAMP_GUI_INPUT_KEY_END;
            break;
        case 108:
            guiKey = NILAMP_GUI_INPUT_KEY_DOWN;
            break;
        case 111:
            guiKey = NILAMP_GUI_INPUT_KEY_DELETE;
            break;
        case 103:
            guiKey = NILAMP_GUI_INPUT_KEY_UP;
            break;
        default:
            break;
        }
        if (guiKey != NILAMP_GUI_INPUT_KEY_UNKNOWN) {
            (void)nilamp_gui_handle_host_key(self->gui, guiKey,
                                             state == WL_KEYBOARD_KEY_STATE_PRESSED);
        }
    }
    static void keyboardModifiers(void *, wl_keyboard *, uint32_t, uint32_t, uint32_t,
                                  uint32_t, uint32_t) {}
    static void keyboardRepeatInfo(void *, wl_keyboard *, int32_t, int32_t) {}

    bool createSurface()
    {
        if (surface) {
            return true;
        }
        if (!compositor || !subcompositor || !parent) {
            nilamp_vst3_log("wayland gui missing globals compositor=%p subcompositor=%p parent=%p",
                            (void *)compositor, (void *)subcompositor, (void *)parent);
            return false;
        }
        surface = wl_compositor_create_surface(compositor);
        subsurface = surface ? wl_subcompositor_get_subsurface(subcompositor, surface, parent) :
                               nullptr;
        if (subsurface) {
            wl_subsurface_set_desync(subsurface);
            wl_subsurface_set_position(subsurface, 0, 0);
        }
        if (!surface || !subsurface) {
            nilamp_vst3_log("wayland gui create surface failed surface=%p subsurface=%p",
                            (void *)surface, (void *)subsurface);
            return false;
        }
        wl_surface_commit(surface);
        return true;
    }

    void tryCreate()
    {
        if (gui || initFailed || !callbacksReady || !compositor || !subcompositor) {
            return;
        }
        if (!createSurface() || !createEgl() || !createGui() || !render()) {
            initFailed = true;
            nilamp_vst3_log("wayland gui async create failed");
            return;
        }
        wl_display_flush(display);
        nilamp_vst3_log("wayland gui attached %dx%d", width, height);
    }

    bool createEgl()
    {
        eglWindow = wl_egl_window_create(surface, width, height);
        if (!eglWindow) {
            nilamp_vst3_log("wayland gui wl_egl_window_create failed");
            return false;
        }

        eglDisplay = eglGetDisplay((EGLNativeDisplayType)display);
        if (eglDisplay == EGL_NO_DISPLAY || !eglInitialize(eglDisplay, nullptr, nullptr) ||
            !eglBindAPI(EGL_OPENGL_API)) {
            nilamp_vst3_log("wayland gui egl init failed error=0x%x", eglGetError());
            return false;
        }

        const EGLint configAttrs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 0,
            EGL_STENCIL_SIZE, 0,
            EGL_NONE,
        };
        EGLint configCount = 0;
        if (!eglChooseConfig(eglDisplay, configAttrs, &eglConfig, 1, &configCount) ||
            configCount <= 0) {
            nilamp_vst3_log("wayland gui egl choose config failed error=0x%x", eglGetError());
            return false;
        }

        eglContext = createContext();
        if (eglContext == EGL_NO_CONTEXT) {
            nilamp_vst3_log("wayland gui egl context failed error=0x%x", eglGetError());
            return false;
        }

        eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig,
                                            (EGLNativeWindowType)eglWindow, nullptr);
        if (eglSurface == EGL_NO_SURFACE ||
            !eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
            nilamp_vst3_log("wayland gui egl surface/current failed error=0x%x", eglGetError());
            return false;
        }
        (void)eglSwapInterval(eglDisplay, 0);
        return true;
    }

    EGLContext createContext()
    {
        const EGLint coreAttrs[] = {
            EGL_CONTEXT_MAJOR_VERSION, 3,
            EGL_CONTEXT_MINOR_VERSION, 3,
#ifdef EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR
            EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
#endif
            EGL_NONE,
        };
        EGLContext context =
            eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, coreAttrs);
        if (context != EGL_NO_CONTEXT) {
            return context;
        }

        const EGLint versionAttrs[] = {
            EGL_CONTEXT_MAJOR_VERSION, 3,
            EGL_CONTEXT_MINOR_VERSION, 3,
            EGL_NONE,
        };
        context = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, versionAttrs);
        if (context != EGL_NO_CONTEXT) {
            return context;
        }
        return eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, nullptr);
    }

    bool createGui()
    {
        gui = nilamp_gui_create(&callbacks, (const NilampGuiParamSpec *)nilamp_control_specs(NULL),
                                NILAMP_PARAM_COUNT,
                                nilamp_model_gui_layout(NILAMP_MODEL_DEFAULT),
                                NILAMP_GUI_API_WAYLAND, false);
        if (!gui || !nilamp_gui_set_scale(gui, scale) ||
            !nilamp_gui_set_size(gui, static_cast<uint32_t>(width),
                                 static_cast<uint32_t>(height)) ||
            !nilamp_gui_realize_external_gl(gui)) {
            nilamp_vst3_log("wayland gui create nilamp gui failed");
            return false;
        }
        return true;
    }

    bool render()
    {
        if (!gui || eglDisplay == EGL_NO_DISPLAY || eglSurface == EGL_NO_SURFACE ||
            eglContext == EGL_NO_CONTEXT) {
            return false;
        }
        if (!eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
            nilamp_vst3_log("wayland gui egl make current failed error=0x%x", eglGetError());
            return false;
        }
        if (!nilamp_gui_render_external_gl(gui)) {
            return false;
        }
        if (!eglSwapBuffers(eglDisplay, eglSurface)) {
            nilamp_vst3_log("wayland gui egl swap failed error=0x%x", eglGetError());
            return false;
        }
        wl_surface_commit(surface);
        return true;
    }

    void pumpWayland()
    {
        if (!display) {
            return;
        }
        while (wl_display_prepare_read(display) != 0) {
            if (wl_display_dispatch_pending(display) < 0) {
                return;
            }
        }
        wl_display_flush(display);
        pollfd fd = {wl_display_get_fd(display), POLLIN, 0};
        if (poll(&fd, 1, 0) > 0 && (fd.revents & POLLIN)) {
            if (wl_display_read_events(display) < 0) {
                return;
            }
        } else {
            wl_display_cancel_read(display);
        }
        (void)wl_display_dispatch_pending(display);
        tryCreate();
    }

    static const wl_registry_listener registryListener;
    static const wl_seat_listener seatListener;
    static const wl_pointer_listener pointerListener;
    static const wl_keyboard_listener keyboardListener;

    IWaylandHost *host = nullptr;
    wl_display *display = nullptr;
    wl_surface *parent = nullptr;
    wl_registry *registry = nullptr;
    wl_compositor *compositor = nullptr;
    wl_subcompositor *subcompositor = nullptr;
    wl_seat *seat = nullptr;
    wl_pointer *pointer = nullptr;
    wl_keyboard *keyboard = nullptr;
    wl_surface *surface = nullptr;
    wl_subsurface *subsurface = nullptr;
    wl_egl_window *eglWindow = nullptr;
    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    EGLConfig eglConfig = nullptr;
    EGLContext eglContext = EGL_NO_CONTEXT;
    EGLSurface eglSurface = EGL_NO_SURFACE;
    NilampGui *gui = nullptr;
    NilampGuiCallbacks callbacks = {};
    int32 width = 1;
    int32 height = 1;
    int32 pointerX = 0;
    int32 pointerY = 0;
    double scale = 1.0;
    bool callbacksReady = false;
    bool initFailed = false;
};

const wl_registry_listener WaylandGuiEditor::registryListener = {
    WaylandGuiEditor::registryGlobal,
    WaylandGuiEditor::registryGlobalRemove,
};

const wl_seat_listener WaylandGuiEditor::seatListener = {
    WaylandGuiEditor::seatCapabilities,
    WaylandGuiEditor::seatName,
};

const wl_pointer_listener WaylandGuiEditor::pointerListener = {
    WaylandGuiEditor::pointerEnter,
    WaylandGuiEditor::pointerLeave,
    WaylandGuiEditor::pointerMotion,
    WaylandGuiEditor::pointerButton,
    WaylandGuiEditor::pointerAxis,
    WaylandGuiEditor::pointerFrame,
    WaylandGuiEditor::pointerAxisSource,
    WaylandGuiEditor::pointerAxisStop,
    WaylandGuiEditor::pointerAxisDiscrete,
    WaylandGuiEditor::pointerAxisValue120,
    WaylandGuiEditor::pointerAxisRelativeDirection,
};

const wl_keyboard_listener WaylandGuiEditor::keyboardListener = {
    WaylandGuiEditor::keyboardKeymap,
    WaylandGuiEditor::keyboardEnter,
    WaylandGuiEditor::keyboardLeave,
    WaylandGuiEditor::keyboardKey,
    WaylandGuiEditor::keyboardModifiers,
    WaylandGuiEditor::keyboardRepeatInfo,
};

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
    tresult PLUGIN_API onKeyDown(char16 key, int16 keyCode,
                                 int16 modifiers) SMTG_OVERRIDE;
    tresult PLUGIN_API onKeyUp(char16 key, int16 keyCode,
                               int16 modifiers) SMTG_OVERRIDE;
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
    void requestFastPump(const char *reason);
    bool shouldPumpThisTick();
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
    WaylandGuiEditor *waylandGui = nullptr;
    bool runLoopTimerRegistered = false;
#endif
    uint32_t idlePumpTicks = 0u;
    uint32_t idlePumpAccumMs = 0u;
    bool idlePumpMode = false;
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
    ~Controller() override
    {
#if defined(__linux__)
        if (hostApplication) {
            hostApplication->release();
            hostApplication = nullptr;
        }
#endif
        nilamp_host_core_deinit(&core);
    }

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
        if (hostApplication) {
            hostApplication->release();
            hostApplication = nullptr;
        }
        if (context &&
            context->queryInterface(IHostApplication::iid,
                                    reinterpret_cast<void **>(&hostApplication)) !=
                kResultOk) {
            hostApplication = nullptr;
        }
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
            String128 unitLabel = {};
            copyAsciiToTChar(title, spec->host_name ? spec->host_name : spec->name);
            copyAsciiToTChar(unitLabel, spec->unit);
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
                auto *param = new StringListParameter(title, spec->id, unitLabel, flags, unitId);
                for (uint32_t item = 0; item < spec->enum_count; item++) {
                    String128 itemName = {};
                    copyAsciiToTChar(itemName, spec->enum_names[item]);
                    param->appendString(itemName);
                }
                param->setNormalized(normalizedDefault);
                parameters.addParameter(param);
            } else {
                auto *param = new RangeParameter(title, spec->id, unitLabel, spec->min_value,
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
            // Controller's core has no engines; stage the value to keep
            // params/values mirrors in sync without walking the no-op
            // apply path that nilamp_host_core_set_param would trigger.
            (void)nilamp_host_core_stage_param(&core, tag,
                                               normalizedToPlain(spec, value));
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

    IWaylandHost *createWaylandHost() const
    {
        if (!hostApplication) {
            return nullptr;
        }
        IWaylandHost *waylandHost = nullptr;
        TUID iid = INLINE_UID(0x5E9582EE, 0x86594652, 0xB213678E, 0x7F1A705E);
        if (hostApplication->createInstance(iid, iid,
                                            reinterpret_cast<void **>(&waylandHost)) !=
            kResultOk) {
            return nullptr;
        }
        return waylandHost;
    }
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
    IHostApplication *hostApplication = nullptr;
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
#if defined(__linux__)
    delete waylandGui;
    waylandGui = nullptr;
#endif
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
    return type && (std::strcmp(type, kPlatformTypeX11EmbedWindowID) == 0 ||
                    std::strcmp(type, kPlatformTypeWaylandSurfaceID) == 0) ?
               kResultTrue :
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
#if defined(__linux__)
    if (std::strcmp(type, kPlatformTypeWaylandSurfaceID) == 0) {
        IWaylandHost *waylandHost = controller->createWaylandHost();
        if (!waylandHost) {
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
        WaylandGuiEditor *waylandEditor = new WaylandGuiEditor();
        if (!waylandEditor->attach(waylandHost, parent, callbacks, rect.getWidth(),
                                   rect.getHeight(), contentScale)) {
            waylandHost->release();
            delete waylandEditor;
            return kResultFalse;
        }
        waylandHost->release();
        waylandGui = waylandEditor;
        controller->registerEditor(this);
        systemWindow = parent;
        startTimer();
        return kResultOk;
    }
#endif
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
    nilamp_gui_on_main_thread(gui);
    startTimer();
    return kResultOk;
}

void Editor::startTimer()
{
    idlePumpTicks = 0u;
    idlePumpAccumMs = 0u;
    idlePumpMode = false;
#if defined(__APPLE__)
    if (timer) {
        return;
    }
    timer = [NSTimer scheduledTimerWithTimeInterval:(double)kEditorFastPumpMs / 1000.0
                                            repeats:YES
                                              block:^(NSTimer *) {
                                                  this->tick();
                                              }];
    nilamp_vst3_log("editor pump mode=fast reason=start");
#else
    if (timer) {
        return;
    }
    timer = Timer::create(this, static_cast<uint32>(kEditorFastPumpMs));
#if defined(__linux__)
    if (!timer && controller && !runLoopTimer) {
        if (!runLoop) {
            runLoop = controller->getRunLoop();
        }
        if (runLoop) {
            runLoopTimer = new LinuxEditorTimer(this);
            if (runLoop->registerTimer(runLoopTimer, kEditorFastPumpMs) == kResultTrue) {
                runLoopTimerRegistered = true;
            } else {
                runLoopTimer->detach();
                runLoopTimer->release();
                runLoopTimer = nullptr;
            }
        }
    }
#endif
    nilamp_vst3_log("editor pump mode=fast reason=start");
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
    idlePumpTicks = 0u;
    idlePumpAccumMs = 0u;
    idlePumpMode = false;
}

void Editor::requestFastPump(const char *reason)
{
    idlePumpTicks = 0u;
    idlePumpAccumMs = 0u;
    if (idlePumpMode) {
        idlePumpMode = false;
        nilamp_vst3_log("editor pump mode=fast reason=%s", reason ? reason : "(none)");
    }
}

bool Editor::shouldPumpThisTick()
{
    if (!gui) {
        return false;
    }
    if (nilamp_gui_wants_fast_pump(gui)) {
        requestFastPump("active");
        return true;
    }
    if (idlePumpTicks < kEditorIdleTickThreshold) {
        idlePumpTicks++;
        return true;
    }
    if (!idlePumpMode) {
        idlePumpMode = true;
        nilamp_vst3_log("editor pump mode=idle interval=%.3f", (double)kEditorIdlePumpMs / 1000.0);
    }
    idlePumpAccumMs += kEditorFastPumpMs;
    if (idlePumpAccumMs < kEditorIdlePumpMs) {
        return false;
    }
    idlePumpAccumMs = 0u;
    return true;
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
#if defined(__linux__)
    delete waylandGui;
    waylandGui = nullptr;
#endif
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

static bool nilamp_vst3_should_capture_key(const NilampGui *gui, int16 modifiers)
{
    (void)modifiers;
    return nilamp_gui_captures_keyboard(gui);
}

tresult PLUGIN_API Editor::onKeyDown(char16 key, int16 keyCode, int16 modifiers)
{
    if (!nilamp_vst3_should_capture_key(gui, modifiers)) {
        return kResultFalse;
    }

    bool handled = false;
    const NilampGuiInputKey guiKey = nilamp_vst3_key_code_to_gui_key(keyCode);
    if (guiKey != NILAMP_GUI_INPUT_KEY_UNKNOWN) {
        handled = nilamp_gui_handle_host_key(gui, guiKey, true);
    } else {
        const char16 textKey = nilamp_vst3_text_char(key, keyCode);
        if (nilamp_vst3_should_insert_text(textKey, modifiers)) {
            handled = nilamp_gui_handle_host_text(gui, static_cast<uint32_t>(textKey));
        } else if (nilamp_vst3_is_printable_ascii(textKey)) {
            handled = true;
        } else {
            handled = true;
        }
    }

    if (handled) {
        requestFastPump("key");
    }
    return handled ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Editor::onKeyUp(char16, int16 keyCode, int16 modifiers)
{
    if (!nilamp_vst3_should_capture_key(gui, modifiers)) {
        return kResultFalse;
    }

    const NilampGuiInputKey guiKey = nilamp_vst3_key_code_to_gui_key(keyCode);
    if (guiKey == NILAMP_GUI_INPUT_KEY_UNKNOWN) {
        return kResultTrue;
    }
    const bool handled = nilamp_gui_handle_host_key(gui, guiKey, false);
    if (handled) {
        requestFastPump("key");
    }
    return handled ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Editor::onSize(ViewRect *newSize)
{
    if (!newSize) {
        return kInvalidArgument;
    }
    rect = *newSize;
#if defined(__linux__)
    if (waylandGui) {
        return waylandGui->resize(rect.getWidth(), rect.getHeight()) ? kResultTrue :
                                                                       kResultFalse;
    }
#endif
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
    uint32_t preferredWidth = 500u;
    uint32_t preferredHeight = 340u;
    (void)nilamp_gui_preferred_size(layout, &preferredWidth, &preferredHeight);
    const int32 width =
        std::max<int32>(1, static_cast<int32>(std::lround(preferredWidth * contentScale)));
    const int32 height =
        std::max<int32>(1, static_cast<int32>(std::lround(preferredHeight * contentScale)));
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
    if (waylandGui) {
        waylandGui->tick();
        return;
    }
    if (shouldPumpThisTick()) {
        nilamp_gui_on_main_thread(gui);
    }
}

void Editor::hostParamsChanged()
{
    // Host UI-thread parameter pipeline: REAPER (and other VST3 hosts) call
    // IEditController::setParamNormalized per parameter update during
    // automation playback to keep the controller in sync with the processor.
    // Refreshing the GUI synchronously per call burns CPU; instead, request
    // the 30 Hz fast pump so the next Editor::tick() picks up the new
    // values within <=33 ms. Editor-originated edits skip this path via
    // editorEditActive in Controller::setParamNormalized.
    if (gui) {
        requestFastPump("host-param");
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
