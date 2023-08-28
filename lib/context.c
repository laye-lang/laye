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
    context->string_arena = layec_arena_create(1024 * 32);
    return context;
}

void layec_context_destroy(layec_context* context)
{
    for (long long i = 0; i < vector_count(context->sources); i++)
    {
        free((void*)context->sources[i].text.data);
        context->sources[i].text = LAYEC_STRING_VIEW_EMPTY;
    }

    layec_arena_destroy(context->string_arena);

    vector_free(context->include_dirs);
    vector_free(context->input_file_names);
    vector_free(context->sources);
    vector_free(context->interned_strings);

    *context = (layec_context){0};
    free(context);
}

static const char* layec_read_file(layec_string_view file_path, size_t* size)
{
    FILE* stream = NULL;
    char* data = NULL;
    const char* fp = layec_string_view_to_cstring(file_path);
    assert(fp);
    stream = fopen(fp, "r");
    free((void*)fp);
    fp = NULL;
    if (!stream) goto error;
    if (fseek(stream, 0, SEEK_END) < 0) goto error;
    long file_length = ftell(stream);
    if (file_length < 0) goto error;
    assert(file_length >= 0);
    if (fseek(stream, 0, SEEK_SET) < 0) goto error;
    data = malloc((unsigned long long)file_length + 1);
    if (!data) goto error;
    size_t n_read = fread(data, 1, (size_t)file_length, stream);
    assert(n_read == (size_t)file_length);
    if (ferror(stream)) goto error;
    data[file_length] = 0;
    fclose(stream);
    if (size) *size = (size_t)file_length;
    return data;
error:
    if (stream) fclose(stream);
    if (data) fclose(data);
    if (size) *size = 0;
    return NULL;
}

int layec_context_get_or_add_source_buffer_from_file(layec_context* context, layec_string_view file_path)
{
    assert(context);
    for (long long i = 0; i < (long long)vector_count(context->sources); i++)
    {
        if (layec_string_view_equals(file_path, context->sources[i].name))
            return (int)i + 1;
    }
    
    size_t file_source_length = 0;
    const char* file_source_text = layec_read_file(file_path, &file_source_length);
    if (!file_source_text) return 0;

    assert(file_source_text);
    layec_source_buffer source_buffer =
    {
        .name = file_path,
        .text = layec_string_view_create(file_source_text, (long long)file_source_length),
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

static layec_string_view layec_context_intern_string(layec_context* context, const char* s, long long string_len)
{
    if (string_len == 0) return LAYEC_STRING_VIEW_EMPTY;
    assert(context);
    assert(context->string_arena);
    assert(s);
    assert(string_len > 0);
    char* string_data = layec_arena_push(context->string_arena, string_len + 1);
    assert(string_data);
    memcpy(string_data, s, (unsigned long long)string_len);
    return layec_string_view_create(string_data, string_len);
}

layec_string_view layec_context_intern_cstring(layec_context* context, const char* s)
{
    assert(context);
    assert(context->string_arena);
    assert(s);
    long long string_len = (long long)strlen(s);
    return layec_context_intern_string(context, s, string_len);
}

layec_string_view layec_context_intern_string_view(layec_context* context, layec_string_view s)
{
    assert(context);
    assert(context->string_arena);
    return layec_context_intern_string(context, s.data, s.length);
}

layec_string_view layec_context_intern_string_builder(layec_context* context, layec_string_builder sb)
{
    assert(context);
    assert(context->string_arena);
    return layec_context_intern_string(context, sb.data, vector_count(sb.data));
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
