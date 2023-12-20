#include "layec.h"

#include <assert.h>

typedef struct laye_sema {
    layec_context* context;
    layec_dependency_graph* dependencies;
} laye_sema;

static laye_node* laye_sema_lookup_value_node(laye_sema* sema, laye_nameref nameref) {
    assert(false && "todo");
    return NULL;
}

static laye_node* laye_sema_lookup_type_node(laye_sema* sema, laye_nameref nameref) {
    assert(false && "todo");
    return NULL;
}

static void laye_generate_dependencies_for_module(layec_dependency_graph* graph, laye_module* module) {
    assert(graph != NULL);
    assert(module != NULL);

    if (module->dependencies_generated) {
        return;
    }

    for (int64_t i = 0, count = arr_count(module->top_level_nodes); i < count; i++) {
        laye_node* top_level_node = module->top_level_nodes[i];
        assert(top_level_node != NULL);

        switch (top_level_node->kind) {
            default: assert(false && "unreachable"); break;

            case LAYE_NODE_DECL_FUNCTION: {
                // TODO(local): generate dependencies
                layec_depgraph_ensure_tracked(graph, top_level_node);
            } break;
        }
    }

    module->dependencies_generated = true;
}

static void laye_sema_resolve_top_level_types(laye_sema* sema, laye_node** node);
static void laye_sema_analyse_node(laye_sema* sema, laye_node** node);

void laye_analyse(laye_module* module) {
    assert(module != NULL);
    assert(module->context != NULL);
    assert(module->context->laye_dependencies != NULL);

    laye_sema sema = {
        .context = module->context,
        .dependencies = module->context->laye_dependencies,
    };

    dynarr(laye_module*) referenced_modules = NULL;
    arr_push(referenced_modules, module);

    // TODO(local): traverse all imported modules

    for (int64_t i = 0, count = arr_count(referenced_modules); i < count; i++) {
        laye_generate_dependencies_for_module(sema.dependencies, referenced_modules[i]);
    }

    layec_dependency_order_result order_result = layec_dependency_graph_get_ordered_entities(sema.dependencies);
    if (order_result.status == LAYEC_DEP_CYCLE) {
        layec_write_error(
            module->context,
            ((laye_node*)order_result.from)->location,
            "Cyclic dependency detected. %.*s depends on %.*s, and vice versa.",
            STR_EXPAND(((laye_node*)order_result.from)->declared_name),
            STR_EXPAND(((laye_node*)order_result.to)->declared_name)
        );

        layec_write_note(
            module->context,
            ((laye_node*)order_result.to)->location,
            "%.*s declared here.",
            STR_EXPAND(((laye_node*)order_result.to)->declared_name)
        );

        return;
    }

    assert(order_result.status == LAYEC_DEP_OK);
    dynarr(laye_node*) ordered_nodes = (dynarr(laye_node*))order_result.ordered_entities;

    for (int64_t i = 0, count = arr_count(ordered_nodes); i < count; i++) {
        laye_node* node = ordered_nodes[i];
        assert(node != NULL);
        //fprintf(stderr, ANSI_COLOR_BLUE "%016lX\n", (size_t)node);
        laye_sema_resolve_top_level_types(&sema, &node);
        assert(node != NULL);
    }

    for (int64_t i = 0, count = arr_count(ordered_nodes); i < count; i++) {
        laye_node* node = ordered_nodes[i];
        assert(node != NULL);
        laye_sema_analyse_node(&sema, &node);
        assert(node != NULL);
    }

    arr_free(ordered_nodes);
    arr_free(referenced_modules);
}

static void laye_sema_resolve_top_level_types(laye_sema* sema, laye_node** node) {
#define NODE (*node)

    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node != NULL);
    assert(NODE != NULL);
    assert(NODE->module != NULL);
    assert(NODE->module->context == sema->context);

    switch (NODE->kind) {
        default: {
            fprintf(stderr, "on node kind %s\n", laye_node_kind_to_cstring(NODE->kind));
            assert(false && "unreachable");
        } break;

        case LAYE_NODE_DECL_FUNCTION: {
        } break;
    }

#undef NODE
}

static void laye_sema_analyse_node(laye_sema* sema, laye_node** node) {
#define NODE (*node)

    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node != NULL);
    assert(NODE != NULL);
    assert(NODE->module != NULL);
    assert(NODE->module->context == sema->context);

    if (NODE->sema_state == LAYEC_SEMA_OK || NODE->sema_state == LAYEC_SEMA_ERRORED) {
        return;
    }

    if (NODE->sema_state == LAYEC_SEMA_IN_PROGRESS) {
        assert(false && "node already in progress");
        return;
    }

    NODE->sema_state = LAYEC_SEMA_IN_PROGRESS;

    switch (NODE->kind) {
        default: {
            fprintf(stderr, "on node kind %s\n", laye_node_kind_to_cstring(NODE->kind));
            assert(false && "unreachable");
        } break;

        case LAYE_NODE_DECL_FUNCTION: {
        } break;
    }

    if (NODE->sema_state == LAYEC_SEMA_IN_PROGRESS) {
        NODE->sema_state = LAYEC_SEMA_OK;
    }

    assert(NODE->sema_state == LAYEC_SEMA_OK || NODE->sema_state == LAYEC_SEMA_ERRORED);

#undef NODE
}
