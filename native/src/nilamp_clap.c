// SPDX-License-Identifier: MIT
#include "nilamp_dsp.h"
#include "nilamp_compat.h"
#include "nilamp_cpu.h"
#include "nilamp_gui.h"
#include "nilamp_process_log.h"

#include <clap/clap.h>
#include <clap/ext/gui.h>
#include <clap/ext/latency.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/param-indication.h>
#include <clap/ext/remote-controls.h>
#include <clap/ext/timer-support.h>
#include <clap/ext/state-context.h>
#include <clap/ext/tail.h>

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NILAMP_ENABLE_CLAP_GUI
#define NILAMP_ENABLE_CLAP_GUI 1
#endif

#ifndef NILAMP_CLAP_NAME
#define NILAMP_CLAP_NAME "nilamp"
#endif

#if NILAMP_ENABLE_CLAP_GUI
#if defined(__APPLE__)
#define NILAMP_CLAP_WINDOW_API CLAP_WINDOW_API_COCOA
#define NILAMP_GUI_NATIVE_API NILAMP_GUI_API_COCOA
#elif defined(_WIN32)
#define NILAMP_CLAP_WINDOW_API CLAP_WINDOW_API_WIN32
#define NILAMP_GUI_NATIVE_API NILAMP_GUI_API_WIN32
#else
#define NILAMP_CLAP_WINDOW_API CLAP_WINDOW_API_X11
#define NILAMP_GUI_NATIVE_API NILAMP_GUI_API_X11
#endif
#endif

#define NILAMP_PLUGIN_ID "dev.niltempus.nilamp"
#define NILAMP_STATE_MAGIC 0x4e4c4150u
#define NILAMP_STATE_VERSION 4u
#define NILAMP_STATE_VERSION_1 1u
#define NILAMP_STATE_VERSION_1_PARAM_COUNT 6u
#define NILAMP_STATE_VERSION_2 2u
#define NILAMP_STATE_VERSION_2_PARAM_COUNT 17u
#define NILAMP_STATE_VERSION_3 3u
#define NILAMP_STATE_VERSION_3_PARAM_COUNT 19u
#define NILAMP_CLAP_OUTPUT_LIMIT 1.0f
#define NILAMP_CLAP_PORT_CONFIG_MONO 1u
#define NILAMP_CLAP_MIDI_PORT_MAIN 0u
#define NILAMP_CLAP_REMOTE_PAGE_AMP_FACE 1u
#define NILAMP_MIDI_CC_GPC1 16u
#define NILAMP_MIDI_CC_GPC2 17u
#define NILAMP_MIDI_CC_GPC3 18u
#define NILAMP_MIDI_CC_GPC4 19u
#define NILAMP_MIDI_CC_SUSTAIN 64u
#define NILAMP_MIDI_CC_GPC5 80u
#define NILAMP_MIDI_CC_GPC6 81u
#define NILAMP_CLAP_INDICATION_LABEL_LEN 24u
#define NILAMP_GUI_FRAME_INTERVAL_SECONDS (1.0 / 30.0)
#define NILAMP_GUI_HOST_TIMER_MS 33u

#ifndef NILAMP_RELEASE_VERSION
#define NILAMP_RELEASE_VERSION "1.0.2"
#endif

typedef NilampControlSpec NilampParamSpec;
#define nilamp_param_specs (nilamp_control_specs(NULL))

typedef struct NilampClapParamIndication {
    bool has_mapping;
    bool has_mapping_color;
    NilampGuiIndicationColor mapping_color;
    char mapping_label[NILAMP_CLAP_INDICATION_LABEL_LEN];
    uint32_t automation_state;
    bool has_automation_color;
    NilampGuiIndicationColor automation_color;
} NilampClapParamIndication;

typedef struct NilampClap {
    clap_plugin_t plugin;
    const clap_host_t *host;
    const clap_host_params_t *host_params;
    const clap_host_gui_t *host_gui;
    const clap_host_timer_support_t *host_timer;
    NilampGui *gui;
    NilampEngine *engine;
    NilampParams params;
    _Atomic uint32_t param_bits[NILAMP_PARAM_COUNT];
    NilampClapParamIndication indications[NILAMP_PARAM_COUNT];
    atomic_uint gui_gesture_begin_mask;
    atomic_uint gui_dirty_mask;
    atomic_uint gui_gesture_end_mask;
    atomic_uint params_dirty;
    clap_id gui_timer_id;
    double sample_rate;
    bool gui_is_floating;
    bool gui_timer_registered;
    bool active;
    NilampProcessLog *process_log;
} NilampClap;

typedef struct NilampStateBlob {
    uint32_t magic;
    uint32_t version;
    float values[NILAMP_PARAM_COUNT];
} NilampStateBlob;

#if NILAMP_ENABLE_CLAP_GUI
#define nilamp_gui_param_specs ((const NilampGuiParamSpec *)nilamp_control_specs(NULL))

static void nilamp_gui_log(const char *fmt, ...)
{
    const char *path = getenv("NILAMP_GUI_LOG");
    if (!path || !path[0]) {
        return;
    }

    FILE *fp = fopen(path, "a");
    if (!fp) {
        return;
    }

    fputs("[nilamp_clap] ", fp);
    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);
    fputc('\n', fp);
    fclose(fp);
}

static bool nilamp_gui_supports_floating(void)
{
#if defined(__APPLE__)
    return true;
#else
    return false;
#endif
}

#if defined(_WIN32)
static bool nilamp_host_is_element(const clap_host_t *host)
{
    if (!host) {
        return false;
    }
    const bool name_matches =
        host->name && (nilamp_stricmp(host->name, "Element") == 0 ||
                       nilamp_stricmp(host->name, "Kushview Element") == 0);
    const bool vendor_matches =
        host->vendor && (nilamp_stricmp(host->vendor, "Kushview") == 0 ||
                         nilamp_stricmp(host->vendor, "Kushview, LLC") == 0);
    return name_matches || vendor_matches;
}
#endif

static bool nilamp_gui_api_supported(const NilampClap *plug, const char *api,
                                     bool is_floating)
{
#if !defined(_WIN32)
    (void)plug;
#endif
    if (api && strcmp(api, NILAMP_CLAP_WINDOW_API) == 0) {
        return !is_floating || nilamp_gui_supports_floating();
    }
#if defined(_WIN32)
    if (!is_floating && api && strcmp(api, CLAP_WINDOW_API_X11) == 0 &&
        nilamp_host_is_element(plug ? plug->host : NULL)) {
        return true;
    }
#endif
    return is_floating && nilamp_gui_supports_floating() && (!api || !api[0]);
}
#endif

static const char *const nilamp_features[] = {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_DISTORTION,
    CLAP_PLUGIN_FEATURE_MONO,
    CLAP_PLUGIN_FEATURE_STEREO,
    NULL,
};

static const clap_plugin_descriptor_t nilamp_descriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = NILAMP_PLUGIN_ID,
    .name = NILAMP_CLAP_NAME,
    .vendor = "niltempus",
    .url = "",
    .manual_url = "",
    .support_url = "",
    .version = NILAMP_RELEASE_VERSION,
    .description = "Native C guitar amp model",
    .features = nilamp_features,
};

static NilampClap *nilamp_from_plugin(const clap_plugin_t *plugin)
{
    return plugin ? (NilampClap *)plugin->plugin_data : NULL;
}

static uint32_t nilamp_float_to_bits(float value)
{
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float nilamp_bits_to_float(uint32_t bits)
{
    float value = 0.0f;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static double nilamp_clamp(double value, const NilampParamSpec *spec)
{
    if (!isfinite(value)) {
        return spec->default_value;
    }
    if (value < spec->min_value) {
        return spec->min_value;
    }
    if (value > spec->max_value) {
        return spec->max_value;
    }
    return value;
}

static const NilampParamSpec *nilamp_find_param(clap_id id)
{
    // Specs are emitted in NilampParamId order so id == index. O(1) fast
    // path on the audio thread; fall back to a scan only if the layout
    // ever changes.
    if (id < NILAMP_PARAM_COUNT && nilamp_param_specs[id].id == id) {
        return &nilamp_param_specs[id];
    }
    for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
        if (nilamp_param_specs[i].id == id) {
            return &nilamp_param_specs[i];
        }
    }
    return NULL;
}

static uint32_t nilamp_param_index(clap_id id)
{
    if (id < NILAMP_PARAM_COUNT && nilamp_param_specs[id].id == id) {
        return id;
    }
    for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
        if (nilamp_param_specs[i].id == id) {
            return i;
        }
    }
    return NILAMP_PARAM_COUNT;
}

