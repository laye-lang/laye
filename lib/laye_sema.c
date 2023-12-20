#include "layec.h"

#include <assert.h>

typedef struct laye_sema {
    layec_context* context;
    layec_dependency_graph* dependencies;
} laye_sema;

static laye_node* laye_sema_lookup_value_node(laye_sema* sema, laye_module* from_module, laye_nameref nameref) {
    assert(sema != NULL);
    assert(from_module != NULL);
    assert(from_module->context != NULL);

    laye_scope* search_scope = nameref.scope;
    assert(search_scope != NULL);

    assert(arr_count(nameref.pieces) >= 1);
    string_view first_name = string_as_view(nameref.pieces[0].string_value);

    laye_node* found_declaration = NULL;

    while (search_scope != NULL) {
        laye_node* lookup = laye_scope_lookup_value(search_scope, first_name);
        if (lookup != NULL) {
            found_declaration = lookup;
            break;
        }

        search_scope = search_scope->parent;
    }

    laye_module* search_module = from_module;
    int64_t name_index = found_declaration == NULL ? 0 : 1;

    while (found_declaration == NULL && name_index < arr_count(nameref.pieces)) {
        assert(false && "todo laye_sema_lookup_value_node search through imports");
    }

    if (found_declaration == NULL) {
        assert(name_index >= arr_count(nameref.pieces));
        layec_write_error(
            from_module->context,
            arr_back(nameref.pieces)->location,
            "'%.*s' is not a value name in this context.",
            STR_EXPAND(arr_back(nameref.pieces)->string_value)
        );
        return NULL;
    }

    assert(name_index <= arr_count(nameref.pieces));
    if (name_index < arr_count(nameref.pieces)) {
        layec_write_error(
            from_module->context,
            arr_back(nameref.pieces)->location,
            "'%.*s' is not a scope.",
            STR_EXPAND(nameref.pieces[name_index].string_value)
        );
        return NULL;
    }

    assert(name_index == arr_count(nameref.pieces));
    assert(found_declaration != NULL);
    return found_declaration;
}

static laye_node* laye_sema_lookup_type_node(laye_sema* sema, laye_module* from_module, laye_nameref nameref) {
    laye_scope* search_scope = nameref.scope;
    assert(search_scope != NULL);

    assert(false && "todo laye_sema_lookup_type_node");
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
static bool laye_sema_analyse_node(laye_sema* sema, laye_node** node);

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
        // fprintf(stderr, ANSI_COLOR_BLUE "%016lX\n", (size_t)node);
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

static void laye_sema_resolve_top_level_types(laye_sema* sema, laye_node** node_ref) {
    laye_node* node = *node_ref;

    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node != NULL);
    assert(node != NULL);
    assert(node->module != NULL);
    assert(node->module->context == sema->context);

    switch (node->kind) {
        default: {
            fprintf(stderr, "on node kind %s\n", laye_node_kind_to_cstring(node->kind));
            assert(false && "unreachable");
        } break;

        case LAYE_NODE_DECL_FUNCTION: {
            assert(node->decl_function.return_type != NULL);
            if (!laye_sema_analyse_node(sema, &node->decl_function.return_type)) {
                node->sema_state = LAYEC_SEMA_ERRORED;
                node->type = sema->context->laye_types.poison;
            }

            for (int64_t i = 0, count = arr_count(node->decl_function.parameter_declarations); i < count; i++) {
                assert(node->decl_function.parameter_declarations[i] != NULL);
                assert(node->decl_function.parameter_declarations[i]->declared_type != NULL);
                if (!laye_sema_analyse_node(sema, &node->decl_function.parameter_declarations[i]->declared_type)) {
                    node->sema_state = LAYEC_SEMA_ERRORED;
                    node->type = sema->context->laye_types.poison;
                }
            }

            assert(node->declared_type != NULL);
            if (!laye_sema_analyse_node(sema, &node->declared_type)) {
                node->sema_state = LAYEC_SEMA_ERRORED;
                node->type = sema->context->laye_types.poison;
            }
        } break;
    }

    *node_ref = node;
}

