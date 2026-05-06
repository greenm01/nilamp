// SPDX-License-Identifier: MIT
#include "nilamp_gui.h"

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

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NILAMP_GUI_DEFAULT_WIDTH 640u
#define NILAMP_GUI_DEFAULT_HEIGHT 360u
#define NILAMP_GUI_MAX_PARAMS 24u
#define NILAMP_GUI_EDIT_TEXT_LEN 32u
#define NILAMP_GUI_TEXT_INPUT_LEN 64u

typedef enum NilampGuiScreen {
    NILAMP_GUI_SCREEN_MAIN = 0,
    NILAMP_GUI_SCREEN_OPTIONS = 1,
} NilampGuiScreen;

typedef enum NilampGuiMsgType {
    NILAMP_GUI_MSG_NONE = 0,
    NILAMP_GUI_MSG_PARAM_CHANGED,
} NilampGuiMsgType;

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

struct NilampGui {
    PuglWorld *world;
    PuglView *view;
    NilampGuiCallbacks callbacks;
    const NilampGuiParamSpec *params;
    uint32_t param_count;
    NilampGuiApi api;
    NilampGuiModel model;
    double scale;
    int mouse_x;
    int mouse_y;
    bool mouse_down[3];
    bool mouse_up[3];
    bool mouse_motion;
    bool key_down[NK_KEY_MAX];
    char text_input[NILAMP_GUI_TEXT_INPUT_LEN];
    uint32_t text_input_len;
    int active_knob;
    NilampGuiScreen screen;
    bool realized;
    bool is_floating;
    bool visible;
    bool gpu_ready;
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

static uint32_t nilamp_gui_param_index(const NilampGui *gui, uint32_t param_id)
{
    if (!gui) {
        return NILAMP_GUI_MAX_PARAMS;
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
    const float display = nilamp_gui_display_value(param, value);
    (void)snprintf(dst, dst_size, "%.3g", display);
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

static void nilamp_gui_refresh_params(NilampGui *gui)
{
    if (!gui) {
        return;
    }
    for (uint32_t i = 0; i < gui->param_count && i < NILAMP_GUI_MAX_PARAMS; i++) {
        gui->model.param_values[i] = nilamp_gui_read_param(gui, i);
        nilamp_gui_sync_edit_text(gui, i);
    }
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
    if (!gui || !msg || msg->type != NILAMP_GUI_MSG_PARAM_CHANGED) {
        return;
    }

    const uint32_t index = nilamp_gui_param_index(gui, msg->param_id);
    if (index >= gui->param_count || index >= NILAMP_GUI_MAX_PARAMS) {
        return;
    }

    const NilampGuiParamSpec *param = &gui->params[index];
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
    for (uint32_t i = 0; i < NK_KEY_MAX; i++) {
        nk_input_key(ctx, (enum nk_keys)i, gui->key_down[i] ? 1 : 0);
    }
    for (uint32_t i = 0; i < gui->text_input_len && i < NILAMP_GUI_TEXT_INPUT_LEN; i++) {
        nk_input_char(ctx, gui->text_input[i]);
    }
    gui->text_input_len = 0u;
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
    nk_input_end(ctx);
    gui->mouse_motion = false;
}

static float nilamp_gui_minf(float a, float b)
{
    return a < b ? a : b;
}

static struct nk_rect nilamp_gui_scale_rect(float sx, float sy, float x, float y,
                                            float w, float h)
{
    return nk_rect(x * sx, y * sy, w * sx, h * sy);
}

static void nilamp_gui_draw_text(struct nk_context *ctx, struct nk_command_buffer *canvas,
                                 struct nk_rect bounds, const char *text,
                                 struct nk_color color, bool centered)
{
    if (!ctx || !canvas || !text) {
        return;
    }

    const struct nk_user_font *font = ctx->style.font;
    const int len = (int)strlen(text);
    if (centered && font && font->width) {
        const float text_width = font->width(font->userdata, font->height, text, len);
        if (text_width < bounds.w) {
            bounds.x += (bounds.w - text_width) * 0.5f;
            bounds.w = text_width;
        }
    }
    nk_draw_text(canvas, bounds, text, len, font, nk_rgba(0, 0, 0, 0), color);
}

static void nilamp_gui_draw_panel(struct nk_command_buffer *canvas, struct nk_rect bounds,
                                  const char *caption, struct nk_color fill,
                                  struct nk_color border, struct nk_color gold)
{
    nk_fill_rect(canvas, bounds, 8.0f, fill);
    nk_stroke_rect(canvas, bounds, 8.0f, 1.0f, border);
    if (caption) {
        const struct nk_rect strip = nk_rect(bounds.x, bounds.y + bounds.h - 20.0f,
                                            bounds.w, 20.0f);
        nk_stroke_line(canvas, strip.x, strip.y, strip.x + strip.w, strip.y, 1.0f, border);
        (void)gold;
    }
}

static void nilamp_gui_commit_edit(NilampGui *gui, uint32_t index, NilampGuiMsg *outbox,
                                   uint32_t *outbox_count)
{
    if (!gui || index >= gui->param_count || index >= NILAMP_GUI_MAX_PARAMS) {
        return;
    }
    const NilampGuiParamSpec *param = &gui->params[index];
    char *text = gui->model.edit_text[index];
    char *end = NULL;
    const float display = strtof(text, &end);
    if (end == text || !isfinite(display)) {
        nilamp_gui_sync_edit_text(gui, index);
        return;
    }

    const float raw = nilamp_gui_quantize(param, nilamp_gui_raw_from_display(param, display));
    if (raw != gui->model.param_values[index]) {
        nilamp_gui_emit(outbox, outbox_count, NILAMP_GUI_MAX_PARAMS,
                        (NilampGuiMsg){
                            .type = NILAMP_GUI_MSG_PARAM_CHANGED,
                            .param_id = param->id,
                            .value = raw,
                        });
    }
    gui->model.param_values[index] = raw;
    gui->model.edit_active[index] = false;
    nilamp_gui_sync_edit_text(gui, index);
}

static void nilamp_gui_edit_box(NilampGui *gui, struct nk_context *ctx, uint32_t index,
                                struct nk_rect bounds, NilampGuiMsg *outbox,
                                uint32_t *outbox_count)
{
    if (!gui || !ctx || index >= gui->param_count || index >= NILAMP_GUI_MAX_PARAMS) {
        return;
    }

    nk_layout_space_push(ctx, bounds);
    const nk_flags flags = NK_EDIT_FIELD | NK_EDIT_SIG_ENTER | NK_EDIT_AUTO_SELECT |
                           NK_EDIT_NO_HORIZONTAL_SCROLL;
    const nk_flags event =
        nk_edit_string_zero_terminated(ctx, flags, gui->model.edit_text[index],
                                       (int)sizeof(gui->model.edit_text[index]),
                                       nk_filter_float);
    if (event & NK_EDIT_ACTIVATED) {
        gui->model.edit_active[index] = true;
    }
    if (event & NK_EDIT_COMMITTED) {
        nilamp_gui_commit_edit(gui, index, outbox, outbox_count);
        nk_edit_unfocus(ctx);
    } else if (event & NK_EDIT_DEACTIVATED) {
        nilamp_gui_commit_edit(gui, index, outbox, outbox_count);
    } else {
        gui->model.edit_active[index] = (event & NK_EDIT_ACTIVE) != 0;
    }
}

static void nilamp_gui_unit_text(struct nk_context *ctx, struct nk_command_buffer *canvas,
                                 struct nk_rect bounds, const char *unit,
                                 struct nk_color color)
{
    if (!unit || !unit[0]) {
        return;
    }
    nilamp_gui_draw_text(ctx, canvas, bounds, unit, color, false);
}

static bool nilamp_gui_knob(NilampGui *gui, struct nk_context *ctx,
                            struct nk_command_buffer *canvas, uint32_t index,
                            struct nk_rect bounds, NilampGuiMsg *outbox,
                            uint32_t *outbox_count, float radius)
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
    const bool hovered = nk_input_is_mouse_hovering_rect(&ctx->input, bounds);
    if (hovered && nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT)) {
        gui->active_knob = (int)index;
    }
    if (nk_input_is_mouse_released(&ctx->input, NK_BUTTON_LEFT) &&
        gui->active_knob == (int)index) {
        gui->active_knob = -1;
    }
    if (gui->active_knob == (int)index &&
        nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT)) {
        const float delta = (-ctx->input.mouse.delta.y + ctx->input.mouse.delta.x * 0.35f) *
                            range / 180.0f;
        if (delta != 0.0f) {
            value = nilamp_gui_quantize(param, value + delta);
            if (value != gui->model.param_values[index]) {
                nilamp_gui_emit(outbox, outbox_count, NILAMP_GUI_MAX_PARAMS,
                                (NilampGuiMsg){
                                    .type = NILAMP_GUI_MSG_PARAM_CHANGED,
                                    .param_id = param->id,
                                    .value = value,
                                });
            }
        }
    }

