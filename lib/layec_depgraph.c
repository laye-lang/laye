#include "layec.h"

#include <assert.h>

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

dynarr(layec_dependency_entity*) layec_dependency_graph_get_ordered_entities(layec_dependency_graph* graph) {
    assert(false && "todo");
    return NULL;
}
