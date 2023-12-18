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

lca_string_view lca_string_as_view(lca_string s);
bool lca_string_view_equals(lca_string_view a, lca_string_view b);
lca_string lca_string_view_to_string(lca_allocator allocator, lca_string_view s);

#define LCA_SV(S) lca_string_as_view(S)

#ifndef LCA_STR_NO_SHORT_NAMES
#    define SV_EMPTY       LCA_SV_EMPTY
#    define SV_CONSTANT(C) LCA_SV_CONSTANT(C)
#    define STR_EXPAND(S)  LCA_STR_EXPAND(S)
typedef struct lca_string string;
typedef struct lca_string_view string_view;
#    define string_create(A)             lca_string_create(A)
#    define string_from_data(A, D, L, C) lca_string_from_data(A, D, L, C)
#    define string_destroy(S)            lca_string_destroy(S)
#    define string_as_cstring(S)         lca_string_as_cstring(S)
#    define string_equals(A, B)          lca_string_equals(A, B)
#    define string_slice(S, O, L)        lca_string_slice(S, O, L)
#    define string_format(F, ...)        lca_string_format(F, __VA_ARGS__)
#    define string_vformat(F, V)         lca_string_vformat(F, V)
#    define string_as_view(S)            lca_string_as_view(S)
#    define string_view_equals(A, B)     lca_string_view_equals(A, B)
#    define string_view_to_string(A, S)  lca_string_view_to_string(A, S)
#    define SV(S)                        LCA_SV(S)
#endif // !LCA_STR_NO_SHORT_NAMES

#ifdef LCA_STR_IMPLEMENTATION

string lca_string_create(lca_allocator allocator) {
    int64_t capacity = 32;
    char* data = lca_allocate(allocator, (size_t)capacity * sizeof *data);
    assert(data);
    return (string){
        .allocator = allocator,
        .data = data,
        .capacity = capacity,
        .count = 0,
    };
}

string lca_string_from_data(lca_allocator allocator, char* data, int64_t count, int64_t capacity) {
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
    if (s == NULL) return;
    lca_deallocate(s->allocator, s->data);
    *s = (string){};
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

lca_string_view lca_string_as_view(lca_string s) {
    return (lca_string_view){
        .data = s.data,
        .count = s.count,
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

lca_string lca_string_view_to_string(lca_allocator allocator, lca_string_view s) {
    char* data = lca_allocate(allocator, s.count + 1);
    assert(data);
    memcpy(data, s.data, (size_t)s.count);
    return lca_string_from_data(allocator, data, s.count, s.count + 1);
}

#endif // LCA_STR_IMPLEMENTATION

#endif // !LCASTR_H
