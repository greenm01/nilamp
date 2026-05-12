// SPDX-License-Identifier: MIT
#ifndef NILAMP_GUI_H
#define NILAMP_GUI_H

#include "nilamp_dsp.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NilampGui NilampGui;

typedef enum NilampGuiApi {
    NILAMP_GUI_API_X11 = 0,
    NILAMP_GUI_API_COCOA = 1,
    NILAMP_GUI_API_WIN32 = 2,
} NilampGuiApi;

typedef struct NilampGuiParent {
    NilampGuiApi api;
    uintptr_t handle;
} NilampGuiParent;

typedef struct NilampGuiIndicationColor {
    uint8_t alpha;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} NilampGuiIndicationColor;

typedef NilampControlSpec NilampGuiParamSpec;

typedef struct NilampGuiCallbacks {
    void *user;
    float (*get_param)(void *user, uint32_t param_id);
    void (*begin_param_gesture)(void *user, uint32_t param_id);
    void (*set_param)(void *user, uint32_t param_id, float value);
    void (*end_param_gesture)(void *user, uint32_t param_id);
    const char *(*model_name)(void *user);
} NilampGuiCallbacks;

NilampGui *nilamp_gui_create(const NilampGuiCallbacks *callbacks,
                             const NilampGuiParamSpec *params,
                             uint32_t param_count,
                             const NilampGuiLayoutSpec *layout,
                             NilampGuiApi api,
                             bool is_floating);
void nilamp_gui_destroy(NilampGui *gui);

bool nilamp_gui_set_parent(NilampGui *gui, NilampGuiParent parent);
bool nilamp_gui_set_transient(NilampGui *gui, NilampGuiParent parent);
bool nilamp_gui_show(NilampGui *gui);
bool nilamp_gui_hide(NilampGui *gui);
bool nilamp_gui_set_scale(NilampGui *gui, double scale);
bool nilamp_gui_get_size(const NilampGui *gui, uint32_t *width, uint32_t *height);
bool nilamp_gui_set_size(NilampGui *gui, uint32_t width, uint32_t height);
bool nilamp_gui_is_visible(const NilampGui *gui);
bool nilamp_gui_wants_fast_pump(const NilampGui *gui);
bool nilamp_gui_start_frame_timer(NilampGui *gui, double interval_seconds);
void nilamp_gui_stop_frame_timer(NilampGui *gui);
void nilamp_gui_refresh(NilampGui *gui);
void nilamp_gui_on_main_thread(NilampGui *gui);
void nilamp_gui_set_param_mapping_indication(NilampGui *gui,
                                             uint32_t param_id,
                                             bool has_mapping,
                                             const NilampGuiIndicationColor *color,
                                             const char *label);
void nilamp_gui_set_param_automation_indication(NilampGui *gui,
                                                uint32_t param_id,
                                                uint32_t automation_state,
                                                const NilampGuiIndicationColor *color);

#ifdef __cplusplus
}
#endif

#endif
