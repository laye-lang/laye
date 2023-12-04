#include <assert.h>

#include "layec.h"

void laye_module_destroy(laye_module* module) {
    if (module == NULL) return;

    assert(module->context != NULL);
    lca_allocator allocator = module->context->allocator;

    *module = (laye_module){};
    lca_deallocate(allocator, module);
}

layec_source laye_module_get_source(laye_module* module) {
    assert(module != NULL);
    assert(module->context != NULL);
    assert(module->sourceid >= 0);
    return layec_context_get_source(module->context, module->sourceid);
}
