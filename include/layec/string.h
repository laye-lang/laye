#ifndef LAYEC_STRING_H
#define LAYEC_STRING_H

#include <stdbool.h>

typedef struct layec_string_view layec_string_view;

/// Simple string wrapper which does not own its data and does not need to be nul terminated.
/// A view does not own its data. It is simply a window into existing string data.
struct layec_string_view
{
    const char* data;
    long long length;
};

/// Create a string view from a pointer and a length.
/// String views do not own their data, so we do not like them modifying it.
layec_string_view layec_string_view_create(const char* data, long long length);
bool layec_string_view_ends_with_cstring(layec_string_view s, const char* end);

#endif // LAYEC_STRING_H
