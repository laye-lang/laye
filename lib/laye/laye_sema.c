#include "laye.h"
#include "layec.h"

#include <assert.h>

typedef struct laye_sema {
    layec_context* context;
    layec_dependency_graph* dependencies;

    laye_node* current_function;
    laye_node* current_yield_target;
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
static laye_node* laye_sema_get_buffer_of_type(laye_sema* sema, laye_node* element_type, bool is_modifiable);
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
            assert(laye_type_is_function(node->declared_type));
            if (!laye_sema_analyse_node(sema, &node->declared_type, NULL)) {
                node->sema_state = LAYEC_SEMA_ERRORED;
                node->type = sema->context->laye_types.poison;
            }

            bool is_declared_main = string_view_equals(SV_CONSTANT("main"), string_as_view(node->declared_name));
            bool has_foreign_name = node->attributes.foreign_name.count != 0;
            bool has_body = arr_count(node->decl_function.body) != 0;

            if (is_declared_main && !has_foreign_name) {
                node->attributes.calling_convention = LAYEC_CCC;
                node->attributes.linkage = LAYEC_LINK_EXPORTED;
                node->attributes.mangling = LAYEC_MANGLE_NONE;

                node->declared_type->type_function.calling_convention = LAYEC_CCC;

                if (!has_body) {
                    // TODO(local): should we allow declarations of main?
                }
            }
        } break;
    }

    *node_ref = node;
}