static double nilamp_get_param_value(const NilampParams *params, clap_id id)
{
    switch ((NilampParamId)id) {
    case NILAMP_PARAM_GAIN_DB:
        return params->gain_db;
    case NILAMP_PARAM_VOLUME_PCT:
        return params->volume_pct;
    case NILAMP_PARAM_BASS_PCT:
        return params->bass_pct;
    case NILAMP_PARAM_MID_PCT:
        return params->mid_pct;
    case NILAMP_PARAM_TREBLE_PCT:
        return params->treble_pct;
    case NILAMP_PARAM_SAG_PCT:
        return params->sag_pct;
    case NILAMP_PARAM_OUTPUT_GAIN_DB:
        return params->output_gain_db;
    case NILAMP_PARAM_TONE_FMID_DBHZ:
        return params->tone_fmid_dbhz;
    case NILAMP_PARAM_TONE_QMID_DB:
        return params->tone_qmid_db;
    case NILAMP_PARAM_SPK_RES_GAIN1_DB:
        return params->spk_res_gain1_db;
    case NILAMP_PARAM_SPK_RES_GAIN2_DB:
        return params->spk_res_gain2_db;
    case NILAMP_PARAM_SPK_RES_FRES_DBHZ:
        return params->spk_res_fres_dbhz;
    case NILAMP_PARAM_SPK_RES_QTS_DB:
        return params->spk_res_qts_db;
    case NILAMP_PARAM_SPK_IND_GAIN1_DB:
        return params->spk_ind_gain1_db;
    case NILAMP_PARAM_SPK_IND_GAIN2_DB:
        return params->spk_ind_gain2_db;
    case NILAMP_PARAM_SPK_IND_FIND_DBHZ:
        return params->spk_ind_find_dbhz;
    case NILAMP_PARAM_GAIN_COMP:
        return params->gain_comp;
    case NILAMP_PARAM_TUBE1:
        return params->tube1;
    case NILAMP_PARAM_PHASE_SPLITTER:
        return params->phase_splitter;
    case NILAMP_PARAM_BYPASS:
        return params->bypass;
    case NILAMP_PARAM_COUNT:
    default:
        return 0.0;
    }
}

static bool nilamp_set_param_value(NilampParams *params, clap_id id, double value);

static void nilamp_load_params(const NilampClap *plug, NilampParams *params)
{
    if (!plug || !params) {
        return;
    }

    for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
        (void)nilamp_set_param_value(
            params, nilamp_param_specs[i].id,
            nilamp_bits_to_float(atomic_load_explicit(&plug->param_bits[i],
                                                      memory_order_acquire)));
    }
}

static void nilamp_store_params(NilampClap *plug, const NilampParams *params)
{
    if (!plug || !params) {
        return;
    }

    for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
        const float value = (float)nilamp_get_param_value(params, nilamp_param_specs[i].id);
        atomic_store_explicit(&plug->param_bits[i], nilamp_float_to_bits(value),
                              memory_order_release);
    }
}

static double nilamp_load_param_value(const NilampClap *plug, clap_id id)
{
    const uint32_t index = nilamp_param_index(id);
    if (!plug || index >= NILAMP_PARAM_COUNT) {
        return 0.0;
    }
    return nilamp_bits_to_float(
        atomic_load_explicit(&plug->param_bits[index], memory_order_acquire));
}

static bool nilamp_store_param_value(NilampClap *plug, clap_id id, double value,
                                     bool mark_gui_dirty)
{
    const NilampParamSpec *spec = nilamp_find_param(id);
    const uint32_t index = nilamp_param_index(id);
    if (!plug || !spec || index >= NILAMP_PARAM_COUNT) {
        return false;
    }

    const float clamped = (float)nilamp_clamp(value, spec);
    atomic_store_explicit(&plug->param_bits[index], nilamp_float_to_bits(clamped),
                          memory_order_release);
    if (nilamp_set_param_value(&plug->params, id, clamped)) {
        atomic_store_explicit(&plug->params_dirty, 1u, memory_order_release);
        if (mark_gui_dirty) {
            atomic_fetch_or_explicit(&plug->gui_dirty_mask, 1u << index,
                                     memory_order_release);
        }
    }
    return true;
}

static bool nilamp_set_param_value(NilampParams *params, clap_id id, double value)
{
    const NilampParamSpec *spec = nilamp_find_param(id);
    if (!spec) {
        return false;
    }

    const float clamped = (float)nilamp_clamp(value, spec);
    switch ((NilampParamId)id) {
    case NILAMP_PARAM_GAIN_DB:
        params->gain_db = clamped;
        return true;
    case NILAMP_PARAM_VOLUME_PCT:
        params->volume_pct = clamped;
        return true;
    case NILAMP_PARAM_BASS_PCT:
        params->bass_pct = clamped;
        return true;
    case NILAMP_PARAM_MID_PCT:
        params->mid_pct = clamped;
        return true;
    case NILAMP_PARAM_TREBLE_PCT:
        params->treble_pct = clamped;
        return true;
    case NILAMP_PARAM_SAG_PCT:
        params->sag_pct = clamped;
        return true;
    case NILAMP_PARAM_OUTPUT_GAIN_DB:
        params->output_gain_db = clamped;
        return true;
    case NILAMP_PARAM_TONE_FMID_DBHZ:
        params->tone_fmid_dbhz = clamped;
        return true;
    case NILAMP_PARAM_TONE_QMID_DB:
        params->tone_qmid_db = clamped;
        return true;
    case NILAMP_PARAM_SPK_RES_GAIN1_DB:
        params->spk_res_gain1_db = clamped;
        return true;
    case NILAMP_PARAM_SPK_RES_GAIN2_DB:
        params->spk_res_gain2_db = clamped;
        return true;
    case NILAMP_PARAM_SPK_RES_FRES_DBHZ:
        params->spk_res_fres_dbhz = clamped;
        return true;
    case NILAMP_PARAM_SPK_RES_QTS_DB:
        params->spk_res_qts_db = clamped;
        return true;
    case NILAMP_PARAM_SPK_IND_GAIN1_DB:
        params->spk_ind_gain1_db = clamped;
        return true;
    case NILAMP_PARAM_SPK_IND_GAIN2_DB:
        params->spk_ind_gain2_db = clamped;
        return true;
    case NILAMP_PARAM_SPK_IND_FIND_DBHZ:
        params->spk_ind_find_dbhz = clamped;
        return true;
    case NILAMP_PARAM_GAIN_COMP:
        params->gain_comp = clamped;
        return true;
    case NILAMP_PARAM_TUBE1:
        params->tube1 = clamped;
        return true;
    case NILAMP_PARAM_PHASE_SPLITTER:
        params->phase_splitter = clamped;
        return true;
    case NILAMP_PARAM_BYPASS:
        params->bypass = clamped;
        return true;
    case NILAMP_PARAM_COUNT:
    default:
        return false;
    }
}

static void nilamp_apply_params(NilampClap *plug)
{
    if (!plug || !plug->engine) {
        return;
    }
    NilampParams params = nilamp_default_params();
    nilamp_load_params(plug, &params);
    nilamp_engine_set_params(plug->engine, &params);
}

static void nilamp_apply_params_if_dirty(NilampClap *plug)
{
    if (!plug) {
        return;
    }
    if (atomic_exchange_explicit(&plug->params_dirty, 0u, memory_order_acquire) != 0u) {
        nilamp_apply_params(plug);
    }
}

#if NILAMP_ENABLE_CLAP_GUI
static float nilamp_gui_get_param_cb(void *user, uint32_t param_id)
{
    return (float)nilamp_load_param_value((const NilampClap *)user, param_id);
}

static bool nilamp_gui_needs_callback_fallback(const NilampClap *plug)
{
    return plug && plug->gui && nilamp_gui_is_visible(plug->gui) &&
           !plug->gui_timer_registered && plug->host && plug->host->request_callback;
}

static void nilamp_request_gui_callback(NilampClap *plug, const char *reason)
{
    if (!nilamp_gui_needs_callback_fallback(plug)) {
        return;
    }
    plug->host->request_callback(plug->host);
    nilamp_gui_log("request_callback fallback reason=%s", reason ? reason : "(none)");
}

static void nilamp_gui_set_param_cb(void *user, uint32_t param_id, float value)
{
    NilampClap *plug = (NilampClap *)user;
    if (!nilamp_store_param_value(plug, param_id, value, true)) {
        return;
    }

    if (plug->host_params && plug->host_params->request_flush) {
        plug->host_params->request_flush(plug->host);
    }
    if (plug->host && plug->host->request_process) {
        plug->host->request_process(plug->host);
    }
    nilamp_request_gui_callback(plug, "editor-param");
}

