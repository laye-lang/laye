/*
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2023 Local Atticus
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <assert.h>

#define LCA_STR_NO_SHORT_NAMES
#include "lyir.h"

lyir_dependency_graph* lyir_dependency_graph_create_in_context(lyir_context* context) {
    assert(context != NULL);

    lyir_dependency_graph* graph = lca_allocate(context->allocator, sizeof *graph);
    assert(graph != NULL);
    graph->context = context;
    graph->arena = lca_arena_create(context->allocator, 1024 * sizeof(lyir_dependency_entry));
    assert(graph->arena != NULL);
    arr_push(context->_all_depgraphs, graph);

    return graph;
}

void lyir_dependency_graph_destroy(lyir_dependency_graph* graph) {
    if (graph == NULL) return;

    assert(graph->context != NULL);
    assert(graph->arena != NULL);

    lca_allocator allocator = graph->context->allocator;

    for (int64_t i = 0, count = arr_count(graph->entries); i < count; i++) {
        arr_free(graph->entries[i]->dependencies);
    }

    arr_free(graph->entries);
    lca_arena_destroy(graph->arena);

    *graph = (lyir_dependency_graph){0};
    lca_deallocate(allocator, graph);
}

void lyir_depgraph_add_dependency(lyir_dependency_graph* graph, lyir_dependency_entity* node, lyir_dependency_entity* dependency) {
    assert(graph != NULL);
    assert(graph->arena != NULL);
    assert(node != NULL);

    lyir_dependency_entry* entry = NULL;
    for (int64_t i = 0, count = arr_count(graph->entries); i < count; i++) {
        if (graph->entries[i]->node == node) {
            entry = graph->entries[i];
            break;
        }
    }

    if (entry == NULL) {
        entry = lca_arena_push(graph->arena, sizeof *entry);
        entry->node = node;
        arr_push(graph->entries, entry);
    }

    assert(entry != NULL);
    assert(entry->node == node);

    if (dependency != NULL) {
        for (int64_t i = 0, count = arr_count(entry->dependencies); i < count; i++) {
            if (entry->dependencies[i] == dependency) {
                return;
            }
        }

        arr_push(entry->dependencies, dependency);
    }
}

void lyir_depgraph_ensure_tracked(lyir_dependency_graph* graph, lyir_dependency_entity* node) {
    assert(graph != NULL);
    assert(node != NULL);

    lyir_depgraph_add_dependency(graph, node, NULL);
}

static int64_t dynarr_index_of(dynarr(void*) entities, void* entity) {
    for (int64_t i = 0, count = arr_count(entities); i < count; i++) {
        if (entity == entities[i])
            return i;
    }

    return -1;
}

static lyir_dependency_order_result resolve_dependencies(
    lyir_dependency_graph* graph,
    // clang-format off
    dynarr(lyir_dependency_entity*)* resolved,
    dynarr(lyir_dependency_entity*)* seen,
    // clang-format on
    lyir_dependency_entity* entity
) {
#define RESOLVED (*resolved)
#define SEEN     (*seen)

    assert(graph != NULL);
    assert(resolved != NULL);
    assert(seen != NULL);

    lyir_dependency_order_result result = {0};
    if (-1 != dynarr_index_of(RESOLVED, entity)) {
        return result;
    }

    arr_push(SEEN, entity);

    int64_t entry_index = -1; // dynarr_index_of((void**)graph->entries, entity);
    for (int64_t i = 0; i < arr_count(graph->entries) && entry_index == -1; i++) {
        if (graph->entries[i]->node == entity) {
            entry_index = i;
        }
    }

    bool requires_resolution = entry_index >= 0 && arr_count(graph->entries[entry_index]->dependencies) >= 0;

    if (requires_resolution) {
#define DEPS (*dependencies)
        dynarr(lyir_dependency_entity*)* dependencies = &graph->entries[entry_index]->dependencies;

        for (int64_t i = 0, count = arr_count(DEPS); i < count; i++) {
            lyir_dependency_entity* dep = DEPS[i];
            assert(dep != NULL);

            if (-1 != dynarr_index_of((void**)RESOLVED, dep)) {
                continue;
            }

            int64_t dep_seen_index = dynarr_index_of((void**)SEEN, dep);
            if (-1 != dep_seen_index) {
                result.status = LYIR_DEP_CYCLE;
                result.from = entity;
                result.to = dep;
                return result;
            }

            lyir_dependency_order_result dep_result = resolve_dependencies(
                graph,
                resolved,
                seen,
                dep
            );

            if (dep_result.status != LYIR_DEP_OK) {
                return dep_result;
            }
        }
#undef DEPS
    }

    arr_push(RESOLVED, entity);

    int64_t seen_index = dynarr_index_of((void**)SEEN, entity);
    assert(seen_index >= 0);
    assert(seen_index < arr_count(SEEN));
    if (arr_count(SEEN) == 1) {
        arr_pop(SEEN);
    } else {
        SEEN[seen_index] = SEEN[arr_count(SEEN) - 1];
        SEEN[arr_count(SEEN) - 1] = NULL;
        arr_set_count(SEEN, arr_count(SEEN) - 1);
    }

    return result;

#undef SEEN
#undef RESOLVED
}

lyir_dependency_order_result lyir_dependency_graph_get_ordered_entities(lyir_dependency_graph* graph) {
    assert(graph != NULL);

    lyir_dependency_order_result result = {0};
    dynarr(lyir_dependency_entity*) seen = NULL;

    for (int64_t i = 0, count = arr_count(graph->entries); i < count; i++) {
        lyir_dependency_order_result entry_result = resolve_dependencies(
            graph,
            &result.ordered_entities,
            &seen,
            graph->entries[i]->node
        );

        if (entry_result.status != LYIR_DEP_OK) {
            arr_free(seen);
            return entry_result;
        }
    }

    arr_free(seen);
    result.status = LYIR_DEP_OK;
    return result;
}
