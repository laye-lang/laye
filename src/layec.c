#include <assert.h>
#include <stdio.h>

#define LCA_DA_IMPLEMENTATION
#define LCA_MEM_IMPLEMENTATION
#define LCA_STR_IMPLEMENTATION
#include "layec.h"

static char* shift(int* argc, char*** argv) {
    assert(*argc > 0);
    char *result = **argv;
    (*argv)++;
    (*argc)--;
    return result;
}

int main(int argc, char** argv) {
    lca_temp_allocator_init(default_allocator, 1024 * 1024);

    //fprintf(stderr, "layec " LAYEC_VERSION "\n");

    layec_init_targets(default_allocator);

    layec_context* context = layec_context_create(default_allocator);
    assert(context != NULL);
    context->use_color = true;

    layec_sourceid sourceid = layec_context_get_or_add_source_from_file(context, SV_CONSTANT("./test/exit_code.laye"));
    laye_module* module = laye_parse(context, sourceid);
    assert(module != NULL);

    laye_analyse(module);

    string module_string = laye_module_debug_print(module);
    fprintf(stderr, "%.*s", STR_EXPAND(module_string));
    string_destroy(&module_string);

    layec_module* ir_module = laye_irgen(module);
    assert(ir_module != NULL);

    string ir_module_string = layec_module_print(ir_module);
    fprintf(stderr, "%.*s", STR_EXPAND(ir_module_string));
    string_destroy(&ir_module_string);

    // in release/unsafe builds, we don't need to worry about manually tearing
    // down all of our allocations. these should always be run in debug/safe
    // builds so the static analysers (like address sanitizer) can do their magic.
#ifndef NDEBUG
    layec_module_destroy(ir_module);
    laye_module_destroy(module);
    layec_context_destroy(context);
    lca_temp_allocator_clear();
#endif // !NDEBUG

    return 0;
}
