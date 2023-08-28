#ifndef LAYEC_STRING_H
#define LAYEC_STRING_H

#include <stdbool.h>

#include "layec/vector.h"

#define LAYEC_STRING_VIEW_EMPTY ((layec_string_view){0})
#define LAYEC_STRING_VIEW_CONSTANT(C) ((layec_string_view){.data = (C), .length = (sizeof C) - 1})
#define LAYEC_STRING_VIEW_EXPAND(s) ((int)s.length), (s.data)

typedef struct layec_context layec_context;

typedef struct layec_string_view layec_string_view;
typedef struct layec_string_builder layec_string_builder;

/// Simple string wrapper which does not own its data and does not need to be nul terminated.
/// A view does not own its data. It is simply a window into existing string data.
struct layec_string_view
{
    const char* data;
    long long length;
};

struct layec_string_builder
{
    vector(char) data;
};

/// Create a string view from a pointer and a length.
/// String views do not own their data, so we do not like them modifying it.
layec_string_view layec_string_view_create(const char* data, long long length);
layec_string_view layec_string_view_slice(layec_string_view s, long long offset, long long length);
bool layec_string_view_equals(layec_string_view s0, layec_string_view s1);
char* layec_string_view_to_cstring(layec_string_view s);
bool layec_string_view_starts_with_cstring(layec_string_view s, const char* start);
bool layec_string_view_ends_with_cstring(layec_string_view s, const char* end);
layec_string_view layec_string_view_concat(layec_context* context, layec_string_view s0, layec_string_view s1);
layec_string_view layec_string_view_path_concat(layec_context* context, layec_string_view s0, layec_string_view s1);
layec_string_view layec_string_view_get_parent_path(layec_string_view s);

void layec_string_builder_destroy(layec_string_builder* sb);
void layec_string_builder_append_rune(layec_string_builder* sb, int rune);
void layec_string_builder_append_string_view(layec_string_builder* sb, layec_string_view s);
char* layec_string_builder_to_cstring(layec_string_builder* sb);

#endif // LAYEC_STRING_H
