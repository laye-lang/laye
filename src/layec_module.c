#include <assert.h>

#include "layec.h"

layec_module* layec_module_create(layec_context* context, sourceid sourceid) {
    assert(context);
    assert(sourceid >= 0);

    layec_module* module = lca_allocate(context->allocator, sizeof *module);
    assert(module);
    module->context = context;
    module->sourceid = sourceid;
    module->arena = lca_arena_create(context->allocator, 1024 * 1024);
    assert(module->arena);
    return module;
}

void layec_module_destroy(layec_module* module) {
    assert(module);
    assert(module->context);

    lca_allocator allocator = module->context->allocator;

    *module = (layec_module){};
    lca_deallocate(allocator, module);
}

layec_source layec_module_get_source(layec_module* module) {
    assert(module);
    assert(module->context);
    assert(module->sourceid >= 0);
    return layec_context_get_source(module->context, module->sourceid);
}
