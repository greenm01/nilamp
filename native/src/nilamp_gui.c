// SPDX-License-Identifier: MIT
#include "nilamp_gui.h"
#include "nilamp_gui_edit.h"

#include <pugl/gl.h>
#include <pugl/pugl.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_STANDARD_BOOL
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#define SOKOL_GLCORE
#include "sokol_gfx.h"

#define SOKOL_NUKLEAR_NO_SOKOL_APP
#include "sokol_nuklear.h"

#include "nilamp_font_0xproto.h"

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NILAMP_GUI_DEFAULT_WIDTH 500u
#define NILAMP_GUI_DEFAULT_HEIGHT 340u
#define NILAMP_GUI_MAX_PARAMS 24u
#define NILAMP_GUI_MAX_MSGS (NILAMP_GUI_MAX_PARAMS * 3u)
#define NILAMP_GUI_INDICATION_LABEL_LEN 24u
#define NILAMP_GUI_TEXT_INPUT_LEN 64u
#define NILAMP_GUI_MAX_VERTICES 32768u
#define NILAMP_GUI_DOUBLE_CLICK_SECONDS 0.35
#define NILAMP_GUI_DOUBLE_CLICK_DISTANCE 6.0
#define NILAMP_GUI_DEG_TO_RAD 0.017453292519943295f
#define NILAMP_GUI_FRAME_TIMER_ID 1u

typedef enum NilampGuiScreen {
    NILAMP_GUI_SCREEN_MAIN = 0,
    NILAMP_GUI_SCREEN_OPTIONS = 1,
    NILAMP_GUI_SCREEN_ABOUT = 2,
} NilampGuiScreen;

typedef enum NilampGuiMsgType {
    NILAMP_GUI_MSG_NONE = 0,
    NILAMP_GUI_MSG_PARAM_GESTURE_BEGIN,
    NILAMP_GUI_MSG_PARAM_CHANGED,
    NILAMP_GUI_MSG_PARAM_GESTURE_END,
} NilampGuiMsgType;

typedef enum NilampGuiKnobStyle {
    NILAMP_GUI_KNOB_PERCENT = 0,
    NILAMP_GUI_KNOB_GAIN_UNIPOLAR = 1,
    NILAMP_GUI_KNOB_GAIN_BIPOLAR = 2,
} NilampGuiKnobStyle;

typedef enum NilampGuiDropdown {
    NILAMP_GUI_DROPDOWN_NONE = 0,
    NILAMP_GUI_DROPDOWN_ENUM = 1,
} NilampGuiDropdown;

typedef struct NilampGuiMsg {
    NilampGuiMsgType type;
    uint32_t param_id;
    float value;
} NilampGuiMsg;

typedef struct NilampGuiModel {
    float param_values[NILAMP_GUI_MAX_PARAMS];
    char edit_text[NILAMP_GUI_MAX_PARAMS][NILAMP_GUI_EDIT_TEXT_LEN];
    bool edit_active[NILAMP_GUI_MAX_PARAMS];
    uint32_t width;
    uint32_t height;
    bool dirty;
} NilampGuiModel;

typedef struct NilampGuiParamIndication {
    bool has_mapping;
    bool has_mapping_color;
    NilampGuiIndicationColor mapping_color;
    char mapping_label[NILAMP_GUI_INDICATION_LABEL_LEN];
    uint32_t automation_state;
    bool has_automation_color;
    NilampGuiIndicationColor automation_color;
} NilampGuiParamIndication;

struct NilampGui {
    PuglWorld *world;
    PuglView *view;
    NilampGuiCallbacks callbacks;
    const NilampGuiParamSpec *params;
    uint32_t param_count;
    const NilampGuiLayoutSpec *layout;
    NilampGuiApi api;
    NilampGuiModel model;
    NilampGuiParamIndication indications[NILAMP_GUI_MAX_PARAMS];
    double scale;
    int mouse_x;
    int mouse_y;
    bool mouse_down[3];
    bool mouse_up[3];
    bool mouse_motion;
    bool mouse_double_click;
    float scroll_x;
    float scroll_y;
    double last_click_time;
    int last_click_x;
    int last_click_y;
    uint32_t last_click_button;
    bool key_down[NK_KEY_MAX];
    char text_input[NILAMP_GUI_TEXT_INPUT_LEN];
    uint32_t text_input_len;
    bool key_enter;
    bool key_escape;
    bool key_backspace;
    bool key_delete;
    bool key_left;
    bool key_right;
    bool key_home;
    bool key_end;
    bool value_box_hovered;
    bool dropdown_hovered;
    bool edit_replace_on_type;
    size_t edit_cursor;
    int active_knob;
    int active_edit;
    int active_gesture;
    NilampGuiDropdown open_dropdown;
    uint32_t open_dropdown_param;
    struct nk_rect open_dropdown_selector;
    NilampGuiScreen screen;
    struct nk_font_atlas font_atlas;
    struct nk_font *font_default;
    struct nk_font *font_about;
    struct nk_font *font_subtitle;
    struct nk_font *font_title;
    sg_image font_img;
    sg_view font_tex_view;
    sg_sampler font_sampler;
    snk_image_t font_snk_img;
    bool custom_font_atlas_ready;
    bool custom_font_ready;
    bool realized;
    bool is_floating;
    bool visible;
    bool gpu_ready;
    bool frame_timer_running;
};

static bool nilamp_gui_sokol_in_use = false;

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

    fputs("[nilamp_gui] ", fp);
    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);
    fputc('\n', fp);
    fclose(fp);
}

static void nilamp_gui_grab_focus(NilampGui *gui, const char *reason)
{
    if (!gui || !gui->view) {
        return;
    }
    const PuglStatus status = puglGrabFocus(gui->view);
    nilamp_gui_log("grab_focus reason=%s status=%s",
                   reason ? reason : "(none)", puglStrerror(status));
}

