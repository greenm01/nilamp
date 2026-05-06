// SPDX-License-Identifier: MIT
#ifndef NILAMP_GUI_H
#define NILAMP_GUI_H

#include <stdbool.h>
#include <stdint.h>

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

typedef struct NilampGuiParamSpec {
    uint32_t id;
    const char *name;
    const char *unit;
    float min_value;
    float max_value;
} NilampGuiParamSpec;

typedef struct NilampGuiCallbacks {
    void *user;
    float (*get_param)(void *user, uint32_t param_id);
    void (*set_param)(void *user, uint32_t param_id, float value);
    const char *(*model_name)(void *user);
} NilampGuiCallbacks;

NilampGui *nilamp_gui_create(const NilampGuiCallbacks *callbacks,
                             const NilampGuiParamSpec *params,
                             uint32_t param_count,
                             NilampGuiApi api);
void nilamp_gui_destroy(NilampGui *gui);

bool nilamp_gui_set_parent(NilampGui *gui, NilampGuiParent parent);
bool nilamp_gui_show(NilampGui *gui);
bool nilamp_gui_hide(NilampGui *gui);
bool nilamp_gui_set_scale(NilampGui *gui, double scale);
bool nilamp_gui_get_size(const NilampGui *gui, uint32_t *width, uint32_t *height);
bool nilamp_gui_set_size(NilampGui *gui, uint32_t width, uint32_t height);
bool nilamp_gui_is_visible(const NilampGui *gui);
void nilamp_gui_on_main_thread(NilampGui *gui);

#endif
