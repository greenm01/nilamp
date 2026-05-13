// SPDX-License-Identifier: MIT
#include "nilamp_gui_edit.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GUARD_PREFIX 8u
#define GUARD_SUFFIX 8u
#define GUARD_STORAGE 80u
#define GUARD_PRE 0xA7u
#define GUARD_POST 0x5Cu
#define TEXT_FILL 0xCCu

typedef struct GuardedText {
    unsigned char storage[GUARD_STORAGE];
    size_t capacity;
} GuardedText;

static int failures = 0;

static void check_true(bool condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "test_gui_edit: failed: %s\n", name);
        failures++;
    }
}

static char *guarded_text(GuardedText *guarded)
{
    return (char *)&guarded->storage[GUARD_PREFIX];
}

static const char *guarded_const_text(const GuardedText *guarded)
{
    return (const char *)&guarded->storage[GUARD_PREFIX];
}

static void guarded_init(GuardedText *guarded, size_t capacity, const char *text)
{
    if (capacity > GUARD_STORAGE - GUARD_PREFIX - GUARD_SUFFIX) {
        capacity = GUARD_STORAGE - GUARD_PREFIX - GUARD_SUFFIX;
    }
    guarded->capacity = capacity;
    memset(guarded->storage, TEXT_FILL, sizeof(guarded->storage));
    memset(guarded->storage, GUARD_PRE, GUARD_PREFIX);
    memset(&guarded->storage[GUARD_PREFIX + capacity], GUARD_POST, GUARD_SUFFIX);

    if (capacity == 0u) {
        return;
    }
    char *dst = guarded_text(guarded);
    memset(dst, TEXT_FILL, capacity);
    size_t len = 0u;
    while (text && text[len] && len + 1u < capacity) {
        dst[len] = text[len];
        len++;
    }
    dst[len] = '\0';
}

static void guarded_init_unterminated(GuardedText *guarded, size_t capacity, char c)
{
    guarded_init(guarded, capacity, "");
    if (capacity == 0u) {
        return;
    }
    memset(guarded_text(guarded), (unsigned char)c, capacity);
}

static void check_guarded(const GuardedText *guarded, size_t cursor, const char *name)
{
    for (size_t i = 0u; i < GUARD_PREFIX; i++) {
        if (guarded->storage[i] != GUARD_PRE) {
            fprintf(stderr, "test_gui_edit: failed: %s prefix guard %zu\n", name, i);
            failures++;
            break;
        }
    }
    for (size_t i = 0u; i < GUARD_SUFFIX; i++) {
        const size_t index = GUARD_PREFIX + guarded->capacity + i;
        if (guarded->storage[index] != GUARD_POST) {
            fprintf(stderr, "test_gui_edit: failed: %s suffix guard %zu\n", name, i);
            failures++;
            break;
        }
    }
    if (guarded->capacity > 0u) {
        const char *text = guarded_const_text(guarded);
        const size_t len = nilamp_gui_edit_strlen(text, guarded->capacity);
        check_true(len < guarded->capacity, name);
        check_true(text[len] == '\0', name);
        check_true(cursor <= len, name);
    } else {
        check_true(cursor == 0u, name);
    }
}

static void check_text(const GuardedText *guarded, const char *expected, const char *name)
{
    check_true(strcmp(guarded_const_text(guarded), expected) == 0, name);
}

static void expect_delete(const char *initial, size_t cursor, const char *expected,
                          size_t expected_cursor, const char *name)
{
    GuardedText guarded;
    bool replace = true;
    guarded_init(&guarded, 8u, initial);
    const bool changed =
        nilamp_gui_edit_delete(guarded_text(&guarded), guarded.capacity, &cursor, &replace);
    check_true(strcmp(initial, expected) != 0 ? changed : !changed, name);
    check_true(!replace, name);
    check_true(cursor == expected_cursor, name);
    check_text(&guarded, expected, name);
    check_guarded(&guarded, cursor, name);
}

static void expect_backspace(const char *initial, size_t cursor, const char *expected,
                             size_t expected_cursor, const char *name)
{
    GuardedText guarded;
    bool replace = true;
    guarded_init(&guarded, 8u, initial);
    const bool changed =
        nilamp_gui_edit_backspace(guarded_text(&guarded), guarded.capacity, &cursor, &replace);
    check_true(strcmp(initial, expected) != 0 ? changed : !changed, name);
    check_true(!replace, name);
    check_true(cursor == expected_cursor, name);
    check_text(&guarded, expected, name);
    check_guarded(&guarded, cursor, name);
}

