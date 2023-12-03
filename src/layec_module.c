#include "layec.h"

layec_module* layec_module_create(layec_context* context) {
    layec_module* module = lca_allocate(context->allocator, sizeof *module);
    module->context = context;
    module->arena = lca_arena_create(context->allocator, 1024 * 1024);
    return module;
}

void layec_module_destroy(layec_module* module) {
    lca_allocator allocator = module->context->allocator;

    *module = (layec_module){};
    lca_deallocate(allocator, module);
}
