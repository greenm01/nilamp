// SPDX-License-Identifier: MIT
#ifndef NILAMP_GUI_INPUT_H
#define NILAMP_GUI_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum NilampGuiInputKey {
    NILAMP_GUI_INPUT_KEY_UNKNOWN = 0,
    NILAMP_GUI_INPUT_KEY_BACKSPACE,
    NILAMP_GUI_INPUT_KEY_DELETE,
    NILAMP_GUI_INPUT_KEY_ENTER,
    NILAMP_GUI_INPUT_KEY_ESCAPE,
    NILAMP_GUI_INPUT_KEY_TAB,
    NILAMP_GUI_INPUT_KEY_LEFT,
    NILAMP_GUI_INPUT_KEY_RIGHT,
    NILAMP_GUI_INPUT_KEY_UP,
    NILAMP_GUI_INPUT_KEY_DOWN,
    NILAMP_GUI_INPUT_KEY_HOME,
    NILAMP_GUI_INPUT_KEY_END,
} NilampGuiInputKey;

typedef enum NilampGuiInputWidgetKey {
    NILAMP_GUI_INPUT_WIDGET_KEY_NONE = 0,
    NILAMP_GUI_INPUT_WIDGET_KEY_BACKSPACE,
    NILAMP_GUI_INPUT_WIDGET_KEY_DELETE,
    NILAMP_GUI_INPUT_WIDGET_KEY_ENTER,
    NILAMP_GUI_INPUT_WIDGET_KEY_TAB,
    NILAMP_GUI_INPUT_WIDGET_KEY_LEFT,
    NILAMP_GUI_INPUT_WIDGET_KEY_RIGHT,
    NILAMP_GUI_INPUT_WIDGET_KEY_UP,
    NILAMP_GUI_INPUT_WIDGET_KEY_DOWN,
    NILAMP_GUI_INPUT_WIDGET_KEY_HOME,
    NILAMP_GUI_INPUT_WIDGET_KEY_END,
} NilampGuiInputWidgetKey;

typedef struct NilampGuiInputKeyEffect {
    bool handled;
    bool widget_key_valid;
    bool widget_key_down;
    NilampGuiInputWidgetKey widget_key;
    bool key_enter;
    bool key_escape;
    bool key_backspace;
    bool key_delete;
    bool key_left;
    bool key_right;
    bool key_home;
    bool key_end;
} NilampGuiInputKeyEffect;

NilampGuiInputKeyEffect nilamp_gui_input_key_effect(NilampGuiInputKey key,
                                                    bool down,
                                                    bool editing);
bool nilamp_gui_input_append_text(char *text,
                                  size_t capacity,
                                  uint32_t *len,
                                  uint32_t codepoint);

#endif
