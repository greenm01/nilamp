// SPDX-License-Identifier: MIT
#ifndef NILAMP_GUI_EDIT_H
#define NILAMP_GUI_EDIT_H

#include <stdbool.h>
#include <stddef.h>

#define NILAMP_GUI_EDIT_TEXT_LEN 32u

size_t nilamp_gui_edit_strlen(const char *text, size_t capacity);
void nilamp_gui_edit_sanitize(char *text, size_t capacity, size_t *cursor);

bool nilamp_gui_edit_move_home(char *text, size_t capacity, size_t *cursor,
                               bool *replace_on_type);
bool nilamp_gui_edit_move_end(char *text, size_t capacity, size_t *cursor,
                              bool *replace_on_type);
bool nilamp_gui_edit_move_left(char *text, size_t capacity, size_t *cursor,
                               bool *replace_on_type);
bool nilamp_gui_edit_move_right(char *text, size_t capacity, size_t *cursor,
                                bool *replace_on_type);

bool nilamp_gui_edit_backspace(char *text, size_t capacity, size_t *cursor,
                               bool *replace_on_type);
bool nilamp_gui_edit_delete(char *text, size_t capacity, size_t *cursor,
                            bool *replace_on_type);
bool nilamp_gui_edit_insert_char(char *text, size_t capacity, size_t *cursor,
                                 bool *replace_on_type, char c);
bool nilamp_gui_edit_apply_keys(char *text, size_t capacity, size_t *cursor,
                                bool *replace_on_type, bool home, bool end,
                                bool left, bool right, bool backspace,
                                bool delete_key);

#endif
