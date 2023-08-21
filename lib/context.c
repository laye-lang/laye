#include <assert.h>
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
    if (!file_path) return 0;

    for (long long i = 0; i < context->sources_count; i++)
    {
        if (0 == strcmp(file_path, context->sources[i].name))
            return (int)i;
    }
    
    const char* file_source_text = layec_read_file(file_path);
    if (!file_source_text)
    {
        printf("Could not read source file '%s'\n", file_path);
        return 0;
    }

    // If no source buffer exists with that name, allocate space for one and return it.
    if (!context->sources)
    {
        context->sources_count = 0;
        context->sources_capacity = 32;
        context->sources = calloc((unsigned long long)context->sources_capacity, sizeof *context->sources);
    }
    else if (context->sources_count >= context->sources_capacity)
    {
        context->sources_capacity *= 2;
        context->sources = realloc(context->sources, (unsigned long long)context->sources_capacity);
    }

    int source_id = (int)(context->sources_count + 1);
    context->sources[context->sources_count].name = file_path;
    assert(file_source_text);
    context->sources[context->sources_count].text = file_source_text;
    context->sources_count++;

    return source_id;
}

layec_source_buffer layec_context_get_source_buffer(layec_context* context, int source_id)
{
    assert(context);

    if (!context->sources)
        return (layec_source_buffer){ .name = "<unknown>", .text = "" };

    int source_index = source_id - 1;
    if (source_index >= context->sources_count)
        return (layec_source_buffer){ .name = "<unknown>", .text = "" };

    return context->sources[source_index];
}
