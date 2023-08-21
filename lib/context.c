#include <stdlib.h>

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

int layec_context_get_or_add_source_buffer_from_file(layec_context* context, const char* file_path)
{
    //assert(context);
    if (!file_path) return 0;

    for (long long i = 0; i < context->sources_count; i++)
    {
        if (0 == strcmp(file_path, context->sources[i].name))
            return (int)i;
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

    int source_id = context->sources_count + 1;
    context->sources[context->sources_count].name = file_path;
    context->sources[context->sources_count].text = "<source_text>";
    context->sources_count++;

    return source_id;
}

layec_source_buffer layec_context_get_source_buffer(layec_context* context, int source_id)
{
    //assert(context);

    if (!context->sources)
        return (layec_source_buffer){ .name = "<unknown>", .text = "" };

    int source_index = source_id - 1;
    if (source_index >= context->sources_count)
        return (layec_source_buffer){ .name = "<unknown>", .text = "" };

    return context->sources[source_index];
}
