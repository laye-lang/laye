#include <stdio.h>

#define LCA_DA_IMPLEMENTATION
#define LCA_MEM_IMPLEMENTATION
#define LCA_STR_IMPLEMENTATION
#include "layec.h"

void test_mem(void) {
    layec_context* context = layec_context_create(default_allocator);
    arr_push(context->sources, (layec_source){});

    for (int i = 0; i < 100000; i++) {
        void* x = lca_allocate(temp_allocator, 256);
        lca_temp_allocator_dump();
        assert(context->sources != NULL);
        assert(arr_count(context->sources) != 0);
    }

    lca_temp_allocator_clear();
}

int main(int argc, char** argv) {
    lca_temp_allocator_init(default_allocator, 1024 * 1024);

    //test_mem();
    //return 0;

    fprintf(stderr, "layec " LAYEC_VERSION "\n");

    layec_context* context = layec_context_create(default_allocator);
    assert(context != NULL);
    context->use_color = true;

    layec_sourceid sourceid = layec_context_get_or_add_source_from_file(context, SV_CONSTANT("./test/tokens.laye"));
    laye_module* module = laye_parse(context, sourceid);
    assert(module != NULL);

    // in release/unsafe builds, we don't need to worry about manually tearing
    // down all of our allocations. these should always be run in debug/safe
    // builds so the static analysers (like address sanitizer) can do their magic.
#ifndef NDEBUG
    laye_module_destroy(module);
    layec_context_destroy(context);
    lca_temp_allocator_clear();
#endif // !NDEBUG

    return 0;
}
