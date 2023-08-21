#ifndef LAYEC_SOURCE_H
#define LAYEC_SOURCE_H

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
    const char* name;
    /// Nul-terminated string containing the source text for this buffer.
    const char* text;
};

#endif // LAYEC_SOURCE_H