static void test_delete_and_backspace(void)
{
    expect_delete("", 0u, "", 0u, "delete empty");
    expect_delete("123", 0u, "23", 0u, "delete start");
    expect_delete("123", 1u, "13", 1u, "delete middle");
    expect_delete("123", 2u, "12", 2u, "delete last");
    expect_delete("123", 3u, "123", 3u, "delete end");
    expect_delete("123", 99u, "123", 3u, "delete clamped end");

    expect_backspace("", 0u, "", 0u, "backspace empty");
    expect_backspace("123", 0u, "123", 0u, "backspace start");
    expect_backspace("123", 2u, "13", 1u, "backspace middle");
    expect_backspace("1", 1u, "", 0u, "backspace first char");
    expect_backspace("123", 99u, "12", 2u, "backspace clamped end");
}

static void test_navigation_and_insert(void)
{
    GuardedText guarded;
    bool replace = true;
    size_t cursor = 2u;
    guarded_init(&guarded, 8u, "123");

    (void)nilamp_gui_edit_move_left(guarded_text(&guarded), guarded.capacity, &cursor,
                                    &replace);
    check_true(cursor == 1u && !replace, "move left");
    (void)nilamp_gui_edit_move_right(guarded_text(&guarded), guarded.capacity, &cursor,
                                     &replace);
    check_true(cursor == 2u, "move right");
    (void)nilamp_gui_edit_move_home(guarded_text(&guarded), guarded.capacity, &cursor,
                                    &replace);
    check_true(cursor == 0u, "move home");
    (void)nilamp_gui_edit_move_end(guarded_text(&guarded), guarded.capacity, &cursor,
                                   &replace);
    check_true(cursor == 3u, "move end");
    check_text(&guarded, "123", "navigation keeps text");
    check_guarded(&guarded, cursor, "navigation guarded");

    guarded_init(&guarded, 8u, "");
    cursor = 0u;
    replace = false;
    check_true(nilamp_gui_edit_insert_char(guarded_text(&guarded), guarded.capacity,
                                           &cursor, &replace, '7'),
               "insert empty changed");
    check_text(&guarded, "7", "insert empty text");
    check_true(cursor == 1u, "insert empty cursor");

    guarded_init(&guarded, 8u, "13");
    cursor = 1u;
    replace = false;
    check_true(nilamp_gui_edit_insert_char(guarded_text(&guarded), guarded.capacity,
                                           &cursor, &replace, '2'),
               "insert middle changed");
    check_text(&guarded, "123", "insert middle text");
    check_true(cursor == 2u, "insert middle cursor");

    guarded_init(&guarded, 8u, "123");
    cursor = 0u;
    replace = true;
    check_true(nilamp_gui_edit_insert_char(guarded_text(&guarded), guarded.capacity,
                                           &cursor, &replace, '9'),
               "replace insert changed");
    check_text(&guarded, "9", "replace insert text");
    check_true(cursor == 1u && !replace, "replace insert state");
    check_guarded(&guarded, cursor, "insert guarded");
}

static void test_capacity_and_dirty_buffers(void)
{
    GuardedText guarded;
    bool replace = false;
    size_t cursor = 3u;
    guarded_init(&guarded, 4u, "123");
    check_true(!nilamp_gui_edit_insert_char(guarded_text(&guarded), guarded.capacity,
                                            &cursor, &replace, '4'),
               "full insert rejected");
    check_text(&guarded, "123", "full insert text");
    check_true(cursor == 3u, "full insert cursor");
    check_guarded(&guarded, cursor, "full insert guarded");

    guarded_init(&guarded, 1u, "");
    cursor = 12u;
    replace = true;
    check_true(!nilamp_gui_edit_insert_char(guarded_text(&guarded), guarded.capacity,
                                            &cursor, &replace, '1'),
               "capacity one insert rejected");
    check_text(&guarded, "", "capacity one text");
    check_true(cursor == 0u, "capacity one cursor");
    check_guarded(&guarded, cursor, "capacity one guarded");

    guarded_init_unterminated(&guarded, 5u, '8');
    cursor = 99u;
    nilamp_gui_edit_sanitize(guarded_text(&guarded), guarded.capacity, &cursor);
    check_true(cursor == 4u, "unterminated cursor clamped");
    check_true(guarded_const_text(&guarded)[4] == '\0', "unterminated nul");
    check_guarded(&guarded, cursor, "unterminated guarded");
}