static float nilamp_gui_clampf(float value, float min_value, float max_value)
{
    if (!isfinite(value)) {
        return min_value;
    }
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static size_t nilamp_gui_bounded_strlen(const char *text, size_t max_len)
{
    return nilamp_gui_edit_strlen(text, max_len);
}

static uint32_t nilamp_gui_param_index(const NilampGui *gui, uint32_t param_id)
{
    if (!gui) {
        return NILAMP_GUI_MAX_PARAMS;
    }
    // gui->params mirrors the generated control spec table which is
    // emitted in ID order, so param_id == index. Take the O(1) path and
    // fall back to a scan only if a caller ever passes an out-of-order
    // layout.
    if (param_id < gui->param_count && param_id < NILAMP_GUI_MAX_PARAMS &&
        gui->params[param_id].id == param_id) {
        return param_id;
    }
    for (uint32_t i = 0; i < gui->param_count; i++) {
        if (gui->params[i].id == param_id) {
            return i;
        }
    }
    return gui->param_count;
}

static float nilamp_gui_read_param(const NilampGui *gui, uint32_t index)
{
    if (!gui || index >= gui->param_count || !gui->callbacks.get_param) {
        return 0.0f;
    }
    const NilampGuiParamSpec *param = &gui->params[index];
    return nilamp_gui_clampf(gui->callbacks.get_param(gui->callbacks.user, param->id),
                             param->min_value, param->max_value);
}

static float nilamp_gui_display_value(const NilampGuiParamSpec *param, float value)
{
    return nilamp_control_display_value(param, value);
}

static float nilamp_gui_raw_from_display(const NilampGuiParamSpec *param, float value)
{
    if (!param || param->display != NILAMP_CONTROL_DISPLAY_ISO266) {
        return value;
    }
    if (value <= 0.0f) {
        return param->min_value;
    }
    return 20.0f * log10f(value);
}

static bool nilamp_gui_is_hz_param(const NilampGuiParamSpec *param)
{
    return param && param->unit && strcmp(param->unit, "Hz") == 0;
}

static bool nilamp_gui_is_percent_param(const NilampGuiParamSpec *param)
{
    return param && param->unit && strcmp(param->unit, "%") == 0;
}

static bool nilamp_gui_is_db_param(const NilampGuiParamSpec *param)
{
    return param && param->unit && strcmp(param->unit, "dB") == 0;
}

static const char *nilamp_gui_skip_spaces(const char *text)
{
    if (!text) {
        return NULL;
    }
    while (*text == ' ' || *text == '\t') {
        text++;
    }
    return text;
}

static bool nilamp_gui_is_text_end(const char *text)
{
    text = nilamp_gui_skip_spaces(text);
    return text && *text == '\0';
}

static bool nilamp_gui_accept_hz_suffix(const char **end)
{
    const char *text = nilamp_gui_skip_spaces(*end);
    if (!text) {
        return false;
    }

    if (*text == 'k' || *text == 'K') {
        text++;
        if (*text == 'h' || *text == 'H') {
            text++;
            if (*text == 'z' || *text == 'Z') {
                text++;
            }
        }
        *end = text;
        return true;
    }
    if (*text == 'h' || *text == 'H') {
        text++;
        if (*text == 'z' || *text == 'Z') {
            text++;
        }
        *end = text;
        return true;
    }
    return false;
}

static float nilamp_gui_display_box_value(const NilampGuiParamSpec *param, float value,
                                          const char **unit)
{
    float display = nilamp_gui_display_value(param, value);
    if (unit) {
        *unit = param ? param->unit : "";
    }
    if (nilamp_gui_is_hz_param(param) && fabsf(display) >= 1000.0f) {
        display *= 0.001f;
        if (unit) {
            *unit = "kHz";
        }
    }
    return display;
}

static float nilamp_gui_parse_box_value(const NilampGuiParamSpec *param, const char *text,
                                        bool current_unit_khz, bool *ok)
{
    if (ok) {
        *ok = false;
    }
    if (!param || !text) {
        return 0.0f;
    }
    char safe_text[NILAMP_GUI_EDIT_TEXT_LEN];
    const size_t len = nilamp_gui_bounded_strlen(text, NILAMP_GUI_EDIT_TEXT_LEN);
    if (len == 0u || len >= sizeof(safe_text)) {
        return 0.0f;
    }
    memcpy(safe_text, text, len);
    safe_text[len] = '\0';

    const char *start = nilamp_gui_skip_spaces(safe_text);
    if (!start || *start == '\0') {
        return 0.0f;
    }

    if (nilamp_gui_is_percent_param(param)) {
        float display = 0.0f;
        const char *scan = start;
        while (*scan >= '0' && *scan <= '9') {
            display = display * 10.0f + (float)(*scan - '0');
            scan++;
        }
        if (scan == start || !nilamp_gui_is_text_end(scan)) {
            return 0.0f;
        }
        if (ok) {
            *ok = true;
        }
        return display;
    }

    char *end = NULL;
    float display = strtof(start, &end);
    if (end == start || !isfinite(display)) {
        return 0.0f;
    }
    if (nilamp_gui_is_hz_param(param)) {
        const char *suffix = end;
        if (nilamp_gui_accept_hz_suffix(&suffix) &&
            (*(nilamp_gui_skip_spaces(end)) == 'k' ||
             *(nilamp_gui_skip_spaces(end)) == 'K')) {
            display *= 1000.0f;
            end = (char *)suffix;
        } else if (suffix != end) {
            end = (char *)suffix;
        } else if (current_unit_khz) {
            display *= 1000.0f;
        }
    }
    if (!nilamp_gui_is_text_end(end)) {
        return 0.0f;
    }
    if (ok) {
        *ok = true;
    }
    return display;
}

static float nilamp_gui_quantize(const NilampGuiParamSpec *param, float value)
{
    if (!param) {
        return value;
    }
    float clamped = nilamp_gui_clampf(value, param->min_value, param->max_value);
    if (param->step > 0.0f) {
        clamped = param->min_value +
                  roundf((clamped - param->min_value) / param->step) * param->step;
        clamped = nilamp_gui_clampf(clamped, param->min_value, param->max_value);
    }
    return clamped;
}

static void nilamp_gui_format_edit_value(const NilampGuiParamSpec *param, float value,
                                         char *dst, size_t dst_size)
{
    if (!dst || dst_size == 0) {
        return;
    }
    if (!param) {
        dst[0] = '\0';
        return;
    }
    const float display = nilamp_gui_display_box_value(param, value, NULL);
    if (nilamp_gui_is_percent_param(param)) {
        (void)snprintf(dst, dst_size, "%.0f", display);
    } else {
        (void)snprintf(dst, dst_size, "%.3g", display);
    }
}

static void nilamp_gui_sync_edit_text(NilampGui *gui, uint32_t index)
{
    if (!gui || index >= gui->param_count || index >= NILAMP_GUI_MAX_PARAMS ||
        gui->model.edit_active[index]) {
        return;
    }
    nilamp_gui_format_edit_value(&gui->params[index], gui->model.param_values[index],
                                 gui->model.edit_text[index],
                                 sizeof(gui->model.edit_text[index]));
}

static bool nilamp_gui_refresh_params(NilampGui *gui)
{
    if (!gui) {
        return false;
    }
    bool changed = false;
    for (uint32_t i = 0; i < gui->param_count && i < NILAMP_GUI_MAX_PARAMS; i++) {
        const float value = nilamp_gui_read_param(gui, i);
        if (value != gui->model.param_values[i]) {
            gui->model.param_values[i] = value;
            changed = true;
        }
        nilamp_gui_sync_edit_text(gui, i);
    }
    if (changed) {
        gui->model.dirty = true;
    }
    return changed;
}

static void nilamp_gui_emit(NilampGuiMsg *outbox, uint32_t *outbox_count,
                            uint32_t max_count, NilampGuiMsg msg)
{
    if (!outbox || !outbox_count || *outbox_count >= max_count) {
        return;
    }
    outbox[*outbox_count] = msg;
    *outbox_count += 1u;
}

static void nilamp_gui_update(NilampGui *gui, const NilampGuiMsg *msg)
{
    if (!gui || !msg) {
        return;
    }

    const uint32_t index = nilamp_gui_param_index(gui, msg->param_id);
    if (index >= gui->param_count || index >= NILAMP_GUI_MAX_PARAMS) {
        return;
    }

    const NilampGuiParamSpec *param = &gui->params[index];
    switch (msg->type) {
    case NILAMP_GUI_MSG_PARAM_GESTURE_BEGIN:
        if (gui->callbacks.begin_param_gesture) {
            gui->callbacks.begin_param_gesture(gui->callbacks.user, param->id);
        }
        return;
    case NILAMP_GUI_MSG_PARAM_CHANGED:
        break;
    case NILAMP_GUI_MSG_PARAM_GESTURE_END:
        if (gui->callbacks.end_param_gesture) {
            gui->callbacks.end_param_gesture(gui->callbacks.user, param->id);
        }
        return;
    case NILAMP_GUI_MSG_NONE:
    default:
        return;
    }

    const float value = nilamp_gui_clampf(msg->value, param->min_value, param->max_value);
    gui->model.param_values[index] = value;
    gui->model.dirty = true;
    if (gui->callbacks.set_param) {
        gui->callbacks.set_param(gui->callbacks.user, param->id, value);
    }
}

static void nilamp_gui_drain_outbox(NilampGui *gui, const NilampGuiMsg *outbox,
                                    uint32_t outbox_count)
{
    for (uint32_t i = 0; i < outbox_count; i++) {
        nilamp_gui_update(gui, &outbox[i]);
    }
}

static void nilamp_gui_emit_param_gesture_begin(NilampGui *gui, uint32_t index,
                                                NilampGuiMsg *outbox,
                                                uint32_t *outbox_count)
{
    if (!gui || index >= gui->param_count || index >= NILAMP_GUI_MAX_PARAMS) {
        return;
    }
    nilamp_gui_emit(outbox, outbox_count, NILAMP_GUI_MAX_MSGS,
                    (NilampGuiMsg){
                        .type = NILAMP_GUI_MSG_PARAM_GESTURE_BEGIN,
                        .param_id = gui->params[index].id,
                    });
}

static void nilamp_gui_emit_param_gesture_end(NilampGui *gui, uint32_t index,
                                              NilampGuiMsg *outbox,
                                              uint32_t *outbox_count)
{
    if (!gui || index >= gui->param_count || index >= NILAMP_GUI_MAX_PARAMS) {
        return;
    }
    nilamp_gui_emit(outbox, outbox_count, NILAMP_GUI_MAX_MSGS,
                    (NilampGuiMsg){
                        .type = NILAMP_GUI_MSG_PARAM_GESTURE_END,
                        .param_id = gui->params[index].id,
                    });
}

static void nilamp_gui_begin_param_gesture(NilampGui *gui, uint32_t index,
                                           NilampGuiMsg *outbox,
                                           uint32_t *outbox_count)
{
    if (!gui || index >= gui->param_count || index >= NILAMP_GUI_MAX_PARAMS ||
        gui->active_gesture == (int)index) {
        return;
    }
    if (gui->active_gesture >= 0) {
        nilamp_gui_emit_param_gesture_end(gui, (uint32_t)gui->active_gesture,
                                          outbox, outbox_count);
    }
    gui->active_gesture = (int)index;
    nilamp_gui_emit_param_gesture_begin(gui, index, outbox, outbox_count);
}

static void nilamp_gui_end_param_gesture(NilampGui *gui, uint32_t index,
                                         NilampGuiMsg *outbox,
                                         uint32_t *outbox_count)
{
    if (!gui || index >= gui->param_count || index >= NILAMP_GUI_MAX_PARAMS ||
        gui->active_gesture != (int)index) {
        return;
    }
    gui->active_gesture = -1;
    nilamp_gui_emit_param_gesture_end(gui, index, outbox, outbox_count);
}

static void nilamp_gui_request_redraw(NilampGui *gui)
{
    if (gui && gui->view) {
        (void)puglObscureView(gui->view);
    }
}

static void nilamp_gui_style(struct nk_context *ctx)
{
    struct nk_color table[NK_COLOR_COUNT];
    table[NK_COLOR_TEXT] = nk_rgb(255, 205, 32);
    table[NK_COLOR_WINDOW] = nk_rgb(31, 47, 63);
    table[NK_COLOR_HEADER] = nk_rgb(37, 55, 72);
    table[NK_COLOR_BORDER] = nk_rgb(105, 122, 136);
    table[NK_COLOR_BUTTON] = nk_rgb(51, 73, 94);
    table[NK_COLOR_BUTTON_HOVER] = nk_rgb(60, 84, 106);
    table[NK_COLOR_BUTTON_ACTIVE] = nk_rgb(214, 168, 42);
    table[NK_COLOR_TOGGLE] = nk_rgb(51, 73, 94);
    table[NK_COLOR_TOGGLE_HOVER] = nk_rgb(60, 84, 106);
    table[NK_COLOR_TOGGLE_CURSOR] = nk_rgb(255, 205, 32);
    table[NK_COLOR_SELECT] = nk_rgb(51, 73, 94);
    table[NK_COLOR_SELECT_ACTIVE] = nk_rgb(214, 168, 42);
    table[NK_COLOR_SLIDER] = nk_rgb(38, 56, 72);
    table[NK_COLOR_SLIDER_CURSOR] = nk_rgb(255, 205, 32);
    table[NK_COLOR_SLIDER_CURSOR_HOVER] = nk_rgb(255, 219, 81);
    table[NK_COLOR_SLIDER_CURSOR_ACTIVE] = nk_rgb(255, 232, 116);
    table[NK_COLOR_PROPERTY] = nk_rgb(38, 56, 72);
    table[NK_COLOR_EDIT] = nk_rgb(38, 56, 72);
    table[NK_COLOR_EDIT_CURSOR] = nk_rgb(255, 205, 32);
    table[NK_COLOR_COMBO] = nk_rgb(51, 73, 94);
    table[NK_COLOR_CHART] = nk_rgb(38, 56, 72);
    table[NK_COLOR_CHART_COLOR] = nk_rgb(255, 205, 32);
    table[NK_COLOR_CHART_COLOR_HIGHLIGHT] = nk_rgb(255, 232, 116);
    table[NK_COLOR_SCROLLBAR] = nk_rgb(38, 56, 72);
    table[NK_COLOR_SCROLLBAR_CURSOR] = nk_rgb(105, 122, 136);
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = nk_rgb(130, 148, 164);
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgb(255, 205, 32);
    table[NK_COLOR_TAB_HEADER] = nk_rgb(37, 55, 72);
    nk_style_from_table(ctx, table);
    nk_style_hide_cursor(ctx);
    ctx->style.window.padding = nk_vec2(0.0f, 0.0f);
    ctx->style.window.spacing = nk_vec2(0.0f, 0.0f);
    ctx->style.window.border = 0.0f;
    ctx->style.edit.normal = nk_style_item_color(nk_rgb(24, 37, 50));
    ctx->style.edit.hover = nk_style_item_color(nk_rgb(30, 47, 62));
    ctx->style.edit.active = nk_style_item_color(nk_rgb(18, 29, 42));
    ctx->style.edit.border_color = nk_rgb(105, 123, 137);
    ctx->style.edit.text_normal = nk_rgb(255, 205, 32);
    ctx->style.edit.text_hover = nk_rgb(255, 219, 81);
    ctx->style.edit.text_active = nk_rgb(255, 232, 116);
    ctx->style.edit.cursor_normal = nk_rgb(255, 205, 32);
    ctx->style.edit.cursor_hover = nk_rgb(255, 232, 116);
    ctx->style.edit.cursor_text_normal = nk_rgb(18, 29, 42);
    ctx->style.edit.cursor_text_hover = nk_rgb(18, 29, 42);
    ctx->style.edit.border = 1.0f;
    ctx->style.edit.rounding = 2.0f;
}

static void nilamp_gui_feed_input(NilampGui *gui, struct nk_context *ctx)
{
    nk_input_begin(ctx);
    nk_input_motion(ctx, gui->mouse_x, gui->mouse_y);
    if (gui->active_edit < 0) {
        for (uint32_t i = 0; i < NK_KEY_MAX; i++) {
            nk_input_key(ctx, (enum nk_keys)i, gui->key_down[i] ? 1 : 0);
        }
    }
    for (uint32_t i = 0; i < 3; i++) {
        if (gui->mouse_down[i]) {
            nk_input_button(ctx, (enum nk_buttons)i, gui->mouse_x, gui->mouse_y, 1);
            gui->mouse_down[i] = false;
        }
        if (gui->mouse_up[i]) {
            nk_input_button(ctx, (enum nk_buttons)i, gui->mouse_x, gui->mouse_y, 0);
            gui->mouse_up[i] = false;
        }
    }
    if (gui->scroll_x != 0.0f || gui->scroll_y != 0.0f) {
        nk_input_scroll(ctx, nk_vec2(gui->scroll_x, gui->scroll_y));
    }
    nk_input_end(ctx);
    gui->mouse_motion = false;
}

static void nilamp_gui_clear_transient_input(NilampGui *gui)
{
    if (!gui) {
        return;
    }
    gui->text_input_len = 0u;
    gui->text_input[0] = '\0';
    gui->mouse_double_click = false;
    gui->key_enter = false;
    gui->key_escape = false;
    gui->key_backspace = false;
    gui->key_delete = false;
    gui->key_left = false;
    gui->key_right = false;
    gui->key_home = false;
    gui->key_end = false;
    gui->scroll_x = 0.0f;
    gui->scroll_y = 0.0f;
}

static bool nilamp_gui_widget_key_to_nuklear(NilampGuiInputWidgetKey key,
                                             enum nk_keys *out_key)
{
    if (!out_key) {
        return false;
    }
    switch (key) {
    case NILAMP_GUI_INPUT_WIDGET_KEY_BACKSPACE:
        *out_key = NK_KEY_BACKSPACE;
        return true;
    case NILAMP_GUI_INPUT_WIDGET_KEY_DELETE:
        *out_key = NK_KEY_DEL;
        return true;
    case NILAMP_GUI_INPUT_WIDGET_KEY_ENTER:
        *out_key = NK_KEY_ENTER;
        return true;
    case NILAMP_GUI_INPUT_WIDGET_KEY_TAB:
        *out_key = NK_KEY_TAB;
        return true;
    case NILAMP_GUI_INPUT_WIDGET_KEY_LEFT:
        *out_key = NK_KEY_LEFT;
        return true;
    case NILAMP_GUI_INPUT_WIDGET_KEY_RIGHT:
        *out_key = NK_KEY_RIGHT;
        return true;
    case NILAMP_GUI_INPUT_WIDGET_KEY_UP:
        *out_key = NK_KEY_UP;
        return true;
    case NILAMP_GUI_INPUT_WIDGET_KEY_DOWN:
        *out_key = NK_KEY_DOWN;
        return true;
    case NILAMP_GUI_INPUT_WIDGET_KEY_HOME:
        *out_key = NK_KEY_TEXT_LINE_START;
        return true;
    case NILAMP_GUI_INPUT_WIDGET_KEY_END:
        *out_key = NK_KEY_TEXT_LINE_END;
        return true;
    case NILAMP_GUI_INPUT_WIDGET_KEY_NONE:
    default:
        *out_key = NK_KEY_NONE;
        return false;
    }
}

bool nilamp_gui_handle_host_key(NilampGui *gui, NilampGuiInputKey key, bool down)
{
    if (!gui || key == NILAMP_GUI_INPUT_KEY_UNKNOWN) {
        return false;
    }

    const NilampGuiInputKeyEffect effect =
        nilamp_gui_input_key_effect(key, down, gui->active_edit >= 0);
    if (effect.key_backspace) {
        gui->key_backspace = true;
        nilamp_gui_log("key_backspace active_edit=%d", gui->active_edit);
    }
    if (effect.key_delete) {
        nilamp_gui_log("key_delete_ignored active_edit=%d", gui->active_edit);
    }
    gui->key_enter = gui->key_enter || effect.key_enter;
    gui->key_escape = gui->key_escape || effect.key_escape;
    gui->key_left = gui->key_left || effect.key_left;
    gui->key_right = gui->key_right || effect.key_right;
    gui->key_home = gui->key_home || effect.key_home;
    gui->key_end = gui->key_end || effect.key_end;

    enum nk_keys nk_key = NK_KEY_NONE;
    if (effect.widget_key_valid &&
        nilamp_gui_widget_key_to_nuklear(effect.widget_key, &nk_key) && nk_key >= 0 &&
        nk_key < NK_KEY_MAX) {
        gui->key_down[nk_key] = effect.widget_key_down;
    }

    if (effect.handled) {
        nilamp_gui_request_redraw(gui);
    }
    return effect.handled;
}

bool nilamp_gui_handle_host_text(NilampGui *gui, uint32_t codepoint)
{
    if (!gui || !nilamp_gui_input_append_text(gui->text_input,
                                              NILAMP_GUI_TEXT_INPUT_LEN,
                                              &gui->text_input_len, codepoint)) {
        return false;
    }
    nilamp_gui_request_redraw(gui);
    return true;
}

static float nilamp_gui_minf(float a, float b)
{
    return a < b ? a : b;
}

static float nilamp_gui_maxf(float a, float b)
{
    return a > b ? a : b;
}

static size_t nilamp_gui_max_size(size_t a, size_t b)
{
    return a > b ? a : b;
}

static size_t nilamp_gui_clamp_size(size_t value, size_t min_value, size_t max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static float nilamp_gui_font_height(const struct nk_context *ctx)
{
    return ctx && ctx->style.font ? ctx->style.font->height : 24.0f;
}

static float nilamp_gui_line_height(const struct nk_context *ctx, float s)
{
    return nilamp_gui_maxf(nilamp_gui_font_height(ctx) + 4.0f * s, 18.0f * s);
}

static float nilamp_gui_box_height(const struct nk_context *ctx, float s)
{
    return nilamp_gui_maxf(nilamp_gui_font_height(ctx) + 6.0f * s, 19.0f * s);
}

static float nilamp_gui_text_width(const struct nk_context *ctx, const char *text)
{
    const struct nk_user_font *font = ctx ? ctx->style.font : NULL;
    if (!font || !font->width || !text) {
        return 0.0f;
    }
    int len = 0;
    while (len < (int)NILAMP_GUI_TEXT_INPUT_LEN && text[len] != '\0') {
        len++;
    }
    return len > 0 ? font->width(font->userdata, font->height, text, len) : 0.0f;
}

static struct nk_rect nilamp_gui_scale_rect(float sx, float sy, float x, float y,
                                            float w, float h)
{
    return nk_rect(x * sx, y * sy, w * sx, h * sy);
}

static struct nk_rect nilamp_gui_snap_rect(struct nk_rect rect)
{
    rect.x = roundf(rect.x);
    rect.y = roundf(rect.y);
    rect.w = roundf(rect.w);
    rect.h = roundf(rect.h);
    return rect;
}

static void nilamp_gui_draw_text_with_font(struct nk_context *ctx,
                                           struct nk_command_buffer *canvas,
                                           struct nk_rect bounds, const char *text,
                                           struct nk_color color, bool centered,
                                           const struct nk_user_font *font)
{
    if (!ctx || !canvas || !text || bounds.w <= 0.0f || bounds.h <= 0.0f) {
        return;
    }

    if (!font) {
        font = ctx->style.font;
    }
    if (!font) {
        return;
    }
    const int len = (int)nilamp_gui_bounded_strlen(text, NILAMP_GUI_TEXT_INPUT_LEN);
    if (len <= 0) {
        return;
    }
    if (centered && font && font->width) {
        const float text_width = font->width(font->userdata, font->height, text, len);
        if (text_width < bounds.w) {
            bounds.x += (bounds.w - text_width) * 0.5f;
            bounds.w = text_width;
        }
    }
    bounds = nilamp_gui_snap_rect(bounds);
    nk_draw_text(canvas, bounds, text, len, font, nk_rgba(0, 0, 0, 0), color);
}

static void nilamp_gui_draw_text(struct nk_context *ctx, struct nk_command_buffer *canvas,
                                 struct nk_rect bounds, const char *text,
                                 struct nk_color color, bool centered)
{
    nilamp_gui_draw_text_with_font(ctx, canvas, bounds, text, color, centered, NULL);
}

static void nilamp_gui_draw_panel(struct nk_context *ctx, struct nk_command_buffer *canvas,
                                  struct nk_rect bounds,
                                  const char *caption, struct nk_color fill,
                                  struct nk_color border, struct nk_color gold,
                                  float s)
{
    nk_fill_rect(canvas, bounds, 8.0f, fill);
    nk_stroke_rect(canvas, bounds, 8.0f, 1.0f, border);
    if (caption) {
        const float strip_h = nilamp_gui_line_height(ctx, s) + 2.0f * s;
        const float pad_y = 3.0f * s;
        const struct nk_rect strip = nk_rect(bounds.x, bounds.y + bounds.h - strip_h,
                                            bounds.w, strip_h);
        nk_stroke_line(canvas, strip.x, strip.y, strip.x + strip.w, strip.y, 1.0f, border);
        nilamp_gui_draw_text(ctx, canvas,
                             nk_rect(strip.x, strip.y + pad_y, strip.w,
                                     strip.h - pad_y * 2.0f),
                             caption, gold, true);
    }
}

static void nilamp_gui_begin_edit(NilampGui *gui, uint32_t index)
{
    if (!gui || index >= gui->param_count || index >= NILAMP_GUI_MAX_PARAMS) {
        return;
    }
    nilamp_gui_grab_focus(gui, "begin_edit");
    for (uint32_t i = 0; i < gui->param_count && i < NILAMP_GUI_MAX_PARAMS; i++) {
        gui->model.edit_active[i] = false;
    }
    gui->active_edit = (int)index;
    gui->model.edit_active[index] = true;
    gui->edit_replace_on_type = true;
    nilamp_gui_format_edit_value(&gui->params[index], gui->model.param_values[index],
                                 gui->model.edit_text[index],
                                 sizeof(gui->model.edit_text[index]));
    gui->edit_cursor =
        nilamp_gui_bounded_strlen(gui->model.edit_text[index], NILAMP_GUI_EDIT_TEXT_LEN);
    nilamp_gui_log("begin_edit index=%u param=%u text=%s", index,
                   gui->params[index].id, gui->model.edit_text[index]);
}

static void nilamp_gui_end_edit(NilampGui *gui, bool commit, NilampGuiMsg *outbox,
                                uint32_t *outbox_count)
{
    if (!gui || gui->active_edit < 0 ||
        (uint32_t)gui->active_edit >= gui->param_count ||
        (uint32_t)gui->active_edit >= NILAMP_GUI_MAX_PARAMS) {
        return;
    }

    const uint32_t index = (uint32_t)gui->active_edit;
    const NilampGuiParamSpec *param = &gui->params[index];
    float raw = gui->model.param_values[index];
    if (commit) {
        const char *unit = NULL;
        (void)nilamp_gui_display_box_value(param, raw, &unit);
        bool ok = false;
        const float display =
            nilamp_gui_parse_box_value(param, gui->model.edit_text[index],
                                       unit && strcmp(unit, "kHz") == 0, &ok);
        if (ok) {
            raw = nilamp_gui_quantize(param, nilamp_gui_raw_from_display(param, display));
        }
    }

    if (commit && raw != gui->model.param_values[index]) {
        nilamp_gui_emit_param_gesture_begin(gui, index, outbox, outbox_count);
        nilamp_gui_emit(outbox, outbox_count, NILAMP_GUI_MAX_MSGS,
                        (NilampGuiMsg){
                            .type = NILAMP_GUI_MSG_PARAM_CHANGED,
                            .param_id = param->id,
                            .value = raw,
                        });
        nilamp_gui_emit_param_gesture_end(gui, index, outbox, outbox_count);
    }
    gui->model.param_values[index] = raw;
    gui->model.edit_active[index] = false;
    gui->active_edit = -1;
    gui->edit_replace_on_type = false;
    gui->edit_cursor = 0u;
    nilamp_gui_sync_edit_text(gui, index);
    nilamp_gui_log("end_edit index=%u param=%u commit=%d raw=%.9g",
                   index, param->id, commit ? 1 : 0, raw);
}

static bool nilamp_gui_edit_accepts_char(const NilampGuiParamSpec *param, char c)
{
    if (!param || c < 32 || c > 126) {
        return false;
    }
    if (nilamp_gui_is_percent_param(param)) {
        return c >= '0' && c <= '9';
    }
    if (nilamp_gui_is_hz_param(param)) {
        return (c >= '0' && c <= '9') || c == '.';
    }
    if (nilamp_gui_is_db_param(param)) {
        return (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+';
    }
    return (c >= '0' && c <= '9') || c == '.';
}

static size_t nilamp_gui_param_edit_limit(const NilampGuiParamSpec *param, float raw_value)
{
    if (!param) {
        return 4u;
    }
    if (nilamp_gui_is_percent_param(param)) {
        return 3u;
    }

    char text[3][NILAMP_GUI_EDIT_TEXT_LEN];
    nilamp_gui_format_edit_value(param, param->min_value, text[0], sizeof(text[0]));
    nilamp_gui_format_edit_value(param, raw_value, text[1], sizeof(text[1]));
    nilamp_gui_format_edit_value(param, param->max_value, text[2], sizeof(text[2]));
    size_t limit = 0u;
    for (size_t i = 0u; i < 3u; i++) {
        limit = nilamp_gui_max_size(
            limit, nilamp_gui_bounded_strlen(text[i], NILAMP_GUI_EDIT_TEXT_LEN));
    }
    return nilamp_gui_clamp_size(limit + 2u, 4u, 8u);
}

static void nilamp_gui_update_active_edit(NilampGui *gui, NilampGuiMsg *outbox,
                                          uint32_t *outbox_count)
{
    if (!gui || gui->active_edit < 0 ||
        (uint32_t)gui->active_edit >= gui->param_count ||
        (uint32_t)gui->active_edit >= NILAMP_GUI_MAX_PARAMS) {
        return;
    }

    if (gui->key_escape) {
        nilamp_gui_end_edit(gui, false, outbox, outbox_count);
        return;
    }
    if (gui->key_enter) {
        nilamp_gui_end_edit(gui, true, outbox, outbox_count);
        return;
    }

    char *text = gui->model.edit_text[gui->active_edit];
    const NilampGuiParamSpec *param = &gui->params[gui->active_edit];
    const size_t edit_limit =
        nilamp_gui_param_edit_limit(param, gui->model.param_values[gui->active_edit]);
    nilamp_gui_edit_sanitize(text, NILAMP_GUI_EDIT_TEXT_LEN, &gui->edit_cursor);
    const size_t log_len = nilamp_gui_bounded_strlen(text, NILAMP_GUI_EDIT_TEXT_LEN);
    bool changed = false;
    if (gui->key_backspace || gui->key_delete) {
        nilamp_gui_log("edit_key index=%d backspace=%d delete=%d len=%zu cursor=%zu replace=%d",
                       gui->active_edit, gui->key_backspace ? 1 : 0,
                       gui->key_delete ? 1 : 0, log_len, gui->edit_cursor,
                       gui->edit_replace_on_type ? 1 : 0);
    }
    changed = nilamp_gui_edit_apply_keys(text, NILAMP_GUI_EDIT_TEXT_LEN, &gui->edit_cursor,
                                         &gui->edit_replace_on_type, gui->key_home,
                                         gui->key_end, gui->key_left, gui->key_right,
                                         gui->key_backspace, gui->key_delete) ||
              changed;
    for (uint32_t i = 0; i < gui->text_input_len && i < NILAMP_GUI_TEXT_INPUT_LEN; i++) {
        const char c = gui->text_input[i];
        if (nilamp_gui_edit_accepts_char(param, c)) {
            if (!nilamp_gui_edit_insert_char_limited(
                    text, NILAMP_GUI_EDIT_TEXT_LEN, edit_limit, &gui->edit_cursor,
                    &gui->edit_replace_on_type, c)) {
                break;
            }
            changed = true;
        }
    }
    if (changed) {
        gui->model.dirty = true;
        nilamp_gui_log("edit_changed index=%d text=%s", gui->active_edit, text);
    }
}

static void nilamp_gui_value_box(NilampGui *gui, struct nk_context *ctx,
                                 struct nk_command_buffer *canvas, uint32_t index,
                                 struct nk_rect bounds, const char *unit,
                                 NilampGuiMsg *outbox, uint32_t *outbox_count,
                                 float s)
{
    if (!gui || !ctx || !canvas || index >= gui->param_count ||
        index >= NILAMP_GUI_MAX_PARAMS) {
        return;
    }
    const bool hovered = nk_input_is_mouse_hovering_rect(&ctx->input, bounds);
    gui->value_box_hovered = gui->value_box_hovered || hovered;
    if (hovered && nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT)) {
        if (gui->active_edit >= 0 && gui->active_edit != (int)index) {
            nilamp_gui_end_edit(gui, true, outbox, outbox_count);
        }
        if (gui->active_edit != (int)index) {
            nilamp_gui_begin_edit(gui, index);
        }
    }

    const bool active = gui->active_edit == (int)index;
    const struct nk_color border = active ? nk_rgb(255, 205, 32) : nk_rgb(94, 116, 134);
    const struct nk_color fill = active ? nk_rgb(18, 29, 42) : nk_rgb(20, 33, 44);
    const struct nk_color text = active ? nk_rgb(255, 232, 116) : nk_rgb(255, 205, 32);
    nk_fill_rect(canvas, bounds, 2.0f * s, fill);
    nk_stroke_rect(canvas, bounds, 2.0f * s, 1.0f, border);
    gui->model.edit_text[index][NILAMP_GUI_EDIT_TEXT_LEN - 1u] = '\0';
    const char *box_text = gui->model.edit_text[index];
    const float pad_x = 4.0f * s;
    const float pad_y = 3.0f * s;
    const struct nk_rect text_rect = nk_rect(bounds.x + pad_x, bounds.y + pad_y,
                                            bounds.w - pad_x * 2.0f,
                                            bounds.h - pad_y * 2.0f);
    char display_text[NILAMP_GUI_EDIT_TEXT_LEN + 16u];
    if (unit && unit[0]) {
        (void)snprintf(display_text, sizeof(display_text), "%s %s", box_text, unit);
    } else {
        (void)snprintf(display_text, sizeof(display_text), "%s", box_text);
    }
    nilamp_gui_draw_text(ctx, canvas, text_rect, display_text, text, true);
    if (active) {
        const struct nk_user_font *font = ctx->style.font;
        const int len = (int)nilamp_gui_bounded_strlen(box_text, NILAMP_GUI_EDIT_TEXT_LEN);
        const int display_len =
            (int)nilamp_gui_bounded_strlen(display_text, sizeof(display_text));
        const int cursor_len =
            (int)(gui->edit_cursor < (size_t)len ? gui->edit_cursor : (size_t)len);
        float text_width = 0.0f;
        float display_width = 0.0f;
        if (font && font->width) {
            if (cursor_len > 0) {
                text_width = font->width(font->userdata, font->height,
                                         box_text, cursor_len);
            }
            if (display_len > 0) {
                display_width = font->width(font->userdata, font->height,
                                            display_text, display_len);
            }
        }
        float text_x = text_rect.x;
        if (display_width < text_rect.w) {
            text_x += (text_rect.w - display_width) * 0.5f;
        }
        const float caret_x = nilamp_gui_clampf(text_x + text_width + 1.0f,
                                                bounds.x + pad_x,
                                                bounds.x + bounds.w - pad_x);
        nk_stroke_line(canvas, caret_x, bounds.y + pad_y, caret_x,
                       bounds.y + bounds.h - pad_y, 1.0f, text);
    }
}

static float nilamp_gui_knob_noon_value(const NilampGuiParamSpec *param,
                                        NilampGuiKnobStyle knob_style)
{
    if (!param) {
        return 0.0f;
    }
    if (knob_style == NILAMP_GUI_KNOB_GAIN_UNIPOLAR ||
        knob_style == NILAMP_GUI_KNOB_GAIN_BIPOLAR) {
        return nilamp_gui_quantize(param, 0.0f);
    }
    return nilamp_gui_quantize(param, param->min_value +
                                      (param->max_value - param->min_value) * 0.5f);
}

static bool nilamp_gui_has_vertical_scroll(const NilampGui *gui)
{
    return gui && fabsf(gui->scroll_y) >= 0.0001f;
}

static float nilamp_gui_scroll_step_value(const NilampGuiParamSpec *param, float value,
                                          float scroll_y)
{
    if (!param || scroll_y == 0.0f) {
        return value;
    }
    const float range = param->max_value - param->min_value;
    if (range <= 0.0f) {
        return value;
    }
    const float step = param->step > 0.0f ? param->step : range / 100.0f;
    return nilamp_gui_quantize(param, value + step * scroll_y);
}

static bool nilamp_gui_emit_param_value(NilampGui *gui, uint32_t index, float value,
                                        NilampGuiMsg *outbox, uint32_t *outbox_count)
{
    if (!gui || index >= gui->param_count || index >= NILAMP_GUI_MAX_PARAMS) {
        return false;
    }
    const NilampGuiParamSpec *param = &gui->params[index];
    value = nilamp_gui_quantize(param, value);
    if (value == gui->model.param_values[index]) {
        return false;
    }
    nilamp_gui_emit(outbox, outbox_count, NILAMP_GUI_MAX_MSGS,
                    (NilampGuiMsg){
                        .type = NILAMP_GUI_MSG_PARAM_CHANGED,
                        .param_id = param->id,
                        .value = value,
                    });
    return true;
}

static bool nilamp_gui_emit_param_value_once(NilampGui *gui, uint32_t index, float value,
                                             NilampGuiMsg *outbox,
                                             uint32_t *outbox_count)
{
    const uint32_t before = outbox_count ? *outbox_count : 0u;
    if (!nilamp_gui_emit_param_value(gui, index, value, outbox, outbox_count)) {
        return false;
    }
    if (outbox_count && *outbox_count > before) {
        const NilampGuiMsg value_msg = outbox[before];
        outbox[before] = (NilampGuiMsg){
            .type = NILAMP_GUI_MSG_PARAM_GESTURE_BEGIN,
            .param_id = value_msg.param_id,
        };
        nilamp_gui_emit(outbox, outbox_count, NILAMP_GUI_MAX_MSGS, value_msg);
        nilamp_gui_emit(outbox, outbox_count, NILAMP_GUI_MAX_MSGS,
                        (NilampGuiMsg){
                            .type = NILAMP_GUI_MSG_PARAM_GESTURE_END,
                            .param_id = value_msg.param_id,
                        });
    }
    return true;
}

static bool nilamp_gui_knob_has_bubble(NilampGuiKnobStyle knob_style)
{
    return knob_style == NILAMP_GUI_KNOB_PERCENT ||
           knob_style == NILAMP_GUI_KNOB_GAIN_BIPOLAR;
}

static bool nilamp_gui_knob(NilampGui *gui, struct nk_context *ctx,
                            struct nk_command_buffer *canvas, uint32_t index,
                            struct nk_rect bounds, NilampGuiMsg *outbox,
                            uint32_t *outbox_count, float radius,
                            NilampGuiKnobStyle knob_style, float s)
{
    if (!gui || !ctx || !canvas || index >= gui->param_count ||
        index >= NILAMP_GUI_MAX_PARAMS) {
        return false;
    }

    const NilampGuiParamSpec *param = &gui->params[index];
    const float range = param->max_value - param->min_value;
    if (range <= 0.0f) {
        return false;
    }

    float value = gui->model.param_values[index];
    const float cx = bounds.x + bounds.w * 0.5f;
    const char *unit = NULL;
    (void)nilamp_gui_display_box_value(param, value, &unit);
    const char *box_text = gui->model.edit_text[index];
    char display_text[NILAMP_GUI_EDIT_TEXT_LEN + 16u];
    if (unit && unit[0]) {
        (void)snprintf(display_text, sizeof(display_text), "%s %s", box_text, unit);
    } else {
        (void)snprintf(display_text, sizeof(display_text), "%s", box_text);
    }
    const float label_h = nilamp_gui_line_height(ctx, s);
    const float box_h = nilamp_gui_box_height(ctx, s);
    const float wanted_box_w = nilamp_gui_text_width(ctx, display_text) + 12.0f * s;
    const float max_box_w = nilamp_gui_maxf(20.0f * s, bounds.w - 8.0f * s);
    const float box_w = fminf(max_box_w, nilamp_gui_maxf(54.0f * s, wanted_box_w));
    const struct nk_rect edit_rect =
        nk_rect(cx - box_w * 0.5f, bounds.y + bounds.h - box_h - 7.0f * s,
                box_w, box_h);
    const float label_bottom = bounds.y + label_h;
    const float box_top = edit_rect.y;
    float cy = (label_bottom + box_top) * 0.5f;
    const float knob_gap = 2.0f * s;
    const float min_cy = label_bottom + radius + knob_gap;
    const float max_cy = box_top - radius - knob_gap;
    if (max_cy >= min_cy) {
        cy = nilamp_gui_clampf(cy, min_cy, max_cy);
    }
    const struct nk_rect circle = nk_rect(cx - radius, cy - radius, radius * 2.0f,
                                          radius * 2.0f);
    const bool hovered = nk_input_is_mouse_hovering_rect(&ctx->input, bounds);
    const bool knob_hovered = nk_input_is_mouse_hovering_rect(&ctx->input, circle);
    const bool pressed = knob_hovered && nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT);
    if (hovered && nilamp_gui_has_vertical_scroll(gui) && gui->active_edit < 0) {
        value = nilamp_gui_scroll_step_value(param, value, gui->scroll_y);
        (void)nilamp_gui_emit_param_value_once(gui, index, value, outbox, outbox_count);
    }
    if (pressed && gui->mouse_double_click) {
        value = nilamp_gui_knob_noon_value(param, knob_style);
        gui->active_knob = -1;
        (void)nilamp_gui_emit_param_value_once(gui, index, value, outbox, outbox_count);
    } else if (pressed) {
        gui->active_knob = (int)index;
        nilamp_gui_begin_param_gesture(gui, index, outbox, outbox_count);
    }
    if (nk_input_is_mouse_released(&ctx->input, NK_BUTTON_LEFT) &&
        gui->active_knob == (int)index) {
        gui->active_knob = -1;
        nilamp_gui_end_param_gesture(gui, index, outbox, outbox_count);
    }
    if (gui->active_knob == (int)index &&
        nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT)) {
        const float delta = (-ctx->input.mouse.delta.y + ctx->input.mouse.delta.x * 0.35f) *
                            range / 180.0f;
        if (delta != 0.0f) {
            value = nilamp_gui_quantize(param, value + delta);
            (void)nilamp_gui_emit_param_value(gui, index, value, outbox, outbox_count);
        }
    }

    const struct nk_color gold = nk_rgb(255, 205, 32);
    const struct nk_color gold_hi = nk_rgb(255, 232, 116);
    const struct nk_color knob = nk_rgb(49, 76, 103);
    const struct nk_color knob_hi = nk_rgb(68, 96, 122);
    const struct nk_color shadow = nk_rgb(18, 29, 42);
    const struct nk_color edge = nk_rgb(94, 116, 134);
    const struct nk_color text = nk_rgb(255, 205, 32);

    nilamp_gui_draw_text(ctx, canvas,
                         nk_rect(bounds.x, bounds.y, bounds.w, label_h),
                         param->name, text, true);

    nk_fill_circle(canvas, nk_rect(circle.x + 2.0f, circle.y + 3.0f, circle.w, circle.h),
                   shadow);
    nk_fill_circle(canvas, circle, gui->active_knob == (int)index ? knob_hi : knob);
    nk_stroke_circle(canvas, circle, 1.0f, edge);
    if (nilamp_gui_knob_has_bubble(knob_style)) {
        const float bubble_angle = 135.0f * NILAMP_GUI_DEG_TO_RAD;
        nk_fill_circle(canvas,
                       nk_rect(cx + cosf(bubble_angle) * radius * 0.78f - radius * 0.12f,
                               cy + sinf(bubble_angle) * radius * 0.78f - radius * 0.12f,
                               radius * 0.24f, radius * 0.24f),
                       nk_rgba(126, 158, 185, 65));
    }

    const float normalized = (value - param->min_value) / range;
    float angle_deg = 135.0f + normalized * 270.0f;
    if (knob_style == NILAMP_GUI_KNOB_GAIN_UNIPOLAR) {
        angle_deg = -90.0f + normalized * 270.0f;
    } else if (knob_style == NILAMP_GUI_KNOB_GAIN_BIPOLAR) {
        const float center = nilamp_gui_clampf((0.0f - param->min_value) / range, 0.0f, 1.0f);
        angle_deg = -90.0f + ((normalized - center) / fmaxf(center, 1.0f - center)) * 135.0f;
    }
    const float notch_angle = -90.0f * NILAMP_GUI_DEG_TO_RAD;
    nk_stroke_line(canvas,
                   cx + cosf(notch_angle) * radius * 0.76f,
                   cy + sinf(notch_angle) * radius * 0.76f,
                   cx + cosf(notch_angle) * radius * 0.95f,
                   cy + sinf(notch_angle) * radius * 0.95f,
                   1.0f, nk_rgba(255, 205, 32, 115));
    const float angle = angle_deg * NILAMP_GUI_DEG_TO_RAD;
    const float needle_len = radius * 0.78f;
    const float x1 = cx + cosf(angle) * radius * 0.18f;
    const float y1 = cy + sinf(angle) * radius * 0.18f;
    const float x2 = cx + cosf(angle) * needle_len;
    const float y2 = cy + sinf(angle) * needle_len;
    nk_stroke_line(canvas, x1, y1, x2, y2, 2.0f,
                   gui->active_knob == (int)index ? gold_hi : gold);

    nilamp_gui_value_box(gui, ctx, canvas, index, edit_rect, unit, outbox, outbox_count,
                         s);
    return hovered || gui->active_knob == (int)index;
}

