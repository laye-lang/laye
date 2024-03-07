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

#ifndef LCA_DA_H
#define LCA_DA_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// Header data for a light-weight implelentation of typed dynamic arrays.
typedef struct lca_da_header {
    int64_t capacity;
    int64_t count;
} lca_da_header;

void lca_da_maybe_expand(void** da_ref, int64_t element_size, int64_t required_count);

#ifndef LCA_DA_MALLOC
#    define LCA_DA_MALLOC(N) (malloc(N))
#endif

#ifndef LCA_DA_REALLOC
#    define LCA_DA_REALLOC(P, N) (realloc(P, N))
#endif

#ifndef LCA_DA_FREE
#    define LCA_DA_FREE(N) (free(N))
#endif

#define lca_da(T)            T*
#define lca_da_get_header(V) (((struct lca_da_header*)(V)) - 1)
#define lca_da_count(V)      ((V) ? lca_da_get_header(V)->count : 0)
#define lca_da_capacity(V)   ((V) ? lca_da_get_header(V)->capacity : 0)
#define lca_da_reserve(V, N) \
    do { lca_da_maybe_expand((void**)&(V), (int64_t)sizeof *(V), N); } while (0)
#define lca_da_set_count(V, N)                    \
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
        lca_da_set_count((V), lca_da_count((V)) + 1);                                          \
        memmove(&(V)[(I) + 1], &(V)[(I)], sizeof(*(V)) * (size_t)(lca_da_count((V)) - (I)-1)); \
        (V)[(I)] = (E);                                                                        \
    } while (0)
#define lca_da_back(V) (&(V)[lca_da_count(V) - 1])
#define lca_da_free(V)                                             \
    do {                                                           \
        if (V) {                                                   \
            memset(V, 0, (size_t)lca_da_count(V) * (sizeof *(V))); \
            LCA_DA_FREE(lca_da_get_header(V));                     \
            (V) = NULL;                                            \
        }                                                          \
    } while (0)
#define lca_da_free_all(V, F)                                                                                    \
    do {                                                                                                         \
        if (V) {                                                                                                 \
            for (int64_t lca_da_index = 0; lca_da_index < lca_da_count(V); lca_da_index++) F((V)[lca_da_index]); \
            memset(V, 0, (size_t)lca_da_count(V) * (sizeof *(V)));                                               \
            LCA_DA_FREE(lca_da_get_header(V));                                                                   \
            (V) = NULL;                                                                                          \
        }                                                                                                        \
    } while (0)
#define lca_da_foreach(T, N, V)     for (T N, *N##_ptr = V, *N##_endptr = V + lca_da_count(V); (N##_ptr < N##_endptr) && (N = *N##_ptr, true); N##_ptr += 1)
#define lca_da_foreach_ptr(T, N, V) for (T N, **N##_ptr = (T*)V, **N##_endptr = (T*)V + lca_da_count(V); (N##_ptr < N##_endptr) && (N = *N##_ptr, true); N##_ptr += 1)

#ifndef LCA_DA_NO_SHORT_NAMES
#    define dynarr(T)                T*
#    define arr_count(V)             lca_da_count(V)
#    define arr_capacity(V)          lca_da_capacity(V)
#    define arr_reserve(V, N)        lca_da_reserve(V, N)
#    define arr_set_count(V, N)      lca_da_set_count(V, N)
#    define arr_push(V, E)           lca_da_push(V, E)
#    define arr_pop(V)               lca_da_pop(V)
#    define arr_insert(V, I, E)      lca_da_insert(V, I, E)
#    define arr_back(V)              lca_da_back(V)
#    define arr_free(V)              lca_da_free(V)
#    define arr_free_all(V, F)       lca_da_free_all(V, F)
#    define arr_foreach(T, N, V)     lca_da_foreach (T, N, V)
#    define arr_foreach_ptr(T, N, V) lca_da_foreach_ptr (T, N, V)
#endif // !LCA_DA_NO_SHORT_NAMES

#ifdef LCA_DA_IMPLEMENTATION

#    include <stdlib.h>

void lca_da_maybe_expand(void** da_ref, int64_t element_size, int64_t required_count) {
    if (required_count <= 0) return;

    struct lca_da_header* header = lca_da_get_header(*da_ref);
    if (!*da_ref) {
        int64_t initial_capacity = 32;
        void* new_data = LCA_DA_MALLOC((sizeof *header) + (size_t)(initial_capacity * element_size));
        header = new_data;

        header->capacity = initial_capacity;
        header->count = 0;
    } else if (required_count > header->capacity) {
        while (required_count > header->capacity)
            header->capacity *= 2;
        header = LCA_DA_REALLOC(header, (sizeof *header) + (size_t)(header->capacity * element_size));
    }

    *da_ref = (void*)(header + 1);
}

#endif // LCA_DA_IMPLEMENTATION

#endif // !LCA_DA_H