    const struct nk_color gold = nk_rgb(255, 205, 32);
    const struct nk_color gold_hi = nk_rgb(255, 232, 116);
    const struct nk_color knob = nk_rgb(49, 76, 103);
    const struct nk_color knob_hi = nk_rgb(68, 96, 122);
    const struct nk_color shadow = nk_rgb(18, 29, 42);
    const struct nk_color edge = nk_rgb(94, 116, 134);
    const struct nk_color text = nk_rgb(255, 205, 32);

    nilamp_gui_draw_text(ctx, canvas, nk_rect(bounds.x, bounds.y, bounds.w, 18.0f),
                         param->name, text, true);

    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + 50.0f;
    const struct nk_rect circle = nk_rect(cx - radius, cy - radius, radius * 2.0f,
                                          radius * 2.0f);
    nk_fill_circle(canvas, nk_rect(circle.x + 2.0f, circle.y + 3.0f, circle.w, circle.h),
                   shadow);
    nk_fill_circle(canvas, circle, gui->active_knob == (int)index ? knob_hi : knob);
    nk_stroke_circle(canvas, circle, 1.0f, edge);
    nk_fill_circle(canvas, nk_rect(circle.x + radius * 0.20f, circle.y + radius * 0.12f,
                                   radius * 0.48f, radius * 0.34f),
                   nk_rgba(126, 158, 185, 60));

