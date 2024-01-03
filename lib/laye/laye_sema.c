#include <assert.h>

#include "layec.h"
#include "laye.h"

typedef struct laye_sema {
    layec_context* context;
    layec_dependency_graph* dependencies;

    laye_node* current_function;
} laye_sema;

static bool laye_sema_analyse_node(laye_sema* sema, laye_node** node, laye_node* expected_type);
static bool laye_sema_analyse_node_and_discard(laye_sema* sema, laye_node** node);

static void laye_sema_discard(laye_sema* sema, laye_node** node);
static bool laye_sema_has_side_effects(laye_sema* sema, laye_node* node);

static bool laye_sema_convert(laye_sema* sema, laye_node** node, laye_node* to);
static void laye_sema_convert_or_error(laye_sema* sema, laye_node** node, laye_node* to);
static void laye_sema_convert_to_c_varargs_or_error(laye_sema* sema, laye_node** node);
static bool laye_sema_convert_to_common_type(laye_sema* sema, laye_node** a, laye_node** b);
static int laye_sema_try_convert(laye_sema* sema, laye_node** node, laye_node* to);

static void laye_sema_wrap_with_cast(laye_sema* sema, laye_node** node, laye_node* type, laye_cast_kind cast_kind);
static void laye_sema_insert_pointer_to_integer_cast(laye_sema* sema, laye_node** node);
static void laye_sema_insert_implicit_cast(laye_sema* sema, laye_node** node, laye_node* to);
static void laye_sema_lvalue_to_rvalue(laye_sema* sema, laye_node** node, bool strip_ref);
static bool laye_sema_implicit_dereference(laye_sema* sema, laye_node** node);

static laye_node* laye_sema_get_pointer_to_type(laye_sema* sema, laye_node* element_type, bool is_modifiable);
static laye_node* laye_sema_get_reference_to_type(laye_sema* sema, laye_node* element_type, bool is_modifiable);

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
        laye_sema_analyse_node(&sema, &node, NULL);
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
            if (!laye_sema_analyse_node(sema, &node->decl_function.return_type, NULL)) {
                node->sema_state = LAYEC_SEMA_ERRORED;
                node->type = sema->context->laye_types.poison;
            }

            for (int64_t i = 0, count = arr_count(node->decl_function.parameter_declarations); i < count; i++) {
                assert(node->decl_function.parameter_declarations[i] != NULL);
                assert(node->decl_function.parameter_declarations[i]->declared_type != NULL);
                if (!laye_sema_analyse_node(sema, &node->decl_function.parameter_declarations[i]->declared_type, NULL)) {
                    node->sema_state = LAYEC_SEMA_ERRORED;
                    node->type = sema->context->laye_types.poison;
                }
            }

            assert(node->declared_type != NULL);
            if (!laye_sema_analyse_node(sema, &node->declared_type, NULL)) {
                node->sema_state = LAYEC_SEMA_ERRORED;
                node->type = sema->context->laye_types.poison;
            }
        } break;
    }

    *node_ref = node;
}

