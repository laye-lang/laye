#include <assert.h>

#include "layec/context.h"
#include "layec/source.h"

static void get_line_from_source(const char* text, layec_location location,   
    long long* line_number, long long* line_start_offset, long long* line_end_offset)
{
    assert(text);

    /// Seek to the start of the line. Keep track of the line number.
    long long line = 1;
    long long line_start = 0;
    for (long long i = (long long) location.offset; i > 0; --i) {
        if (text[i] == '\n') {
            if (!line_start)
                line_start = i + 1;
            line++;
        }
    }

    /// Don’t include the newline in the line.
    if (text[line_start] == '\n') ++line_start;

    /// Seek to the end of the line.
    long long line_end = location.offset;
    while (text[line_end] != 0 && text[line_end] != '\n')
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

    printf("%s:%lld:%lld", source_buffer.name, line_number, 1 + location.offset - line_start_offset);
}
