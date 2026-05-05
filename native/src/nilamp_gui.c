// SPDX-License-Identifier: MIT
#include "nilamp_gui.h"

#include <pugl/gl.h>
#include <pugl/pugl.h>

#include <GL/gl.h>

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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define NILAMP_GUI_DEFAULT_WIDTH 540u
#define NILAMP_GUI_DEFAULT_HEIGHT 360u
#define NILAMP_GUI_MAX_PARAMS 16u

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
    NilampGuiModel model;
    double scale;
    int mouse_x;
    int mouse_y;
    bool mouse_down[3];
    bool mouse_up[3];
    bool mouse_motion;
    bool realized;
    bool visible;
    bool gpu_ready;
};

static bool nilamp_gui_sokol_in_use = false;

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

static void nilamp_gui_refresh_params(NilampGui *gui)
{
    if (!gui) {
        return;
    }
    for (uint32_t i = 0; i < gui->param_count && i < NILAMP_GUI_MAX_PARAMS; i++) {
        gui->model.param_values[i] = nilamp_gui_read_param(gui, i);
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
    table[NK_COLOR_TEXT] = nk_rgb(230, 224, 214);
    table[NK_COLOR_WINDOW] = nk_rgb(22, 23, 24);
    table[NK_COLOR_HEADER] = nk_rgb(56, 50, 43);
    table[NK_COLOR_BORDER] = nk_rgb(62, 61, 58);
    table[NK_COLOR_BUTTON] = nk_rgb(47, 48, 49);
    table[NK_COLOR_BUTTON_HOVER] = nk_rgb(62, 61, 58);
    table[NK_COLOR_BUTTON_ACTIVE] = nk_rgb(201, 74, 50);
    table[NK_COLOR_TOGGLE] = nk_rgb(47, 48, 49);
    table[NK_COLOR_TOGGLE_HOVER] = nk_rgb(62, 61, 58);
    table[NK_COLOR_TOGGLE_CURSOR] = nk_rgb(201, 74, 50);
    table[NK_COLOR_SELECT] = nk_rgb(47, 48, 49);
    table[NK_COLOR_SELECT_ACTIVE] = nk_rgb(201, 74, 50);
    table[NK_COLOR_SLIDER] = nk_rgb(38, 39, 40);
    table[NK_COLOR_SLIDER_CURSOR] = nk_rgb(201, 74, 50);
    table[NK_COLOR_SLIDER_CURSOR_HOVER] = nk_rgb(224, 96, 56);
    table[NK_COLOR_SLIDER_CURSOR_ACTIVE] = nk_rgb(236, 116, 70);
    table[NK_COLOR_PROPERTY] = nk_rgb(38, 39, 40);
    table[NK_COLOR_EDIT] = nk_rgb(38, 39, 40);
    table[NK_COLOR_EDIT_CURSOR] = nk_rgb(230, 224, 214);
    table[NK_COLOR_COMBO] = nk_rgb(47, 48, 49);
    table[NK_COLOR_CHART] = nk_rgb(38, 39, 40);
    table[NK_COLOR_CHART_COLOR] = nk_rgb(201, 74, 50);
    table[NK_COLOR_CHART_COLOR_HIGHLIGHT] = nk_rgb(224, 96, 56);
    table[NK_COLOR_SCROLLBAR] = nk_rgb(38, 39, 40);
    table[NK_COLOR_SCROLLBAR_CURSOR] = nk_rgb(88, 87, 82);
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = nk_rgb(110, 105, 96);
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgb(201, 74, 50);
    table[NK_COLOR_TAB_HEADER] = nk_rgb(56, 50, 43);
    nk_style_from_table(ctx, table);
    ctx->style.window.padding = nk_vec2(24.0f, 20.0f);
    ctx->style.window.spacing = nk_vec2(10.0f, 10.0f);
    ctx->style.window.border = 0.0f;
    ctx->style.slider.rounding = 4.0f;
    ctx->style.slider.bar_height = 10.0f;
}

static void nilamp_gui_feed_input(NilampGui *gui, struct nk_context *ctx)
{
    nk_input_begin(ctx);
    nk_input_motion(ctx, gui->mouse_x, gui->mouse_y);
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

    nk_layout_row_dynamic(ctx, 22.0f, 1);
    nk_label(ctx, "nilamp", NK_TEXT_LEFT);
    const char *model = gui->callbacks.model_name ? gui->callbacks.model_name(gui->callbacks.user)
                                                  : "Keller TWD DLX II";
    nk_label(ctx, model, NK_TEXT_LEFT);

    nk_layout_row_dynamic(ctx, 16.0f, 1);
    nk_spacing(ctx, 1);

    for (uint32_t i = 0; i < gui->param_count && i < NILAMP_GUI_MAX_PARAMS; i++) {
        const NilampGuiParamSpec *param = &gui->params[i];
        float value = gui->model.param_values[i];

        nk_layout_row_begin(ctx, NK_STATIC, 24.0f, 3);
        nk_layout_row_push(ctx, 86.0f);
        nk_label(ctx, param->name, NK_TEXT_LEFT);
        nk_layout_row_push(ctx, width - 210.0f);
        if (nk_slider_float(ctx, param->min_value, &value, param->max_value, 0.1f)) {
            nilamp_gui_emit(outbox, outbox_count, NILAMP_GUI_MAX_PARAMS,
                            (NilampGuiMsg){
                                .type = NILAMP_GUI_MSG_PARAM_CHANGED,
                                .param_id = param->id,
                                .value = value,
                            });
        }
        nk_layout_row_push(ctx, 68.0f);
        nk_labelf(ctx, NK_TEXT_RIGHT, "%.1f %s", value, param->unit);
        nk_layout_row_end(ctx);
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
    default:
        break;
    }
    return PUGL_SUCCESS;
}

NilampGui *nilamp_gui_create(const NilampGuiCallbacks *callbacks,
                             const NilampGuiParamSpec *params,
                             uint32_t param_count)
{
    if (!callbacks || !callbacks->get_param || !callbacks->set_param || !params ||
        param_count == 0 || param_count > NILAMP_GUI_MAX_PARAMS) {
        return NULL;
    }

    NilampGui *gui = (NilampGui *)calloc(1, sizeof(*gui));
    if (!gui) {
        return NULL;
    }

    gui->callbacks = *callbacks;
    gui->params = params;
    gui->param_count = param_count;
    gui->model.width = NILAMP_GUI_DEFAULT_WIDTH;
    gui->model.height = NILAMP_GUI_DEFAULT_HEIGHT;
    gui->model.dirty = true;
    gui->scale = 1.0;
    nilamp_gui_refresh_params(gui);

    gui->world = puglNewWorld(PUGL_MODULE, 0u);
    gui->view = gui->world ? puglNewView(gui->world) : NULL;
    if (!gui->view) {
        nilamp_gui_destroy(gui);
        return NULL;
    }

    puglSetHandle(gui->view, gui);
    if (puglSetBackend(gui->view, puglGlBackend()) ||
        puglSetEventFunc(gui->view, nilamp_gui_event) ||
        puglSetViewHint(gui->view, PUGL_CONTEXT_API, PUGL_OPENGL_API) ||
        puglSetViewHint(gui->view, PUGL_CONTEXT_VERSION_MAJOR, 3) ||
        puglSetViewHint(gui->view, PUGL_CONTEXT_VERSION_MINOR, 3) ||
        puglSetViewHint(gui->view, PUGL_CONTEXT_PROFILE, PUGL_OPENGL_CORE_PROFILE) ||
        puglSetViewHint(gui->view, PUGL_DOUBLE_BUFFER, 1) ||
        puglSetViewHint(gui->view, PUGL_STENCIL_BITS, 0) ||
        puglSetViewHint(gui->view, PUGL_RESIZABLE, 0) ||
        puglSetSizeHint(gui->view, PUGL_DEFAULT_SIZE, gui->model.width, gui->model.height) ||
        puglSetSizeHint(gui->view, PUGL_CURRENT_SIZE, gui->model.width, gui->model.height)) {
        nilamp_gui_destroy(gui);
        return NULL;
    }

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

bool nilamp_gui_set_parent_x11(NilampGui *gui, unsigned long parent)
{
    if (!gui || !gui->view || parent == 0ul) {
        return false;
    }
    if (!gui->realized) {
        if (puglSetParent(gui->view, (PuglNativeView)parent) || puglRealize(gui->view)) {
            return false;
        }
        gui->realized = true;
    }
    return true;
}

bool nilamp_gui_show(NilampGui *gui)
{
    if (!gui || !gui->view || !gui->realized || !gui->gpu_ready) {
        return false;
    }
    if (puglShow(gui->view, PUGL_SHOW_PASSIVE) > PUGL_FAILURE) {
        return false;
    }
    gui->visible = true;
    nilamp_gui_request_redraw(gui);
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