static bool laye_sema_analyse_node(laye_sema* sema, laye_node** node_ref, laye_node* expected_type) {
    laye_node* node = *node_ref;

    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node_ref != NULL);
    assert(node != NULL);
    if (!laye_node_is_type(node)) {
        assert(node->module != NULL);
        assert(node->module->context == sema->context);
    }
    assert(node->type != NULL);

    if (node->sema_state == LAYEC_SEMA_OK || node->sema_state == LAYEC_SEMA_ERRORED) {
        return node->sema_state == LAYEC_SEMA_OK;
    }

    if (node->sema_state == LAYEC_SEMA_IN_PROGRESS) {
        assert(false && "node already in progress");
        return false;
    }

    node->sema_state = LAYEC_SEMA_IN_PROGRESS;
    laye_sema_analyse_node(sema, &node->type, NULL);

    switch (node->kind) {
        default: {
            fprintf(stderr, "on node kind %s\n", laye_node_kind_to_cstring(node->kind));
            assert(false && "unreachable");
        } break;

        case LAYE_NODE_DECL_FUNCTION: {
            laye_node* prev_function = sema->current_function;
            sema->current_function = node;

            if (node->decl_function.body != NULL) {
                laye_sema_analyse_node(sema, &node->decl_function.body, NULL);
            }

            sema->current_function = prev_function;
        } break;

        case LAYE_NODE_DECL_BINDING: {
            laye_sema_analyse_node(sema, &node->declared_type, NULL);
            if (node->decl_binding.initializer != NULL) {
                if (laye_sema_analyse_node(sema, &node->decl_binding.initializer, NULL)) {
                    laye_sema_convert_or_error(sema, &node->decl_binding.initializer, node->declared_type);
                }
            }
        } break;

        case LAYE_NODE_RETURN: {
            assert(sema->current_function != NULL);
            assert(sema->current_function->type != NULL);
            assert(laye_node_is_type(sema->current_function->type));
            assert(laye_type_is_function(sema->current_function->type));

            laye_node* expected_return_type = sema->current_function->type->type_function.return_type;
            assert(expected_return_type != NULL);
            assert(laye_node_is_type(expected_return_type));

            if (node->_return.value != NULL) {
                laye_sema_analyse_node(sema, &node->_return.value, expected_return_type);
                if (laye_type_is_void(expected_return_type) || laye_type_is_noreturn(expected_return_type)) {
                    layec_write_error(sema->context, node->location, "Cannot return a value from a `void` or `noreturn` function.");
                } else {
                    laye_sema_convert_or_error(sema, &node->_return.value, expected_return_type);
                }
            } else {
                if (!laye_type_is_void(expected_return_type) && !laye_type_is_noreturn(expected_return_type)) {
                    layec_write_error(sema->context, node->location, "Must return a value from a non-void function.");
                }
            }
        } break;

        case LAYE_NODE_XYZZY: {
        } break;

        case LAYE_NODE_COMPOUND: {
            for (int64_t i = 0, count = arr_count(node->compound.children); i < count; i++) {
                laye_node** child_ref = &node->compound.children[i];
                assert(*child_ref != NULL);

                laye_sema_analyse_node(sema, child_ref, NULL);
                laye_node* child = *child_ref;

                if (laye_type_is_noreturn(child->type)) {
                    node->type = sema->context->laye_types.noreturn;
                }
            }
        } break;

        case LAYE_NODE_EVALUATED_CONSTANT: break;

        case LAYE_NODE_CALL: {
            laye_sema_analyse_node(sema, &node->call.callee, NULL);

            for (int64_t i = 0, count = arr_count(node->call.arguments); i < count; i++) {
                laye_node** argument_node_ref = &node->call.arguments[i];
                assert(*argument_node_ref != NULL);
                laye_sema_analyse_node(sema, argument_node_ref, NULL);
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

                    if (callee_type->type_function.varargs_style == LAYE_VARARGS_NONE) {
                        if (arr_count(node->call.arguments) != arr_count(callee_type->type_function.parameter_types)) {
                            node->sema_state = LAYEC_SEMA_ERRORED;
                            layec_write_error(
                                sema->context,
                                node->location,
                                "Expected %lld arguments to call, got %lld.",
                                arr_count(callee_type->type_function.parameter_types),
                                arr_count(node->call.arguments)
                            );
                            break;
                        }

                        for (int64_t i = 0, count = arr_count(node->call.arguments); i < count; i++) {
                            laye_sema_convert_or_error(sema, &node->call.arguments[i], callee_type->type_function.parameter_types[i]);
                        }
                    } else {
                        assert(false && "todo analyse call unhandled varargs");
                    }
                } break;
            }
        } break;

        case LAYE_NODE_NAMEREF: {
            laye_node* referenced_decl_node = node->nameref.referenced_declaration;

            if (referenced_decl_node == NULL) {
                referenced_decl_node = laye_sema_lookup_value_node(sema, node->module, node->nameref);
                if (referenced_decl_node == NULL) {
                    node->sema_state = LAYEC_SEMA_ERRORED;
                    node->type = sema->context->laye_types.poison;
                    break;
                }
            }

            assert(referenced_decl_node != NULL);
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

                case LAYE_NODE_DECL_BINDING: {
                    laye_expr_set_lvalue(node, true);
                } break;
            }
        } break;

        case LAYE_NODE_CAST: {
            if (
                node->cast.kind == LAYE_CAST_IMPLICIT ||
                node->cast.kind == LAYE_CAST_LVALUE_TO_RVALUE ||
                node->cast.kind == LAYE_CAST_LVALUE_TO_REFERENCE ||
                node->cast.kind == LAYE_CAST_REFERENCE_TO_LVALUE
            ) {
                laye_expr_set_lvalue(node, node->cast.kind == LAYE_CAST_REFERENCE_TO_LVALUE);
                break;
            }

            assert(node->cast.kind != LAYE_CAST_IMPLICIT);
            assert(node->cast.kind != LAYE_CAST_LVALUE_TO_RVALUE);
            assert(node->cast.kind != LAYE_CAST_LVALUE_TO_REFERENCE);
            assert(node->cast.kind != LAYE_CAST_REFERENCE_TO_LVALUE);

            if (!laye_sema_analyse_node(sema, &node->cast.operand, node->type)) {
                break;
            }

            if (laye_sema_convert(sema, &node->cast.operand, node->type)) {
                break;
            }

            assert(false && "todo cast sema");
        } break;

        case LAYE_NODE_LITINT: {
            node->type = sema->context->laye_types._int;
        } break;

        case LAYE_NODE_TYPE_NORETURN: {
        } break;

        case LAYE_NODE_TYPE_INT: {
        } break;

        case LAYE_NODE_TYPE_FUNCTION: {
            if (node->type_function.calling_convention == LAYEC_DEFAULTCC) {
                node->type_function.calling_convention = LAYEC_LAYECC;
            }

            assert(node->type_function.return_type != NULL);
            if (!laye_sema_analyse_node(sema, &node->type_function.return_type, NULL)) {
                node->sema_state = LAYEC_SEMA_ERRORED;
                node->type = sema->context->laye_types.poison;
            }

            for (int64_t i = 0, count = arr_count(node->type_function.parameter_types); i < count; i++) {
                if (!laye_sema_analyse_node(sema, &node->type_function.parameter_types[i], NULL)) {
                    node->sema_state = LAYEC_SEMA_ERRORED;
                    node->type = sema->context->laye_types.poison;
                }
            }
        } break;
    }

    assert(node != NULL);
    if (expected_type != NULL) {
        assert(laye_node_is_type(expected_type));
        laye_sema_convert_or_error(sema, &node, expected_type);
    }

    if (node->sema_state == LAYEC_SEMA_IN_PROGRESS) {
        node->sema_state = LAYEC_SEMA_OK;
    }

    assert(node != NULL);
    assert(node->sema_state == LAYEC_SEMA_OK || node->sema_state == LAYEC_SEMA_ERRORED);
    assert(node->type != NULL);
    assert(node->type->kind != LAYE_NODE_INVALID);
    assert(node->type->kind != LAYE_NODE_TYPE_UNKNOWN);

    *node_ref = node;
    return node->sema_state == LAYEC_SEMA_OK;
}