static void nilamp_gui_begin_param_gesture_cb(void *user, uint32_t param_id)
{
    NilampClap *plug = (NilampClap *)user;
    const uint32_t index = nilamp_param_index(param_id);
    if (!plug || index >= NILAMP_PARAM_COUNT) {
        return;
    }
    atomic_fetch_or_explicit(&plug->gui_gesture_begin_mask, 1u << index,
                             memory_order_release);
    if (plug->host_params && plug->host_params->request_flush) {
        plug->host_params->request_flush(plug->host);
    }
    if (plug->host && plug->host->request_process) {
        plug->host->request_process(plug->host);
    }
    nilamp_request_gui_callback(plug, "editor-param-gesture-begin");
}

static void nilamp_gui_end_param_gesture_cb(void *user, uint32_t param_id)
{
    NilampClap *plug = (NilampClap *)user;
    const uint32_t index = nilamp_param_index(param_id);
    if (!plug || index >= NILAMP_PARAM_COUNT) {
        return;
    }
    atomic_fetch_or_explicit(&plug->gui_gesture_end_mask, 1u << index,
                             memory_order_release);
    if (plug->host_params && plug->host_params->request_flush) {
        plug->host_params->request_flush(plug->host);
    }
    if (plug->host && plug->host->request_process) {
        plug->host->request_process(plug->host);
    }
    nilamp_request_gui_callback(plug, "editor-param-gesture-end");
}

static const char *nilamp_gui_model_name_cb(void *user)
{
    (void)user;
    return "Keller TWD DLX II";
}
#endif

static void nilamp_destroy_engine(NilampClap *plug)
{
    nilamp_engine_destroy(plug->engine);
    plug->engine = NULL;
}

static bool nilamp_create_engine(NilampClap *plug, double sample_rate)
{
    nilamp_destroy_engine(plug);

    plug->engine = nilamp_engine_create(sample_rate);
    if (!plug->engine) {
        return false;
    }

    plug->sample_rate = sample_rate;
    nilamp_apply_params(plug);
    atomic_store_explicit(&plug->params_dirty, 0u, memory_order_release);
    return true;
}

static void nilamp_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    (void)snprintf(dst, dst_size, "%s", src);
}

static bool nilamp_init(const clap_plugin_t *plugin)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug || !plug->host || !clap_version_is_compatible(plug->host->clap_version)) {
        return false;
    }

    if (plug->host->get_extension) {
        plug->host_params =
            (const clap_host_params_t *)plug->host->get_extension(plug->host, CLAP_EXT_PARAMS);
        plug->host_gui =
            (const clap_host_gui_t *)plug->host->get_extension(plug->host, CLAP_EXT_GUI);
        plug->host_timer = (const clap_host_timer_support_t *)plug->host->get_extension(
            plug->host, CLAP_EXT_TIMER_SUPPORT);
    }
    return true;
}

static void nilamp_unregister_gui_timer(NilampClap *plug)
{
#if NILAMP_ENABLE_CLAP_GUI
    if (!plug || !plug->gui_timer_registered || !plug->host_timer ||
        !plug->host_timer->unregister_timer) {
        return;
    }

    if (plug->host_timer->unregister_timer(plug->host, plug->gui_timer_id)) {
        plug->gui_timer_registered = false;
        plug->gui_timer_id = CLAP_INVALID_ID;
    }
#else
    (void)plug;
#endif
}

static void nilamp_destroy(const clap_plugin_t *plugin)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug) {
        return;
    }
    nilamp_unregister_gui_timer(plug);
#if NILAMP_ENABLE_CLAP_GUI
    nilamp_gui_destroy(plug->gui);
    plug->gui = NULL;
#endif
    nilamp_destroy_engine(plug);
    nilamp_process_log_destroy(plug->process_log);
    plug->process_log = NULL;
    free(plug);
}

static bool nilamp_activate(const clap_plugin_t *plugin, double sample_rate,
                            uint32_t min_frames_count, uint32_t max_frames_count)
{
    (void)min_frames_count;
    (void)max_frames_count;

    nilamp_cpu_enable_realtime_float_mode();
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug || !isfinite(sample_rate) || sample_rate <= 0.0) {
        return false;
    }

    if (!nilamp_create_engine(plug, sample_rate)) {
        return false;
    }
    plug->active = true;
    return true;
}

static void nilamp_deactivate(const clap_plugin_t *plugin)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug) {
        return;
    }
    plug->active = false;
    nilamp_destroy_engine(plug);
}

static bool nilamp_start_processing(const clap_plugin_t *plugin)
{
    nilamp_cpu_enable_realtime_float_mode();
    return nilamp_from_plugin(plugin) != NULL;
}

static void nilamp_stop_processing(const clap_plugin_t *plugin)
{
    (void)plugin;
}

static void nilamp_reset(const clap_plugin_t *plugin)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug) {
        return;
    }

    if (plug->engine) {
        nilamp_engine_reset(plug->engine);
    }
    nilamp_apply_params(plug);
    atomic_store_explicit(&plug->params_dirty, 0u, memory_order_release);
}

static void nilamp_zero_channel(float *output, uint32_t offset, uint32_t nframes)
{
    if (output && nframes > 0) {
        memset(output + offset, 0, sizeof(float) * nframes);
    }
}

static float nilamp_sanitize_host_sample(float sample)
{
    if (!isfinite(sample)) {
        return 0.0f;
    }
    if (sample != 0.0f && fabsf(sample) < FLT_MIN) {
        return 0.0f;
    }
    if (sample > NILAMP_CLAP_OUTPUT_LIMIT) {
        return NILAMP_CLAP_OUTPUT_LIMIT;
    }
    if (sample < -NILAMP_CLAP_OUTPUT_LIMIT) {
        return -NILAMP_CLAP_OUTPUT_LIMIT;
    }
    return sample;
}

static void nilamp_sanitize_host_channel(float *output, uint32_t offset, uint32_t nframes)
{
    if (!output) {
        return;
    }
    for (uint32_t i = 0; i < nframes; i++) {
        output[offset + i] = nilamp_sanitize_host_sample(output[offset + i]);
    }
}

static void nilamp_process_mono(NilampEngine *engine, const clap_audio_buffer_t *input,
                                clap_audio_buffer_t *output, uint32_t offset,
                                uint32_t nframes)
{
    float *out = output->data32[0];
    if (!out) {
        return;
    }
    if (!engine) {
        nilamp_zero_channel(out, offset, nframes);
        return;
    }

    const bool has_input = input && input->data32 && input->channel_count >= 1u &&
                           input->data32[0];
    const bool input_is_constant =
        has_input && ((input->constant_mask & UINT64_C(1)) != 0u);

    if (has_input && !input_is_constant) {
        nilamp_engine_process(engine, input->data32[0] + offset, out + offset, nframes);
        nilamp_sanitize_host_channel(out, offset, nframes);
        return;
    }

    const float zero = 0.0f;
    const float *sample = has_input ? input->data32[0] : &zero;
    for (uint32_t i = 0; i < nframes; i++) {
        nilamp_engine_process(engine, sample, out + offset + i, 1);
        out[offset + i] = nilamp_sanitize_host_sample(out[offset + i]);
    }
}

static void nilamp_passthrough_mono(const clap_audio_buffer_t *input,
                                    clap_audio_buffer_t *output,
                                    uint32_t offset, uint32_t nframes)
{
    if (!output || !output->data32 || output->channel_count == 0u ||
        !output->data32[0]) {
        return;
    }
    float *out = output->data32[0];
    const bool has_input = input && input->data32 && input->channel_count >= 1u &&
                           input->data32[0];
    if (!has_input) {
        nilamp_zero_channel(out, offset, nframes);
        return;
    }

    const float *in = input->data32[0];
    const bool input_is_constant = (input->constant_mask & UINT64_C(1)) != 0u;
    if (input_is_constant) {
        const float clamped = nilamp_sanitize_host_sample(in[0]);
        for (uint32_t i = 0; i < nframes; i++) {
            out[offset + i] = clamped;
        }
    } else if (out != in) {
        memcpy(out + offset, in + offset, sizeof(float) * nframes);
        nilamp_sanitize_host_channel(out, offset, nframes);
    }
}

static bool nilamp_process_segment(NilampClap *plug, const clap_process_t *process,
                                   uint32_t start, uint32_t end)
{
    if (end <= start) {
        return true;
    }
    if (!process || process->audio_outputs_count == 0 || !process->audio_outputs) {
        return false;
    }

    const clap_audio_buffer_t *input =
        (process->audio_inputs_count > 0 && process->audio_inputs) ? &process->audio_inputs[0] : NULL;
    clap_audio_buffer_t *output = &process->audio_outputs[0];
    if (!output->data32 || output->channel_count == 0u) {
        return false;
    }

    const uint32_t frames = end - start;

    if (nilamp_load_param_value(plug, NILAMP_PARAM_BYPASS) >= 0.5) {
        nilamp_passthrough_mono(input, output, start, frames);
        return true;
    }

    nilamp_process_mono(plug->engine, input, output, start, frames);
    return true;
}