static bool laye_sema_analyse_node(laye_sema* sema, laye_node** node_ref) {
    laye_node* node = *node_ref;

    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node_ref != NULL);
    assert(node != NULL);
    assert(node->module != NULL);
    assert(node->module->context == sema->context);
    assert(node->type != NULL);

    if (node->sema_state == LAYEC_SEMA_OK || node->sema_state == LAYEC_SEMA_ERRORED) {
        return node->sema_state == LAYEC_SEMA_OK;
    }

    if (node->sema_state == LAYEC_SEMA_IN_PROGRESS) {
        assert(false && "node already in progress");
        return false;
    }

    node->sema_state = LAYEC_SEMA_IN_PROGRESS;

    switch (node->kind) {
        default: {
            fprintf(stderr, "on node kind %s\n", laye_node_kind_to_cstring(node->kind));
            assert(false && "unreachable");
        } break;

        case LAYE_NODE_DECL_FUNCTION: {
            if (node->decl_function.body != NULL) {
                laye_sema_analyse_node(sema, &node->decl_function.body);
            }
        } break;

        case LAYE_NODE_COMPOUND: {
            for (int64_t i = 0, count = arr_count(node->compound.children); i < count; i++) {
                laye_node** child_ref = &node->compound.children[i];
                assert(*child_ref != NULL);
                
                laye_sema_analyse_node(sema, child_ref);
                laye_node* child = *child_ref;

                if (laye_type_is_noreturn(child->type)) {
                    node->type = sema->context->laye_types.noreturn;
                }
            }
        } break;

        case LAYE_NODE_CALL: {
            laye_sema_analyse_node(sema, &node->call.callee);

            for (int64_t i = 0, count = arr_count(node->call.arguments); i < count; i++) {
                laye_node** argument_node_ref = &node->call.arguments[i];
                assert(*argument_node_ref != NULL);
                laye_sema_analyse_node(sema, argument_node_ref);
            }

            laye_node* callee_type = node->call.callee->type;
            assert(callee_type->sema_state == LAYEC_SEMA_OK || callee_type->sema_state == LAYEC_SEMA_ERRORED);

            switch (callee_type->kind) {
                default: {
                    fprintf(stderr, "on node kind %s\n", laye_node_kind_to_cstring(callee_type->kind));
                    assert(false && "todo callee type");
                } break;

                case LAYE_NODE_TYPE_POISON: {
                    node->type = sema->context->laye_types.poison;
                } break;

                case LAYE_NODE_TYPE_FUNCTION: {
                    assert(callee_type->type_function.return_type != NULL);
                    node->type = callee_type->type_function.return_type;
                } break;
            }
        } break;

        case LAYE_NODE_NAMEREF: {
            laye_node* referenced_decl_node = laye_sema_lookup_value_node(sema, node->module, node->nameref);
            if (referenced_decl_node == NULL) {
                node->sema_state = LAYEC_SEMA_ERRORED;
                node->type = sema->context->laye_types.poison;
                break;
            }

            assert(laye_node_is_decl(referenced_decl_node));
            node->nameref.referenced_declaration = referenced_decl_node;
            assert(referenced_decl_node->declared_type != NULL);
            node->type = referenced_decl_node->declared_type;

            switch (referenced_decl_node->kind) {
                default: {
                    fprintf(stderr, "on node kind %s\n", laye_node_kind_to_cstring(referenced_decl_node->kind));
                    assert(false && "todo nameref unknown declaration");
                } break;

                case LAYE_NODE_DECL_FUNCTION: {
                } break;
            }
        } break;

        case LAYE_NODE_LITINT: {
            node->type = sema->context->laye_types._int;
        } break;

        case LAYE_NODE_TYPE_NORETURN: {
        } break;

        case LAYE_NODE_TYPE_INT: {
        } break;

        case LAYE_NODE_TYPE_FUNCTION: {
            assert(node->type_function.return_type != NULL);
            if (!laye_sema_analyse_node(sema, &node->type_function.return_type)) {
                node->sema_state = LAYEC_SEMA_ERRORED;
                node->type = sema->context->laye_types.poison;
            }

            for (int64_t i = 0, count = arr_count(node->type_function.parameter_types); i < count; i++) {
                if (!laye_sema_analyse_node(sema, &node->type_function.parameter_types[i])) {
                    node->sema_state = LAYEC_SEMA_ERRORED;
                    node->type = sema->context->laye_types.poison;
                }
            }
        } break;
    }

    if (node->sema_state == LAYEC_SEMA_IN_PROGRESS) {
        node->sema_state = LAYEC_SEMA_OK;
    }

    assert(node->sema_state == LAYEC_SEMA_OK || node->sema_state == LAYEC_SEMA_ERRORED);
    assert(node->type != NULL);
    assert(node->type->kind != LAYE_NODE_INVALID);
    // assert(node->type->kind != LAYE_NODE_TYPE_UNKNOWN);

    *node_ref = node;
    return node->sema_state == LAYEC_SEMA_OK;
}