static void test_limited_insert(void)
{
    GuardedText guarded;
    bool replace = false;
    size_t cursor = 2u;
    guarded_init(&guarded, 8u, "12");
    check_true(!nilamp_gui_edit_insert_char_limited(guarded_text(&guarded),
                                                    guarded.capacity, 2u,
                                                    &cursor, &replace, '3'),
               "limited insert rejected");
    check_text(&guarded, "12", "limited insert text");
    check_true(cursor == 2u && !replace, "limited insert state");
    check_true(nilamp_gui_edit_backspace(guarded_text(&guarded), guarded.capacity,
                                         &cursor, &replace),
               "limited backspace still works");
    check_true(nilamp_gui_edit_insert_char_limited(guarded_text(&guarded),
                                                   guarded.capacity, 2u,
                                                   &cursor, &replace, '3'),
               "limited insert after delete");
    check_text(&guarded, "13", "limited insert after delete text");
    check_guarded(&guarded, cursor, "limited insert guarded");

    guarded_init(&guarded, 8u, "12345");
    replace = true;
    cursor = 5u;
    check_true(nilamp_gui_edit_insert_char_limited(guarded_text(&guarded),
                                                   guarded.capacity, 2u,
                                                   &cursor, &replace, '9'),
               "limited replace insert");
    check_text(&guarded, "9", "limited replace insert text");
    check_true(cursor == 1u && !replace, "limited replace insert state");
    check_guarded(&guarded, cursor, "limited replace guarded");
}

static void test_apply_keys(void)
{
    GuardedText guarded;
    bool replace = true;
    size_t cursor = 2u;
    guarded_init(&guarded, 8u, "123");
    const bool changed = nilamp_gui_edit_apply_keys(
        guarded_text(&guarded), guarded.capacity, &cursor, &replace, false, false,
        false, false, true, true);
    check_true(changed, "combined delete changed");
    check_text(&guarded, "13", "combined backspace precedence text");
    check_true(cursor == 1u && !replace, "combined backspace precedence state");
    check_guarded(&guarded, cursor, "combined guarded");

    guarded_init(&guarded, 8u, "123");
    replace = true;
    cursor = 1u;
    check_true(!nilamp_gui_edit_apply_keys(guarded_text(&guarded), guarded.capacity,
                                           &cursor, &replace, false, false, false,
                                           false, false, true),
               "forward delete ignored");
    check_text(&guarded, "123", "forward delete ignored text");
    check_true(cursor == 1u && replace, "forward delete ignored state");
    check_guarded(&guarded, cursor, "forward delete ignored guarded");

    guarded_init(&guarded, 8u, "123");
    replace = true;
    cursor = 1u;
    check_true(!nilamp_gui_edit_apply_keys(guarded_text(&guarded), guarded.capacity,
                                           &cursor, &replace, true, false, false,
                                           false, false, false),
               "navigation no text change");
    check_true(cursor == 0u && !replace, "navigation clears replace");
    check_text(&guarded, "123", "navigation apply text");
}

static uint32_t rng_next(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static void test_randomized_invariants(void)
{
    static const size_t capacities[] = {1u, 2u, 3u, 4u, 8u, NILAMP_GUI_EDIT_TEXT_LEN};
    uint32_t rng = 0x1234abcdu;
    for (size_t ci = 0u; ci < sizeof(capacities) / sizeof(capacities[0]); ci++) {
        GuardedText guarded;
        guarded_init(&guarded, capacities[ci], "12345");
        size_t cursor = (size_t)(rng_next(&rng) % 16u);
        bool replace = (rng_next(&rng) & 1u) != 0u;
        for (size_t step = 0u; step < 10000u; step++) {
            const uint32_t op = rng_next(&rng) % 10u;
            switch (op) {
            case 0u:
                (void)nilamp_gui_edit_move_home(guarded_text(&guarded), guarded.capacity,
                                                &cursor, &replace);
                break;
            case 1u:
                (void)nilamp_gui_edit_move_end(guarded_text(&guarded), guarded.capacity,
                                              &cursor, &replace);
                break;
            case 2u:
                (void)nilamp_gui_edit_move_left(guarded_text(&guarded), guarded.capacity,
                                                &cursor, &replace);
                break;
            case 3u:
                (void)nilamp_gui_edit_move_right(guarded_text(&guarded), guarded.capacity,
                                                 &cursor, &replace);
                break;
            case 4u:
                (void)nilamp_gui_edit_backspace(guarded_text(&guarded), guarded.capacity,
                                                &cursor, &replace);
                break;
            case 5u:
                (void)nilamp_gui_edit_delete(guarded_text(&guarded), guarded.capacity,
                                             &cursor, &replace);
                break;
            case 6u:
            case 7u:
            case 8u:
                (void)nilamp_gui_edit_insert_char_limited(
                    guarded_text(&guarded), guarded.capacity,
                    (size_t)(1u + (rng_next(&rng) % 7u)), &cursor, &replace,
                    (char)('0' + (rng_next(&rng) % 10u)));
                break;
            default:
                replace = true;
                break;
            }
            check_guarded(&guarded, cursor, "randomized invariants");
        }
    }
}

int main(void)
{
    test_delete_and_backspace();
    test_navigation_and_insert();
    test_capacity_and_dirty_buffers();
    test_limited_insert();
    test_apply_keys();
    test_randomized_invariants();
    if (failures != 0) {
        return 1;
    }
    puts("test_gui_edit: ok");
    return 0;
}