static bool laye_sema_analyse_node_and_discard(laye_sema* sema, laye_node** node) {
    assert(sema != NULL);
    assert(node != NULL);
    assert(*node != NULL);
    if (!laye_sema_analyse_node(sema, node, NULL)) return false;
    laye_sema_discard(sema, node);
    return true;
}

static void laye_sema_discard(laye_sema* sema, laye_node** node_ref) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node_ref != NULL);

    laye_node* node = *node_ref;
    assert(node != NULL);
    assert(node->type != NULL);

    if (node->kind == LAYE_NODE_CALL) {
        // TODO(local): check discardable nature of the callee
    }

    if (node->type->kind == LAYE_NODE_TYPE_VOID || node->type->kind == LAYE_NODE_TYPE_NORETURN) {
        return;
    }

    laye_sema_insert_implicit_cast(sema, node_ref, sema->context->laye_types._void);
}

static bool laye_sema_has_side_effects(laye_sema* sema, laye_node* node) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node != NULL);

    // TODO(local): calculate if something is pure or not
    return true;
}

enum {
    LAYE_CONVERT_CONTAINS_ERRORS = -2,
    LAYE_CONVERT_IMPOSSIBLE = -1,
    LAYE_CONVERT_NOOP = 0,
};

static int laye_sema_convert_impl(laye_sema* sema, laye_node** node, laye_node* to, bool perform_conversion) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node != NULL);
    assert(*node != NULL);
    assert(to != NULL);
    assert(laye_node_is_type(to));

    laye_node* from = (*node)->type;
    assert(from != NULL);
    assert(laye_node_is_type(from));

    if (from->sema_state == LAYEC_SEMA_ERRORED || to->sema_state == LAYEC_SEMA_ERRORED) {
        return LAYE_CONVERT_CONTAINS_ERRORS;
    }

    assert(from->sema_state == LAYEC_SEMA_OK);
    assert(to->sema_state == LAYEC_SEMA_OK);

    if (laye_type_equals(from, to)) {
        return LAYE_CONVERT_NOOP;
    }

    int score = 0;
    if (laye_expr_is_lvalue(*node)) {
        score = 1;
    }

    if (perform_conversion) {
        laye_sema_lvalue_to_rvalue(sema, node, false);
    }

    if (laye_type_is_int(from) && laye_type_is_int(to)) {
        int from_size = laye_type_size_in_bits(from);
        int to_size = laye_type_size_in_bits(to);

        layec_evaluated_constant eval_result = {0};
        if (laye_expr_evaluate(*node, &eval_result, false)) {
            assert(eval_result.kind == LAYEC_EVAL_INT);
            int sig_bits = layec_get_significant_bits(eval_result.int_value);
            if (sig_bits <= to_size) {
                if (perform_conversion) {
                    laye_sema_insert_implicit_cast(sema, node, to);

                    laye_node* constant_node = laye_node_create((*node)->module, LAYE_NODE_EVALUATED_CONSTANT, (*node)->location, to);
                    assert(constant_node != NULL);
                    constant_node->compiler_generated = true;
                    constant_node->evaluated_constant.expr = *node;
                    constant_node->evaluated_constant.result = eval_result;

                    laye_sema_analyse_node(sema, &constant_node, to);
                    *node = constant_node;
                }

                return 1 + score;
            }
        }

        if (from_size <= to_size) {
            if (perform_conversion) {
                laye_sema_insert_implicit_cast(sema, node, to);
            }

            return 1 + score;
        }

        return LAYE_CONVERT_IMPOSSIBLE;
    }

    return LAYE_CONVERT_IMPOSSIBLE;
}

