/*
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2023 Local Atticus
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef LCA_H
#define LCA_H

#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(LCA_MALLOC) || defined(LCA_REALLOC) || defined(LCA_DEALLOC)
#    if !defined(LCA_MALLOC) || !defined(LCA_REALLOC) || !defined(LCA_DEALLOC)
#        error "The LCA library requires all three of LCA_MALLOC, LCA_REALLOC and LCA_DEALLOC to be defined if at least one is."
#    endif
#else
#    define LCA_MALLOC(N)     (lca_allocate(lca_default_allocator, N))
#    define LCA_REALLOC(P, N) (lca_reallocate(lca_default_allocator, P, N))
#    define LCA_FREE(P)       (lca_deallocate(lca_default_allocator, P))
#endif

#define ANSI_COLOR_RESET             "\x1b[0m"
#define ANSI_COLOR_BLACK             "\x1b[30m"
#define ANSI_COLOR_RED               "\x1b[31m"
#define ANSI_COLOR_GREEN             "\x1b[32m"
#define ANSI_COLOR_YELLOW            "\x1b[33m"
#define ANSI_COLOR_BLUE              "\x1b[34m"
#define ANSI_COLOR_MAGENTA           "\x1b[35m"
#define ANSI_COLOR_CYAN              "\x1b[36m"
#define ANSI_COLOR_WHITE             "\x1b[37m"
#define ANSI_COLOR_BRIGHT_BLACK      "\x1b[30;1m"
#define ANSI_COLOR_BRIGHT_RED        "\x1b[31;1m"
#define ANSI_COLOR_BRIGHT_GREEN      "\x1b[32;1m"
#define ANSI_COLOR_BRIGHT_YELLOW     "\x1b[33;1m"
#define ANSI_COLOR_BRIGHT_BLUE       "\x1b[34;1m"
#define ANSI_COLOR_BRIGHT_MAGENTA    "\x1b[35;1m"
#define ANSI_COLOR_BRIGHT_CYAN       "\x1b[36;1m"
#define ANSI_COLOR_BRIGHT_WHITE      "\x1b[37;1m"
#define ANSI_BG_COLOR_BLACK          "\x1b[40m"
#define ANSI_BG_COLOR_RED            "\x1b[41m"
#define ANSI_BG_COLOR_GREEN          "\x1b[42m"
#define ANSI_BG_COLOR_YELLOW         "\x1b[43m"
#define ANSI_BG_COLOR_BLUE           "\x1b[44m"
#define ANSI_BG_COLOR_MAGENTA        "\x1b[45m"
#define ANSI_BG_COLOR_CYAN           "\x1b[46m"
#define ANSI_BG_COLOR_WHITE          "\x1b[47m"
#define ANSI_BG_COLOR_BRIGHT_BLACK   "\x1b[40;1m"
#define ANSI_BG_COLOR_BRIGHT_RED     "\x1b[41;1m"
#define ANSI_BG_COLOR_BRIGHT_GREEN   "\x1b[42;1m"
#define ANSI_BG_COLOR_BRIGHT_YELLOW  "\x1b[43;1m"
#define ANSI_BG_COLOR_BRIGHT_BLUE    "\x1b[44;1m"
#define ANSI_BG_COLOR_BRIGHT_MAGENTA "\x1b[45;1m"
#define ANSI_BG_COLOR_BRIGHT_CYAN    "\x1b[46;1m"
#define ANSI_BG_COLOR_BRIGHT_WHITE   "\x1b[47;1m"
#define ANSI_STYLE_BOLD              "\x1b[1m"
#define ANSI_STYLE_UNDERLINE         "\x1b[4m"
#define ANSI_STYLE_REVERSED          "\x1b[7m"

#define LCA_SV_EMPTY       ((lca_string_view){0})
#define LCA_SV_CONSTANT(C) ((lca_string_view){.data = (C), .count = (sizeof C) - 1})
#define LCA_STR_EXPAND(s)  ((int)s.count), (s.data)

typedef void* (*lca_allocator_function)(void* user_data, size_t count, void* ptr);

typedef struct lca_allocator {
    void* user_data;
    lca_allocator_function allocator_function;
} lca_allocator;

typedef struct lca_arena lca_arena;

extern lca_allocator lca_default_allocator;
extern lca_allocator temp_allocator;

/// Header data for a light-weight implelentation of typed dynamic arrays.
typedef struct lca_da_header {
    int64_t capacity;
    int64_t count;
} lca_da_header;

// invariant: should always be nul terminated
typedef struct lca_string {
    lca_allocator allocator;
    char* data;
    int64_t count;
    int64_t capacity;
} lca_string;

typedef struct lca_string_view {
    const char* data;
    int64_t count;
} lca_string_view;

typedef struct lca_command_result {
    bool exited;
    int exit_code;
} lca_command_result;

typedef struct lca_command {
    const char** arguments;
    int64_t count;
    int64_t capacity;
} lca_command;

void lca_da_maybe_expand(void** da_ref, int64_t element_size, int64_t required_count);

void* lca_allocate(lca_allocator allocator, size_t n);
void lca_deallocate(lca_allocator allocator, void* ptr);
void* lca_reallocate(lca_allocator allocator, void* ptr, size_t n);

void lca_temp_allocator_init(lca_allocator allocator, int64_t block_size);
void lca_temp_allocator_clear(void);
void lca_temp_allocator_dump(void);

char* lca_temp_sprintf(const char* format, ...);
char* lca_temp_vsprintf(const char* format, va_list v);

lca_arena* lca_arena_create(lca_allocator allocator, size_t block_size);
void lca_arena_destroy(lca_arena* arena);
void* lca_arena_push(lca_arena* arena, size_t size);
void lca_arena_clear(lca_arena* arena);
void lca_arena_dump(lca_arena* arena);

lca_string lca_string_create(lca_allocator allocator);
lca_string lca_string_from_data(lca_allocator allocator, char* data, int64_t count, int64_t capacity);
void lca_string_destroy(lca_string* s);
char* lca_string_as_cstring(lca_string s);
bool lca_string_equals(lca_string a, lca_string b);
lca_string_view lca_string_slice(lca_string s, int64_t offset, int64_t length);
lca_string lca_string_format(const char* format, ...);
lca_string lca_string_vformat(const char* format, va_list v);
void lca_string_append_format(lca_string* s, const char* format, ...);
void lca_string_append_vformat(lca_string* s, const char* format, va_list v);
void lca_string_append_rune(lca_string* s, int rune);

void lca_string_path_parent(lca_string* string);
void lca_string_path_append(lca_string* path, lca_string s);
void lca_string_path_append_cstring(lca_string* path, const char* s);
void lca_string_path_append_view(lca_string* path, lca_string_view s);

lca_string_view lca_string_view_from_cstring(const char* s);
lca_string_view lca_string_as_view(lca_string s);
lca_string_view lca_string_view_slice(lca_string_view sv, int64_t offset, int64_t length);
bool lca_string_view_equals(lca_string_view a, lca_string_view b);
bool lca_string_view_equals_cstring(lca_string_view a, const char* b);
bool lca_string_view_starts_with(lca_string_view a, lca_string_view b);
lca_string lca_string_view_to_string(lca_allocator allocator, lca_string_view s);
char* lca_string_view_to_cstring(lca_allocator allocator, lca_string_view s);
int64_t lca_string_view_index_of(lca_string_view s, char c);
int64_t lca_string_view_last_index_of(lca_string_view s, char c);
bool lca_string_view_ends_with_cstring(lca_string_view s, const char* cstr);
lca_string lca_string_view_change_extension(lca_allocator allocator, lca_string_view s, const char* new_ext);

lca_string_view lca_string_view_path_file_name(lca_string_view s);

#define lca_da(T)            T*
#define lca_da_get_header(V) (((struct lca_da_header*)(V)) - 1)
#define lca_da_count(V)      ((V) ? lca_da_get_header(V)->count : 0)
#define lca_da_capacity(V)   ((V) ? lca_da_get_header(V)->capacity : 0)
#define lca_da_reserve(V, N) \
    do { lca_da_maybe_expand((void**)&(V), (int64_t)sizeof *(V), N); } while (0)
#define lca_da_count_set(V, N)                    \
    do {                                          \
        lca_da_reserve(V, N);                     \
        if (V) lca_da_get_header(V)->count = (N); \
    } while (0)
#define lca_da_push(V, E)                                                             \
    do {                                                                              \
        lca_da_maybe_expand((void**)&(V), (int64_t)sizeof *(V), lca_da_count(V) + 1); \
        (V)[lca_da_count(V)] = E;                                                     \
        lca_da_get_header(V)->count++;                                                \
    } while (0)
#define lca_da_pop(V)                                                   \
    do {                                                                \
        if (lca_da_get_header(V)->count) lca_da_get_header(V)->count--; \
    } while (0)
#define lca_da_insert(V, I, E)                                                                 \
    assert((I) > 0);                                                                           \
    do {                                                                                       \
        lca_da_count_set((V), lca_da_count((V)) + 1);                                          \
        memmove(&(V)[(I) + 1], &(V)[(I)], sizeof(*(V)) * (size_t)(lca_da_count((V)) - (I)-1)); \
        (V)[(I)] = (E);                                                                        \
    } while (0)
#define lca_da_back(V) (&(V)[lca_da_count(V) - 1])
#define lca_da_free(V)                                             \
    do {                                                           \
        if (V) {                                                   \
            memset(V, 0, (size_t)lca_da_count(V) * (sizeof *(V))); \
            LCA_FREE(lca_da_get_header(V));                        \
            (V) = NULL;                                            \
        }                                                          \
    } while (0)
#define lca_da_free_all(V, F)                                                                                    \
    do {                                                                                                         \
        if (V) {                                                                                                 \
            for (int64_t lca_da_index = 0; lca_da_index < lca_da_count(V); lca_da_index++) F((V)[lca_da_index]); \
            memset(V, 0, (size_t)lca_da_count(V) * (sizeof *(V)));                                               \
            LCA_FREE(lca_da_get_header(V));                                                                      \
            (V) = NULL;                                                                                          \
        }                                                                                                        \
    } while (0)

char* lca_shift_args(int* argc, char*** argv);

bool lca_stdout_isatty(void);
bool lca_stderr_isatty(void);

bool lcat_file_exists(const char* file_path);
lca_string lca_file_read(lca_allocator allocator, const char* file_path);

char* lca_plat_self_exe(void);

void lca_command_append1(lca_command* command, const char* argument);
void lca_command_append_many(lca_command* command, const char** arguments, int argument_count);
#define lca_command_append(C, ...) lca_command_append_many(C, ((const char*[]){__VA_ARGS__}), (int)(sizeof((const char*[]){__VA_ARGS__}) / sizeof(const char*)))
lca_command_result lca_command_run(lca_command command);

#if defined(LCA_IMPLEMENTATION)

#    include <errno.h>

#    ifdef _WIN32
#        define WIN32_LEAN_AND_MEAN
#        define _WINUSER_
#        define _WINGDI_
#        define _IMM_
#        define _WINCON_
#        include <direct.h>
#        include <io.h>
#        include <shellapi.h>
#        include <windows.h>
#    else
#        include <execinfo.h>
#        include <fcntl.h>
#        include <sys/stat.h>
#        include <sys/types.h>
#        include <sys/wait.h>
#        include <unistd.h>
#    endif

void lca_da_maybe_expand(void** da_ref, int64_t element_size, int64_t required_count) {
    if (required_count <= 0) return;

    struct lca_da_header* header = lca_da_get_header(*da_ref);
    if (!*da_ref) {
        int64_t initial_capacity = 32;
        void* new_data = LCA_MALLOC((sizeof *header) + (size_t)(initial_capacity * element_size));
        header = new_data;

        header->capacity = initial_capacity;
        header->count = 0;
    } else if (required_count > header->capacity) {
        while (required_count > header->capacity)
            header->capacity *= 2;
        header = LCA_REALLOC(header, (sizeof *header) + (size_t)(header->capacity * element_size));
    }

    *da_ref = (void*)(header + 1);
}

lca_string lca_string_create(lca_allocator allocator) {
    int64_t capacity = 32;
    char* data = lca_allocate(allocator, (size_t)capacity * sizeof *data);
    assert(data);
    return (lca_string){
        .allocator = allocator,
        .data = data,
        .capacity = capacity,
        .count = 0,
    };
}

lca_string lca_string_from_data(lca_allocator allocator, char* data, int64_t count, int64_t capacity) {
    assert(data);
    assert(capacity > 0);
    assert(count < capacity);
    assert(data[count] == 0);
    return (lca_string){
        .allocator = allocator,
        .data = data,
        .capacity = capacity,
        .count = count,
    };
}

void lca_string_destroy(lca_string* s) {
    if (s == NULL || s->data == NULL) return;
    lca_deallocate(s->allocator, s->data);
    *s = (lca_string){0};
}

char* lca_string_as_cstring(lca_string s) {
    assert(s.count < s.capacity);
    assert(s.data[s.count] == 0);
    return s.data;
}

bool lca_string_equals(lca_string a, lca_string b) {
    if (a.count != b.count) return false;
    for (int64_t i = 0; i < a.count; i++) {
        if (a.data[i] != b.data[i])
            return false;
    }
    return true;
}

lca_string_view lca_string_slice(lca_string s, int64_t offset, int64_t length) {
    assert(offset >= 0);
    assert(length >= 0);
    assert(offset + length <= s.count);

    return (lca_string_view){
        .data = s.data + offset,
        .count = length,
    };
}

lca_string lca_string_format(const char* format, ...) {
    va_list v;
    va_start(v, format);
    lca_string result = lca_string_vformat(format, v);
    va_end(v);
    return result;
}

lca_string lca_string_vformat(const char* format, va_list v) {
    char* buffer = lca_temp_vsprintf(format, v);
    int64_t count = strlen(buffer);
    return lca_string_from_data(temp_allocator, buffer, count, count + 1);
}

static void lca_string_ensure_capacity(lca_string* s, int64_t min_capacity) {
    if (s == NULL) return;
    if (s->capacity >= min_capacity) return;

    int64_t new_capacity = s->capacity;
    if (new_capacity == 0) {
        new_capacity = min_capacity;
    } else {
        while (new_capacity < min_capacity) {
            new_capacity <<= 1;
        }
    }

    s->data = lca_reallocate(s->allocator, s->data, new_capacity);
    assert(s->data != NULL);
    s->capacity = new_capacity;
}

void lca_string_append_format(lca_string* s, const char* format, ...) {
    assert(s != NULL);
    va_list v;
    va_start(v, format);
    lca_string_append_vformat(s, format, v);
    va_end(v);
}

void lca_string_append_vformat(lca_string* s, const char* format, va_list v) {
    assert(s != NULL);

    va_list v1;
    va_copy(v1, v);
    int n = vsnprintf(NULL, 0, format, v1);
    va_end(v1);

    lca_string_ensure_capacity(s, s->count + n + 1);
    vsnprintf(s->data + s->count, n + 1, format, v);

    s->count += n;
}

void lca_string_append_rune(lca_string* s, int rune) {
    assert(s != NULL);
    lca_string_ensure_capacity(s, s->count + 1);
    s->data[s->count] = (char)rune;
    s->count += 1;
}

void lca_string_path_parent(lca_string* string) {
    if (string->count > 0 && (string->data[string->count - 1] == '/' || string->data[string->count - 1] == '\\')) {
        string->data[string->count - 1] = 0;
        string->count--;
    }

    for (int64_t i = string->count - 1; i >= 0 && string->data[i] != '/' && string->data[i] != '\\'; i--) {
        string->data[i] = 0;
        string->count--;
    }
}

void lca_string_path_append_view(lca_string* path, lca_string_view s) {
    assert(path != NULL);

    bool path_ends_with_slash = path->count > 0 && (path->data[path->count - 1] == '/' || path->data[path->count - 1] == '\\');
    bool view_starts_with_slash = s.count > 0 && (s.data[0] == '/' || s.data[0] == '\\');

    if (path_ends_with_slash && view_starts_with_slash) {
        s.data++;
        s.count--;
        view_starts_with_slash = false;
    }

    if (path_ends_with_slash) {
        lca_string_append_format(path, "%.*s", LCA_STR_EXPAND(s));
    } else {
        lca_string_append_format(path, "/%.*s", LCA_STR_EXPAND(s));
    }
}

lca_string_view lca_string_view_from_cstring(const char* s) {
    return (lca_string_view){
        .data = s,
        .count = strlen(s),
    };
}

lca_string_view lca_string_as_view(lca_string s) {
    return (lca_string_view){
        .data = s.data,
        .count = s.count,
    };
}

lca_string_view lca_string_view_slice(lca_string_view sv, int64_t offset, int64_t length) {
    assert(offset >= 0 && offset < sv.count);
    if (length == -1) {
        length = sv.count - offset;
    }
    assert(length >= 0 && length <= sv.count - offset);

    return (lca_string_view){
        .data = sv.data + offset,
        .count = length,
    };
}

bool lca_string_view_equals(lca_string_view a, lca_string_view b) {
    if (a.count != b.count) return false;
    for (int64_t i = 0; i < a.count; i++) {
        if (a.data[i] != b.data[i])
            return false;
    }
    return true;
}

bool lca_string_view_equals_cstring(lca_string_view a, const char* b) {
    for (int64_t i = 0; i < a.count; i++) {
        if (a.data[i] != b[i])
            return false;
        if (b[i] == 0)
            return false;
    }
    return b[a.count] == 0;
}

bool lca_string_view_starts_with(lca_string_view a, lca_string_view b) {
    if (a.count < b.count) return false;
    for (int64_t i = 0; i < b.count; i++) {
        if (a.data[i] != b.data[i])
            return false;
    }
    return true;
}

lca_string lca_string_view_to_string(lca_allocator allocator, lca_string_view s) {
    char* data = lca_allocate(allocator, s.count + 1);
    assert(data);
    memcpy(data, s.data, (size_t)s.count);
    return lca_string_from_data(allocator, data, s.count, s.count + 1);
}

char* lca_string_view_to_cstring(lca_allocator allocator, lca_string_view s) {
    char* result = lca_allocate(allocator, s.count + 1);
    memcpy(result, s.data, s.count);
    result[s.count] = 0;
    return result;
}

int64_t lca_string_view_index_of(lca_string_view s, char c) {
    for (int64_t i = 0; i < s.count; i++) {
        if (s.data[i] == c) {
            return i;
        }
    }

    return -1;
}

int64_t lca_string_view_last_index_of(lca_string_view s, char c) {
    for (int64_t i = s.count - 1; i >= 0; i--) {
        if (s.data[i] == c) {
            return i;
        }
    }

    return -1;
}

bool lca_string_view_ends_with_cstring(lca_string_view s, const char* cstr) {
    int64_t cstr_len = (int64_t)strlen(cstr);
    if (cstr_len > s.count) {
        return false;
    }

    return 0 == strncmp(s.data + s.count - cstr_len, cstr, (size_t)cstr_len);
}

lca_string lca_string_view_change_extension(lca_allocator allocator, lca_string_view s, const char* new_ext) {
    int64_t last_slash_index = 0;
    for (int64_t i = last_slash_index - 1; i >= 0; i--) {
        if (s.data[i] == '/' || s.data[i] == '\\') {
            last_slash_index = i;
            break;
        }
    }

    int64_t last_dot_index = s.count;
    for (int64_t i = last_dot_index - 1; i > last_slash_index; i--) {
        if (s.data[i] == '.') {
            last_dot_index = i;
            break;
        }
    }

    int64_t new_ext_length = strlen(new_ext);
    lca_string result = lca_string_create(allocator);
    lca_string_ensure_capacity(&result, last_dot_index + new_ext_length + 1);

    lca_string_append_format(&result, "%.*s%s", (int)last_dot_index, s.data, new_ext);

    return result;
}

lca_string_view lca_string_view_path_file_name(lca_string_view s) {
    int64_t start_index = s.count - 1;
    while (start_index > 0) {
        if (s.data[start_index - 1] == '/' || s.data[start_index - 1] == '\\') {
            break;
        }
        start_index--;
    }

    s.data += start_index;
    s.count -= start_index;

    return s;
}

typedef struct lca_arena_block {
    void* memory;
    int64_t allocated;
    int64_t capacity;
} lca_arena_block;

struct lca_arena {
    lca_allocator allocator;
    lca_da(lca_arena_block) blocks;
    int64_t block_size;
};

void* lca_lca_default_allocator_function(void* user_data, size_t count, void* ptr);
void* lca_temp_allocator_function(void* user_data, size_t count, void* ptr);

lca_allocator lca_default_allocator = {
    .allocator_function = lca_lca_default_allocator_function
};

lca_allocator temp_allocator = {0};

void* lca_allocate(lca_allocator allocator, size_t n) {
    return allocator.allocator_function(allocator.user_data, n, NULL);
}

void lca_deallocate(lca_allocator allocator, void* n) {
    allocator.allocator_function(allocator.user_data, 0, n);
}

void* lca_reallocate(lca_allocator allocator, void* ptr, size_t n) {
    return allocator.allocator_function(allocator.user_data, n, ptr);
}

void* lca_lca_default_allocator_function(void* user_data, size_t count, void* ptr) {
    if (count == 0) {
        free(ptr);
        return NULL;
    } else if (ptr == NULL) {
        void* data = calloc(1, count);
        assert(data != NULL);
        return data;
    } else {
        void* data = realloc(ptr, count);
        assert(data != NULL);
        return data;
    }
}

void* lca_temp_allocator_function(void* user_data, size_t count, void* ptr) {
    lca_arena* temp_arena = temp_allocator.user_data;
    assert(temp_arena != NULL && "Where did the arena go? did you init it?");
    assert(ptr == NULL && "Cannot reallocate temp arena memory");
    return lca_arena_push(temp_arena, count);
}

void lca_temp_allocator_init(lca_allocator allocator, int64_t block_size) {
    assert(temp_allocator.user_data == NULL);
    temp_allocator = (lca_allocator){
        .user_data = lca_arena_create(allocator, block_size),
        .allocator_function = lca_temp_allocator_function,
    };

    assert(temp_allocator.user_data != NULL);
}

void lca_temp_allocator_clear(void) {
    lca_arena* temp_arena = temp_allocator.user_data;
    lca_arena_clear(temp_arena);
}

void lca_temp_allocator_dump(void) {
    lca_arena* temp_arena = temp_allocator.user_data;
    lca_arena_dump(temp_arena);
}

char* lca_temp_sprintf(const char* format, ...) {
    va_list v;
    va_start(v, format);
    char* result = lca_temp_vsprintf(format, v);
    va_end(v);
    return result;
}

char* lca_temp_vsprintf(const char* format, va_list v) {
    va_list v1;
    va_copy(v1, v);
    int n = vsnprintf(NULL, 0, format, v1);
    va_end(v1);

    char* result = lca_allocate(temp_allocator, (size_t)n + 1);
    vsnprintf(result, n + 1, format, v);

    return result;
}

lca_arena_block lca_arena_block_create(lca_arena* arena) {
    return (lca_arena_block){
        .memory = lca_allocate(arena->allocator, arena->block_size),
        .capacity = arena->block_size,
    };
}

lca_arena* lca_arena_create(lca_allocator allocator, size_t block_size) {
    lca_arena* arena = lca_allocate(allocator, block_size);
    *arena = (lca_arena){
        .allocator = allocator,
        .block_size = block_size
    };

    lca_arena_block first_block = lca_arena_block_create(arena);
    lca_da_push(arena->blocks, first_block);

    return arena;
}

void lca_arena_destroy(lca_arena* arena) {
    if (arena == NULL) return;

    lca_allocator allocator = arena->allocator;

    for (int64_t i = 0, count = lca_da_count(arena->blocks); i < count; i++) {
        lca_arena_block* block = &arena->blocks[i];
        lca_deallocate(allocator, block->memory);
        *block = (lca_arena_block){0};
    }

    lca_da_free(arena->blocks);

    *arena = (lca_arena){0};
    lca_deallocate(allocator, arena);
}

void* lca_arena_push(lca_arena* arena, size_t count) {
    assert(count <= (size_t)arena->block_size && "Requested more memory than a temp arena block can hold :(");

    lca_arena_block* block = &arena->blocks[lca_da_count(arena->blocks) - 1];
    if (block->capacity - block->allocated < (int64_t)count) {
        lca_arena_block new_block = lca_arena_block_create(arena);
        lca_da_push(arena->blocks, new_block);
        block = &arena->blocks[lca_da_count(arena->blocks) - 1];
    }

    void* result = (char*)block->memory + block->allocated;
    block->allocated += count;
    memset(result, 0, count);

    return result;
}

void lca_arena_clear(lca_arena* arena) {
    for (int64_t i = 0, count = lca_da_count(arena->blocks); i < count; i++) {
        lca_arena_block* block = &arena->blocks[i];
        memset(block->memory, 0, block->capacity);
        lca_deallocate(arena->allocator, block->memory);
    }

    lca_da_free(arena->blocks);
    arena->blocks = NULL;

    lca_arena_block first_block = lca_arena_block_create(arena);
    lca_da_push(arena->blocks, first_block);
}

void lca_arena_dump(lca_arena* arena) {
    fprintf(stderr, "<Memory Arena %p>\n", (void*)arena);
    fprintf(stderr, "  Block Count: %ld\n", lca_da_count(arena->blocks));
    fprintf(stderr, "  Block Size: %ld\n", arena->block_size);
    fprintf(stderr, "  Block Storage: %p\n", (void*)arena->blocks);
    fprintf(stderr, "  Allocator:\n");
    fprintf(stderr, "    User Data: %p\n", (void*)arena->allocator.user_data);
    // fprintf(stderr, "    Function: %p\n", arena->allocator.allocator_function);
    fprintf(stderr, "  Blocks:\n");

    for (int64_t i = 0, count = lca_da_count(arena->blocks); i < count; i++) {
        fprintf(stderr, "    %ld:\n", i);
        lca_arena_block block = arena->blocks[i];

        fprintf(stderr, "      Memory: %p\n", (void*)block.memory);
        fprintf(stderr, "      Allocated: %ld\n", block.allocated);
        fprintf(stderr, "      Capacity: %ld\n", block.capacity);
    }
}

char* lca_shift_args(int* argc, char*** argv) {
    assert(argc != NULL);
    assert(argv != NULL);
    assert(*argv != NULL);
    char* result = **argv;
    if (result != NULL) {
        assert(*argc > 0);
        (*argv) += 1;
        (*argc) -= 1;
    }
    return result;
}

bool lca_stdout_isatty(void) {
    return isatty(fileno(stdout));
}

bool lca_stderr_isatty(void) {
    return isatty(fileno(stderr));
}

bool lcat_file_exists(const char* file_path) {
#    if _WIN32
    // TODO: distinguish between "does not exists" and other errors
    DWORD dwAttrib = GetFileAttributesA(file_path);
    return dwAttrib != INVALID_FILE_ATTRIBUTES;
#    else
    struct stat statbuf;
    if (stat(file_path, &statbuf) < 0) {
        if (errno == ENOENT) return 0;
        return -1;
    }
    return 1;
#    endif
}

lca_string lca_file_read(lca_allocator allocator, const char* file_path) {
    assert(file_path != NULL);
    FILE* stream = fopen(file_path, "r");
    if (stream == NULL) {
        return lca_string_create(allocator);
    }
    fseek(stream, 0, SEEK_END);
    int64_t count = ftell(stream);
    fseek(stream, 0, SEEK_SET);
    char* data = lca_allocate(allocator, count + 1);
    fread(data, (size_t)count, 1, stream);
    data[count] = 0;
    fclose(stream);
    return lca_string_from_data(allocator, data, count, count + 1);
}

char* lca_plat_self_exe(void) {
#    if defined(__linux__)
    char* buffer = malloc(1024);
    memset(buffer, 0, 1024);
    ssize_t n = readlink("/proc/self/exe", buffer, 1024);
    if (n < 0) {
        free(buffer);
        return NULL;
    }
    return buffer;
#    elif define(_WIN32)
    assert(false && "lca_plat_self_exe is not implemented on this platform");
    return NULL;
#    else
    assert(false && "lca_plat_self_exe is not implemented on this platform");
    return NULL;
#    endif
}

#endif

#endif // !LCA_H