static void nilamp_gui_dropdown_box(struct nk_context *ctx,
                                    struct nk_command_buffer *canvas,
                                    struct nk_rect bounds, const char *text,
                                    bool hovered, float s)
{
    const struct nk_color fill = hovered ? nk_rgb(57, 84, 107) : nk_rgb(49, 76, 103);
    const struct nk_color border = nk_rgb(105, 123, 137);
    const struct nk_color gold = nk_rgb(255, 205, 32);
    nk_fill_rect(canvas, bounds, 5.0f * s, fill);
    nk_stroke_rect(canvas, bounds, 5.0f * s, 1.0f, border);
    nilamp_gui_draw_text(ctx, canvas, nk_rect(bounds.x + 5.0f * s, bounds.y + 4.0f * s,
                                             bounds.w - 19.0f * s,
                                             bounds.h - 7.0f * s),
                         text, gold, true);
    const float ax = bounds.x + bounds.w - 13.0f * s;
    const float ay = bounds.y + bounds.h * 0.5f - 1.0f;
    nk_fill_triangle(canvas, ax - 4.0f * s, ay - 2.0f * s,
                     ax + 4.0f * s, ay - 2.0f * s,
                     ax, ay + 3.0f * s, gold);
}

static void nilamp_gui_close_dropdown(NilampGui *gui);