static bool nilamp_midi_cc_to_param(uint8_t cc, clap_id *out_param_id)
{
    if (!out_param_id) {
        return false;
    }
    if (cc == NILAMP_MIDI_CC_SUSTAIN) {
        *out_param_id = NILAMP_PARAM_BYPASS;
        return true;
    }

    static const uint8_t front_face_ccs[] = {
        NILAMP_MIDI_CC_GPC1,
        NILAMP_MIDI_CC_GPC2,
        NILAMP_MIDI_CC_GPC3,
        NILAMP_MIDI_CC_GPC4,
        NILAMP_MIDI_CC_GPC5,
        NILAMP_MIDI_CC_GPC6,
    };

    const NilampGuiLayoutSpec *layout = nilamp_model_gui_layout(NILAMP_MODEL_DEFAULT);
    if (!layout) {
        return false;
    }
    uint32_t cc_index = 0u;
    for (uint32_t screen_index = 0; screen_index < layout->screen_count; screen_index++) {
        const NilampGuiScreenSpec *screen = &layout->screens[screen_index];
        if (screen->id != layout->default_screen) {
            continue;
        }
        for (uint32_t widget_index = 0; widget_index < screen->widget_count; widget_index++) {
            const NilampGuiWidgetSpec *widget = &screen->widgets[widget_index];
            if (widget->type != NILAMP_GUI_WIDGET_KNOB ||
                widget->param_id >= NILAMP_PARAM_COUNT) {
                continue;
            }
            if (cc_index >= sizeof(front_face_ccs) / sizeof(front_face_ccs[0])) {
                return false;
            }
            if (front_face_ccs[cc_index] == cc) {
                *out_param_id = widget->param_id;
                return true;
            }
            cc_index++;
        }
        return false;
    }
    return false;
}

static double nilamp_midi_cc_to_plain_value(clap_id param_id, uint8_t value)
{
    const NilampParamSpec *spec = nilamp_find_param(param_id);
    if (!spec) {
        return 0.0;
    }
    if (param_id == NILAMP_PARAM_BYPASS) {
        return value >= 64u ? 1.0 : 0.0;
    }
    const double normalized = (double)value / 127.0;
    return nilamp_clamp(spec->min_value + normalized * (spec->max_value - spec->min_value),
                        spec);
}

static void nilamp_push_midi_param_event(const clap_output_events_t *out,
                                         const clap_event_header_t *source_event,
                                         clap_id param_id, double value)
{
    if (!out || !out->try_push || !source_event) {
        return;
    }

    const clap_event_param_value_t event = {
        .header = {
            .size = sizeof(event),
            .time = source_event->time,
            .space_id = CLAP_CORE_EVENT_SPACE_ID,
            .type = CLAP_EVENT_PARAM_VALUE,
            .flags = CLAP_EVENT_DONT_RECORD |
                     (source_event->flags & CLAP_EVENT_IS_LIVE),
        },
        .param_id = param_id,
        .cookie = NULL,
        .note_id = -1,
        .port_index = -1,
        .channel = -1,
        .key = -1,
        .value = value,
    };
    (void)out->try_push(out, &event.header);
}

static void nilamp_handle_event(NilampClap *plug, const clap_event_header_t *event)
{
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID ||
        event->type != CLAP_EVENT_PARAM_VALUE ||
        event->size < sizeof(clap_event_param_value_t)) {
        return;
    }

    const clap_event_param_value_t *param_event = (const clap_event_param_value_t *)event;
    if (nilamp_store_param_value(plug, param_event->param_id, param_event->value, false)) {
        nilamp_apply_params_if_dirty(plug);
    }
}

static void nilamp_handle_process_event(NilampClap *plug, const clap_event_header_t *event,
                                        const clap_output_events_t *out)
{
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) {
        return;
    }
    if (event->type == CLAP_EVENT_PARAM_VALUE &&
        event->size >= sizeof(clap_event_param_value_t)) {
        nilamp_handle_event(plug, event);
        return;
    }
    if (event->type != CLAP_EVENT_MIDI || event->size < sizeof(clap_event_midi_t)) {
        return;
    }

    const clap_event_midi_t *midi = (const clap_event_midi_t *)event;
    if (midi->port_index != NILAMP_CLAP_MIDI_PORT_MAIN ||
        (midi->data[0] & 0xf0u) != 0xb0u) {
        return;
    }

    clap_id param_id = CLAP_INVALID_ID;
    if (!nilamp_midi_cc_to_param(midi->data[1], &param_id)) {
        return;
    }

    const double value = nilamp_midi_cc_to_plain_value(param_id, midi->data[2]);
    if (nilamp_store_param_value(plug, param_id, value, false)) {
        nilamp_apply_params_if_dirty(plug);
        nilamp_push_midi_param_event(out, event, param_id, value);
    }
}

static void nilamp_push_gui_param_events(NilampClap *plug, const clap_output_events_t *out);

static clap_process_status nilamp_process(const clap_plugin_t *plugin,
                                          const clap_process_t *process)
{
    nilamp_cpu_enable_realtime_float_mode();
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug || !process || process->frames_count == 0) {
        return CLAP_PROCESS_CONTINUE;
    }
    const uint64_t log_start_ns = nilamp_process_log_now(plug->process_log);
    nilamp_apply_params_if_dirty(plug);
    nilamp_push_gui_param_events(plug, process->out_events);

    uint32_t cursor = 0;
    const uint32_t event_count =
        process->in_events ? process->in_events->size(process->in_events) : 0u;

    for (uint32_t i = 0; i < event_count; i++) {
        const clap_event_header_t *event = process->in_events->get(process->in_events, i);
        const uint32_t event_time =
            event && event->time < process->frames_count ? event->time : process->frames_count;
        if (!nilamp_process_segment(plug, process, cursor, event_time)) {
            nilamp_process_log_record(plug->process_log, log_start_ns, process->frames_count);
            return CLAP_PROCESS_ERROR;
        }
        nilamp_handle_process_event(plug, event, process->out_events);
        cursor = event_time;
    }

    if (!nilamp_process_segment(plug, process, cursor, process->frames_count)) {
        nilamp_process_log_record(plug->process_log, log_start_ns, process->frames_count);
        return CLAP_PROCESS_ERROR;
    }
    nilamp_process_log_record(plug->process_log, log_start_ns, process->frames_count);
    return CLAP_PROCESS_CONTINUE;
}

static const void *nilamp_get_extension(const clap_plugin_t *plugin, const char *id);

static void nilamp_on_main_thread(const clap_plugin_t *plugin)
{
#if NILAMP_ENABLE_CLAP_GUI
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug || !plug->gui) {
        return;
    }
    nilamp_gui_log("on_main_thread");
    nilamp_gui_on_main_thread(plug->gui);
    nilamp_request_gui_callback(plug, "main-thread-pump");
#else
    (void)plugin;
#endif
}

static uint32_t nilamp_audio_ports_count(const clap_plugin_t *plugin, bool is_input)
{
    (void)plugin;
    (void)is_input;
    return 1;
}