    const float normalized = (value - param->min_value) / range;
    const float angle = (135.0f + normalized * 270.0f) * 0.017453292519943295f;
    const float needle_len = radius * 0.78f;
    const float x1 = cx + cosf(angle) * radius * 0.18f;
    const float y1 = cy + sinf(angle) * radius * 0.18f;
    const float x2 = cx + cosf(angle) * needle_len;
    const float y2 = cy + sinf(angle) * needle_len;
    nk_stroke_line(canvas, x1, y1, x2, y2, 2.0f,
                   gui->active_knob == (int)index ? gold_hi : gold);

    const struct nk_rect edit_rect =
        nk_rect(bounds.x + 4.0f, bounds.y + bounds.h - 24.0f, bounds.w - 22.0f, 18.0f);
    nilamp_gui_edit_box(gui, ctx, index, edit_rect, outbox, outbox_count);
    nilamp_gui_unit_text(ctx, canvas,
                         nk_rect(bounds.x + bounds.w - 17.0f, bounds.y + bounds.h - 22.0f,
                                 20.0f, 18.0f),
                         param->unit, text);
    return hovered || gui->active_knob == (int)index;
}

static void nilamp_gui_enum_button(NilampGui *gui, struct nk_context *ctx,
                                   struct nk_command_buffer *canvas, uint32_t index,
                                   struct nk_rect bounds, NilampGuiMsg *outbox,
                                   uint32_t *outbox_count)
{
    static const char *const names[] = {"Off", "Tube 1", "Splitter", "Both"};
    if (!gui || !ctx || !canvas || index >= gui->param_count ||
        index >= NILAMP_GUI_MAX_PARAMS) {
        return;
    }
    const struct nk_color gold = nk_rgb(255, 205, 32);
    const struct nk_color fill = nk_rgb(49, 76, 103);
    const int value = (int)lroundf(gui->model.param_values[index]);
    const int safe_value = value >= 0 && value < 4 ? value : 2;
    nilamp_gui_draw_text(ctx, canvas, nk_rect(bounds.x, bounds.y, bounds.w, 18.0f),
                         gui->params[index].name, gold, true);
    nilamp_gui_draw_panel(canvas, nk_rect(bounds.x + 13.0f, bounds.y + 29.0f,
                                          bounds.w - 26.0f, 42.0f),
                          NULL, fill, nk_rgb(94, 116, 134), gold);
    nk_layout_space_push(ctx, nk_rect(bounds.x + 4.0f, bounds.y + 38.0f,
                                      bounds.w - 8.0f, 24.0f));
    if (nk_button_label(ctx, names[safe_value])) {
        const float next = (float)((safe_value + 1) % 4);
        nilamp_gui_emit(outbox, outbox_count, NILAMP_GUI_MAX_PARAMS,
                        (NilampGuiMsg){
                            .type = NILAMP_GUI_MSG_PARAM_CHANGED,
                            .param_id = gui->params[index].id,
                            .value = next,
                        });
    }
}