static bool laye_sema_convert(laye_sema* sema, laye_node** node, laye_node* to) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node != NULL);
    assert(*node != NULL);
    assert(to != NULL);
    assert(laye_node_is_type(to));

    if ((*node)->sema_state == LAYEC_SEMA_ERRORED) {
        return true;
    }

    laye_sema_lvalue_to_rvalue(sema, node, false);

    return laye_sema_convert_impl(sema, node, to, true) >= 0;
}

static void laye_sema_convert_or_error(laye_sema* sema, laye_node** node, laye_node* to) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node != NULL);
    assert(*node != NULL);
    assert(to != NULL);
    assert(laye_node_is_type(to));

    if (!laye_sema_convert(sema, node, to)) {
        string type_string = string_create(sema->context->allocator);
        laye_type_print_to_string(to, &type_string, sema->context->use_color);
        layec_write_error(
            sema->context,
            (*node)->location,
            "Expression is not convertible to %.*s",
            STR_EXPAND(type_string)
        );
        string_destroy(&type_string);
    }
}

static void laye_sema_convert_to_c_varargs_or_error(laye_sema* sema, laye_node** node) {
    assert(false && "todo");
}

static bool laye_sema_convert_to_common_type(laye_sema* sema, laye_node** a, laye_node** b) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(a != NULL);
    assert(*a != NULL);
    assert((*a)->type != NULL);
    assert(b != NULL);
    assert(*b != NULL);
    assert((*b)->type != NULL);

    return laye_sema_convert(sema, a, (*b)->type) || laye_sema_convert(sema, b, (*a)->type);
}

