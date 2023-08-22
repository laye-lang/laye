#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "layec/context.h"

layec_context* layec_context_create(void)
{
    layec_context* context = calloc(1, sizeof *context);
    return context;
}

void layec_context_destroy(layec_context* context)
{
    free(context);
}

static const char* layec_read_file(const char* file_path)
{
    assert(file_path);

    /// TODO(local): file reading error handling
    FILE* stream = fopen(file_path, "r");
    if (!stream) return NULL;
    fseek(stream, 0, SEEK_END);
    long file_length = ftell(stream);
    assert(file_length >= 0);
    fseek(stream, 0, SEEK_SET);
    char* data = malloc((unsigned long long)file_length + 1);
    fread(data, (size_t)file_length, 1, stream);
    data[file_length] = 0;
    fclose(stream);

    return data;
}

int layec_context_get_or_add_source_buffer_from_file(layec_context* context, const char* file_path)
{
    assert(context);
    if (!file_path)
    {
        printf("No input file path given\n");
        return 0;
    }

    for (long long i = 0; i < (long long)vector_count(context->sources); i++)
    {
        if (0 == strcmp(file_path, context->sources[i].name))
            return (int)i + 1;
    }
    
    const char* file_source_text = layec_read_file(file_path);
    if (!file_source_text)
    {
        printf("Could not read source file '%s'\n", file_path);
        return 0;
    }

    assert(file_source_text);
    layec_source_buffer source_buffer =
    {
        .name = file_path,
        .text = file_source_text,
    };

    int source_id = (int)vector_count(context->sources) + 1;
    vector_push(context->sources, source_buffer);

    return source_id;
}

layec_source_buffer layec_context_get_source_buffer(layec_context* context, int source_id)
{
    assert(context);

    if (!context->sources)
        return (layec_source_buffer){ .name = "<unknown>", .text = "" };

    int source_index = source_id - 1;
    if (source_index >= vector_count(context->sources))
        return (layec_source_buffer){ .name = "<unknown>", .text = "" };

    return context->sources[source_index];
}

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

static const char *severity_names[LAYEC_SEV_COUNT] = {
    "Info",
    "Warning",
    "Error",
    "Internal Compiler Error",
    "Sorry, unimplemented",
};

static const char *severity_colors[LAYEC_SEV_COUNT] = {
    ANSI_COLOR_BRIGHT_BLACK,
    ANSI_COLOR_YELLOW,
    ANSI_COLOR_RED,
    ANSI_COLOR_BRIGHT_RED,
    ANSI_COLOR_MAGENTA,
};

void layec_context_issue_diagnostic_prolog(layec_context* context, layec_diagnostic_severity severity, layec_location location)
{
    assert(context);
    if (location.source_id <= 0) return;

    layec_source_buffer source_buffer = layec_context_get_source_buffer(context, location.source_id);
    assert(source_buffer.name);
    assert(source_buffer.text);

    long long line_number;
    long long line_start_offset;
    long long line_end_offset;
    get_line_from_source(source_buffer.text, location, &line_number, &line_start_offset, &line_end_offset);

    const char* file_name = source_buffer.name;
    printf("%s:%lld:%lld: %s%s" ANSI_COLOR_RESET ": ", file_name, line_number, 1 + location.offset - line_start_offset,
        severity_colors[severity], severity_names[severity]);
}

void layec_context_issue_diagnostic_epilog(layec_context* context, layec_diagnostic_severity severity, layec_location location)
{
    printf("\n");
}
