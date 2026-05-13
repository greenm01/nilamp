// SPDX-License-Identifier: MIT
#include "nilamp_gui_input.h"

static NilampGuiInputWidgetKey nilamp_gui_input_widget_key(NilampGuiInputKey key)
{
    switch (key) {
    case NILAMP_GUI_INPUT_KEY_BACKSPACE:
        return NILAMP_GUI_INPUT_WIDGET_KEY_BACKSPACE;
    case NILAMP_GUI_INPUT_KEY_DELETE:
        return NILAMP_GUI_INPUT_WIDGET_KEY_DELETE;
    case NILAMP_GUI_INPUT_KEY_ENTER:
        return NILAMP_GUI_INPUT_WIDGET_KEY_ENTER;
    case NILAMP_GUI_INPUT_KEY_TAB:
        return NILAMP_GUI_INPUT_WIDGET_KEY_TAB;
    case NILAMP_GUI_INPUT_KEY_LEFT:
        return NILAMP_GUI_INPUT_WIDGET_KEY_LEFT;
    case NILAMP_GUI_INPUT_KEY_RIGHT:
        return NILAMP_GUI_INPUT_WIDGET_KEY_RIGHT;
    case NILAMP_GUI_INPUT_KEY_UP:
        return NILAMP_GUI_INPUT_WIDGET_KEY_UP;
    case NILAMP_GUI_INPUT_KEY_DOWN:
        return NILAMP_GUI_INPUT_WIDGET_KEY_DOWN;
    case NILAMP_GUI_INPUT_KEY_HOME:
        return NILAMP_GUI_INPUT_WIDGET_KEY_HOME;
    case NILAMP_GUI_INPUT_KEY_END:
        return NILAMP_GUI_INPUT_WIDGET_KEY_END;
    case NILAMP_GUI_INPUT_KEY_UNKNOWN:
    case NILAMP_GUI_INPUT_KEY_ESCAPE:
    default:
        return NILAMP_GUI_INPUT_WIDGET_KEY_NONE;
    }
}

NilampGuiInputKeyEffect nilamp_gui_input_key_effect(NilampGuiInputKey key,
                                                    bool down,
                                                    bool editing)
{
    NilampGuiInputKeyEffect effect = {0};
    if (key == NILAMP_GUI_INPUT_KEY_UNKNOWN) {
        return effect;
    }

    effect.widget_key = nilamp_gui_input_widget_key(key);
    effect.widget_key_valid = effect.widget_key != NILAMP_GUI_INPUT_WIDGET_KEY_NONE;
    effect.widget_key_down = editing ? false : down;

    switch (key) {
    case NILAMP_GUI_INPUT_KEY_BACKSPACE:
        if (down && editing) {
            effect.key_backspace = true;
            effect.handled = true;
        }
        break;
    case NILAMP_GUI_INPUT_KEY_DELETE:
        if (down && editing) {
            effect.key_delete = true;
            effect.handled = true;
        }
        break;
    case NILAMP_GUI_INPUT_KEY_ENTER:
        if (down && editing) {
            effect.key_enter = true;
            effect.handled = true;
        }
        break;
    case NILAMP_GUI_INPUT_KEY_ESCAPE:
        if (down) {
            effect.key_escape = true;
            effect.handled = true;
        }
        break;
    case NILAMP_GUI_INPUT_KEY_LEFT:
        if (down && editing) {
            effect.key_left = true;
            effect.handled = true;
        }
        break;
    case NILAMP_GUI_INPUT_KEY_RIGHT:
        if (down && editing) {
            effect.key_right = true;
            effect.handled = true;
        }
        break;
    case NILAMP_GUI_INPUT_KEY_HOME:
        if (down && editing) {
            effect.key_home = true;
            effect.handled = true;
        }
        break;
    case NILAMP_GUI_INPUT_KEY_END:
        if (down && editing) {
            effect.key_end = true;
            effect.handled = true;
        }
        break;
    case NILAMP_GUI_INPUT_KEY_TAB:
    case NILAMP_GUI_INPUT_KEY_UP:
    case NILAMP_GUI_INPUT_KEY_DOWN:
        effect.handled = editing;
        break;
    case NILAMP_GUI_INPUT_KEY_UNKNOWN:
    default:
        break;
    }

    if (effect.widget_key_valid) {
        effect.handled = true;
    }
    return effect;
}

bool nilamp_gui_input_append_text(char *text,
                                  size_t capacity,
                                  uint32_t *len,
                                  uint32_t codepoint)
{
    if (!text || !len || codepoint < 32u || codepoint > 126u ||
        *len + 1u >= capacity) {
        return false;
    }
    text[*len] = (char)codepoint;
    *len += 1u;
    text[*len] = '\0';
    return true;
}
