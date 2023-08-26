#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "layec/ansi.h"
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

static const char* layec_read_file(layec_string_view file_path)
{
    const char* fp = layec_string_view_to_cstring(file_path);
    assert(fp);

    /// TODO(local): file reading error handling
    FILE* stream = fopen(fp, "r");
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

int layec_context_get_or_add_source_buffer_from_file(layec_context* context, layec_string_view file_path)
{
    assert(context);
    for (long long i = 0; i < (long long)vector_count(context->sources); i++)
    {
        if (layec_string_view_equals(file_path, context->sources[i].name))
            return (int)i + 1;
    }
    
    const char* file_source_text = layec_read_file(file_path);
    if (!file_source_text) return 0;

    assert(file_source_text);
    layec_source_buffer source_buffer =
    {
        .name = file_path,
        .text = layec_string_view_create(file_source_text, (long long)strlen(file_source_text)),
    };

    int source_id = (int)vector_count(context->sources) + 1;
    vector_push(context->sources, source_buffer);

    return source_id;
}

layec_source_buffer layec_context_get_source_buffer(layec_context* context, int source_id)
{
    assert(context);

    if (!context->sources)
        return (layec_source_buffer){ .name = LAYEC_STRING_VIEW_CONSTANT("<unknown>"), .text = LAYEC_STRING_VIEW_EMPTY };

    int source_index = source_id - 1;
    if (source_index >= vector_count(context->sources))
        return (layec_source_buffer){ .name = LAYEC_STRING_VIEW_CONSTANT("<unknown>"), .text = LAYEC_STRING_VIEW_EMPTY };

    return context->sources[source_index];
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

    layec_location_print(context, location);
    printf(": %s%s" ANSI_COLOR_RESET ": ", severity_colors[severity], severity_names[severity]);

    //layec_source_buffer source_buffer = layec_context_get_source_buffer(context, location.source_id);
}

void layec_context_issue_diagnostic_epilog(layec_context* context, layec_diagnostic_severity severity, layec_location location)
{
    printf("\n");
}