static void nilamp_gui_enum_toggle(NilampGui *gui, struct nk_context *ctx,
                                   struct nk_command_buffer *canvas, uint32_t index,
                                   struct nk_rect bounds, NilampGuiMsg *outbox,
                                   uint32_t *outbox_count, float s)
{
    if (!gui || !ctx || !canvas || index >= gui->param_count ||
        index >= NILAMP_GUI_MAX_PARAMS || !gui->params[index].enum_names ||
        gui->params[index].enum_count != 2u) {
        return;
    }

    const NilampGuiParamSpec *spec = &gui->params[index];
    const struct nk_color gold = nk_rgb(255, 205, 32);
    const struct nk_color gold_hi = nk_rgb(255, 232, 116);
    const struct nk_color track = nk_rgb(20, 32, 43);
    const struct nk_color border = nk_rgb(105, 123, 137);
    const int value = (int)lroundf(gui->model.param_values[index]);
    const uint32_t safe_value = (value >= 0 && (uint32_t)value < spec->enum_count) ?
                                    (uint32_t)value :
                                    (uint32_t)lroundf(spec->default_value);

    nilamp_gui_draw_text(ctx, canvas,
                         nk_rect(bounds.x, bounds.y, bounds.w,
                                 nilamp_gui_line_height(ctx, s)),
                         spec->name, gold, true);

    const float track_w = fminf(bounds.w - 28.0f * s, 36.0f * s);
    const float track_h = 14.0f * s;
    const struct nk_rect switch_rect =
        nk_rect(bounds.x + (bounds.w - track_w) * 0.5f, bounds.y + 30.0f * s,
                track_w, track_h);
    const struct nk_rect hit_rect =
        nk_rect(switch_rect.x - 6.0f * s, switch_rect.y - 8.0f * s,
                switch_rect.w + 12.0f * s, switch_rect.h + 31.0f * s);
    const bool hovered = nk_input_is_mouse_hovering_rect(&ctx->input, hit_rect);

    nk_fill_rect(canvas, switch_rect, 1.0f, track);
    nk_stroke_rect(canvas, switch_rect, 1.0f, 1.0f, border);
    const float thumb_w = switch_rect.w * 0.5f;
    const float thumb_x = safe_value == 0u ?
                              switch_rect.x + 1.0f :
                              switch_rect.x + switch_rect.w - thumb_w - 1.0f;
    nk_fill_rect(canvas, nk_rect(thumb_x, switch_rect.y + 1.0f,
                                 thumb_w, switch_rect.h - 2.0f),
                 1.0f, hovered ? gold_hi : gold);

    nilamp_gui_draw_text(ctx, canvas,
                         nk_rect(bounds.x, bounds.y + 52.0f * s, bounds.w,
                                 nilamp_gui_line_height(ctx, s)),
                         spec->enum_names[safe_value], gold, true);

    if (hovered && nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT)) {
        nilamp_gui_close_dropdown(gui);
        if (gui->active_edit >= 0) {
            nilamp_gui_end_edit(gui, true, outbox, outbox_count);
        }
        (void)nilamp_gui_emit_param_value_once(gui, index,
                                               safe_value == 0u ? 1.0f : 0.0f,
                                               outbox, outbox_count);
    } else if (hovered && nilamp_gui_has_vertical_scroll(gui) && gui->active_edit < 0) {
        nilamp_gui_close_dropdown(gui);
        (void)nilamp_gui_emit_param_value_once(
            gui, index, nilamp_gui_scroll_step_value(spec, gui->model.param_values[index],
                                                     gui->scroll_y),
            outbox, outbox_count);
    }
}

static void nilamp_gui_close_dropdown(NilampGui *gui)
{
    if (!gui) {
        return;
    }
    gui->open_dropdown = NILAMP_GUI_DROPDOWN_NONE;
    gui->open_dropdown_param = 0u;
}

