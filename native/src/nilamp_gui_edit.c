// SPDX-License-Identifier: MIT
#include "nilamp_gui_edit.h"

#include <string.h>

size_t nilamp_gui_edit_strlen(const char *text, size_t capacity)
{
    size_t len = 0u;
    if (!text) {
        return 0u;
    }
    while (len < capacity && text[len] != '\0') {
        len++;
    }
    return len;
}

void nilamp_gui_edit_sanitize(char *text, size_t capacity, size_t *cursor)
{
    if (!text || capacity == 0u) {
        if (cursor) {
            *cursor = 0u;
        }
        return;
    }

    text[capacity - 1u] = '\0';
    const size_t len = nilamp_gui_edit_strlen(text, capacity);
    if (cursor && *cursor > len) {
        *cursor = len;
    }
}

static void nilamp_gui_edit_clear_replace(bool *replace_on_type)
{
    if (replace_on_type) {
        *replace_on_type = false;
    }
}

bool nilamp_gui_edit_move_home(char *text, size_t capacity, size_t *cursor,
                               bool *replace_on_type)
{
    nilamp_gui_edit_sanitize(text, capacity, cursor);
    nilamp_gui_edit_clear_replace(replace_on_type);
    if (!cursor || *cursor == 0u) {
        return false;
    }
    *cursor = 0u;
    return true;
}

bool nilamp_gui_edit_move_end(char *text, size_t capacity, size_t *cursor,
                              bool *replace_on_type)
{
    nilamp_gui_edit_sanitize(text, capacity, cursor);
    nilamp_gui_edit_clear_replace(replace_on_type);
    const size_t len = nilamp_gui_edit_strlen(text, capacity);
    if (!cursor || *cursor == len) {
        return false;
    }
    *cursor = len;
    return true;
}

bool nilamp_gui_edit_move_left(char *text, size_t capacity, size_t *cursor,
                               bool *replace_on_type)
{
    nilamp_gui_edit_sanitize(text, capacity, cursor);
    nilamp_gui_edit_clear_replace(replace_on_type);
    if (!cursor || *cursor == 0u) {
        return false;
    }
    *cursor -= 1u;
    return true;
}

bool nilamp_gui_edit_move_right(char *text, size_t capacity, size_t *cursor,
                                bool *replace_on_type)
{
    nilamp_gui_edit_sanitize(text, capacity, cursor);
    nilamp_gui_edit_clear_replace(replace_on_type);
    const size_t len = nilamp_gui_edit_strlen(text, capacity);
    if (!cursor || *cursor >= len) {
        return false;
    }
    *cursor += 1u;
    return true;
}

bool nilamp_gui_edit_backspace(char *text, size_t capacity, size_t *cursor,
                               bool *replace_on_type)
{
    nilamp_gui_edit_sanitize(text, capacity, cursor);
    nilamp_gui_edit_clear_replace(replace_on_type);
    if (!text || capacity == 0u || !cursor || *cursor == 0u) {
        return false;
    }

    const size_t len = nilamp_gui_edit_strlen(text, capacity);
    const size_t pos = *cursor;
    if (pos > len) {
        *cursor = len;
        return false;
    }

    memmove(&text[pos - 1u], &text[pos], len - pos + 1u);
    *cursor = pos - 1u;
    return true;
}

bool nilamp_gui_edit_delete(char *text, size_t capacity, size_t *cursor,
                            bool *replace_on_type)
{
    nilamp_gui_edit_sanitize(text, capacity, cursor);
    nilamp_gui_edit_clear_replace(replace_on_type);
    if (!text || capacity == 0u || !cursor) {
        return false;
    }

    const size_t len = nilamp_gui_edit_strlen(text, capacity);
    const size_t pos = *cursor;
    if (pos >= len) {
        return false;
    }

    memmove(&text[pos], &text[pos + 1u], len - pos);
    return true;
}

bool nilamp_gui_edit_insert_char(char *text, size_t capacity, size_t *cursor,
                                 bool *replace_on_type, char c)
{
    nilamp_gui_edit_sanitize(text, capacity, cursor);
    if (!text || capacity == 0u || !cursor) {
        return false;
    }

    size_t len = nilamp_gui_edit_strlen(text, capacity);
    if (replace_on_type && *replace_on_type) {
        text[0] = '\0';
        len = 0u;
        *cursor = 0u;
        *replace_on_type = false;
    }
    if (len + 1u >= capacity) {
        return false;
    }

    const size_t pos = *cursor <= len ? *cursor : len;
    memmove(&text[pos + 1u], &text[pos], len - pos + 1u);
    text[pos] = c;
    *cursor = pos + 1u;
    return true;
}

bool nilamp_gui_edit_apply_keys(char *text, size_t capacity, size_t *cursor,
                                bool *replace_on_type, bool home, bool end,
                                bool left, bool right, bool backspace,
                                bool delete_key)
{
    nilamp_gui_edit_sanitize(text, capacity, cursor);
    if (home) {
        (void)nilamp_gui_edit_move_home(text, capacity, cursor, replace_on_type);
    }
    if (end) {
        (void)nilamp_gui_edit_move_end(text, capacity, cursor, replace_on_type);
    }
    if (left) {
        (void)nilamp_gui_edit_move_left(text, capacity, cursor, replace_on_type);
    }
    if (right) {
        (void)nilamp_gui_edit_move_right(text, capacity, cursor, replace_on_type);
    }
    if (backspace) {
        return nilamp_gui_edit_backspace(text, capacity, cursor, replace_on_type);
    }
    if (delete_key) {
        return nilamp_gui_edit_delete(text, capacity, cursor, replace_on_type);
    }
    return false;
}
