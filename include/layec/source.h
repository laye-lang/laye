#ifndef LAYEC_SOURCE_H
#define LAYEC_SOURCE_H

#include <stdbool.h>

#include "layec/string.h"

typedef struct layec_context layec_context;

typedef struct layec_location layec_location;
typedef struct layec_source_buffer layec_source_buffer;

/// A reference to a specific span of text in a layec_source_buffer.
struct layec_location
{
    /// The ID of the source buffer that this location references. And ID <= 0 is not valid.
    /// Call layec_context_get_source_buffer(layec_context* context, int source_id) to get the layec_source_buffer itself.
    int source_id;
    /// Offset index, in bytes, into the layec_source_buffer's text where this location starts.
    long long offset;
    /// The length of this location, in bytes.
    long long length;
};

/// High-level construct for storing source text information.
struct layec_source_buffer
{
    /// The name of this buffer. This could come from the file path used to create it or be arbitrary for compiler-created buffers.
    layec_string_view name;
    /// Nul-terminated string containing the source text for this buffer.
    layec_string_view text;
};

bool is_space(int c);
bool is_digit(int c);
bool is_alpha(int c);
bool is_alpha_numeric(int c);
bool is_hex_digit(int c);
int get_digit_value(int c);

/// Return a view into the original source text at this location.
layec_string_view layec_location_get_source_image(layec_context* context, layec_location location);
void layec_location_print(layec_context* context, layec_location location);

#endif // LAYEC_SOURCE_H