static void nilamp_gui_enum_dropdown(NilampGui *gui, struct nk_context *ctx,
                                     struct nk_command_buffer *canvas, uint32_t index,
                                     struct nk_rect bounds, NilampGuiMsg *outbox,
                                     uint32_t *outbox_count, float s)
{
    if (!gui || !ctx || !canvas || index >= gui->param_count ||
        index >= NILAMP_GUI_MAX_PARAMS || !gui->params[index].enum_names ||
        gui->params[index].enum_count == 0u) {
        return;
    }
    const NilampGuiParamSpec *spec = &gui->params[index];
    const struct nk_color gold = nk_rgb(255, 205, 32);
    const int value = (int)lroundf(gui->model.param_values[index]);
    const uint32_t safe_value = (value >= 0 && (uint32_t)value < spec->enum_count) ?
                                    (uint32_t)value :
                                    (uint32_t)lroundf(spec->default_value);
    nilamp_gui_draw_text(ctx, canvas,
                         nk_rect(bounds.x, bounds.y, bounds.w,
                                 nilamp_gui_line_height(ctx, s)),
                         spec->name, gold, true);
    const float selector_h = nilamp_gui_maxf(nilamp_gui_box_height(ctx, s), 25.0f * s);
    const struct nk_rect selector =
        nk_rect(bounds.x + 4.0f * s, bounds.y + 47.0f * s,
                bounds.w - 8.0f * s, selector_h);
    const bool hovered = nk_input_is_mouse_hovering_rect(&ctx->input, selector);
    const bool is_open = gui->open_dropdown == NILAMP_GUI_DROPDOWN_ENUM &&
                         gui->open_dropdown_param == spec->id;
    gui->dropdown_hovered = gui->dropdown_hovered || hovered;
    nilamp_gui_dropdown_box(ctx, canvas, selector, spec->enum_names[safe_value],
                            hovered, s);
    if (hovered && nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT)) {
        if (gui->active_edit >= 0) {
            nilamp_gui_end_edit(gui, true, outbox, outbox_count);
        }
        if (is_open) {
            nilamp_gui_close_dropdown(gui);
        } else {
            gui->open_dropdown = NILAMP_GUI_DROPDOWN_ENUM;
            gui->open_dropdown_param = spec->id;
            gui->open_dropdown_selector = selector;
        }
    } else if (hovered && nilamp_gui_has_vertical_scroll(gui) && gui->active_edit < 0) {
        nilamp_gui_close_dropdown(gui);
        (void)nilamp_gui_emit_param_value_once(
            gui, index, nilamp_gui_scroll_step_value(spec, gui->model.param_values[index],
                                                     gui->scroll_y),
            outbox, outbox_count);
    }
}

static void nilamp_gui_draw_open_dropdown(NilampGui *gui, struct nk_context *ctx,
                                          struct nk_command_buffer *canvas,
                                          NilampGuiMsg *outbox, uint32_t *outbox_count)
{
    if (!gui || !ctx || !canvas ||
        gui->open_dropdown != NILAMP_GUI_DROPDOWN_ENUM) {
        return;
    }
    const uint32_t index = nilamp_gui_param_index(gui, gui->open_dropdown_param);
    if (index >= gui->param_count || index >= NILAMP_GUI_MAX_PARAMS ||
        !gui->params[index].enum_names || gui->params[index].enum_count == 0u) {
        nilamp_gui_close_dropdown(gui);
        return;
    }
    const NilampGuiParamSpec *spec = &gui->params[index];

    const struct nk_color fill = nk_rgb(32, 51, 68);
    const struct nk_color fill_hover = nk_rgb(57, 84, 107);
    const struct nk_color border = nk_rgb(105, 123, 137);
    const struct nk_color gold = nk_rgb(255, 205, 32);
    const float row_h = nilamp_gui_maxf(nilamp_gui_font_height(ctx) + 8.0f,
                                        gui->open_dropdown_selector.h);
    const float gap = 3.0f;
    const struct nk_rect selector = gui->open_dropdown_selector;
    const float list_h = row_h * (float)spec->enum_count;
    const struct nk_rect list = nk_rect(selector.x, selector.y - list_h - gap,
                                       selector.w, list_h);
    const bool list_hovered = nk_input_is_mouse_hovering_rect(&ctx->input, list);
    gui->dropdown_hovered = gui->dropdown_hovered || list_hovered;

    nk_fill_rect(canvas, list, 5.0f, fill);
    nk_stroke_rect(canvas, list, 5.0f, 1.0f, border);
    for (uint32_t i = 0; i < spec->enum_count; i++) {
        const struct nk_rect item = nk_rect(list.x + 1.0f, list.y + (float)i * row_h + 1.0f,
                                           list.w - 2.0f, row_h - 2.0f);
        const bool item_hovered = nk_input_is_mouse_hovering_rect(&ctx->input, item);
        if (item_hovered) {
            nk_fill_rect(canvas, item, 4.0f, fill_hover);
        }
        nilamp_gui_draw_text(ctx, canvas, nk_rect(item.x + 4.0f, item.y + 4.0f,
                                                 item.w - 8.0f, item.h - 8.0f),
                             spec->enum_names[i], gold, true);
        if (item_hovered && nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT)) {
            (void)nilamp_gui_emit_param_value_once(gui, index, (float)i,
                                                   outbox, outbox_count);
            nilamp_gui_close_dropdown(gui);
            break;
        }
    }
}

static uint32_t nilamp_gui_find_param_index(const NilampGui *gui, uint32_t param_id)
{
    const uint32_t index = nilamp_gui_param_index(gui, param_id);
    return index < gui->param_count ? index : NILAMP_GUI_MAX_PARAMS;
}

static const struct nk_user_font *nilamp_gui_custom_font(const NilampGui *gui,
                                                         const struct nk_font *font)
{
    return gui && gui->custom_font_ready && font ? &font->handle : NULL;
}

static struct nk_color nilamp_gui_color(uint32_t rgb)
{
    return nk_rgb((int)((rgb >> 16) & 0xffu),
                  (int)((rgb >> 8) & 0xffu),
                  (int)(rgb & 0xffu));
}

static struct nk_color nilamp_gui_indication_color(const NilampGuiIndicationColor *color,
                                                   struct nk_color fallback)
{
    if (!color) {
        return fallback;
    }
    return nk_rgba((int)color->red, (int)color->green, (int)color->blue,
                   (int)color->alpha);
}

static struct nk_rect nilamp_gui_scale_spec_rect(float sx, float sy,
                                                 NilampGuiRectSpec rect)
{
    return nilamp_gui_scale_rect(sx, sy, rect.x, rect.y, rect.w, rect.h);
}

static const NilampGuiScreenSpec *nilamp_gui_current_screen_spec(const NilampGui *gui)
{
    if (!gui || !gui->layout) {
        return NULL;
    }
    const NilampGuiScreenId id = (NilampGuiScreenId)gui->screen;
    for (uint32_t i = 0; i < gui->layout->screen_count; i++) {
        if (gui->layout->screens[i].id == id) {
            return &gui->layout->screens[i];
        }
    }
    return NULL;
}

static const struct nk_user_font *nilamp_gui_font_for_text(const NilampGui *gui,
                                                           NilampGuiTextStyle style)
{
    switch (style) {
    case NILAMP_GUI_TEXT_TITLE:
        return nilamp_gui_custom_font(gui, gui ? gui->font_title : NULL);
    case NILAMP_GUI_TEXT_SUBTITLE:
        return nilamp_gui_custom_font(gui, gui ? gui->font_subtitle : NULL);
    case NILAMP_GUI_TEXT_ABOUT:
        return nilamp_gui_custom_font(gui, gui ? gui->font_about : NULL);
    case NILAMP_GUI_TEXT_NORMAL:
    default:
        return NULL;
    }
}

static NilampGuiKnobStyle nilamp_gui_knob_style_from_spec(NilampGuiKnobDisplay display)
{
    switch (display) {
    case NILAMP_GUI_KNOB_DISPLAY_GAIN_UNIPOLAR:
        return NILAMP_GUI_KNOB_GAIN_UNIPOLAR;
    case NILAMP_GUI_KNOB_DISPLAY_GAIN_BIPOLAR:
        return NILAMP_GUI_KNOB_GAIN_BIPOLAR;
    case NILAMP_GUI_KNOB_DISPLAY_PERCENT:
    default:
        return NILAMP_GUI_KNOB_PERCENT;
    }
}

static void nilamp_gui_draw_param_indication(NilampGui *gui,
                                             struct nk_context *ctx,
                                             struct nk_command_buffer *canvas,
                                             uint32_t index,
                                             struct nk_rect bounds)
{
    if (!gui || !ctx || !canvas || index >= gui->param_count ||
        index >= NILAMP_GUI_MAX_PARAMS) {
        return;
    }

    const NilampGuiParamIndication *indication = &gui->indications[index];
    if (!indication->has_mapping && indication->automation_state == 0u) {
        return;
    }

    const struct nk_color mapping_default = nk_rgba(82, 190, 255, 230);
    const struct nk_color automation_default = nk_rgba(255, 96, 112, 230);
    if (indication->has_mapping) {
        const struct nk_color color = nilamp_gui_indication_color(
            indication->has_mapping_color ? &indication->mapping_color : NULL,
            mapping_default);
        const struct nk_rect badge = nk_rect(bounds.x + bounds.w - 18.0f, bounds.y + 3.0f,
                                            14.0f, 14.0f);
        nk_fill_rect(canvas, badge, 3.0f, color);
        if (indication->mapping_label[0]) {
            nilamp_gui_draw_text(ctx, canvas,
                                 nk_rect(bounds.x + bounds.w - 64.0f, bounds.y + 1.0f,
                                         44.0f, 16.0f),
                                 indication->mapping_label, color, false);
        }
    }

    if (indication->automation_state != 0u) {
        const struct nk_color color = nilamp_gui_indication_color(
            indication->has_automation_color ? &indication->automation_color : NULL,
            automation_default);
        nk_fill_rect(canvas, nk_rect(bounds.x + 8.0f, bounds.y + bounds.h - 3.0f,
                                     bounds.w - 16.0f, 2.0f),
                     1.0f, color);
    }
}

static void nilamp_gui_draw_layout_widget(NilampGui *gui, struct nk_context *ctx,
                                          struct nk_command_buffer *canvas,
                                          const NilampGuiWidgetSpec *widget,
                                          float sx, float sy, float s,
                                          struct nk_color panel,
                                          struct nk_color border,
                                          struct nk_color text,
                                          NilampGuiMsg *outbox,
                                          uint32_t *outbox_count)
{
    if (!gui || !ctx || !canvas || !widget) {
        return;
    }
    const struct nk_rect bounds = nilamp_gui_scale_spec_rect(sx, sy, widget->bounds);
    uint32_t indication_index = NILAMP_GUI_MAX_PARAMS;
    switch (widget->type) {
    case NILAMP_GUI_WIDGET_TEXT:
        nilamp_gui_draw_text_with_font(
            ctx, canvas, bounds, widget->label, text, true,
            nilamp_gui_font_for_text(gui, widget->text_style));
        break;
    case NILAMP_GUI_WIDGET_BUTTON:
        nk_layout_space_push(ctx, bounds);
        if (nk_button_label(ctx, widget->label)) {
            nilamp_gui_close_dropdown(gui);
            if (gui->active_edit >= 0) {
                nilamp_gui_end_edit(gui, true, outbox, outbox_count);
            }
            gui->screen = (NilampGuiScreen)widget->target_screen;
            gui->model.dirty = true;
        }
        break;
    case NILAMP_GUI_WIDGET_PANEL:
        nilamp_gui_draw_panel(ctx, canvas, bounds, widget->label, panel, border, text, s);
        break;
    case NILAMP_GUI_WIDGET_KNOB:
        indication_index = nilamp_gui_find_param_index(gui, widget->param_id);
        (void)nilamp_gui_knob(gui, ctx, canvas,
                              indication_index,
                              bounds, outbox, outbox_count,
                              widget->radius * s,
                              nilamp_gui_knob_style_from_spec(widget->knob_display), s);
        break;
    case NILAMP_GUI_WIDGET_ENUM:
        indication_index = nilamp_gui_find_param_index(gui, widget->param_id);
        nilamp_gui_enum_dropdown(gui, ctx, canvas,
                                  indication_index, bounds, outbox, outbox_count, s);
        break;
    case NILAMP_GUI_WIDGET_TOGGLE:
        indication_index = nilamp_gui_find_param_index(gui, widget->param_id);
        nilamp_gui_enum_toggle(gui, ctx, canvas,
                               indication_index, bounds, outbox, outbox_count, s);
        break;
    default:
        break;
    }
    nilamp_gui_draw_param_indication(gui, ctx, canvas, indication_index, bounds);
}

static bool nilamp_gui_build_generated(NilampGui *gui, struct nk_context *ctx,
                                       struct nk_command_buffer *canvas,
                                       float width, float height,
                                       NilampGuiMsg *outbox,
                                       uint32_t *outbox_count)
{
    if (!gui || !ctx || !canvas || !gui->layout ||
        gui->layout->design_width == 0u || gui->layout->design_height == 0u) {
        return false;
    }

    const NilampGuiScreenSpec *screen = nilamp_gui_current_screen_spec(gui);
    if (!screen) {
        gui->screen = (NilampGuiScreen)gui->layout->default_screen;
        screen = nilamp_gui_current_screen_spec(gui);
        if (!screen) {
            return false;
        }
    }

    const float sx = width / (float)gui->layout->design_width;
    const float sy = height / (float)gui->layout->design_height;
    const float s = nilamp_gui_minf(sx, sy);
    const struct nk_color bg = nilamp_gui_color(gui->layout->theme.background);
    const struct nk_color header = nilamp_gui_color(gui->layout->theme.header);
    const struct nk_color header_rule = nilamp_gui_color(gui->layout->theme.header_rule);
    const struct nk_color panel = nilamp_gui_color(gui->layout->theme.panel);
    const struct nk_color border = nilamp_gui_color(gui->layout->theme.border);
    const struct nk_color text = nilamp_gui_color(gui->layout->theme.text);

    nk_fill_rect(canvas, nk_rect(0.0f, 0.0f, width, height), 0.0f, bg);
    nk_fill_rect(canvas, nilamp_gui_scale_rect(sx, sy, 0.0f, 0.0f,
                                               (float)gui->layout->design_width, 34.0f),
                 0.0f, header);
    nk_stroke_line(canvas, 0.0f, 33.0f * sy, width, 33.0f * sy, 1.0f, header_rule);

    gui->value_box_hovered = false;
    gui->dropdown_hovered = false;
    if (gui->key_escape) {
        nilamp_gui_close_dropdown(gui);
    }
    nilamp_gui_update_active_edit(gui, outbox, outbox_count);

    nk_layout_space_begin(ctx, NK_STATIC, height, 80);
    for (uint32_t i = 0; i < screen->widget_count; i++) {
        nilamp_gui_draw_layout_widget(gui, ctx, canvas, &screen->widgets[i], sx, sy, s,
                                      panel, border, text, outbox, outbox_count);
    }
    nk_layout_space_end(ctx);
    nilamp_gui_draw_open_dropdown(gui, ctx, canvas, outbox, outbox_count);
    if (gui->open_dropdown != NILAMP_GUI_DROPDOWN_NONE && !gui->dropdown_hovered &&
        nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT)) {
        nilamp_gui_close_dropdown(gui);
    }
    if (gui->active_edit >= 0 && !gui->value_box_hovered &&
        nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT)) {
        nilamp_gui_end_edit(gui, true, outbox, outbox_count);
    }
    return true;
}