static uint32_t nilamp_gui_find_param_index(const NilampGui *gui, uint32_t param_id)
{
    const uint32_t index = nilamp_gui_param_index(gui, param_id);
    return index < gui->param_count ? index : NILAMP_GUI_MAX_PARAMS;
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
    const float sx = width / (float)NILAMP_GUI_DEFAULT_WIDTH;
    const float sy = height / (float)NILAMP_GUI_DEFAULT_HEIGHT;
    const float s = nilamp_gui_minf(sx, sy);
    const struct nk_color bg = nk_rgb(28, 45, 61);
    const struct nk_color panel = nk_rgb(34, 52, 68);
    const struct nk_color panel_dark = nk_rgb(26, 40, 54);
    const struct nk_color border = nk_rgb(105, 123, 137);
    const struct nk_color gold = nk_rgb(255, 205, 32);
    const struct nk_color gold_dim = nk_rgb(214, 168, 42);

    nk_fill_rect(canvas, nk_rect(0.0f, 0.0f, width, height), 0.0f, bg);
    nk_fill_rect(canvas, nilamp_gui_scale_rect(sx, sy, 0.0f, 0.0f, 640.0f, 34.0f),
                 0.0f, nk_rgb(24, 37, 50));
    nk_stroke_line(canvas, 0.0f, 33.0f * sy, width, 33.0f * sy, 1.0f,
                   nk_rgb(83, 103, 119));

    nk_layout_space_begin(ctx, NK_STATIC, height, 80);
    if (gui->screen == NILAMP_GUI_SCREEN_MAIN) {
        nilamp_gui_draw_text(ctx, canvas,
                             nilamp_gui_scale_rect(sx, sy, 0.0f, 10.0f, 640.0f, 18.0f),
                             "TWD DLX II", gold, true);
        nk_layout_space_push(ctx, nilamp_gui_scale_rect(sx, sy, 560.0f, 2.0f, 72.0f, 28.0f));
        if (nk_button_label(ctx, "Options")) {
            gui->screen = NILAMP_GUI_SCREEN_OPTIONS;
            gui->model.dirty = true;
        }

        nilamp_gui_draw_panel(canvas, nilamp_gui_scale_rect(sx, sy, 10.0f, 44.0f, 94.0f, 139.0f),
                              "Input", panel, border, gold);
        nilamp_gui_draw_panel(canvas, nilamp_gui_scale_rect(sx, sy, 536.0f, 44.0f, 94.0f, 139.0f),
                              "Output", panel, border, gold);
        nilamp_gui_draw_panel(canvas, nilamp_gui_scale_rect(sx, sy, 10.0f, 190.0f, 510.0f, 138.0f),
                              "Pre Amp", panel, border, gold);
        nilamp_gui_draw_panel(canvas, nilamp_gui_scale_rect(sx, sy, 536.0f, 190.0f, 94.0f, 138.0f),
                              "Cab", panel, border, gold);

        nilamp_gui_draw_text(ctx, canvas,
                             nilamp_gui_scale_rect(sx, sy, 0.0f, 78.0f, 640.0f, 34.0f),
                             "nilamp", gold, true);
        const char *model = gui->callbacks.model_name ?
                                gui->callbacks.model_name(gui->callbacks.user) :
                                "Keller TWD DLX II";
        nilamp_gui_draw_text(ctx, canvas,
                             nilamp_gui_scale_rect(sx, sy, 0.0f, 122.0f, 640.0f, 18.0f),
                             model, gold, true);

        const uint32_t input_gain = nilamp_gui_find_param_index(gui, NILAMP_PARAM_GAIN_DB);
        const uint32_t output_gain = nilamp_gui_find_param_index(gui, NILAMP_PARAM_OUTPUT_GAIN_DB);
        const uint32_t volume = nilamp_gui_find_param_index(gui, NILAMP_PARAM_VOLUME_PCT);
        const uint32_t bass = nilamp_gui_find_param_index(gui, NILAMP_PARAM_BASS_PCT);
        const uint32_t mid = nilamp_gui_find_param_index(gui, NILAMP_PARAM_MID_PCT);
        const uint32_t treble = nilamp_gui_find_param_index(gui, NILAMP_PARAM_TREBLE_PCT);
        const float small_radius = 23.0f * s;
        const float pre_radius = 23.0f * s;
        (void)nilamp_gui_knob(gui, ctx, canvas, input_gain,
                              nilamp_gui_scale_rect(sx, sy, 23.0f, 60.0f, 66.0f, 104.0f),
                              outbox, outbox_count, small_radius);
        (void)nilamp_gui_knob(gui, ctx, canvas, output_gain,
                              nilamp_gui_scale_rect(sx, sy, 549.0f, 60.0f, 66.0f, 104.0f),
                              outbox, outbox_count, small_radius);
        nilamp_gui_draw_text(ctx, canvas,
                             nilamp_gui_scale_rect(sx, sy, 34.0f, 210.0f, 56.0f, 18.0f),
                             "Tube 1", gold, true);
        nk_fill_rect(canvas, nilamp_gui_scale_rect(sx, sy, 38.0f, 230.0f, 42.0f, 15.0f),
                     0.0f, panel_dark);
        nk_fill_rect(canvas, nilamp_gui_scale_rect(sx, sy, 61.0f, 231.0f, 17.0f, 13.0f),
                     0.0f, gold_dim);
        nilamp_gui_draw_text(ctx, canvas,
                             nilamp_gui_scale_rect(sx, sy, 34.0f, 257.0f, 56.0f, 18.0f),
                             "12AX7", gold, true);
        (void)nilamp_gui_knob(gui, ctx, canvas, volume,
                              nilamp_gui_scale_rect(sx, sy, 104.0f, 208.0f, 74.0f, 104.0f),
                              outbox, outbox_count, pre_radius);
        (void)nilamp_gui_knob(gui, ctx, canvas, bass,
                              nilamp_gui_scale_rect(sx, sy, 184.0f, 208.0f, 74.0f, 104.0f),
                              outbox, outbox_count, pre_radius);
        (void)nilamp_gui_knob(gui, ctx, canvas, mid,
                              nilamp_gui_scale_rect(sx, sy, 264.0f, 208.0f, 74.0f, 104.0f),
                              outbox, outbox_count, pre_radius);
        (void)nilamp_gui_knob(gui, ctx, canvas, treble,
                              nilamp_gui_scale_rect(sx, sy, 344.0f, 208.0f, 74.0f, 104.0f),
                              outbox, outbox_count, pre_radius);
        nilamp_gui_draw_text(ctx, canvas,
                             nilamp_gui_scale_rect(sx, sy, 556.0f, 235.0f, 54.0f, 18.0f),
                             "TWD", gold, true);
        nilamp_gui_draw_text(ctx, canvas,
                             nilamp_gui_scale_rect(sx, sy, 556.0f, 255.0f, 54.0f, 18.0f),
                             "DLX", gold, true);
    } else {
        nilamp_gui_draw_text(ctx, canvas,
                             nilamp_gui_scale_rect(sx, sy, 0.0f, 10.0f, 640.0f, 18.0f),
                             "Options", gold, true);
        nk_layout_space_push(ctx, nilamp_gui_scale_rect(sx, sy, 0.0f, 2.0f, 72.0f, 28.0f));
        if (nk_button_label(ctx, "< back")) {
            gui->screen = NILAMP_GUI_SCREEN_MAIN;
            gui->model.dirty = true;
        }
        nk_layout_space_push(ctx, nilamp_gui_scale_rect(sx, sy, 560.0f, 2.0f, 72.0f, 28.0f));
        (void)nk_button_label(ctx, "About");

        nilamp_gui_draw_panel(canvas, nilamp_gui_scale_rect(sx, sy, 10.0f, 44.0f, 165.0f, 139.0f),
                              "Tone Stack", panel, border, gold);
        nilamp_gui_draw_panel(canvas, nilamp_gui_scale_rect(sx, sy, 255.0f, 44.0f, 375.0f, 139.0f),
                              "Speaker Inductor", panel, border, gold);
        nilamp_gui_draw_panel(canvas, nilamp_gui_scale_rect(sx, sy, 10.0f, 190.0f, 94.0f, 65.0f),
                              "Power", panel, border, gold);
        nilamp_gui_draw_panel(canvas, nilamp_gui_scale_rect(sx, sy, 10.0f, 262.0f, 94.0f, 65.0f),
                              "Gain", panel, border, gold);
        nilamp_gui_draw_panel(canvas, nilamp_gui_scale_rect(sx, sy, 184.0f, 190.0f, 446.0f, 138.0f),
                              "Speaker Resonance", panel, border, gold);

        const float radius = 23.0f * s;
        (void)nilamp_gui_knob(gui, ctx, canvas,
                              nilamp_gui_find_param_index(gui, NILAMP_PARAM_TONE_FMID_DBHZ),
                              nilamp_gui_scale_rect(sx, sy, 25.0f, 61.0f, 66.0f, 104.0f),
                              outbox, outbox_count, radius);
        (void)nilamp_gui_knob(gui, ctx, canvas,
                              nilamp_gui_find_param_index(gui, NILAMP_PARAM_TONE_QMID_DB),
                              nilamp_gui_scale_rect(sx, sy, 100.0f, 61.0f, 66.0f, 104.0f),
                              outbox, outbox_count, radius);
        (void)nilamp_gui_knob(gui, ctx, canvas,
                              nilamp_gui_find_param_index(gui, NILAMP_PARAM_SPK_IND_GAIN1_DB),
                              nilamp_gui_scale_rect(sx, sy, 279.0f, 61.0f, 66.0f, 104.0f),
                              outbox, outbox_count, radius);
        (void)nilamp_gui_knob(gui, ctx, canvas,
                              nilamp_gui_find_param_index(gui, NILAMP_PARAM_SPK_IND_GAIN2_DB),
                              nilamp_gui_scale_rect(sx, sy, 351.0f, 61.0f, 66.0f, 104.0f),
                              outbox, outbox_count, radius);
        (void)nilamp_gui_knob(gui, ctx, canvas,
                              nilamp_gui_find_param_index(gui, NILAMP_PARAM_SPK_IND_FIND_DBHZ),
                              nilamp_gui_scale_rect(sx, sy, 423.0f, 61.0f, 66.0f, 104.0f),
                              outbox, outbox_count, radius);
        (void)nilamp_gui_knob(gui, ctx, canvas,
                              nilamp_gui_find_param_index(gui, NILAMP_PARAM_SAG_PCT),
                              nilamp_gui_scale_rect(sx, sy, 23.0f, 202.0f, 66.0f, 52.0f),
                              outbox, outbox_count, radius);
        nilamp_gui_enum_button(gui, ctx, canvas,
                               nilamp_gui_find_param_index(gui, NILAMP_PARAM_GAIN_COMP),
                               nilamp_gui_scale_rect(sx, sy, 18.0f, 266.0f, 78.0f, 58.0f),
                               outbox, outbox_count);
        (void)nilamp_gui_knob(gui, ctx, canvas,
                              nilamp_gui_find_param_index(gui, NILAMP_PARAM_SPK_RES_GAIN1_DB),
                              nilamp_gui_scale_rect(sx, sy, 207.0f, 208.0f, 66.0f, 104.0f),
                              outbox, outbox_count, radius);
        (void)nilamp_gui_knob(gui, ctx, canvas,
                              nilamp_gui_find_param_index(gui, NILAMP_PARAM_SPK_RES_GAIN2_DB),
                              nilamp_gui_scale_rect(sx, sy, 279.0f, 208.0f, 66.0f, 104.0f),
                              outbox, outbox_count, radius);
        (void)nilamp_gui_knob(gui, ctx, canvas,
                              nilamp_gui_find_param_index(gui, NILAMP_PARAM_SPK_RES_FRES_DBHZ),
                              nilamp_gui_scale_rect(sx, sy, 351.0f, 208.0f, 66.0f, 104.0f),
                              outbox, outbox_count, radius);
        (void)nilamp_gui_knob(gui, ctx, canvas,
                              nilamp_gui_find_param_index(gui, NILAMP_PARAM_SPK_RES_QTS_DB),
                              nilamp_gui_scale_rect(sx, sy, 423.0f, 208.0f, 66.0f, 104.0f),
                              outbox, outbox_count, radius);
    }
    nk_layout_space_end(ctx);

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

    NilampGuiMsg outbox[NILAMP_GUI_MAX_PARAMS];
    uint32_t outbox_count = 0u;
    nilamp_gui_build(gui, ctx, outbox, &outbox_count);
    nilamp_gui_drain_outbox(gui, outbox, outbox_count);

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
        .max_vertices = 8192,
        .image_pool_size = 8,
        .color_format = SG_PIXELFORMAT_RGBA8,
        .depth_format = SG_PIXELFORMAT_NONE,
        .sample_count = 1,
        .dpi_scale = (float)gui->scale,
    });
    gui->gpu_ready = true;
    nilamp_gui_sokol_in_use = true;
    return true;
}

