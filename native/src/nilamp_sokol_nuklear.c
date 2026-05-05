// SPDX-License-Identifier: MIT
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
#define SOKOL_NUKLEAR_IMPL
#include "sokol_nuklear.h"