static bool nilamp_audio_ports_get(const clap_plugin_t *plugin, uint32_t index,
                                   bool is_input, clap_audio_port_info_t *info)
{
    (void)plugin;
    if (index != 0 || !info) {
        return false;
    }
    // Guitar amp: mono in / mono out only. No stereo config exposed.
    info->id = is_input ? 0u : 1u;
    nilamp_copy_text(info->name, sizeof(info->name), is_input ? "Audio In" : "Audio Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 1u;
    info->port_type = CLAP_PORT_MONO;
    info->in_place_pair = is_input ? 1u : 0u;
    return true;
}

static const clap_plugin_audio_ports_t nilamp_audio_ports_ext = {
    .count = nilamp_audio_ports_count,
    .get = nilamp_audio_ports_get,
};

static uint32_t nilamp_note_ports_count(const clap_plugin_t *plugin, bool is_input)
{
    (void)plugin;
    return is_input ? 1u : 0u;
}

static bool nilamp_note_ports_get(const clap_plugin_t *plugin, uint32_t index,
                                  bool is_input, clap_note_port_info_t *info)
{
    (void)plugin;
    if (!is_input || index != 0u || !info) {
        return false;
    }

    memset(info, 0, sizeof(*info));
    info->id = NILAMP_CLAP_MIDI_PORT_MAIN;
    info->supported_dialects = CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    nilamp_copy_text(info->name, sizeof(info->name), "MIDI In");
    return true;
}

static const clap_plugin_note_ports_t nilamp_note_ports_ext = {
    .count = nilamp_note_ports_count,
    .get = nilamp_note_ports_get,
};

static uint32_t nilamp_params_count(const clap_plugin_t *plugin)
{
    (void)plugin;
    return NILAMP_PARAM_COUNT;
}

static bool nilamp_params_get_info(const clap_plugin_t *plugin, uint32_t param_index,
                                   clap_param_info_t *param_info)
{
    (void)plugin;
    if (!param_info || param_index >= NILAMP_PARAM_COUNT) {
        return false;
    }

    const NilampParamSpec *spec = &nilamp_param_specs[param_index];
    memset(param_info, 0, sizeof(*param_info));
    param_info->id = spec->id;
    param_info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS;
    if (spec->display == NILAMP_CONTROL_DISPLAY_ENUM) {
        param_info->flags |= CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
    }
    if (spec->id == NILAMP_PARAM_BYPASS) {
        param_info->flags |= CLAP_PARAM_IS_BYPASS | CLAP_PARAM_IS_STEPPED;
    }
    param_info->cookie = (void *)spec;
    nilamp_copy_text(param_info->name, sizeof(param_info->name),
                     spec->host_name ? spec->host_name : spec->name);
    nilamp_copy_text(param_info->module, sizeof(param_info->module), spec->module);
    param_info->min_value = spec->min_value;
    param_info->max_value = spec->max_value;
    param_info->default_value = spec->default_value;
    return true;
}

static bool nilamp_params_get_value(const clap_plugin_t *plugin, clap_id param_id,
                                    double *out_value)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug || !out_value || !nilamp_find_param(param_id)) {
        return false;
    }
    *out_value = nilamp_load_param_value(plug, param_id);
    return true;
}

static bool nilamp_params_value_to_text(const clap_plugin_t *plugin, clap_id param_id,
                                        double value, char *out_buffer,
                                        uint32_t out_buffer_capacity)
{
    (void)plugin;
    const NilampParamSpec *spec = nilamp_find_param(param_id);
    if (!spec || !out_buffer || out_buffer_capacity == 0) {
        return false;
    }

    const double clamped = nilamp_clamp(value, spec);
    if (spec->display == NILAMP_CONTROL_DISPLAY_ENUM && spec->enum_names &&
        spec->enum_count > 0u) {
        const int index = (int)lround(clamped);
        const char *name = (index >= 0 && (uint32_t)index < spec->enum_count) ?
                               spec->enum_names[index] :
                               spec->enum_names[(uint32_t)lround(spec->default_value)];
        const int written = snprintf(out_buffer, out_buffer_capacity, "%s", name);
        return written >= 0 && (uint32_t)written < out_buffer_capacity;
    }
    const double display = nilamp_control_display_value(spec, (float)clamped);
    const int written = snprintf(out_buffer, out_buffer_capacity, "%.3g %s", display, spec->unit);
    return written >= 0 && (uint32_t)written < out_buffer_capacity;
}

static bool nilamp_params_text_to_value(const clap_plugin_t *plugin, clap_id param_id,
                                        const char *param_value_text, double *out_value)
{
    (void)plugin;
    const NilampParamSpec *spec = nilamp_find_param(param_id);
    if (!spec || !param_value_text || !out_value) {
        return false;
    }

    if (spec->display == NILAMP_CONTROL_DISPLAY_ENUM && spec->enum_names &&
        spec->enum_count > 0u) {
        for (uint32_t i = 0; i < spec->enum_count; i++) {
            if (nilamp_stricmp(param_value_text, spec->enum_names[i]) == 0) {
                *out_value = (double)i;
                return true;
            }
        }
        if (spec->id == NILAMP_PARAM_GAIN_COMP && nilamp_stricmp(param_value_text, "tube1") == 0) {
            *out_value = 1.0;
            return true;
        }
    }

    errno = 0;
    char *end = NULL;
    double parsed = strtod(param_value_text, &end);
    if (end == param_value_text || errno == ERANGE || !isfinite(parsed)) {
        return false;
    }
    if (spec->display == NILAMP_CONTROL_DISPLAY_ISO266) {
        if (parsed <= 0.0) {
            return false;
        }
        parsed = 20.0 * log10(parsed);
    }
    *out_value = nilamp_clamp(parsed, spec);
    return true;
}

static bool nilamp_push_gui_gesture_events(NilampClap *plug,
                                           const clap_output_events_t *out,
                                           atomic_uint *mask_source,
                                           uint16_t event_type)
{
    if (!plug || !out || !out->try_push) {
        return false;
    }

    const uint32_t mask = atomic_exchange_explicit(mask_source, 0u,
                                                   memory_order_acquire);
    for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
        if ((mask & (1u << i)) == 0u) {
            continue;
        }
        const clap_event_param_gesture_t event = {
            .header = {
                .size = sizeof(event),
                .time = 0,
                .space_id = CLAP_CORE_EVENT_SPACE_ID,
                .type = event_type,
                .flags = CLAP_EVENT_IS_LIVE,
            },
            .param_id = nilamp_param_specs[i].id,
        };
        if (!out->try_push(out, &event.header)) {
            atomic_fetch_or_explicit(mask_source, mask & ~((1u << i) - 1u),
                                     memory_order_release);
            return false;
        }
    }
    return true;
}

static bool nilamp_push_gui_value_events(NilampClap *plug, const clap_output_events_t *out)
{
    if (!plug || !out || !out->try_push) {
        return false;
    }

    const uint32_t mask = atomic_exchange_explicit(&plug->gui_dirty_mask, 0u,
                                                   memory_order_acquire);
    for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
        if ((mask & (1u << i)) == 0u) {
            continue;
        }
        const clap_event_param_value_t event = {
            .header = {
                .size = sizeof(event),
                .time = 0,
                .space_id = CLAP_CORE_EVENT_SPACE_ID,
                .type = CLAP_EVENT_PARAM_VALUE,
                .flags = CLAP_EVENT_IS_LIVE,
            },
            .param_id = nilamp_param_specs[i].id,
            .cookie = NULL,
            .note_id = -1,
            .port_index = -1,
            .channel = -1,
            .key = -1,
            .value = nilamp_load_param_value(plug, nilamp_param_specs[i].id),
        };
        if (!out->try_push(out, &event.header)) {
            atomic_fetch_or_explicit(&plug->gui_dirty_mask, mask & ~((1u << i) - 1u),
                                     memory_order_release);
            return false;
        }
    }
    return true;
}

static void nilamp_push_gui_param_events(NilampClap *plug, const clap_output_events_t *out)
{
    if (!nilamp_push_gui_gesture_events(plug, out, &plug->gui_gesture_begin_mask,
                                        CLAP_EVENT_PARAM_GESTURE_BEGIN)) {
        return;
    }
    if (!nilamp_push_gui_value_events(plug, out)) {
        return;
    }
    (void)nilamp_push_gui_gesture_events(plug, out, &plug->gui_gesture_end_mask,
                                         CLAP_EVENT_PARAM_GESTURE_END);
}

static void nilamp_params_flush(const clap_plugin_t *plugin,
                                const clap_input_events_t *in,
                                const clap_output_events_t *out)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug) {
        return;
    }

    if (in) {
        const uint32_t event_count = in->size(in);
        for (uint32_t i = 0; i < event_count; i++) {
            nilamp_handle_event(plug, in->get(in, i));
        }
#if NILAMP_ENABLE_CLAP_GUI
        if (event_count > 0 && !plug->active && plug->gui &&
            nilamp_gui_is_visible(plug->gui)) {
            nilamp_gui_log("inactive flush host-param-events count=%u", event_count);
            nilamp_gui_refresh(plug->gui);
            nilamp_request_gui_callback(plug, "inactive-flush");
        }
#endif
    }
    nilamp_push_gui_param_events(plug, out);
}

static const clap_plugin_params_t nilamp_params_ext = {
    .count = nilamp_params_count,
    .get_info = nilamp_params_get_info,
    .get_value = nilamp_params_get_value,
    .value_to_text = nilamp_params_value_to_text,
    .text_to_value = nilamp_params_text_to_value,
    .flush = nilamp_params_flush,
};

static bool nilamp_write_all(const clap_ostream_t *stream, const void *data, uint64_t size)
{
    const uint8_t *cursor = (const uint8_t *)data;
    while (size > 0) {
        const int64_t written = stream->write(stream, cursor, size);
        if (written <= 0) {
            return false;
        }
        cursor += (uint64_t)written;
        size -= (uint64_t)written;
    }
    return true;
}

