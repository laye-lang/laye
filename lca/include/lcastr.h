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

#ifndef LCASTR_H
#define LCASTR_H

#include "lcamem.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#define LCA_SV_EMPTY       ((lca_string_view){0})
#define LCA_SV_CONSTANT(C) ((lca_string_view){.data = (C), .count = (sizeof C) - 1})
#define LCA_STR_EXPAND(s)  ((int)s.count), (s.data)

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

#ifdef LCA_STR_IMPLEMENTATION

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
        .capacity = count + 1,
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

#endif // LCA_STR_IMPLEMENTATION

#endif // !LCASTR_H