static void nilamp_gui_shutdown_gpu(NilampGui *gui)
{
    if (!gui || !gui->gpu_ready) {
        return;
    }
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
    case PUGL_BUTTON_PRESS:
        if (event->button.button < 3u) {
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
    case PUGL_KEY_PRESS:
    case PUGL_KEY_RELEASE: {
        const bool down = event->type == PUGL_KEY_PRESS;
        enum nk_keys key = NK_KEY_NONE;
        switch (event->key.key) {
        case PUGL_KEY_BACKSPACE:
            key = NK_KEY_BACKSPACE;
            break;
        case PUGL_KEY_DELETE:
            key = NK_KEY_DEL;
            break;
        case PUGL_KEY_ENTER:
        case PUGL_KEY_PAD_ENTER:
            key = NK_KEY_ENTER;
            break;
        case PUGL_KEY_TAB:
            key = NK_KEY_TAB;
            break;
        case PUGL_KEY_LEFT:
            key = NK_KEY_LEFT;
            break;
        case PUGL_KEY_RIGHT:
            key = NK_KEY_RIGHT;
            break;
        case PUGL_KEY_UP:
            key = NK_KEY_UP;
            break;
        case PUGL_KEY_DOWN:
            key = NK_KEY_DOWN;
            break;
        case PUGL_KEY_HOME:
            key = NK_KEY_TEXT_LINE_START;
            break;
        case PUGL_KEY_END:
            key = NK_KEY_TEXT_LINE_END;
            break;
        case PUGL_KEY_SHIFT_L:
        case PUGL_KEY_SHIFT_R:
            key = NK_KEY_SHIFT;
            break;
        case PUGL_KEY_CTRL_L:
        case PUGL_KEY_CTRL_R:
            key = NK_KEY_CTRL;
            break;
        default:
            break;
        }
        if (key != NK_KEY_NONE) {
            gui->key_down[key] = down;
            nilamp_gui_request_redraw(gui);
        }
        break;
    }
    case PUGL_TEXT:
        if (event->text.string[0] >= 32 && gui->text_input_len + 1u < NILAMP_GUI_TEXT_INPUT_LEN) {
            gui->text_input[gui->text_input_len++] = event->text.string[0];
            nilamp_gui_request_redraw(gui);
        }
        break;
    default:
        break;
    }
    return PUGL_SUCCESS;
}

NilampGui *nilamp_gui_create(const NilampGuiCallbacks *callbacks,
                             const NilampGuiParamSpec *params,
                             uint32_t param_count,
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
    gui->api = api;
    gui->is_floating = is_floating;
    gui->model.width = NILAMP_GUI_DEFAULT_WIDTH;
    gui->model.height = NILAMP_GUI_DEFAULT_HEIGHT;
    gui->model.dirty = true;
    gui->scale = 1.0;
    gui->active_knob = -1;
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
    if (gui->view) {
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

void nilamp_gui_on_main_thread(NilampGui *gui)
{
    if (!gui || !gui->world || !gui->visible) {
        return;
    }
    (void)puglUpdate(gui->world, 0.0);
    if (gui->model.dirty) {
        nilamp_gui_request_redraw(gui);
    }
}