static int laye_sema_try_convert(laye_sema* sema, laye_node** node, laye_node* to) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node != NULL);
    assert(*node != NULL);
    assert(to != NULL);
    assert(laye_node_is_type(to));

    return laye_sema_convert_impl(sema, node, to, false);
}

static void laye_sema_wrap_with_cast(laye_sema* sema, laye_node** node, laye_node* type, laye_cast_kind cast_kind) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node != NULL);
    assert(*node != NULL);
    assert((*node)->module != NULL);
    assert(type != NULL);
    assert(laye_node_is_type(type));

    laye_node* cast_node = laye_node_create((*node)->module, LAYE_NODE_CAST, (*node)->location, type);
    assert(cast_node != NULL);
    cast_node->compiler_generated = true;
    cast_node->type = type;
    cast_node->cast.kind = cast_kind;
    cast_node->cast.operand = *node;

    laye_sema_analyse_node(sema, &cast_node, type);
    assert(cast_node != NULL);

    *node = cast_node;
}

static void laye_sema_insert_pointer_to_integer_cast(laye_sema* sema, laye_node** node) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node != NULL);
    assert(*node != NULL);
    assert((*node)->module != NULL);
    assert((*node)->type != NULL);

    if (laye_type_is_pointer((*node)->type) || laye_type_is_buffer((*node)->type)) {
        laye_sema_wrap_with_cast(sema, node, sema->context->laye_types._int, LAYE_CAST_IMPLICIT);
    }
}

static void laye_sema_insert_implicit_cast(laye_sema* sema, laye_node** node, laye_node* to) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node != NULL);
    assert(*node != NULL);
    assert((*node)->module != NULL);
    assert(to != NULL);
    assert(laye_node_is_type(to));

    laye_sema_wrap_with_cast(sema, node, to, LAYE_CAST_IMPLICIT);
}

static void laye_sema_lvalue_to_rvalue(laye_sema* sema, laye_node** node, bool strip_ref) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node != NULL);
    assert(*node != NULL);
    assert((*node)->module != NULL);

    if ((*node)->sema_state == LAYEC_SEMA_ERRORED) return;

    if (laye_node_is_lvalue(*node)) {
        laye_sema_wrap_with_cast(sema, node, (*node)->type, LAYE_CAST_LVALUE_TO_RVALUE);
    }

    if (strip_ref && laye_type_is_reference((*node)->type)) {
        laye_sema_wrap_with_cast(sema, node, (*node)->type->type_container.element_type, LAYE_CAST_REFERENCE_TO_LVALUE);
        laye_sema_lvalue_to_rvalue(sema, node, false);
    }
}

static bool laye_sema_implicit_dereference(laye_sema* sema, laye_node** node) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node != NULL);
    assert(*node != NULL);
    assert((*node)->module != NULL);
    assert((*node)->type != NULL);

    if (laye_type_is_reference((*node)->type)) {
        laye_sema_lvalue_to_rvalue(sema, node, false);
        laye_sema_wrap_with_cast(sema, node, (*node)->type->type_container.element_type, LAYE_CAST_REFERENCE_TO_LVALUE);
    }

    assert(node != NULL);
    assert(*node != NULL);
    assert((*node)->type != NULL);

    while (laye_type_is_pointer((*node)->type)) {
        assert((*node)->module != NULL);
        laye_node* deref_node = laye_node_create((*node)->module, LAYE_NODE_UNARY, (*node)->location, (*node)->type->type_container.element_type);
        assert(deref_node != NULL);
        deref_node->compiler_generated = true;
        deref_node->unary.operand = *node;
        deref_node->unary.operator=(laye_token){
            .kind = '*',
            .location = (*node)->location,
        };
        deref_node->unary.operator.location.length = 0;

        bool deref_analyse_result = laye_sema_analyse_node(sema, &deref_node, NULL);
        assert(deref_analyse_result);

        *node = deref_node;

        assert(node != NULL);
        assert(*node != NULL);
        assert((*node)->type != NULL);
    }

    return laye_expr_is_lvalue(*node);
}

static laye_node* laye_sema_get_pointer_to_type(laye_sema* sema, laye_node* element_type, bool is_modifiable);
static laye_node* laye_sema_get_reference_to_type(laye_sema* sema, laye_node* element_type, bool is_modifiable);
