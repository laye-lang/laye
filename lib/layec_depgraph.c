#include <assert.h>

#include "layec.h"

layec_dependency_graph* layec_dependency_graph_create_in_context(layec_context* context) {
    assert(context != NULL);

    layec_dependency_graph* graph = lca_allocate(context->allocator, sizeof *graph);
    assert(graph != NULL);
    graph->context = context;
    graph->arena = lca_arena_create(context->allocator, 1024 * sizeof(layec_dependency_entry));
    assert(graph->arena != NULL);
    arr_push(context->_all_depgraphs, graph);

    return graph;
}

void layec_dependency_graph_destroy(layec_dependency_graph* graph) {
    if (graph == NULL) return;

    assert(graph->context != NULL);
    assert(graph->arena != NULL);

    lca_allocator allocator = graph->context->allocator;

    for (int64_t i = 0, count = arr_count(graph->entries); i < count; i++) {
        arr_free(graph->entries[i]->dependencies);
    }

    arr_free(graph->entries);
    lca_arena_destroy(graph->arena);

    *graph = (layec_dependency_graph){};
    lca_deallocate(allocator, graph);
}

void layec_depgraph_add_dependency(layec_dependency_graph* graph, layec_dependency_entity* node, layec_dependency_entity* dependency) {
    assert(graph != NULL);
    assert(graph->arena != NULL);
    assert(node != NULL);

    layec_dependency_entry* entry = NULL;
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

void layec_depgraph_ensure_tracked(layec_dependency_graph* graph, layec_dependency_entity* node) {
    assert(graph != NULL);
    assert(node != NULL);

    layec_depgraph_add_dependency(graph, node, NULL);
}

static int64_t dynarr_index_of(dynarr(void*) entities, void* entity) {
    for (int64_t i = 0, count = arr_count(entities); i < count; i++) {
        if (entity == entities[i])
            return i;
    }

    return -1;
}

static layec_dependency_order_result resolve_dependencies(
    layec_dependency_graph* graph,
    // clang-format off
    dynarr(layec_dependency_entity*)* resolved,
    dynarr(layec_dependency_entity*)* seen,
    // clang-format on
    layec_dependency_entity* entity
) {
#define RESOLVED (*resolved)
#define SEEN     (*seen)

    assert(graph != NULL);
    assert(resolved != NULL);
    assert(seen != NULL);

    layec_dependency_order_result result = {};
    if (-1 != dynarr_index_of(RESOLVED, entity)) {
        return result;
    }

    arr_push(SEEN, entity);

    int64_t entry_index = dynarr_index_of((void**)graph->entries, entity);
    bool requires_resolution = entry_index >= 0 && arr_count(graph->entries[entry_index]->dependencies) >= 0;

    if (requires_resolution) {
#define DEPS (*dependencies)
        dynarr(layec_dependency_entity*)* dependencies = &graph->entries[entry_index]->dependencies;

        for (int64_t i = 0, count = arr_count(DEPS); i < count; i++) {
            layec_dependency_entity* dep = DEPS[i];
            assert(dep != NULL);

            if (-1 != dynarr_index_of((void**)RESOLVED, dep)) {
                continue;
            }

            int64_t dep_seen_index = dynarr_index_of((void**)SEEN, dep);
            if (-1 != dep_seen_index) {
                result.status = LAYEC_DEP_CYCLE;
                result.from = entity;
                result.to = dep;
                return result;
            }

            layec_dependency_order_result dep_result = resolve_dependencies(
                graph,
                resolved,
                seen,
                dep
            );

            if (dep_result.status != LAYEC_DEP_OK) {
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

layec_dependency_order_result layec_dependency_graph_get_ordered_entities(layec_dependency_graph* graph) {
    assert(graph != NULL);

    layec_dependency_order_result result = {};
    dynarr(layec_dependency_entity*) seen = NULL;

    for (int64_t i = 0, count = arr_count(graph->entries); i < count; i++) {
        layec_dependency_order_result entry_result = resolve_dependencies(
            graph,
            &result.ordered_entities,
            &seen,
            graph->entries[i]->node
        );

        if (entry_result.status != LAYEC_DEP_OK) {
            arr_free(seen);
            return entry_result;
        }
    }

    arr_free(seen);
    result.status = LAYEC_DEP_OK;
    return result;
}
