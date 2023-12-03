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