static bool nilamp_read_all(const clap_istream_t *stream, void *data, uint64_t size)
{
    uint8_t *cursor = (uint8_t *)data;
    while (size > 0) {
        const int64_t bytes_read = stream->read(stream, cursor, size);
        if (bytes_read <= 0) {
            return false;
        }
        cursor += (uint64_t)bytes_read;
        size -= (uint64_t)bytes_read;
    }
    return true;
}

static void nilamp_params_to_values(const NilampParams *params,
                                    float values[NILAMP_PARAM_COUNT])
{
    for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
        values[i] = (float)nilamp_get_param_value(params, nilamp_param_specs[i].id);
    }
}

static bool nilamp_values_to_params(const float values[NILAMP_PARAM_COUNT],
                                    uint32_t value_count,
                                    NilampParams *params)
{
    if (value_count > NILAMP_PARAM_COUNT) {
        return false;
    }
    for (uint32_t i = 0; i < value_count; i++) {
        if (!nilamp_set_param_value(params, nilamp_param_specs[i].id, values[i])) {
            return false;
        }
    }
    return true;
}

static bool nilamp_state_save(const clap_plugin_t *plugin, const clap_ostream_t *stream)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug || !stream || !stream->write) {
        return false;
    }

    NilampStateBlob blob = {
        .magic = NILAMP_STATE_MAGIC,
        .version = NILAMP_STATE_VERSION,
    };
    NilampParams params = nilamp_default_params();
    nilamp_load_params(plug, &params);
    nilamp_params_to_values(&params, blob.values);
    return nilamp_write_all(stream, &blob, sizeof(blob));
}

static bool nilamp_state_load(const clap_plugin_t *plugin, const clap_istream_t *stream)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug || !stream || !stream->read) {
        return false;
    }

    struct {
        uint32_t magic;
        uint32_t version;
    } header = {0};
    NilampParams params = nilamp_default_params();
    float values[NILAMP_PARAM_COUNT] = {0};
    nilamp_params_to_values(&params, values);

    if (!nilamp_read_all(stream, &header, sizeof(header)) ||
        header.magic != NILAMP_STATE_MAGIC) {
        return false;
    }

    uint32_t value_count = 0u;
    if (header.version == NILAMP_STATE_VERSION_1) {
        value_count = NILAMP_STATE_VERSION_1_PARAM_COUNT;
    } else if (header.version == NILAMP_STATE_VERSION_2) {
        value_count = NILAMP_STATE_VERSION_2_PARAM_COUNT;
    } else if (header.version == NILAMP_STATE_VERSION_3) {
        value_count = NILAMP_STATE_VERSION_3_PARAM_COUNT;
    } else if (header.version == NILAMP_STATE_VERSION) {
        value_count = NILAMP_PARAM_COUNT;
    } else {
        return false;
    }

    if (!nilamp_read_all(stream, values, sizeof(values[0]) * value_count) ||
        !nilamp_values_to_params(values, value_count, &params)) {
        return false;
    }
    if (header.version == NILAMP_STATE_VERSION_1 ||
        header.version == NILAMP_STATE_VERSION_2) {
        (void)nilamp_set_param_value(&params, NILAMP_PARAM_TUBE1, 1.0);
        (void)nilamp_set_param_value(&params, NILAMP_PARAM_PHASE_SPLITTER, 0.0);
    }
    plug->params = params;
    nilamp_store_params(plug, &params);
    nilamp_apply_params(plug);
    atomic_store_explicit(&plug->params_dirty, 0u, memory_order_release);
    if (plug->host_params && plug->host_params->rescan) {
        plug->host_params->rescan(plug->host, CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);
    }
#if NILAMP_ENABLE_CLAP_GUI
    if (plug->gui && nilamp_gui_is_visible(plug->gui)) {
        nilamp_gui_log("state load gui refresh");
        nilamp_gui_refresh(plug->gui);
        nilamp_request_gui_callback(plug, "state-load");
    }
#endif
    return true;
}

static const clap_plugin_state_t nilamp_state_ext = {
    .save = nilamp_state_save,
    .load = nilamp_state_load,
};

static bool nilamp_state_context_valid(uint32_t context_type)
{
    return context_type == CLAP_STATE_CONTEXT_FOR_PRESET ||
           context_type == CLAP_STATE_CONTEXT_FOR_DUPLICATE ||
           context_type == CLAP_STATE_CONTEXT_FOR_PROJECT;
}

static bool nilamp_state_context_save(const clap_plugin_t *plugin,
                                      const clap_ostream_t *stream,
                                      uint32_t context_type)
{
    if (!nilamp_state_context_valid(context_type)) {
        return false;
    }
    return nilamp_state_save(plugin, stream);
}

static bool nilamp_state_context_load(const clap_plugin_t *plugin,
                                      const clap_istream_t *stream,
                                      uint32_t context_type)
{
    if (!nilamp_state_context_valid(context_type)) {
        return false;
    }
    return nilamp_state_load(plugin, stream);
}

static const clap_plugin_state_context_t nilamp_state_context_ext = {
    .save = nilamp_state_context_save,
    .load = nilamp_state_context_load,
};

static uint32_t nilamp_latency_get(const clap_plugin_t *plugin)
{
    (void)plugin;
    return 0u;
}

static const clap_plugin_latency_t nilamp_latency_ext = {
    .get = nilamp_latency_get,
};

static uint32_t nilamp_tail_get(const clap_plugin_t *plugin)
{
    (void)plugin;
    return 0u;
}

static const clap_plugin_tail_t nilamp_tail_ext = {
    .get = nilamp_tail_get,
};

static uint32_t nilamp_remote_controls_count(const clap_plugin_t *plugin)
{
    (void)plugin;
    return 1u;
}

static bool nilamp_remote_controls_get(const clap_plugin_t *plugin, uint32_t page_index,
                                       clap_remote_controls_page_t *page)
{
    (void)plugin;
    if (!page || page_index != 0u) {
        return false;
    }

    memset(page, 0, sizeof(*page));
    nilamp_copy_text(page->section_name, sizeof(page->section_name), "Main");
    page->page_id = NILAMP_CLAP_REMOTE_PAGE_AMP_FACE;
    nilamp_copy_text(page->page_name, sizeof(page->page_name), "Amp Face");
    page->is_for_preset = false;
    for (uint32_t i = 0; i < CLAP_REMOTE_CONTROLS_COUNT; i++) {
        page->param_ids[i] = CLAP_INVALID_ID;
    }

    page->param_ids[0] = NILAMP_PARAM_BYPASS;
    uint32_t slot = 1u;
    const NilampGuiLayoutSpec *layout = nilamp_model_gui_layout(NILAMP_MODEL_DEFAULT);
    if (!layout) {
        return true;
    }

    const NilampGuiScreenSpec *default_screen = NULL;
    for (uint32_t i = 0; i < layout->screen_count; i++) {
        if (layout->screens[i].id == layout->default_screen) {
            default_screen = &layout->screens[i];
            break;
        }
    }
    if (!default_screen) {
        return true;
    }

    for (uint32_t i = 0; i < default_screen->widget_count; i++) {
        const NilampGuiWidgetSpec *widget = &default_screen->widgets[i];
        if (widget->type != NILAMP_GUI_WIDGET_KNOB || widget->param_id >= NILAMP_PARAM_COUNT) {
            continue;
        }
        if (slot >= CLAP_REMOTE_CONTROLS_COUNT) {
            break;
        }
        page->param_ids[slot++] = widget->param_id;
    }
    return true;
}

static const clap_plugin_remote_controls_t nilamp_remote_controls_ext = {
    .count = nilamp_remote_controls_count,
    .get = nilamp_remote_controls_get,
};

#if NILAMP_ENABLE_CLAP_GUI
static NilampGuiIndicationColor nilamp_gui_indication_color_from_clap(
    const clap_color_t *color)
{
    if (!color) {
        return (NilampGuiIndicationColor){0};
    }
    return (NilampGuiIndicationColor){
        .alpha = color->alpha,
        .red = color->red,
        .green = color->green,
        .blue = color->blue,
    };
}

static void nilamp_apply_param_indication_to_gui(NilampClap *plug, uint32_t index)
{
    if (!plug || !plug->gui || index >= NILAMP_PARAM_COUNT) {
        return;
    }

    const clap_id param_id = nilamp_param_specs[index].id;
    const NilampClapParamIndication *indication = &plug->indications[index];
    nilamp_gui_set_param_mapping_indication(
        plug->gui, param_id, indication->has_mapping,
        indication->has_mapping_color ? &indication->mapping_color : NULL,
        indication->mapping_label);
    nilamp_gui_set_param_automation_indication(
        plug->gui, param_id, indication->automation_state,
        indication->has_automation_color ? &indication->automation_color : NULL);
}

