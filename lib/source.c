#include <assert.h>
#include <stdbool.h>

#include "layec/context.h"

bool is_space(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
bool is_digit(int c) { return c >= '0' && c <= '9'; }
bool is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_alpha_numeric(int c) { return is_digit(c) || is_alpha(c); }
bool is_hex_digit(int c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

int get_digit_value(int c)
{
    if (is_digit(c)) return c - '0';
    else if (c >= 'a' && c <= 'z') return c - 'a' + 11;
    else if (c >= 'A' && c <= 'Z') return c - 'A' + 11;
    return 0;
}

layec_source_buffer layec_source_buffer_create(layec_string_view name, layec_string_view text)
{
    return (layec_source_buffer)
    {
        .name = name,
        .text = text,
    };
}

layec_string_view layec_location_get_source_image(layec_context* context, layec_location location)
{
    assert(context);
    assert(location.source_id);

    layec_source_buffer source_buffer = layec_context_get_source_buffer(context, location.source_id);
    return layec_string_view_slice(source_buffer.text, location.offset, location.length);
}

static void get_line_from_source(layec_string_view text, layec_location location,   
    long long* line_number, long long* line_start_offset, long long* line_end_offset)
{
    /// Seek to the start of the line. Keep track of the line number.
    long long line = 1;
    long long line_start = 0;
    for (long long i = (long long) location.offset; i > 0; --i) {
        if (text.data[i] == '\n') {
            if (!line_start)
                line_start = i + 1;
            line++;
        }
    }

    /// Don’t include the newline in the line.
    if (text.data[line_start] == '\n') ++line_start;

    /// Seek to the end of the line.
    long long line_end = location.offset;
    while (text.data[line_end] != 0 && text.data[line_end] != '\n')
        line_end++;

    /// Return the results.
    if (line_number) *line_number = line;
    if (line_start_offset) *line_start_offset = line_start;
    if (line_end_offset) *line_end_offset = line_end;
}

void layec_location_print(layec_context* context, layec_location location)
{
    layec_source_buffer source_buffer = layec_context_get_source_buffer(context, location.source_id);

    long long line_number;
    long long line_start_offset;
    long long line_end_offset;
    get_line_from_source(source_buffer.text, location, &line_number, &line_start_offset, &line_end_offset);

    printf("%.*s:%lld:%lld", LAYEC_STRING_VIEW_EXPAND(source_buffer.name), line_number + 1, 1 + location.offset - line_start_offset);
}