static void nilamp_gui_build(NilampGui *gui, struct nk_context *ctx,
                             NilampGuiMsg *outbox, uint32_t *outbox_count)
{
    const float width = (float)gui->model.width;
    const float height = (float)gui->model.height;
    const nk_flags flags = NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND;
    if (!nk_begin(ctx, "nilamp", nk_rect(0.0f, 0.0f, width, height), flags)) {
        nk_end(ctx);
        return;
    }

    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
    if (nilamp_gui_build_generated(gui, ctx, canvas, width, height,
                                   outbox, outbox_count)) {
        nk_end(ctx);
        return;
    }

    const float sx = width / (float)NILAMP_GUI_DEFAULT_WIDTH;
    const float sy = height / (float)NILAMP_GUI_DEFAULT_HEIGHT;
    const float s = nilamp_gui_minf(sx, sy);
    const struct nk_color bg = nk_rgb(28, 45, 61);
    const struct nk_color panel = nk_rgb(34, 52, 68);
    const struct nk_color border = nk_rgb(105, 123, 137);
    const struct nk_color gold = nk_rgb(255, 205, 32);

    nk_fill_rect(canvas, nk_rect(0.0f, 0.0f, width, height), 0.0f, bg);
    nk_fill_rect(canvas, nilamp_gui_scale_rect(sx, sy, 0.0f, 0.0f, 500.0f, 34.0f),
                 0.0f, nk_rgb(24, 37, 50));
    nk_stroke_line(canvas, 0.0f, 33.0f * sy, width, 33.0f * sy, 1.0f,
                   nk_rgb(83, 103, 119));

    gui->value_box_hovered = false;
    gui->dropdown_hovered = false;
    if (gui->key_escape) {
        nilamp_gui_close_dropdown(gui);
    }
    nilamp_gui_update_active_edit(gui, outbox, outbox_count);

    nk_layout_space_begin(ctx, NK_STATIC, height, 80);
    if (gui->screen == NILAMP_GUI_SCREEN_MAIN) {
        nilamp_gui_draw_text(ctx, canvas,
                             nilamp_gui_scale_rect(sx, sy, 0.0f, 10.0f, 500.0f, 18.0f),
                             "TWD DLX II", gold, true);
        nk_layout_space_push(ctx, nilamp_gui_scale_rect(sx, sy, 433.0f, 2.0f, 65.0f, 28.0f));
        if (nk_button_label(ctx, "Options")) {
            nilamp_gui_close_dropdown(gui);
            gui->screen = NILAMP_GUI_SCREEN_OPTIONS;
            gui->model.dirty = true;
        }

        nilamp_gui_draw_panel(ctx, canvas, nilamp_gui_scale_rect(sx, sy, 10.0f, 43.0f, 93.0f, 139.0f),
                              "Input", panel, border, gold, s);
        nilamp_gui_draw_panel(ctx, canvas, nilamp_gui_scale_rect(sx, sy, 400.0f, 43.0f, 93.0f, 139.0f),
                              "Output", panel, border, gold, s);
        nilamp_gui_draw_panel(ctx, canvas, nilamp_gui_scale_rect(sx, sy, 10.0f, 189.0f, 381.0f, 138.0f),
                              "Pre Amp", panel, border, gold, s);
        nilamp_gui_draw_panel(ctx, canvas, nilamp_gui_scale_rect(sx, sy, 400.0f, 189.0f, 93.0f, 138.0f),
                              "Splitter", panel, border, gold, s);

        nilamp_gui_draw_text_with_font(
            ctx, canvas, nilamp_gui_scale_rect(sx, sy, 0.0f, 66.0f, 500.0f, 60.0f),
            "nilamp", gold, true,
            nilamp_gui_custom_font(gui, gui->font_title));
        nilamp_gui_draw_text_with_font(
            ctx, canvas, nilamp_gui_scale_rect(sx, sy, 0.0f, 124.0f, 500.0f, 40.0f),
            "KELLER TWD DLX II", gold, true,
            nilamp_gui_custom_font(gui, gui->font_subtitle));

        const uint32_t input_gain = nilamp_gui_find_param_index(gui, NILAMP_PARAM_GAIN_DB);
        const uint32_t output_gain = nilamp_gui_find_param_index(gui, NILAMP_PARAM_OUTPUT_GAIN_DB);
        const uint32_t volume = nilamp_gui_find_param_index(gui, NILAMP_PARAM_VOLUME_PCT);
        const uint32_t bass = nilamp_gui_find_param_index(gui, NILAMP_PARAM_BASS_PCT);
        const uint32_t mid = nilamp_gui_find_param_index(gui, NILAMP_PARAM_MID_PCT);
        const uint32_t treble = nilamp_gui_find_param_index(gui, NILAMP_PARAM_TREBLE_PCT);
        const uint32_t tube1 = nilamp_gui_find_param_index(gui, NILAMP_PARAM_TUBE1);
        const uint32_t splitter = nilamp_gui_find_param_index(gui, NILAMP_PARAM_PHASE_SPLITTER);
        const float small_radius = 23.0f * s;
        const float pre_radius = 23.0f * s;
        (void)nilamp_gui_knob(gui, ctx, canvas, input_gain,
                              nilamp_gui_scale_rect(sx, sy, 24.0f, 60.0f, 66.0f, 104.0f),
                              outbox, outbox_count, small_radius,
                              NILAMP_GUI_KNOB_GAIN_BIPOLAR, s);
        (void)nilamp_gui_knob(gui, ctx, canvas, output_gain,
                              nilamp_gui_scale_rect(sx, sy, 413.0f, 60.0f, 66.0f, 104.0f),
                              outbox, outbox_count, small_radius,
                              NILAMP_GUI_KNOB_GAIN_BIPOLAR, s);
        nilamp_gui_enum_dropdown(gui, ctx, canvas, tube1,
                                  nilamp_gui_scale_rect(sx, sy, 21.0f, 207.0f,
                                                        76.0f, 104.0f),
                                  outbox, outbox_count, s);
        (void)nilamp_gui_knob(gui, ctx, canvas, volume,
                              nilamp_gui_scale_rect(sx, sy, 100.0f, 207.0f, 66.0f, 104.0f),
                              outbox, outbox_count, pre_radius, NILAMP_GUI_KNOB_PERCENT, s);
        (void)nilamp_gui_knob(gui, ctx, canvas, bass,
                              nilamp_gui_scale_rect(sx, sy, 176.0f, 207.0f, 66.0f, 104.0f),
                              outbox, outbox_count, pre_radius, NILAMP_GUI_KNOB_PERCENT, s);
        (void)nilamp_gui_knob(gui, ctx, canvas, mid,
                              nilamp_gui_scale_rect(sx, sy, 252.0f, 207.0f, 66.0f, 104.0f),
                              outbox, outbox_count, pre_radius, NILAMP_GUI_KNOB_PERCENT, s);
        (void)nilamp_gui_knob(gui, ctx, canvas, treble,
                              nilamp_gui_scale_rect(sx, sy, 328.0f, 207.0f, 66.0f, 104.0f),
                              outbox, outbox_count, pre_radius, NILAMP_GUI_KNOB_PERCENT, s);
        nilamp_gui_enum_dropdown(gui, ctx, canvas, splitter,
                                  nilamp_gui_scale_rect(sx, sy, 407.0f, 207.0f,
                                                        79.0f, 104.0f),
                                  outbox, outbox_count, s);
    } else if (gui->screen == NILAMP_GUI_SCREEN_OPTIONS) {
        nilamp_gui_draw_text(ctx, canvas,
                             nilamp_gui_scale_rect(sx, sy, 0.0f, 10.0f, 500.0f, 18.0f),
                             "Options", gold, true);
        nk_layout_space_push(ctx, nilamp_gui_scale_rect(sx, sy, 0.0f, 2.0f, 65.0f, 28.0f));
        if (nk_button_label(ctx, "< back")) {
            nilamp_gui_close_dropdown(gui);
            gui->screen = NILAMP_GUI_SCREEN_MAIN;
            gui->model.dirty = true;
        }
        nk_layout_space_push(ctx, nilamp_gui_scale_rect(sx, sy, 435.0f, 2.0f, 63.0f, 28.0f));
        if (nk_button_label(ctx, "About")) {
            nilamp_gui_close_dropdown(gui);
            if (gui->active_edit >= 0) {
                nilamp_gui_end_edit(gui, true, outbox, outbox_count);
            }
            gui->screen = NILAMP_GUI_SCREEN_ABOUT;
            gui->model.dirty = true;
        }

        nilamp_gui_draw_panel(ctx, canvas, nilamp_gui_scale_rect(sx, sy, 10.0f, 43.0f, 165.0f, 139.0f),
                              "Tone Stack", panel, border, gold, s);
        nilamp_gui_draw_panel(ctx, canvas, nilamp_gui_scale_rect(sx, sy, 256.0f, 43.0f, 237.0f, 139.0f),
                              "Speaker Inductor", panel, border, gold, s);
        nilamp_gui_draw_panel(ctx, canvas, nilamp_gui_scale_rect(sx, sy, 10.0f, 189.0f, 78.0f, 138.0f),
                              "Power", panel, border, gold, s);
        nilamp_gui_draw_panel(ctx, canvas, nilamp_gui_scale_rect(sx, sy, 97.0f, 189.0f, 94.0f, 138.0f),
                              "Gain", panel, border, gold, s);
        nilamp_gui_draw_panel(ctx, canvas, nilamp_gui_scale_rect(sx, sy, 200.0f, 189.0f, 293.0f, 138.0f),
                              "Speaker Resonance", panel, border, gold, s);

        const float radius = 23.0f * s;
        (void)nilamp_gui_knob(gui, ctx, canvas,
                              nilamp_gui_find_param_index(gui, NILAMP_PARAM_TONE_FMID_DBHZ),
                              nilamp_gui_scale_rect(sx, sy, 25.0f, 60.0f, 66.0f, 104.0f),
                              outbox, outbox_count, radius, NILAMP_GUI_KNOB_PERCENT, s);
        (void)nilamp_gui_knob(gui, ctx, canvas,
                              nilamp_gui_find_param_index(gui, NILAMP_PARAM_TONE_QMID_DB),
                              nilamp_gui_scale_rect(sx, sy, 100.0f, 60.0f, 66.0f, 104.0f),
                              outbox, outbox_count, radius, NILAMP_GUI_KNOB_PERCENT, s);
        (void)nilamp_gui_knob(gui, ctx, canvas,
                              nilamp_gui_find_param_index(gui, NILAMP_PARAM_SPK_IND_GAIN1_DB),
                              nilamp_gui_scale_rect(sx, sy, 280.0f, 60.0f, 66.0f, 104.0f),
                              outbox, outbox_count, radius, NILAMP_GUI_KNOB_PERCENT, s);
        (void)nilamp_gui_knob(gui, ctx, canvas,
                              nilamp_gui_find_param_index(gui, NILAMP_PARAM_SPK_IND_GAIN2_DB),
                              nilamp_gui_scale_rect(sx, sy, 352.0f, 60.0f, 66.0f, 104.0f),
                              outbox, outbox_count, radius, NILAMP_GUI_KNOB_PERCENT, s);
        (void)nilamp_gui_knob(gui, ctx, canvas,
                              nilamp_gui_find_param_index(gui, NILAMP_PARAM_SPK_IND_FIND_DBHZ),
                              nilamp_gui_scale_rect(sx, sy, 424.0f, 60.0f, 66.0f, 104.0f),
                              outbox, outbox_count, radius, NILAMP_GUI_KNOB_PERCENT, s);
        (void)nilamp_gui_knob(gui, ctx, canvas,
                              nilamp_gui_find_param_index(gui, NILAMP_PARAM_SAG_PCT),
                              nilamp_gui_scale_rect(sx, sy, 16.0f, 207.0f, 66.0f, 104.0f),
                              outbox, outbox_count, radius, NILAMP_GUI_KNOB_PERCENT, s);
        nilamp_gui_enum_dropdown(gui, ctx, canvas,
                                  nilamp_gui_find_param_index(gui, NILAMP_PARAM_GAIN_COMP),
                                  nilamp_gui_scale_rect(sx, sy, 101.0f, 207.0f, 86.0f, 104.0f),
                                  outbox, outbox_count, s);
        (void)nilamp_gui_knob(gui, ctx, canvas,
                              nilamp_gui_find_param_index(gui, NILAMP_PARAM_SPK_RES_GAIN1_DB),
                              nilamp_gui_scale_rect(sx, sy, 210.0f, 207.0f, 66.0f, 104.0f),
                              outbox, outbox_count, radius, NILAMP_GUI_KNOB_PERCENT, s);
        (void)nilamp_gui_knob(gui, ctx, canvas,
                              nilamp_gui_find_param_index(gui, NILAMP_PARAM_SPK_RES_GAIN2_DB),
                              nilamp_gui_scale_rect(sx, sy, 282.0f, 207.0f, 66.0f, 104.0f),
                              outbox, outbox_count, radius, NILAMP_GUI_KNOB_PERCENT, s);
        (void)nilamp_gui_knob(gui, ctx, canvas,
                              nilamp_gui_find_param_index(gui, NILAMP_PARAM_SPK_RES_FRES_DBHZ),
                              nilamp_gui_scale_rect(sx, sy, 354.0f, 207.0f, 66.0f, 104.0f),
                              outbox, outbox_count, radius, NILAMP_GUI_KNOB_PERCENT, s);
        (void)nilamp_gui_knob(gui, ctx, canvas,
                              nilamp_gui_find_param_index(gui, NILAMP_PARAM_SPK_RES_QTS_DB),
                              nilamp_gui_scale_rect(sx, sy, 426.0f, 207.0f, 66.0f, 104.0f),
                              outbox, outbox_count, radius, NILAMP_GUI_KNOB_PERCENT, s);
    } else {
        nilamp_gui_draw_text(ctx, canvas,
                             nilamp_gui_scale_rect(sx, sy, 0.0f, 10.0f, 500.0f, 18.0f),
                             "About", gold, true);
        nk_layout_space_push(ctx, nilamp_gui_scale_rect(sx, sy, 0.0f, 2.0f, 65.0f, 28.0f));
        if (nk_button_label(ctx, "< back")) {
            nilamp_gui_close_dropdown(gui);
            gui->screen = NILAMP_GUI_SCREEN_OPTIONS;
            gui->model.dirty = true;
        }

        const struct nk_user_font *about_font =
            nilamp_gui_custom_font(gui, gui->font_about);
        nilamp_gui_draw_text_with_font(
            ctx, canvas, nilamp_gui_scale_rect(sx, sy, 0.0f, 72.0f, 500.0f, 25.0f),
            "TWD DLX II", gold, true, about_font);
        nilamp_gui_draw_text_with_font(
            ctx, canvas, nilamp_gui_scale_rect(sx, sy, 0.0f, 104.0f, 500.0f, 25.0f),
            "Keller JSFX Version 1.0.4", gold, true, about_font);
        nilamp_gui_draw_text_with_font(
            ctx, canvas, nilamp_gui_scale_rect(sx, sy, 0.0f, 136.0f, 500.0f, 25.0f),
            "by Helmut Keller", gold, true, about_font);
        nilamp_gui_draw_text_with_font(
            ctx, canvas, nilamp_gui_scale_rect(sx, sy, 0.0f, 168.0f, 500.0f, 25.0f),
            "Ported to C by niltempus", gold, true, about_font);
    }
    nk_layout_space_end(ctx);
    nilamp_gui_draw_open_dropdown(gui, ctx, canvas, outbox, outbox_count);
    if (gui->open_dropdown != NILAMP_GUI_DROPDOWN_NONE && !gui->dropdown_hovered &&
        nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT)) {
        nilamp_gui_close_dropdown(gui);
    }
    if (gui->active_edit >= 0 && !gui->value_box_hovered &&
        nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT)) {
        nilamp_gui_end_edit(gui, true, outbox, outbox_count);
    }

    nk_end(ctx);
}