static void nilamp_apply_param_indications_to_gui(NilampClap *plug)
{
    if (!plug || !plug->gui) {
        return;
    }
    for (uint32_t i = 0; i < NILAMP_PARAM_COUNT; i++) {
        nilamp_apply_param_indication_to_gui(plug, i);
    }
}

static void nilamp_param_indication_set_mapping(const clap_plugin_t *plugin,
                                                clap_id param_id,
                                                bool has_mapping,
                                                const clap_color_t *color,
                                                const char *label,
                                                const char *description)
{
    (void)description;
    NilampClap *plug = nilamp_from_plugin(plugin);
    const uint32_t index = nilamp_param_index(param_id);
    if (!plug || index >= NILAMP_PARAM_COUNT) {
        return;
    }

    NilampClapParamIndication *indication = &plug->indications[index];
    indication->has_mapping = has_mapping;
    indication->has_mapping_color = color != NULL;
    indication->mapping_color = nilamp_gui_indication_color_from_clap(color);
    indication->mapping_label[0] = '\0';
    if (has_mapping && label) {
        nilamp_copy_text(indication->mapping_label, sizeof(indication->mapping_label),
                         label);
    }
    nilamp_apply_param_indication_to_gui(plug, index);
}

static void nilamp_param_indication_set_automation(const clap_plugin_t *plugin,
                                                   clap_id param_id,
                                                   uint32_t automation_state,
                                                   const clap_color_t *color)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    const uint32_t index = nilamp_param_index(param_id);
    if (!plug || index >= NILAMP_PARAM_COUNT ||
        automation_state > CLAP_PARAM_INDICATION_AUTOMATION_OVERRIDING) {
        return;
    }

    NilampClapParamIndication *indication = &plug->indications[index];
    indication->automation_state = automation_state;
    indication->has_automation_color = color != NULL &&
                                       automation_state != CLAP_PARAM_INDICATION_AUTOMATION_NONE;
    indication->automation_color = nilamp_gui_indication_color_from_clap(color);
    nilamp_apply_param_indication_to_gui(plug, index);
}

static const clap_plugin_param_indication_t nilamp_param_indication_ext = {
    .set_mapping = nilamp_param_indication_set_mapping,
    .set_automation = nilamp_param_indication_set_automation,
};

static bool nilamp_gui_is_api_supported(const clap_plugin_t *plugin, const char *api,
                                        bool is_floating)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    const bool ok = nilamp_gui_api_supported(plug, api, is_floating);
    nilamp_gui_log("is_api_supported api=%s floating=%d -> %d",
                   api ? api : "(null)", is_floating ? 1 : 0, ok ? 1 : 0);
    return ok;
}

static bool nilamp_gui_get_preferred_api(const clap_plugin_t *plugin, const char **api,
                                         bool *is_floating)
{
    (void)plugin;
    if (!api || !is_floating) {
        return false;
    }
    *api = NILAMP_CLAP_WINDOW_API;
    *is_floating = false;
    nilamp_gui_log("get_preferred_api -> api=%s floating=0", NILAMP_CLAP_WINDOW_API);
    return true;
}

static bool nilamp_gui_create_ext(const clap_plugin_t *plugin, const char *api,
                                  bool is_floating)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug || plug->gui || !nilamp_gui_api_supported(plug, api, is_floating)) {
        nilamp_gui_log("create rejected api=%s floating=%d plug=%p existing_gui=%p",
                       api ? api : "(null)", is_floating ? 1 : 0, (void *)plug,
                       plug ? (void *)plug->gui : NULL);
        return false;
    }

    const NilampGuiCallbacks callbacks = {
        .user = plug,
        .get_param = nilamp_gui_get_param_cb,
        .begin_param_gesture = nilamp_gui_begin_param_gesture_cb,
        .set_param = nilamp_gui_set_param_cb,
        .end_param_gesture = nilamp_gui_end_param_gesture_cb,
        .model_name = nilamp_gui_model_name_cb,
    };
    plug->gui = nilamp_gui_create(&callbacks, nilamp_gui_param_specs, NILAMP_PARAM_COUNT,
                                  nilamp_model_gui_layout(NILAMP_MODEL_DEFAULT),
                                  NILAMP_GUI_NATIVE_API, is_floating);
    const bool ok = plug->gui != NULL;
    plug->gui_is_floating = ok && is_floating;
    if (ok) {
        nilamp_apply_param_indications_to_gui(plug);
    }
    nilamp_gui_log("create api=%s floating=%d -> %d", api ? api : "(null)",
                   is_floating ? 1 : 0, ok ? 1 : 0);
    return ok;
}

static void nilamp_gui_destroy_ext(const clap_plugin_t *plugin)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug) {
        return;
    }
    nilamp_gui_stop_frame_timer(plug->gui);
    nilamp_unregister_gui_timer(plug);
    nilamp_gui_destroy(plug->gui);
    plug->gui = NULL;
    plug->gui_is_floating = false;
    nilamp_gui_log("destroy");
}

static bool nilamp_gui_set_scale_ext(const clap_plugin_t *plugin, double scale)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    const bool ok = plug && plug->gui && nilamp_gui_set_scale(plug->gui, scale);
    nilamp_gui_log("set_scale scale=%.3f -> %d", scale, ok ? 1 : 0);
    return ok;
}

static bool nilamp_gui_get_size_ext(const clap_plugin_t *plugin, uint32_t *width,
                                    uint32_t *height)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    const bool ok = plug && plug->gui && nilamp_gui_get_size(plug->gui, width, height);
    nilamp_gui_log("get_size -> %d width=%u height=%u", ok ? 1 : 0,
                   ok && width ? *width : 0u, ok && height ? *height : 0u);
    return ok;
}

static bool nilamp_gui_can_resize_ext(const clap_plugin_t *plugin)
{
    (void)plugin;
    nilamp_gui_log("can_resize -> 0");
    return false;
}

static bool nilamp_gui_get_resize_hints_ext(const clap_plugin_t *plugin,
                                            clap_gui_resize_hints_t *hints)
{
    (void)plugin;
    if (hints) {
        memset(hints, 0, sizeof(*hints));
    }
    nilamp_gui_log("get_resize_hints -> 0");
    return false;
}

static bool nilamp_gui_adjust_size_ext(const clap_plugin_t *plugin, uint32_t *width,
                                       uint32_t *height)
{
    (void)plugin;
    if (!width || !height) {
        return false;
    }
    *width = 540u;
    *height = 360u;
    nilamp_gui_log("adjust_size -> width=%u height=%u", *width, *height);
    return true;
}

static bool nilamp_gui_set_size_ext(const clap_plugin_t *plugin, uint32_t width,
                                    uint32_t height)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    const bool ok = plug && plug->gui && nilamp_gui_set_size(plug->gui, width, height);
    nilamp_gui_log("set_size width=%u height=%u -> %d", width, height, ok ? 1 : 0);
    return ok;
}

static bool nilamp_gui_set_parent_ext(const clap_plugin_t *plugin,
                                      const clap_window_t *window)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    uintptr_t handle = 0u;
#if defined(__APPLE__)
    if (window) {
        handle = (uintptr_t)window->cocoa;
    }
#elif defined(_WIN32)
    if (window) {
        handle = (uintptr_t)window->win32;
    }
#else
    if (window) {
        handle = (uintptr_t)window->x11;
    }
#endif
    if (!plug || !plug->gui || !window || handle == 0u) {
        nilamp_gui_log("set_parent rejected plug=%p gui=%p window=%p handle=%p",
                       (void *)plug, plug ? (void *)plug->gui : NULL,
                       (const void *)window, (void *)handle);
        return false;
    }
    const bool ok = nilamp_gui_set_parent(
        plug->gui,
        (NilampGuiParent){
            .api = NILAMP_GUI_NATIVE_API,
            .handle = handle,
        });
    nilamp_gui_log("set_parent handle=%p -> %d", (void *)handle, ok ? 1 : 0);
    return ok;
}

static bool nilamp_gui_set_transient_ext(const clap_plugin_t *plugin,
                                         const clap_window_t *window)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    uintptr_t handle = 0u;
#if defined(__APPLE__)
    if (window) {
        handle = (uintptr_t)window->cocoa;
    }
#elif defined(_WIN32)
    if (window) {
        handle = (uintptr_t)window->win32;
    }
#else
    if (window) {
        handle = (uintptr_t)window->x11;
    }