static laye_node* wrap_yieldable_value_in_compound(laye_sema* sema, laye_node** value_ref, laye_node* expected_type) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(value_ref != NULL);

    laye_sema_analyse_node(sema, value_ref, expected_type);
    laye_node* value = *value_ref;
    assert(value != NULL);

    laye_node* yield_node = laye_node_create(value->module, LAYE_NODE_YIELD, value->location, sema->context->laye_types._void);
    assert(yield_node != NULL);
    yield_node->compiler_generated = true;
    yield_node->yield.value = value;

    laye_node* compound_node = laye_node_create(value->module, LAYE_NODE_COMPOUND, value->location, sema->context->laye_types._void);
    assert(compound_node != NULL);
    compound_node->compiler_generated = true;
    arr_push(compound_node->compound.children, yield_node);

    return compound_node;
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
                if (laye_sema_analyse_node(sema, &node->decl_binding.initializer, node->declared_type)) {
                    laye_sema_convert_or_error(sema, &node->decl_binding.initializer, node->declared_type);
                }
            }
        } break;

        case LAYE_NODE_IF: {
            bool is_expression = node->_if.is_expr;
            bool is_noreturn = true;

            assert(arr_count(node->_if.conditions) == arr_count(node->_if.passes));
            for (int64_t i = 0, count = arr_count(node->_if.conditions); i < count; i++) {
                if (laye_sema_analyse_node(sema, &node->_if.conditions[i], sema->context->laye_types._bool)) {
                    laye_sema_convert_or_error(sema, &node->_if.conditions[i], sema->context->laye_types._bool);
                } else {
                    node->sema_state = LAYEC_SEMA_ERRORED;
                }

                laye_node* prev_yield_target = sema->current_yield_target;

                if (is_expression) {
                    if (node->_if.passes[i]->kind != LAYE_NODE_COMPOUND) {
                        node->_if.passes[i] = wrap_yieldable_value_in_compound(sema, &node->_if.passes[i], expected_type);
                    }

                    sema->current_yield_target = node->_if.passes[i];
                }

                if (laye_sema_analyse_node(sema, &node->_if.passes[i], expected_type)) {
                    if (is_expression) {
                        laye_sema_convert_or_error(sema, &node->_if.passes[i], expected_type);
                    }
                } else {
                    node->sema_state = LAYEC_SEMA_ERRORED;
                }

                sema->current_yield_target = prev_yield_target;

                if (!laye_type_is_noreturn(node->_if.passes[i]->type)) {
                    is_noreturn = false;
                }
            }

            if (node->_if.fail != NULL) {
                laye_node* prev_yield_target = sema->current_yield_target;

                if (is_expression) {
                    if (node->_if.fail->kind != LAYE_NODE_COMPOUND) {
                        node->_if.fail = wrap_yieldable_value_in_compound(sema, &node->_if.fail, expected_type);
                    }

                    sema->current_yield_target = node->_if.fail;
                }

                if (!laye_sema_analyse_node(sema, &node->_if.fail, expected_type)) {
                    node->sema_state = LAYEC_SEMA_ERRORED;
                }

                sema->current_yield_target = prev_yield_target;

                if (!laye_type_is_noreturn(node->_if.fail->type)) {
                    is_noreturn = false;
                }
            } else {
                is_noreturn = false;
            }

            if (is_noreturn) {
                node->type = sema->context->laye_types.noreturn;
            } else {
                if (is_expression) {
                    node->type = expected_type;
                } else {
                    assert(node->type != NULL);
                    assert(laye_type_is_void(node->type));
                }
            }
        } break;

        case LAYE_NODE_RETURN: {
            assert(sema->current_function != NULL);
            assert(sema->current_function->type != NULL);
            assert(laye_node_is_type(sema->current_function->type));
            assert(laye_type_is_function(sema->current_function->declared_type));

            laye_node* expected_return_type = sema->current_function->declared_type->type_function.return_type;
            assert(expected_return_type != NULL);
            assert(laye_node_is_type(expected_return_type));

            if (node->_return.value != NULL) {
                laye_sema_analyse_node(sema, &node->_return.value, expected_return_type);
                laye_sema_lvalue_to_rvalue(sema, &node->_return.value, true);
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

        case LAYE_NODE_YIELD: {
            laye_sema_analyse_node(sema, &node->yield.value, expected_type);

            if (sema->current_yield_target == NULL) {
                layec_write_error(sema->context, node->location, "Must yield a value from a yieldable block.");
            } else {
                if (expected_type != NULL) {
                    laye_sema_convert_or_error(sema, &node->yield.value, expected_type);
                }

                assert(sema->current_yield_target->kind == LAYE_NODE_COMPOUND);
                sema->current_yield_target->type = node->yield.value->type;

                if (sema->current_yield_target->compound.is_expr && laye_expr_is_lvalue(node->yield.value)) {
                    laye_expr_set_lvalue(sema->current_yield_target, true);
                }
            }
        } break;

        case LAYE_NODE_XYZZY: {
        } break;

        case LAYE_NODE_ASSIGNMENT: {
            laye_sema_analyse_node(sema, &node->assignment.lhs, NULL);
            assert(node->assignment.lhs->type != NULL);

            laye_sema_analyse_node(sema, &node->assignment.rhs, node->assignment.lhs->type);
            laye_sema_lvalue_to_rvalue(sema, &node->assignment.rhs, true);
            assert(node->assignment.rhs->type != NULL);

            if (!laye_expr_is_lvalue(node->assignment.lhs)) {
                layec_write_error(sema->context, node->assignment.lhs->location, "Cannot assign to a non-lvalue.");
                node->sema_state = LAYEC_SEMA_ERRORED;
            } else {
                if (node->assignment.lhs->type->kind == LAYE_NODE_TYPE_REFERENCE) {
                    assert(false && "handle assignment to a reference in sema");
                }

                laye_node* nonref_target_type = laye_type_strip_references(node->assignment.lhs->type);
                laye_sema_convert_or_error(sema, &node->assignment.rhs, nonref_target_type);
            }

            if (!node->assignment.lhs->type->type_is_modifiable) {
                layec_write_error(sema->context, node->assignment.lhs->location, "Left-hand side of assignment is not mutable.");
                node->sema_state = LAYEC_SEMA_ERRORED;
            }

            if (node->assignment.lhs->sema_state != LAYEC_SEMA_OK || node->assignment.rhs->sema_state != LAYEC_SEMA_OK) {
                node->sema_state = LAYEC_SEMA_ERRORED;
            }
        } break;

        case LAYE_NODE_COMPOUND: {
            bool is_expression = node->compound.is_expr;

            laye_node* prev_yield_target = sema->current_yield_target;

            if (is_expression) {
                // assert(expected_type != NULL);
                sema->current_yield_target = node;
            }

            for (int64_t i = 0, count = arr_count(node->compound.children); i < count; i++) {
                laye_node** child_ref = &node->compound.children[i];
                assert(*child_ref != NULL);

                if ((*child_ref)->kind == LAYE_NODE_YIELD) {
                    laye_sema_analyse_node(sema, child_ref, expected_type);
                } else {
                    laye_sema_analyse_node(sema, child_ref, NULL);
                }

                laye_node* child = *child_ref;
                if (laye_type_is_noreturn(child->type)) {
                    node->type = sema->context->laye_types.noreturn;
                }
            }

            sema->current_yield_target = prev_yield_target;
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

                    int64_t param_count = arr_count(callee_type->type_function.parameter_types);

                    if (callee_type->type_function.varargs_style == LAYE_VARARGS_NONE) {
                        if (arr_count(node->call.arguments) != param_count) {
                            node->sema_state = LAYEC_SEMA_ERRORED;
                            layec_write_error(
                                sema->context,
                                node->location,
                                "Expected %lld arguments to call, got %lld.",
                                param_count,
                                arr_count(node->call.arguments)
                            );
                            break;
                        }

                        for (int64_t i = 0, count = arr_count(node->call.arguments); i < count; i++) {
                            laye_sema_convert_or_error(sema, &node->call.arguments[i], callee_type->type_function.parameter_types[i]);
                        }
                    } else if (callee_type->type_function.varargs_style == LAYE_VARARGS_C) {
                        if (arr_count(node->call.arguments) < param_count) {
                            node->sema_state = LAYEC_SEMA_ERRORED;
                            layec_write_error(
                                sema->context,
                                node->location,
                                "Expected at least %lld arguments to call, got %lld.",
                                param_count,
                                arr_count(node->call.arguments)
                            );
                            break;
                        }

                        for (int64_t i = 0, count = arr_count(node->call.arguments); i < count; i++) {
                            if (i < param_count) {
                                laye_sema_convert_or_error(sema, &node->call.arguments[i], callee_type->type_function.parameter_types[i]);
                            } else {
                                laye_sema_convert_to_c_varargs_or_error(sema, &node->call.arguments[i]);
                            }
                        }
                    } else {
                        assert(false && "todo analyse call unhandled varargs");
                    }
                } break;
            }
        } break;

        case LAYE_NODE_INDEX: {
            laye_sema_analyse_node(sema, &node->index.value, NULL);

            for (int64_t i = 0, count = arr_count(node->index.indices); i < count; i++) {
                laye_node** index_node_ref = &node->index.indices[i];
                assert(*index_node_ref != NULL);
                laye_sema_analyse_node(sema, index_node_ref, sema->context->laye_types._int);
                // laye_sema_convert_or_error(sema, index_node_ref, sema->context->laye_types._int);

                if ((*index_node_ref)->type->kind == LAYE_NODE_TYPE_INT) {
                    if ((*index_node_ref)->type->type_primitive.is_signed) {
                        laye_sema_convert_or_error(sema, index_node_ref, sema->context->laye_types._int);
                        laye_sema_insert_implicit_cast(sema, index_node_ref, sema->context->laye_types._uint);
                    } else {
                        laye_sema_convert_or_error(sema, index_node_ref, sema->context->laye_types._uint);
                    }
                } else {
                    layec_write_error(sema->context, (*index_node_ref)->location, "Indices must be of integer type or convertible to an integer.");
                }
            }

            laye_node* value_type = node->index.value->type;
            assert(value_type->sema_state == LAYEC_SEMA_OK || value_type->sema_state == LAYEC_SEMA_ERRORED);

            switch (value_type->kind) {
                default: {
                    string type_string = string_create(sema->context->allocator);
                    laye_type_print_to_string(value_type, &type_string, sema->context->use_color);
                    layec_write_error(sema->context, node->index.value->location, "Cannot index type %.*s.", STR_EXPAND(type_string));
                    string_destroy(&type_string);
                    node->type = sema->context->laye_types.poison;
                } break;

                case LAYE_NODE_TYPE_ARRAY: {
                    if (arr_count(node->index.indices) != arr_count(value_type->type_container.length_values)) {
                        string type_string = string_create(sema->context->allocator);
                        laye_type_print_to_string(value_type, &type_string, sema->context->use_color);
                        layec_write_error(
                            sema->context,
                            node->location,
                            "Expected %lld indices to type %.*s, got %lld.",
                            arr_count(value_type->type_container.length_values),
                            STR_EXPAND(type_string),
                            arr_count(node->index.indices)
                        );
                        string_destroy(&type_string);
                    }

                    node->type = value_type->type_container.element_type;
                } break;
            }

            assert(node->type != NULL);
            assert(node->type->kind != LAYE_NODE_INVALID);

            bool is_lvalue = laye_node_is_lvalue(node->index.value);
            laye_expr_set_lvalue(node, is_lvalue);
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

        case LAYE_NODE_UNARY: {
            if (!laye_sema_analyse_node(sema, &node->unary.operand, NULL)) {
                node->sema_state = LAYEC_SEMA_ERRORED;
                node->type = sema->context->laye_types.poison;
                break;
            }

            switch (node->unary.operator.kind) {
                default: {
                    fprintf(stderr, "for token kind %s\n", laye_token_kind_to_cstring(node->unary.operator.kind));
                    assert(false && "unimplemented unary operator");
                } break;

                case '+':
                case '-': {
                    laye_sema_implicit_dereference(sema, &node->unary.operand);
                    laye_sema_lvalue_to_rvalue(sema, &node->unary.operand, true);

                    if (
                        !laye_type_is_int(node->unary.operand->type) &&
                        !laye_type_is_float(node->unary.operand->type) &&
                        !laye_type_is_buffer(node->unary.operand->type)
                    ) {
                        layec_write_error(sema->context, node->location, "Expression must have an arithmetic type.");
                        node->sema_state = LAYEC_SEMA_ERRORED;
                        node->type = sema->context->laye_types.poison;
                        break;
                    }

                    node->type = node->unary.operand->type;
                } break;

                case '~': {
                    laye_sema_implicit_dereference(sema, &node->unary.operand);
                    laye_sema_lvalue_to_rvalue(sema, &node->unary.operand, true);

                    if (!laye_type_is_int(node->unary.operand->type)) {
                        layec_write_error(sema->context, node->location, "Expression must have an integer type.");
                        node->sema_state = LAYEC_SEMA_ERRORED;
                        node->type = sema->context->laye_types.poison;
                        break;
                    }

                    node->type = node->unary.operand->type;
                } break;

                case '&': {
                    if (!laye_expr_is_lvalue(node->unary.operand)) {
                        layec_write_error(sema->context, node->location, "Cannot take the address of a non-lvalue expression.");
                        node->sema_state = LAYEC_SEMA_ERRORED;
                        node->type = sema->context->laye_types.poison;
                        break;
                    }

                    assert(node->type->kind == LAYE_NODE_TYPE_POINTER); // from the parser, already constructed
                    node->type->type_container.element_type = node->unary.operand->type;
                    node->type->type_is_modifiable = true;
                } break;

                case '*': {
                    laye_sema_lvalue_to_rvalue(sema, &node->unary.operand, true);

                    laye_node* value_type_noref = laye_type_strip_references(node->unary.operand->type);
                    assert(value_type_noref != NULL);
                    if (!laye_type_is_pointer(value_type_noref)) {
                        goto cannot_dereference_type;
                    }

                    if (!laye_sema_convert(sema, &node->unary.operand, value_type_noref)) {
                        node->sema_state = LAYEC_SEMA_ERRORED;
                        node->type = sema->context->laye_types.poison;
                        break;
                    }

                    laye_node* pointer_type = value_type_noref;
                    if (pointer_type->type_container.element_type->kind == LAYE_NODE_TYPE_VOID || pointer_type->type_container.element_type->kind == LAYE_NODE_TYPE_NORETURN) {
                        goto cannot_dereference_type;
                    }

                    node->type = pointer_type->type_container.element_type;
                    laye_expr_set_lvalue(node, true);
                    break;

                cannot_dereference_type:;
                    string type_string = string_create(default_allocator);
                    laye_type_print_to_string(node->unary.operand->type, &type_string, sema->context->use_color);
                    layec_write_error(sema->context, node->location, "Cannot dereference type %.*s.", STR_EXPAND(type_string));
                    string_destroy(&type_string);
                    node->sema_state = LAYEC_SEMA_ERRORED;
                    node->type = sema->context->laye_types.poison;
                } break;
            }
        } break;

        case LAYE_NODE_BINARY: {
            if (!laye_sema_analyse_node(sema, &node->binary.lhs, NULL) || !laye_sema_analyse_node(sema, &node->binary.rhs, NULL)) {
                node->sema_state = LAYEC_SEMA_ERRORED;
                node->type = sema->context->laye_types.poison;
                break;
            }

            switch (node->binary.operator.kind) {
                default: {
                    fprintf(stderr, "for token kind %s\n", laye_token_kind_to_cstring(node->binary.operator.kind));
                    assert(false && "unhandled binary operator");
                } break;

                case LAYE_TOKEN_AND:
                case LAYE_TOKEN_OR:
                case LAYE_TOKEN_XOR: {
                    node->type = sema->context->laye_types._bool;

                    laye_sema_implicit_dereference(sema, &node->binary.lhs);
                    laye_sema_implicit_dereference(sema, &node->binary.rhs);

                    laye_sema_lvalue_to_rvalue(sema, &node->binary.lhs, true);
                    laye_sema_lvalue_to_rvalue(sema, &node->binary.rhs, true);

                    laye_sema_convert_or_error(sema, &node->binary.lhs, sema->context->laye_types._bool);
                    laye_sema_convert_or_error(sema, &node->binary.rhs, sema->context->laye_types._bool);
                } break;

                case LAYE_TOKEN_PLUS:
                case LAYE_TOKEN_MINUS:
                case LAYE_TOKEN_STAR:
                case LAYE_TOKEN_SLASH:
                case LAYE_TOKEN_PERCENT:
                case LAYE_TOKEN_AMPERSAND:
                case LAYE_TOKEN_PIPE:
                case LAYE_TOKEN_TILDE:
                case LAYE_TOKEN_LESSLESS:
                case LAYE_TOKEN_GREATERGREATER: {
                    // clang-format off
                    bool is_bitwise_operation =
                        node->binary.operator.kind == LAYE_TOKEN_AMPERSAND ||
                        node->binary.operator.kind == LAYE_TOKEN_PIPE ||
                        node->binary.operator.kind == LAYE_TOKEN_TILDE ||
                        node->binary.operator.kind == LAYE_TOKEN_LESSLESS ||
                        node->binary.operator.kind == LAYE_TOKEN_GREATERGREATER;
                    // clang-format on

                    laye_sema_implicit_dereference(sema, &node->binary.lhs);
                    laye_sema_implicit_dereference(sema, &node->binary.rhs);

                    laye_sema_lvalue_to_rvalue(sema, &node->binary.lhs, true);
                    laye_sema_lvalue_to_rvalue(sema, &node->binary.rhs, true);

                    laye_node* lhs_type = node->binary.lhs->type;
                    assert(lhs_type != NULL);
                    laye_node* rhs_type = node->binary.rhs->type;
                    assert(rhs_type != NULL);

                    if (laye_type_is_int(lhs_type) && laye_type_is_int(rhs_type)) {
                        if (!laye_sema_convert_to_common_type(sema, &node->binary.lhs, &node->binary.rhs)) {
                            goto cannot_arith_types;
                        }

                        node->type = node->binary.lhs->type;
                    } else if (laye_type_is_float(lhs_type) && laye_type_is_float(rhs_type)) {
                        if (is_bitwise_operation || !laye_sema_convert_to_common_type(sema, &node->binary.lhs, &node->binary.rhs)) {
                            goto cannot_arith_types;
                        }

                        node->type = node->binary.lhs->type;
                    } else {
                        // TODO(local): pointer arith
                        goto cannot_arith_types;
                    }

                    assert(node->type->kind != LAYE_NODE_TYPE_UNKNOWN);
                    break;

                cannot_arith_types:;
                    node->sema_state = LAYEC_SEMA_ERRORED;
                    node->type = sema->context->laye_types.poison;

                    string lhs_type_string = string_create(default_allocator);
                    string rhs_type_string = string_create(default_allocator);

                    laye_type_print_to_string(lhs_type, &lhs_type_string, sema->context->use_color);
                    laye_type_print_to_string(rhs_type, &rhs_type_string, sema->context->use_color);

                    layec_write_error(
                        sema->context,
                        node->location,
                        "Cannot perform arithmetic on %.*s and %.*s.",
                        STR_EXPAND(lhs_type_string),
                        STR_EXPAND(rhs_type_string)
                    );

                    string_destroy(&rhs_type_string);
                    string_destroy(&lhs_type_string);
                } break;

                case LAYE_TOKEN_EQUALEQUAL:
                case LAYE_TOKEN_BANGEQUAL:
                case LAYE_TOKEN_LESS:
                case LAYE_TOKEN_LESSEQUAL:
                case LAYE_TOKEN_GREATER:
                case LAYE_TOKEN_GREATEREQUAL: {
                    bool is_equality_compare = node->binary.operator.kind == LAYE_TOKEN_EQUALEQUAL || node->binary.operator.kind == LAYE_TOKEN_BANGEQUAL;

                    node->type = sema->context->laye_types._bool;

                    laye_sema_implicit_dereference(sema, &node->binary.lhs);
                    laye_sema_implicit_dereference(sema, &node->binary.rhs);

                    laye_sema_lvalue_to_rvalue(sema, &node->binary.lhs, true);
                    laye_sema_lvalue_to_rvalue(sema, &node->binary.rhs, true);

                    laye_node* lhs_type = node->binary.lhs->type;
                    assert(lhs_type != NULL);
                    laye_node* rhs_type = node->binary.rhs->type;
                    assert(rhs_type != NULL);

                    if (laye_type_is_int(lhs_type) && laye_type_is_int(rhs_type)) {
                        if (!laye_sema_convert_to_common_type(sema, &node->binary.lhs, &node->binary.rhs)) {
                            goto cannot_compare_types;
                        }
                    } else if (laye_type_is_float(lhs_type) && laye_type_is_float(rhs_type)) {
                        if (!laye_sema_convert_to_common_type(sema, &node->binary.lhs, &node->binary.rhs)) {
                            goto cannot_compare_types;
                        }
                    } else if (laye_type_is_bool(lhs_type) && laye_type_is_bool(rhs_type)) {
                        // xyzzy;
                    } else if (laye_type_is_pointer(lhs_type) && laye_type_is_pointer(rhs_type)) {
                        if (!is_equality_compare || !laye_type_equals(lhs_type->type_container.element_type, rhs_type->type_container.element_type, LAYE_MUT_IGNORE)) {
                            goto cannot_compare_types;
                        }
                    } else if (laye_type_is_buffer(lhs_type) && laye_type_is_buffer(rhs_type)) {
                        if (!laye_type_equals(lhs_type->type_container.element_type, rhs_type->type_container.element_type, LAYE_MUT_IGNORE)) {
                            goto cannot_compare_types;
                        }
                    } else {
                        goto cannot_compare_types;
                    }

                    break;

                cannot_compare_types:;
                    node->sema_state = LAYEC_SEMA_ERRORED;
                    node->type = sema->context->laye_types.poison;

                    string lhs_type_string = string_create(default_allocator);
                    string rhs_type_string = string_create(default_allocator);

                    laye_type_print_to_string(lhs_type, &lhs_type_string, sema->context->use_color);
                    laye_type_print_to_string(rhs_type, &rhs_type_string, sema->context->use_color);

                    layec_write_error(
                        sema->context,
                        node->location,
                        "Cannot compare %.*s and %.*s.",
                        STR_EXPAND(lhs_type_string),
                        STR_EXPAND(rhs_type_string)
                    );

                    string_destroy(&rhs_type_string);
                    string_destroy(&lhs_type_string);
                } break;
            }
        } break;

        case LAYE_NODE_LITBOOL: {
            // assert we populated this at parse time
            assert(node->type != NULL);
            if (expected_type != NULL) {
                laye_sema_convert_or_error(sema, node_ref, expected_type);
            }
            assert(laye_type_is_bool(node->type));
        } break;

        case LAYE_NODE_LITINT: {
            // assert we populated this at parse time
            assert(node->type != NULL);
            if (expected_type != NULL) {
                laye_sema_convert_or_error(sema, node_ref, expected_type);
            }
            assert(laye_type_is_int(node->type));
        } break;

        case LAYE_NODE_LITSTRING: {
            // assert we populated this at parse time
            assert(node->type != NULL);
            if (expected_type != NULL) {
                laye_sema_convert_or_error(sema, node_ref, expected_type);
            }
            assert(laye_type_is_buffer(node->type));
        } break;

        case LAYE_NODE_TYPE_NORETURN:
        case LAYE_NODE_TYPE_VOID:
        case LAYE_NODE_TYPE_BOOL:
        case LAYE_NODE_TYPE_INT:
        case LAYE_NODE_TYPE_FLOAT: {
        } break;

        case LAYE_NODE_TYPE_REFERENCE:
        case LAYE_NODE_TYPE_POINTER:
        case LAYE_NODE_TYPE_BUFFER: {
            if (!laye_sema_analyse_node(sema, &node->type_container.element_type, NULL)) {
                node->sema_state = LAYEC_SEMA_ERRORED;
            }
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

        case LAYE_NODE_TYPE_ARRAY: {
            if (!laye_sema_analyse_node(sema, &node->type_container.element_type, NULL)) {
                node->sema_state = LAYEC_SEMA_ERRORED;
            }

            for (int64_t i = 0, count = arr_count(node->type_container.length_values); i < count; i++) {
                if (!laye_sema_analyse_node(sema, &node->type_container.length_values[i], NULL)) {
                    node->sema_state = LAYEC_SEMA_ERRORED;
                    continue;
                }

                layec_evaluated_constant constant_value = {0};
                if (!laye_expr_evaluate(node->type_container.length_values[i], &constant_value, true)) {
                    layec_write_error(sema->context, node->type_container.length_values[i]->location, "Array length value must be a compile-time known integer value. This expression was unable to be evaluated at compile time.");
                    node->sema_state = LAYEC_SEMA_ERRORED;
                    continue;
                }

                if (constant_value.kind != LAYEC_EVAL_INT) {
                    layec_write_error(sema->context, node->type_container.length_values[i]->location, "Array length value must be a compile-time known integer value. This expression did not evaluate to an integer.");
                    node->sema_state = LAYEC_SEMA_ERRORED;
                    continue;
                }

                laye_node* evaluated_constant = laye_node_create(node->module, LAYE_NODE_EVALUATED_CONSTANT, node->type_container.length_values[i]->location, sema->context->laye_types._int);
                assert(evaluated_constant != NULL);
                evaluated_constant->evaluated_constant.expr = node->type_container.length_values[i];
                evaluated_constant->evaluated_constant.result = constant_value;

                node->type_container.length_values[i] = evaluated_constant;
            }
        } break;
    }

    assert(node != NULL);
    if (expected_type != NULL && node->kind != LAYE_NODE_YIELD) {
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

static laye_node* laye_create_constant_node(laye_sema* sema, laye_node* node, layec_evaluated_constant eval_result) {
    assert(sema != NULL);
    assert(node != NULL);

    laye_node* constant_node = laye_node_create(node->module, LAYE_NODE_EVALUATED_CONSTANT, node->location, node->type);
    assert(constant_node != NULL);
    constant_node->compiler_generated = true;
    constant_node->evaluated_constant.expr = node;
    constant_node->evaluated_constant.result = eval_result;

    laye_sema_analyse_node(sema, &constant_node, node->type);
    return constant_node;
}

static int laye_sema_convert_impl(laye_sema* sema, laye_node** node_ref, laye_node* to, bool perform_conversion) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node_ref != NULL);
    assert(to != NULL);
    assert(laye_node_is_type(to));
    layec_context* context = sema->context;

    laye_node* node = *node_ref;
    assert(node != NULL);

    laye_node* from = (*node_ref)->type;
    assert(from != NULL);
    assert(laye_node_is_type(from));

    if (from->sema_state == LAYEC_SEMA_ERRORED || to->sema_state == LAYEC_SEMA_ERRORED || to->kind == LAYE_NODE_TYPE_POISON) {
        return LAYE_CONVERT_CONTAINS_ERRORS;
    }

    assert(from->sema_state == LAYEC_SEMA_OK);
    assert(to->sema_state == LAYEC_SEMA_OK);

#if 0
    if (perform_conversion) {
        laye_sema_lvalue_to_rvalue(sema, node, false);
    }
#endif

    if (perform_conversion) {
        laye_sema_lvalue_to_rvalue(sema, node_ref, false);
        from = node->type;
    }

    if (laye_type_equals(from, to, LAYE_MUT_CONVERTIBLE)) {
        return LAYE_CONVERT_NOOP;
    }

    int score = 0;
    if (laye_expr_is_lvalue(node)) {
        score = 1;
    }

    if (laye_type_is_reference(from) && laye_type_is_reference(to)) {
        if (laye_type_equals(from, to, LAYE_MUT_CONVERTIBLE)) {
            return LAYE_CONVERT_NOOP;
        }

        if (laye_type_equals(from->type_container.element_type, to->type_container.element_type, LAYE_MUT_CONVERTIBLE)) {
            if (from->type_is_modifiable == to->type_is_modifiable || !to->type_is_modifiable)
                return LAYE_CONVERT_NOOP;
        }

        // TODO(local): struct variants, arrays->element

        return LAYE_CONVERT_IMPOSSIBLE;
    }

    if (perform_conversion) {
        laye_sema_lvalue_to_rvalue(sema, node_ref, false);
        from = node->type;
    } else {
        from = laye_type_strip_references(node->type);
    }

    if (laye_type_equals(from, to, LAYE_MUT_CONVERTIBLE)) {
        return LAYE_CONVERT_NOOP;
    }

    if (laye_type_is_pointer(from) && laye_type_is_reference(to)) {
        if (laye_type_equals(from->type_container.element_type, to->type_container.element_type, LAYE_MUT_CONVERTIBLE)) {
            if (from->type_is_modifiable == to->type_is_modifiable || !to->type_is_modifiable)
                return LAYE_CONVERT_NOOP;
        }

        return LAYE_CONVERT_IMPOSSIBLE;
    }

    if (laye_type_is_pointer(from) && laye_type_is_pointer(to)) {
        if (laye_type_equals(from->type_container.element_type, to->type_container.element_type, LAYE_MUT_CONVERTIBLE)) {
            if (from->type_is_modifiable == to->type_is_modifiable || !to->type_is_modifiable)
                return LAYE_CONVERT_NOOP;
        }

        return LAYE_CONVERT_IMPOSSIBLE;
    }

    int to_size = laye_type_size_in_bits(to);

    layec_evaluated_constant eval_result = {0};
    if (laye_expr_evaluate(node, &eval_result, false)) {
        if (eval_result.kind == LAYEC_EVAL_INT) {
            int sig_bits = layec_get_significant_bits(eval_result.int_value);
            if (sig_bits <= to_size) {
                if (perform_conversion) {
                    laye_sema_insert_implicit_cast(sema, node_ref, to);
                    *node_ref = laye_create_constant_node(sema, *node_ref, eval_result);
                }

                return 1 + score;
            }
        } else if (eval_result.kind == LAYEC_EVAL_STRING) {
        }
    }

    if (laye_type_is_int(from) && laye_type_is_int(to)) {
        int from_size = laye_type_size_in_bits(from);

        if (from_size <= to_size) {
            if (perform_conversion) {
                laye_sema_insert_implicit_cast(sema, node_ref, to);
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

    return laye_sema_convert_impl(sema, node, to, true) >= 0;
}

static void laye_sema_convert_or_error(laye_sema* sema, laye_node** node, laye_node* to) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node != NULL);
    assert(*node != NULL);
    assert((*node)->type != NULL);
    assert(laye_node_is_type((*node)->type));
    assert(to != NULL);
    assert(laye_node_is_type(to));

    if (!laye_sema_convert(sema, node, to)) {
        string from_type_string = string_create(sema->context->allocator);
        laye_type_print_to_string((*node)->type, &from_type_string, sema->context->use_color);

        string to_type_string = string_create(sema->context->allocator);
        laye_type_print_to_string(to, &to_type_string, sema->context->use_color);

        (*node)->sema_state = LAYEC_SEMA_ERRORED;
        layec_write_error(
            sema->context,
            (*node)->location,
            "Expression of type %.*s is not convertible to %.*s",
            STR_EXPAND(from_type_string),
            STR_EXPAND(to_type_string)
        );

        string_destroy(&to_type_string);
        string_destroy(&from_type_string);
    }
}

static void laye_sema_convert_to_c_varargs_or_error(laye_sema* sema, laye_node** node) {
    assert(sema != NULL);
    assert(sema->context != NULL);

    laye_node* varargs_type = NULL;
    laye_sema_lvalue_to_rvalue(sema, node, true);

    int type_size = laye_type_size_in_bits((*node)->type);

    if (laye_type_is_int((*node)->type)) {
        if (type_size < sema->context->target->c.size_of_int) {
            laye_node* ffi_int_type = laye_node_create((*node)->module, LAYE_NODE_TYPE_INT, (*node)->location, sema->context->laye_types.type);
            assert(ffi_int_type != NULL);
            ffi_int_type->type_primitive.is_signed = (*node)->type->type_primitive.is_signed;
            ffi_int_type->type_primitive.bit_width = sema->context->target->c.size_of_int;
            laye_sema_insert_implicit_cast(sema, node, ffi_int_type);
            laye_sema_analyse_node(sema, node, NULL);
            return;
        }
    }

    if (type_size <= sema->context->target->size_of_pointer) {
        return; // fine
    }

    string type_string = string_create(default_allocator);
    laye_type_print_to_string((*node)->type, &type_string, sema->context->use_color);
    layec_write_error(sema->context, (*node)->location, "Cannot convert type %.*s to a type correct for C varargs.", STR_EXPAND(type_string));
    string_destroy(&type_string);

    (*node)->sema_state = LAYEC_SEMA_ERRORED;
    (*node)->type = sema->context->laye_types.poison;
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

    if (laye_sema_try_convert(sema, a, (*b)->type) >= 0) {
        return laye_sema_convert(sema, a, (*b)->type);
    }

    return laye_sema_convert(sema, b, (*a)->type);
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

static laye_node* laye_sema_get_pointer_to_type(laye_sema* sema, laye_node* element_type, bool is_modifiable) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(element_type != NULL);
    assert(element_type->module != NULL);
    assert(laye_node_is_type(element_type));
    laye_node* type = laye_node_create(element_type->module, LAYE_NODE_TYPE_POINTER, element_type->location, sema->context->laye_types.type);
    assert(type != NULL);
    type->type_container.element_type = element_type;
    return type;
}

static laye_node* laye_sema_get_buffer_of_type(laye_sema* sema, laye_node* element_type, bool is_modifiable) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(element_type != NULL);
    assert(element_type->module != NULL);
    assert(laye_node_is_type(element_type));
    laye_node* type = laye_node_create(element_type->module, LAYE_NODE_TYPE_BUFFER, element_type->location, sema->context->laye_types.type);
    assert(type != NULL);
    type->type_container.element_type = element_type;
    return type;
}

static laye_node* laye_sema_get_reference_to_type(laye_sema* sema, laye_node* element_type, bool is_modifiable) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(element_type != NULL);
    assert(element_type->module != NULL);
    assert(laye_node_is_type(element_type));
    laye_node* type = laye_node_create(element_type->module, LAYE_NODE_TYPE_REFERENCE, element_type->location, sema->context->laye_types.type);
    assert(type != NULL);
    type->type_container.element_type = element_type;
    return type;
}
