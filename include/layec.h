#ifndef LAYEC_H
#define LAYEC_H

#include "lcads.h"
#include "lcamem.h"
#include "lcastr.h"

#define LAYEC_VERSION "0.1.0"

typedef struct layec_context {
    lca_allocator allocator;
    dynarr(string) include_directories;
} layec_context;

typedef struct layec_module {
    layec_context* context;
    lca_arena* arena;
} layec_module;

// ========== Context ==========

layec_context* layec_context_create(lca_allocator allocator);
void layec_context_destroy(layec_context* context);

// ========== Module ==========

layec_module* layec_module_create(layec_context* context);
void layec_module_destroy(layec_module* module);

#endif // LAYEC_H
