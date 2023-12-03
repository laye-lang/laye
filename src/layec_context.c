#include <assert.h>
#include <stdio.h>

#include "layec.h"

layec_context* layec_context_create(lca_allocator allocator) {
    layec_context* context = lca_allocate(allocator, sizeof *context);
    context->allocator = allocator;
    return context;
}

void layec_context_destroy(layec_context* context) {
    lca_allocator allocator = context->allocator;

    *context = (layec_context){};
    lca_deallocate(allocator, context);
}

static string read_file_to_string(lca_allocator allocator, string file_path) {
    const char* file_path_cstr = string_as_cstring(file_path);
    FILE* stream = fopen(file_path_cstr, "r");
    fseek(stream, 0, SEEK_END);
    int64_t count = ftell(stream);
    fseek(stream, 0, SEEK_SET);
    char* data = lca_allocate(allocator, count + 1);
    fread(data, (size_t)count, 1, stream);
    data[count] = 0;
    fclose(stream);
    return string_from_data(allocator, data, count, count + 1);
}

sourceid layec_context_get_or_add_source_from_file(layec_context* context, string_view file_path) {
    assert(context);

    sourceid sourceid = 0;
    for (; sourceid < arr_count(context->sources); sourceid++) {
        if (string_view_equals(SV(context->sources[sourceid].name), file_path))
            return sourceid;
    }

    string file_path_owned = string_view_to_string(context->allocator, file_path);
    string text = read_file_to_string(context->allocator, file_path_owned);

    layec_source source = {
        .name = file_path_owned,
        .text = text,
    };

    sourceid = arr_count(context->sources);
    arr_push(context->sources, source);

    return sourceid;
}

layec_source layec_context_get_source(layec_context* context, sourceid sourceid) {
    assert(context);
    assert(sourceid >= 0 && sourceid < arr_count(context->sources));
    return context->sources[sourceid];
}