static void nilamp_gui_draw(NilampGui *gui)
{
    if (!gui || !gui->gpu_ready || gui->model.width == 0u || gui->model.height == 0u) {
        return;
    }

    struct nk_context *ctx = snk_new_frame();
    nilamp_gui_refresh_params(gui);
    nilamp_gui_feed_input(gui, ctx);
    nilamp_gui_style(ctx);

    NilampGuiMsg outbox[NILAMP_GUI_MAX_MSGS];
    uint32_t outbox_count = 0u;
    nilamp_gui_build(gui, ctx, outbox, &outbox_count);
    nilamp_gui_drain_outbox(gui, outbox, outbox_count);
    nilamp_gui_clear_transient_input(gui);

    const sg_pass_action action = {
        .colors[0] = {
            .load_action = SG_LOADACTION_CLEAR,
            .clear_value = {0.075f, 0.078f, 0.082f, 1.0f},
        },
    };
    sg_begin_pass(&(sg_pass){
        .action = action,
        .swapchain = {
            .width = (int)gui->model.width,
            .height = (int)gui->model.height,
            .sample_count = 1,
            .color_format = SG_PIXELFORMAT_RGBA8,
            .depth_format = SG_PIXELFORMAT_NONE,
            .gl = {.framebuffer = 0u},
        },
    });
    snk_render((int)gui->model.width, (int)gui->model.height);
    sg_end_pass();
    sg_commit();
}

static void nilamp_gui_shutdown_custom_font(NilampGui *gui, bool snk_alive);

static bool nilamp_gui_init_custom_font(NilampGui *gui)
{
    if (!gui) {
        return false;
    }

    nk_font_atlas_init_default(&gui->font_atlas);
    gui->custom_font_atlas_ready = true;
    nk_font_atlas_begin(&gui->font_atlas);
    struct nk_font_config font_cfg = nk_font_config(0.0f);
    font_cfg.pixel_snap = 1;
    font_cfg.oversample_h = 1;
    font_cfg.oversample_v = 1;
    font_cfg.range = nk_font_default_glyph_ranges();
    gui->font_default = nk_font_atlas_add_from_memory(
        &gui->font_atlas, (void *)nilamp_font_0xproto_regular_data,
        nilamp_font_0xproto_regular_size, 24.0f, &font_cfg);
    gui->font_about = nk_font_atlas_add_from_memory(
        &gui->font_atlas, (void *)nilamp_font_0xproto_regular_data,
        nilamp_font_0xproto_regular_size, 18.0f, &font_cfg);
    gui->font_subtitle = nk_font_atlas_add_from_memory(
        &gui->font_atlas, (void *)nilamp_font_0xproto_regular_data,
        nilamp_font_0xproto_regular_size, 32.0f, &font_cfg);
    gui->font_title = nk_font_atlas_add_from_memory(
        &gui->font_atlas, (void *)nilamp_font_0xproto_regular_data,
        nilamp_font_0xproto_regular_size, 54.0f, &font_cfg);

    int font_width = 0;
    int font_height = 0;
    const void *pixels =
        nk_font_atlas_bake(&gui->font_atlas, &font_width, &font_height,
                           NK_FONT_ATLAS_RGBA32);
    if (!pixels || font_width <= 0 || font_height <= 0) {
        nk_font_atlas_clear(&gui->font_atlas);
        gui->custom_font_atlas_ready = false;
        return false;
    }

    gui->font_img = sg_make_image(&(sg_image_desc){
        .width = font_width,
        .height = font_height,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .data.mip_levels[0] = {
            .ptr = pixels,
            .size = (size_t)(font_width * font_height) * sizeof(uint32_t),
        },
        .label = "nilamp-gui-font-image",
    });
    gui->font_tex_view = sg_make_view(&(sg_view_desc){
        .texture = {.image = gui->font_img},
        .label = "nilamp-gui-font-view",
    });
    gui->font_sampler = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_NEAREST,
        .mag_filter = SG_FILTER_NEAREST,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
        .label = "nilamp-gui-font-sampler",
    });
    gui->font_snk_img = snk_make_image(&(snk_image_desc_t){
        .texture_view = gui->font_tex_view,
        .sampler = gui->font_sampler,
    });
    if (gui->font_img.id == SG_INVALID_ID || gui->font_tex_view.id == SG_INVALID_ID ||
        gui->font_sampler.id == SG_INVALID_ID || gui->font_snk_img.id == SNK_INVALID_ID) {
        return false;
    }

    nk_font_atlas_end(&gui->font_atlas, snk_nkhandle(gui->font_snk_img), NULL);
    nk_font_atlas_cleanup(&gui->font_atlas);

    struct nk_context *ctx = snk_new_frame();
    if (!ctx || !gui->font_default) {
        return false;
    }
    nk_style_set_font(ctx, &gui->font_default->handle);
    gui->custom_font_ready = true;
    return true;
}

static bool nilamp_gui_init_gpu(NilampGui *gui)
{
    if (!gui || nilamp_gui_sokol_in_use) {
        return false;
    }

    sg_setup(&(sg_desc){
        .environment = {
            .defaults = {
                .color_format = SG_PIXELFORMAT_RGBA8,
                .depth_format = SG_PIXELFORMAT_NONE,
                .sample_count = 1,
            },
        },
    });
    if (!sg_isvalid()) {
        sg_shutdown();
        return false;
    }

    snk_setup(&(snk_desc_t){
        .max_vertices = NILAMP_GUI_MAX_VERTICES,
        .image_pool_size = 8,
        .color_format = SG_PIXELFORMAT_RGBA8,
        .depth_format = SG_PIXELFORMAT_NONE,
        .sample_count = 1,
        .dpi_scale = (float)gui->scale,
    });
    if (!nilamp_gui_init_custom_font(gui)) {
        nilamp_gui_shutdown_custom_font(gui, true);
    }
    gui->gpu_ready = true;
    nilamp_gui_sokol_in_use = true;
    return true;
}

static void nilamp_gui_shutdown_custom_font(NilampGui *gui, bool snk_alive)
{
    if (!gui) {
        return;
    }
    if (snk_alive && gui->font_snk_img.id != SNK_INVALID_ID) {
        snk_destroy_image(gui->font_snk_img);
    }
    gui->font_snk_img.id = SNK_INVALID_ID;
    if (gui->font_sampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(gui->font_sampler);
        gui->font_sampler.id = SG_INVALID_ID;
    }
    if (gui->font_tex_view.id != SG_INVALID_ID) {
        sg_destroy_view(gui->font_tex_view);
        gui->font_tex_view.id = SG_INVALID_ID;
    }
    if (gui->font_img.id != SG_INVALID_ID) {
        sg_destroy_image(gui->font_img);
        gui->font_img.id = SG_INVALID_ID;
    }
    if (gui->custom_font_atlas_ready) {
        nk_font_atlas_clear(&gui->font_atlas);
        gui->custom_font_atlas_ready = false;
        gui->custom_font_ready = false;
    }
}

static void nilamp_gui_shutdown_gpu(NilampGui *gui)
{
    if (!gui || !gui->gpu_ready) {
        return;
    }
    nilamp_gui_shutdown_custom_font(gui, true);
    snk_shutdown();
    sg_shutdown();
    gui->gpu_ready = false;
    nilamp_gui_sokol_in_use = false;
}

static PuglStatus nilamp_gui_event(PuglView *view, const PuglEvent *event)
{
    NilampGui *gui = view ? (NilampGui *)puglGetHandle(view) : NULL;
    if (!gui || !event) {
        return PUGL_SUCCESS;
    }

    switch (event->type) {
    case PUGL_REALIZE:
        if (!nilamp_gui_init_gpu(gui)) {
            return PUGL_BACKEND_FAILED;
        }
        break;
    case PUGL_UNREALIZE:
        nilamp_gui_shutdown_gpu(gui);
        gui->realized = false;
        break;
    case PUGL_CONFIGURE:
        gui->model.width = (uint32_t)event->configure.width;
        gui->model.height = (uint32_t)event->configure.height;
        gui->model.dirty = true;
        break;
    case PUGL_EXPOSE:
        nilamp_gui_draw(gui);
        gui->model.dirty = false;
        break;
    case PUGL_TIMER:
        if (event->timer.id == NILAMP_GUI_FRAME_TIMER_ID && gui->visible) {
            const bool changed = nilamp_gui_refresh_params(gui);
            if (changed || gui->model.dirty) {
                nilamp_gui_request_redraw(gui);
            }
        }
        break;
    case PUGL_BUTTON_PRESS:
        if (event->button.button < 3u) {
            nilamp_gui_grab_focus(gui, "button_press");
            const double dx = event->button.x - (double)gui->last_click_x;
            const double dy = event->button.y - (double)gui->last_click_y;
            const double dist = sqrt(dx * dx + dy * dy);
            gui->mouse_double_click =
                event->button.button == gui->last_click_button &&
                gui->last_click_time > 0.0 &&
                event->button.time - gui->last_click_time <= NILAMP_GUI_DOUBLE_CLICK_SECONDS &&
                dist <= NILAMP_GUI_DOUBLE_CLICK_DISTANCE;
            gui->last_click_time = event->button.time;
            gui->last_click_x = (int)event->button.x;
            gui->last_click_y = (int)event->button.y;
            gui->last_click_button = event->button.button;
            gui->mouse_x = (int)event->button.x;
            gui->mouse_y = (int)event->button.y;
            gui->mouse_down[event->button.button] = true;
            nilamp_gui_request_redraw(gui);
        }
        break;
    case PUGL_BUTTON_RELEASE:
        if (event->button.button < 3u) {
            gui->mouse_x = (int)event->button.x;
            gui->mouse_y = (int)event->button.y;
            gui->mouse_up[event->button.button] = true;
            nilamp_gui_request_redraw(gui);
        }
        break;
    case PUGL_MOTION:
        gui->mouse_x = (int)event->motion.x;
        gui->mouse_y = (int)event->motion.y;
        gui->mouse_motion = true;
        nilamp_gui_request_redraw(gui);
        break;
    case PUGL_SCROLL:
        gui->mouse_x = (int)event->scroll.x;
        gui->mouse_y = (int)event->scroll.y;
        gui->scroll_x += (float)event->scroll.dx;
        gui->scroll_y += (float)event->scroll.dy;
        nilamp_gui_request_redraw(gui);
        break;
    case PUGL_KEY_PRESS:
    case PUGL_KEY_RELEASE: {
        const bool down = event->type == PUGL_KEY_PRESS;
        NilampGuiInputKey key = NILAMP_GUI_INPUT_KEY_UNKNOWN;
        switch (event->key.key) {
        case PUGL_KEY_BACKSPACE:
            key = NILAMP_GUI_INPUT_KEY_BACKSPACE;
            break;
        case PUGL_KEY_DELETE:
        case PUGL_KEY_PAD_DELETE:
            key = NILAMP_GUI_INPUT_KEY_DELETE;
            break;
        case PUGL_KEY_ENTER:
        case PUGL_KEY_PAD_ENTER:
            key = NILAMP_GUI_INPUT_KEY_ENTER;
            break;
        case PUGL_KEY_ESCAPE:
            key = NILAMP_GUI_INPUT_KEY_ESCAPE;
            break;
        case PUGL_KEY_TAB:
            key = NILAMP_GUI_INPUT_KEY_TAB;
            break;
        case PUGL_KEY_LEFT:
            key = NILAMP_GUI_INPUT_KEY_LEFT;
            break;
        case PUGL_KEY_RIGHT:
            key = NILAMP_GUI_INPUT_KEY_RIGHT;
            break;
        case PUGL_KEY_UP:
            key = NILAMP_GUI_INPUT_KEY_UP;
            break;
        case PUGL_KEY_DOWN:
            key = NILAMP_GUI_INPUT_KEY_DOWN;
            break;
        case PUGL_KEY_HOME:
            key = NILAMP_GUI_INPUT_KEY_HOME;
            break;
        case PUGL_KEY_END:
            key = NILAMP_GUI_INPUT_KEY_END;
            break;
        case PUGL_KEY_SHIFT_L:
        case PUGL_KEY_SHIFT_R:
            if (NK_KEY_SHIFT >= 0 && NK_KEY_SHIFT < NK_KEY_MAX) {
                gui->key_down[NK_KEY_SHIFT] = down;
                nilamp_gui_request_redraw(gui);
            }
            break;
        case PUGL_KEY_CTRL_L:
        case PUGL_KEY_CTRL_R:
            if (NK_KEY_CTRL >= 0 && NK_KEY_CTRL < NK_KEY_MAX) {
                gui->key_down[NK_KEY_CTRL] = down;
                nilamp_gui_request_redraw(gui);
            }
            break;
        default:
            break;
        }
        (void)nilamp_gui_handle_host_key(gui, key, down);
        break;
    }
    case PUGL_TEXT:
        (void)nilamp_gui_handle_host_text(gui, event->text.character);
        break;
    default:
        break;
    }
    return PUGL_SUCCESS;
}

