#ifndef LCASTR_H
#define LCASTR_H

#include <stdint.h>

#define LAYEC_SV_EMPTY       ((layec_string_view){0})
#define LAYEC_SV_CONSTANT(C) ((layec_string_view){.data = (C), .count = (sizeof C) - 1})
#define LAYEC_STR_EXPAND(s)  ((int)s.count), (s.data)

typedef struct lca_string {
    char* data;
    int64_t count;
    int64_t capacity;
} lca_string;

typedef struct lca_string_view {
    const char* data;
    int64_t count;
} lca_string_view;

#ifndef LCA_STR_NO_SHORT_NAMES
typedef struct lca_string string;
typedef struct lca_string_view string_view;
#endif // !LCA_STR_NO_SHORT_NAMES

#ifdef LCA_STR_IMPLEMENTATION
#endif // LCA_STR_IMPLEMENTATION

#endif // !LCASTR_H
