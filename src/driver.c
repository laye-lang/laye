#include <stdio.h>

#define LCA_DA_IMPLEMENTATION
#define LCA_MEM_IMPLEMENTATION
#define LCA_STR_IMPLEMENTATION
#include "layec.h"

int main(int argc, char** argv) {
    fprintf(stderr, "layec " LAYEC_VERSION "\n");

    layec_context* context = layec_context_create(default_allocator);

    sourceid sourceid = layec_context_get_or_add_source_from_file(context, SV_CONSTANT("./test/tokens.laye"));
    layec_source source = layec_context_get_source(context, sourceid);

    fprintf(stderr, "// %.*s\n", STR_EXPAND(source.name));
    fprintf(stderr, "%.*s\n", STR_EXPAND(source.text));

    layec_context_destroy(context);

    return 0;
}