NilampGui *nilamp_gui_create(const NilampGuiCallbacks *callbacks,
                             const NilampGuiParamSpec *params,
                             uint32_t param_count,
                             const NilampGuiLayoutSpec *layout,
                             NilampGuiApi api,
                             bool is_floating)
{
    if (!callbacks || !callbacks->get_param || !callbacks->set_param || !params ||
        param_count == 0 || param_count > NILAMP_GUI_MAX_PARAMS) {
        nilamp_gui_log("create rejected: invalid arguments");
        return NULL;
    }

    NilampGui *gui = (NilampGui *)calloc(1, sizeof(*gui));
    if (!gui) {
        return NULL;
    }

    gui->callbacks = *callbacks;
    gui->params = params;
    gui->param_count = param_count;
    gui->layout = layout;
    gui->api = api;
    gui->is_floating = is_floating;
    if (!nilamp_gui_preferred_size(layout, &gui->model.width, &gui->model.height)) {
        gui->model.width = NILAMP_GUI_DEFAULT_WIDTH;
        gui->model.height = NILAMP_GUI_DEFAULT_HEIGHT;
    }
    gui->model.dirty = true;
    gui->scale = 1.0;
    gui->active_knob = -1;
    gui->active_edit = -1;
    gui->active_gesture = -1;
    gui->screen = layout ? (NilampGuiScreen)layout->default_screen : NILAMP_GUI_SCREEN_MAIN;
    nilamp_gui_refresh_params(gui);

    gui->world = puglNewWorld(PUGL_MODULE, 0u);
    gui->view = gui->world ? puglNewView(gui->world) : NULL;
    if (!gui->view) {
        nilamp_gui_destroy(gui);
        return NULL;
    }

    puglSetHandle(gui->view, gui);
    PuglStatus status = PUGL_SUCCESS;
    if ((status = puglSetBackend(gui->view, puglGlBackend())) ||
        (status = puglSetEventFunc(gui->view, nilamp_gui_event)) ||
        (status = puglSetViewHint(gui->view, PUGL_CONTEXT_API, PUGL_OPENGL_API)) ||
        (status = puglSetViewHint(gui->view, PUGL_CONTEXT_VERSION_MAJOR, 3)) ||
        (status = puglSetViewHint(gui->view, PUGL_CONTEXT_VERSION_MINOR, 3)) ||
        (status = puglSetViewHint(gui->view, PUGL_CONTEXT_PROFILE, PUGL_OPENGL_CORE_PROFILE)) ||
        (status = puglSetViewHint(gui->view, PUGL_DOUBLE_BUFFER, 1)) ||
        (status = puglSetViewHint(gui->view, PUGL_STENCIL_BITS, 0)) ||
        (status = puglSetViewHint(gui->view, PUGL_RESIZABLE, 0)) ||
        (status = puglSetSizeHint(gui->view, PUGL_DEFAULT_SIZE, gui->model.width,
                                  gui->model.height))) {
        nilamp_gui_log("create failed configuring Pugl: %s", puglStrerror(status));
        nilamp_gui_destroy(gui);
        return NULL;
    }

    status = puglSetSizeHint(gui->view, PUGL_CURRENT_SIZE, gui->model.width,
                             gui->model.height);
    if (status > PUGL_FAILURE) {
        nilamp_gui_log("create failed setting current size: %s", puglStrerror(status));
        nilamp_gui_destroy(gui);
        return NULL;
    }
    if (status) {
        nilamp_gui_log("create deferred current size: %s", puglStrerror(status));
    }

    nilamp_gui_log("create ok api=%d floating=%d", (int)api, is_floating ? 1 : 0);
    return gui;
}

void nilamp_gui_destroy(NilampGui *gui)
{
    if (!gui) {
        return;
    }
    if (gui->active_gesture >= 0 &&
        (uint32_t)gui->active_gesture < gui->param_count &&
        gui->callbacks.end_param_gesture) {
        gui->callbacks.end_param_gesture(
            gui->callbacks.user, gui->params[gui->active_gesture].id);
        gui->active_gesture = -1;
    }
    if (gui->view) {
        nilamp_gui_stop_frame_timer(gui);
        if (gui->realized) {
            (void)puglUnrealize(gui->view);
        } else {
            nilamp_gui_shutdown_gpu(gui);
        }
        puglFreeView(gui->view);
    } else {
        nilamp_gui_shutdown_gpu(gui);
    }
    puglFreeWorld(gui->world);
    free(gui);
}

bool nilamp_gui_set_parent(NilampGui *gui, NilampGuiParent parent)
{
    if (!gui || !gui->view || parent.handle == 0u || parent.api != gui->api) {
        nilamp_gui_log("set_parent rejected handle=%p api=%d", (void *)parent.handle,
                       (int)parent.api);
        return false;
    }
    if (gui->is_floating) {
        nilamp_gui_log("set_parent rejected for floating gui");
        return false;
    }
    if (!gui->realized) {
        PuglStatus status = puglSetParent(gui->view, (PuglNativeView)parent.handle);
        if (status) {
            nilamp_gui_log("set_parent failed: %s", puglStrerror(status));
            return false;
        }
        status = puglRealize(gui->view);
        if (status) {
            nilamp_gui_log("set_parent realize failed: %s", puglStrerror(status));
            return false;
        }
        nilamp_gui_log("set_parent realize ok");
        gui->realized = true;
    }
    return true;
}

bool nilamp_gui_set_transient(NilampGui *gui, NilampGuiParent parent)
{
    if (!gui || !gui->view || parent.handle == 0u || parent.api != gui->api) {
        nilamp_gui_log("set_transient rejected handle=%p api=%d", (void *)parent.handle,
                       (int)parent.api);
        return false;
    }
    if (!gui->is_floating || puglGetParent(gui->view)) {
        nilamp_gui_log("set_transient rejected floating=%d parent=%p",
                       gui->is_floating ? 1 : 0, (void *)puglGetParent(gui->view));
        return false;
    }

    PuglStatus status = puglSetTransientParent(gui->view, (PuglNativeView)parent.handle);
    if (status && gui->realized) {
        nilamp_gui_log("set_transient failed: %s", puglStrerror(status));
        return false;
    }
    nilamp_gui_log("set_transient ok status=%s", puglStrerror(status));
    return true;
}

bool nilamp_gui_show(NilampGui *gui)
{
    if (!gui || !gui->view) {
        nilamp_gui_log("show rejected: missing gui/view");
        return false;
    }
    if (!gui->realized && gui->is_floating) {
        const PuglStatus status = puglRealize(gui->view);
        if (status) {
            nilamp_gui_log("show floating realize failed: %s", puglStrerror(status));
            return false;
        }
        nilamp_gui_log("show floating realize ok");
        gui->realized = true;
    }
    if (!gui->realized || !gui->gpu_ready) {
        nilamp_gui_log("show rejected realized=%d gpu_ready=%d",
                       gui->realized ? 1 : 0, gui->gpu_ready ? 1 : 0);
        return false;
    }
    const PuglStatus status =
        puglShow(gui->view, gui->is_floating ? PUGL_SHOW_RAISE : PUGL_SHOW_PASSIVE);
    if (status > PUGL_FAILURE) {
        nilamp_gui_log("show failed: %s", puglStrerror(status));
        return false;
    }
    gui->visible = true;
    nilamp_gui_request_redraw(gui);
    nilamp_gui_log("show ok status=%s", puglStrerror(status));
    return true;
}

bool nilamp_gui_hide(NilampGui *gui)
{
    if (!gui || !gui->view) {
        return false;
    }
    if (gui->active_gesture >= 0 &&
        (uint32_t)gui->active_gesture < gui->param_count &&
        gui->callbacks.end_param_gesture) {
        gui->callbacks.end_param_gesture(
            gui->callbacks.user, gui->params[gui->active_gesture].id);
        gui->active_gesture = -1;
    }
    nilamp_gui_stop_frame_timer(gui);
    (void)puglHide(gui->view);
    gui->visible = false;
    return true;
}

bool nilamp_gui_set_scale(NilampGui *gui, double scale)
{
    if (!gui || !isfinite(scale) || scale <= 0.0) {
        return false;
    }
    gui->scale = scale;
    nilamp_gui_request_redraw(gui);
    return true;
}

bool nilamp_gui_get_size(const NilampGui *gui, uint32_t *width, uint32_t *height)
{
    if (!gui || !width || !height) {
        return false;
    }
    *width = gui->model.width;
    *height = gui->model.height;
    return true;
}

bool nilamp_gui_set_size(NilampGui *gui, uint32_t width, uint32_t height)
{
    if (!gui || width < 360u || height < 240u) {
        return false;
    }
    gui->model.width = width;
    gui->model.height = height;
    gui->model.dirty = true;
    if (gui->view) {
        (void)puglSetSizeHint(gui->view, PUGL_CURRENT_SIZE, width, height);
    }
    nilamp_gui_request_redraw(gui);
    return true;
}

bool nilamp_gui_is_visible(const NilampGui *gui)
{
    return gui && gui->visible;
}

bool nilamp_gui_captures_keyboard(const NilampGui *gui)
{
    return gui && gui->active_edit >= 0;
}

bool nilamp_gui_wants_fast_pump(const NilampGui *gui)
{
    if (!gui || !gui->visible) {
        return false;
    }
    if (gui->model.dirty || gui->mouse_motion || gui->mouse_double_click ||
        gui->scroll_x != 0.0f || gui->scroll_y != 0.0f ||
        gui->text_input_len > 0u || gui->key_enter || gui->key_escape ||
        gui->key_backspace || gui->key_delete || gui->key_left ||
        gui->key_right || gui->key_home || gui->key_end || gui->active_knob >= 0 ||
        gui->active_edit >= 0 || gui->open_dropdown != NILAMP_GUI_DROPDOWN_NONE) {
        return true;
    }
    for (uint32_t i = 0; i < 3u; i++) {
        if (gui->mouse_down[i] || gui->mouse_up[i]) {
            return true;
        }
    }
    return false;
}

bool nilamp_gui_start_frame_timer(NilampGui *gui, double interval_seconds)
{
    if (!gui || !gui->view || !gui->realized || interval_seconds <= 0.0) {
        nilamp_gui_log("start_frame_timer rejected");
        return false;
    }
    if (gui->frame_timer_running) {
        return true;
    }

    const PuglStatus status =
        puglStartTimer(gui->view, NILAMP_GUI_FRAME_TIMER_ID, interval_seconds);
    if (status) {
        nilamp_gui_log("start_frame_timer failed: %s", puglStrerror(status));
        return false;
    }

    gui->frame_timer_running = true;
    nilamp_gui_log("start_frame_timer interval=%.3f", interval_seconds);
    return true;
}

void nilamp_gui_stop_frame_timer(NilampGui *gui)
{
    if (!gui || !gui->view || !gui->frame_timer_running) {
        return;
    }

    const PuglStatus status = puglStopTimer(gui->view, NILAMP_GUI_FRAME_TIMER_ID);
    if (status) {
        nilamp_gui_log("stop_frame_timer failed: %s", puglStrerror(status));
    } else {
        nilamp_gui_log("stop_frame_timer");
    }
    gui->frame_timer_running = false;
}

void nilamp_gui_refresh(NilampGui *gui)
{
    if (!gui) {
        return;
    }
    if (nilamp_gui_refresh_params(gui)) {
        nilamp_gui_request_redraw(gui);
    }
}

void nilamp_gui_on_main_thread(NilampGui *gui)
{
    if (!gui || !gui->world || !gui->visible) {
        return;
    }
    nilamp_gui_refresh(gui);
    (void)puglUpdate(gui->world, 0.0);
    if (gui->model.dirty) {
        nilamp_gui_request_redraw(gui);
    }
}

void nilamp_gui_set_param_mapping_indication(NilampGui *gui,
                                             uint32_t param_id,
                                             bool has_mapping,
                                             const NilampGuiIndicationColor *color,
                                             const char *label)
{
    if (!gui) {
        return;
    }
    const uint32_t index = nilamp_gui_find_param_index(gui, param_id);
    if (index >= gui->param_count || index >= NILAMP_GUI_MAX_PARAMS) {
        return;
    }

    NilampGuiParamIndication *indication = &gui->indications[index];
    indication->has_mapping = has_mapping;
    indication->has_mapping_color = color != NULL;
    indication->mapping_color = color ? *color : (NilampGuiIndicationColor){0};
    indication->mapping_label[0] = '\0';
    if (has_mapping && label) {
        (void)snprintf(indication->mapping_label, sizeof(indication->mapping_label),
                       "%s", label);
    }
    gui->model.dirty = true;
    nilamp_gui_request_redraw(gui);
}

void nilamp_gui_set_param_automation_indication(NilampGui *gui,
                                                uint32_t param_id,
                                                uint32_t automation_state,
                                                const NilampGuiIndicationColor *color)
{
    if (!gui) {
        return;
    }
    const uint32_t index = nilamp_gui_find_param_index(gui, param_id);
    if (index >= gui->param_count || index >= NILAMP_GUI_MAX_PARAMS) {
        return;
    }

    NilampGuiParamIndication *indication = &gui->indications[index];
    indication->automation_state = automation_state;
    indication->has_automation_color = color != NULL && automation_state != 0u;
    indication->automation_color =
        indication->has_automation_color ? *color : (NilampGuiIndicationColor){0};
    gui->model.dirty = true;
    nilamp_gui_request_redraw(gui);
}
