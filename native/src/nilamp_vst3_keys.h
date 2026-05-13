// SPDX-License-Identifier: MIT
#ifndef NILAMP_VST3_KEYS_H
#define NILAMP_VST3_KEYS_H

#include "nilamp_gui_input.h"

#include "pluginterfaces/base/keycodes.h"

static inline NilampGuiInputKey nilamp_vst3_key_code_to_gui_key(int16_t keyCode)
{
    switch (keyCode) {
    case Steinberg::KEY_BACK:
        return NILAMP_GUI_INPUT_KEY_BACKSPACE;
    case Steinberg::KEY_DELETE:
        return NILAMP_GUI_INPUT_KEY_DELETE;
    case Steinberg::KEY_RETURN:
    case Steinberg::KEY_ENTER:
        return NILAMP_GUI_INPUT_KEY_ENTER;
    case Steinberg::KEY_ESCAPE:
        return NILAMP_GUI_INPUT_KEY_ESCAPE;
    case Steinberg::KEY_TAB:
        return NILAMP_GUI_INPUT_KEY_TAB;
    case Steinberg::KEY_LEFT:
        return NILAMP_GUI_INPUT_KEY_LEFT;
    case Steinberg::KEY_RIGHT:
        return NILAMP_GUI_INPUT_KEY_RIGHT;
    case Steinberg::KEY_UP:
        return NILAMP_GUI_INPUT_KEY_UP;
    case Steinberg::KEY_DOWN:
        return NILAMP_GUI_INPUT_KEY_DOWN;
    case Steinberg::KEY_HOME:
        return NILAMP_GUI_INPUT_KEY_HOME;
    case Steinberg::KEY_END:
        return NILAMP_GUI_INPUT_KEY_END;
    default:
        return NILAMP_GUI_INPUT_KEY_UNKNOWN;
    }
}

static inline bool nilamp_vst3_is_printable_ascii(Steinberg::char16 key)
{
    return key >= 32 && key <= 126;
}

static inline Steinberg::char16 nilamp_vst3_text_char(Steinberg::char16 key,
                                                      int16_t keyCode)
{
    if (nilamp_vst3_is_printable_ascii(key)) {
        return key;
    }
    return keyCode == Steinberg::KEY_SPACE ? (Steinberg::char16)' ' : 0;
}

static inline bool nilamp_vst3_should_insert_text(Steinberg::char16 key,
                                                  int16_t modifiers)
{
    const int16_t text_blocking_mods =
        static_cast<int16_t>(Steinberg::kAlternateKey | Steinberg::kCommandKey |
                             Steinberg::kControlKey);
    return nilamp_vst3_is_printable_ascii(key) &&
           (modifiers & text_blocking_mods) == 0;
}

#endif
