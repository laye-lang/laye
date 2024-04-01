#include "ccly.h"

c_context* c_context_create(lyir_context* lyir_context) {
    c_context* result = lca_allocate(lyir_context->allocator, sizeof *result);
    assert(result != NULL);

    result->lyir_context = lyir_context;
    result->allocator = lyir_context->allocator;
    result->use_color = lyir_context->use_color;

    return result;
}

void c_context_destroy(c_context* c_context) {
    if (c_context == NULL) return;
    lca_allocator allocator = c_context->allocator;

    lca_da_free(c_context->c_translation_units);
    lca_da_free(c_context->include_directories);
    lca_da_free(c_context->library_directories);
    lca_da_free(c_context->link_libraries);

    lca_deallocate(allocator, c_context);
}