#endif
    if (!plug || !plug->gui || !plug->gui_is_floating || !window || handle == 0u) {
        nilamp_gui_log("set_transient rejected floating=%d plug=%p gui=%p window=%p handle=%p",
                       plug && plug->gui_is_floating ? 1 : 0, (void *)plug,
                       plug ? (void *)plug->gui : NULL, (const void *)window,
                       (void *)handle);
        return false;
    }
    const bool ok = nilamp_gui_set_transient(
        plug->gui,
        (NilampGuiParent){
            .api = NILAMP_GUI_NATIVE_API,
            .handle = handle,
        });
    nilamp_gui_log("set_transient handle=%p -> %d", (void *)handle, ok ? 1 : 0);
    return ok;
}

static void nilamp_gui_suggest_title_ext(const clap_plugin_t *plugin, const char *title)
{
    (void)plugin;
    nilamp_gui_log("suggest_title title=%s", title ? title : "(null)");
}

static bool nilamp_gui_show_ext(const clap_plugin_t *plugin)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    const bool ok = plug && plug->gui && nilamp_gui_show(plug->gui);
    nilamp_gui_log("show -> %d", ok ? 1 : 0);
    if (ok && !nilamp_gui_start_frame_timer(plug->gui, NILAMP_GUI_FRAME_INTERVAL_SECONDS)) {
        nilamp_gui_log("start_frame_timer failed");
    }
    if (ok) {
        nilamp_gui_refresh(plug->gui);
    }
    if (ok && plug->host_timer && plug->host_timer->register_timer &&
        !plug->gui_timer_registered) {
        clap_id timer_id = CLAP_INVALID_ID;
        if (plug->host_timer->register_timer(plug->host, NILAMP_GUI_HOST_TIMER_MS,
                                             &timer_id)) {
            plug->gui_timer_id = timer_id;
            plug->gui_timer_registered = true;
            nilamp_gui_log("register_timer id=%u", timer_id);
        } else {
            nilamp_gui_log("register_timer failed");
        }
    }
    if (ok) {
        nilamp_request_gui_callback(plug, "show");
    }
    return ok;
}

static bool nilamp_gui_hide_ext(const clap_plugin_t *plugin)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (plug) {
        nilamp_gui_stop_frame_timer(plug->gui);
    }
    nilamp_unregister_gui_timer(plug);
    const bool ok = plug && plug->gui && nilamp_gui_hide(plug->gui);
    nilamp_gui_log("hide -> %d", ok ? 1 : 0);
    return ok;
}

static const clap_plugin_gui_t nilamp_gui_ext = {
    .is_api_supported = nilamp_gui_is_api_supported,
    .get_preferred_api = nilamp_gui_get_preferred_api,
    .create = nilamp_gui_create_ext,
    .destroy = nilamp_gui_destroy_ext,
    .set_scale = nilamp_gui_set_scale_ext,
    .get_size = nilamp_gui_get_size_ext,
    .can_resize = nilamp_gui_can_resize_ext,
    .get_resize_hints = nilamp_gui_get_resize_hints_ext,
    .adjust_size = nilamp_gui_adjust_size_ext,
    .set_size = nilamp_gui_set_size_ext,
    .set_parent = nilamp_gui_set_parent_ext,
    .set_transient = nilamp_gui_set_transient_ext,
    .suggest_title = nilamp_gui_suggest_title_ext,
    .show = nilamp_gui_show_ext,
    .hide = nilamp_gui_hide_ext,
};

static void nilamp_timer_on_timer(const clap_plugin_t *plugin, clap_id timer_id)
{
    NilampClap *plug = nilamp_from_plugin(plugin);
    if (!plug || !plug->gui || !nilamp_gui_is_visible(plug->gui)) {
        return;
    }
    if (plug->gui_timer_registered && timer_id != plug->gui_timer_id && timer_id != 0u) {
        return;
    }
    nilamp_gui_log("timer tick id=%u", timer_id);
    nilamp_gui_on_main_thread(plug->gui);
}

static const clap_plugin_timer_support_t nilamp_timer_ext = {
    .on_timer = nilamp_timer_on_timer,
};
#endif

static const void *nilamp_get_extension(const clap_plugin_t *plugin, const char *id)
{
    (void)plugin;
    if (!id) {
        return NULL;
    }
    if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
        return &nilamp_audio_ports_ext;
    }
    if (strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) {
        return &nilamp_note_ports_ext;
    }
    if (strcmp(id, CLAP_EXT_PARAMS) == 0) {
        return &nilamp_params_ext;
    }
    if (strcmp(id, CLAP_EXT_STATE) == 0) {
        return &nilamp_state_ext;
    }
    if (strcmp(id, CLAP_EXT_STATE_CONTEXT) == 0) {
        return &nilamp_state_context_ext;
    }
    if (strcmp(id, CLAP_EXT_LATENCY) == 0) {
        return &nilamp_latency_ext;
    }
    if (strcmp(id, CLAP_EXT_TAIL) == 0) {
        return &nilamp_tail_ext;
    }
    if (strcmp(id, CLAP_EXT_REMOTE_CONTROLS) == 0 ||
        strcmp(id, CLAP_EXT_REMOTE_CONTROLS_COMPAT) == 0) {
        return &nilamp_remote_controls_ext;
    }
#if NILAMP_ENABLE_CLAP_GUI
    if (strcmp(id, CLAP_EXT_PARAM_INDICATION) == 0 ||
        strcmp(id, CLAP_EXT_PARAM_INDICATION_COMPAT) == 0) {
        return &nilamp_param_indication_ext;
    }
    if (strcmp(id, CLAP_EXT_GUI) == 0) {
        return &nilamp_gui_ext;
    }
    if (strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0) {
        return &nilamp_timer_ext;
    }
#endif
    return NULL;
}

static const clap_plugin_t *nilamp_create_plugin(const clap_plugin_factory_t *factory,
                                                 const clap_host_t *host,
                                                 const char *plugin_id)
{
    (void)factory;
    if (!host || !plugin_id || strcmp(plugin_id, NILAMP_PLUGIN_ID) != 0) {
        return NULL;
    }

    NilampClap *plug = (NilampClap *)calloc(1, sizeof(*plug));
    if (!plug) {
        return NULL;
    }

    plug->host = host;
    plug->process_log = nilamp_process_log_create("clap");
    plug->params = nilamp_default_params();
    nilamp_store_params(plug, &plug->params);
    atomic_store_explicit(&plug->gui_gesture_begin_mask, 0u, memory_order_release);
    atomic_store_explicit(&plug->gui_dirty_mask, 0u, memory_order_release);
    atomic_store_explicit(&plug->gui_gesture_end_mask, 0u, memory_order_release);
    atomic_store_explicit(&plug->params_dirty, 1u, memory_order_release);
    plug->gui_timer_id = CLAP_INVALID_ID;
    plug->plugin.desc = &nilamp_descriptor;
    plug->plugin.plugin_data = plug;
    plug->plugin.init = nilamp_init;
    plug->plugin.destroy = nilamp_destroy;
    plug->plugin.activate = nilamp_activate;
    plug->plugin.deactivate = nilamp_deactivate;
    plug->plugin.start_processing = nilamp_start_processing;
    plug->plugin.stop_processing = nilamp_stop_processing;
    plug->plugin.reset = nilamp_reset;
    plug->plugin.process = nilamp_process;
    plug->plugin.get_extension = nilamp_get_extension;
    plug->plugin.on_main_thread = nilamp_on_main_thread;

    return &plug->plugin;
}

static uint32_t nilamp_factory_get_plugin_count(const clap_plugin_factory_t *factory)
{
    (void)factory;
    return 1;
}

static const clap_plugin_descriptor_t *
nilamp_factory_get_plugin_descriptor(const clap_plugin_factory_t *factory, uint32_t index)
{
    (void)factory;
    return index == 0 ? &nilamp_descriptor : NULL;
}

static const clap_plugin_factory_t nilamp_plugin_factory = {
    .get_plugin_count = nilamp_factory_get_plugin_count,
    .get_plugin_descriptor = nilamp_factory_get_plugin_descriptor,
    .create_plugin = nilamp_create_plugin,
};

static bool nilamp_entry_init(const char *plugin_path)
{
    (void)plugin_path;
    return true;
}

static void nilamp_entry_deinit(void) {}

static const void *nilamp_entry_get_factory(const char *factory_id)
{
    if (factory_id && strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) {
        return &nilamp_plugin_factory;
    }
    return NULL;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init = nilamp_entry_init,
    .deinit = nilamp_entry_deinit,
    .get_factory = nilamp_entry_get_factory,
};
