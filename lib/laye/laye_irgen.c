#include "laye.h"
#include "layec.h"

#include <assert.h>

static layec_type* laye_convert_type(laye_type type);
static layec_value* laye_generate_node(layec_builder* builder, laye_node* node);

layec_module* laye_irgen(laye_module* module) {
    assert(module != NULL);
    assert(module->context != NULL);

    layec_source source = layec_context_get_source(module->context, module->sourceid);

    layec_module* ir_module = layec_module_create(module->context, string_as_view(source.name));
    assert(ir_module != NULL);

    // 1. Top-level type generation

    // 2. Top-level function generation
    // TODO(local): generate imports from other modules.
    for (int64_t i = 0, count = arr_count(module->top_level_nodes); i < count; i++) {
        laye_node* top_level_node = module->top_level_nodes[i];
        assert(top_level_node != NULL);

        if (top_level_node->kind == LAYE_NODE_DECL_FUNCTION) {
            string_view function_name = top_level_node->attributes.foreign_name.count != 0 ? top_level_node->attributes.foreign_name : top_level_node->declared_name;

            assert(laye_type_is_function(top_level_node->declared_type));
            layec_type* ir_function_type = laye_convert_type(top_level_node->declared_type);
            assert(ir_function_type != NULL);
            assert(layec_type_is_function(ir_function_type));

            layec_linkage function_linkage;
            if (top_level_node->decl_function.body == NULL) {
                if (top_level_node->attributes.linkage == LAYEC_LINK_EXPORTED) {
                    function_linkage = LAYEC_LINK_REEXPORTED;
                } else {
                    function_linkage = LAYEC_LINK_IMPORTED;
                }
            } else {
                if (top_level_node->attributes.linkage == LAYEC_LINK_EXPORTED) {
                    function_linkage = LAYEC_LINK_EXPORTED;
                } else {
                    function_linkage = LAYEC_LINK_INTERNAL;
                }
            }

            layec_value* ir_function = layec_module_create_function(
                ir_module,
                top_level_node->location,
                function_name,
                ir_function_type,
                function_linkage
            );
            assert(ir_function != NULL);
            top_level_node->ir_value = ir_function;
        }
    }

    // 3. Generate function bodies
    layec_builder* builder = layec_builder_create(module->context);

    for (int64_t i = 0, count = arr_count(module->top_level_nodes); i < count; i++) {
        laye_node* top_level_node = module->top_level_nodes[i];
        assert(top_level_node != NULL);

        if (top_level_node->kind == LAYE_NODE_DECL_FUNCTION) {
            layec_value* function = top_level_node->ir_value;
            assert(function != NULL);
            assert(layec_value_is_function(function));

            if (top_level_node->decl_function.body == NULL) {
                continue;
            }

            layec_value* entry_block = layec_function_append_block(function, SV_CONSTANT("entry"));
            assert(entry_block != NULL);
            layec_builder_position_at_end(builder, entry_block);

            // generate the function body
            laye_generate_node(builder, top_level_node->decl_function.body);

            layec_builder_reset(builder);
        }
    }

    layec_builder_destroy(builder);

    return ir_module;
}

static layec_type* laye_convert_type(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    // laye_module* module = type->module;
    // assert(module != NULL);
    layec_context* context = type.node->context;
    assert(context != NULL);

    switch (type.node->kind) {
        default: {
            fprintf(stderr, "for node kind %s\n", laye_node_kind_to_cstring(type.node->kind));
            assert(false && "unimplemented type kind in laye_convert_type");
            return NULL;
        }

        case LAYE_NODE_TYPE_VOID:
        case LAYE_NODE_TYPE_NORETURN: {
            return layec_void_type(context);
        }

        case LAYE_NODE_TYPE_BOOL: {
            return layec_int_type(context, 1);
        }

        case LAYE_NODE_TYPE_INT: {
            return layec_int_type(context, type.node->type_primitive.bit_width);
        }

        case LAYE_NODE_TYPE_FUNCTION: {
            assert(type.node->type_function.return_type.node != NULL);
            layec_type* return_type = laye_convert_type(type.node->type_function.return_type);
            assert(return_type != NULL);

            dynarr(layec_type*) parameter_types = NULL;
            for (int64_t i = 0, count = arr_count(type.node->type_function.parameter_types); i < count; i++) {
                laye_type pt = type.node->type_function.parameter_types[i];
                assert(pt.node != NULL);

                layec_type* parameter_type = laye_convert_type(pt);
                assert(parameter_type != NULL);

                arr_push(parameter_types, parameter_type);
            }

            layec_calling_convention calling_convention = type.node->type_function.calling_convention;
            assert(calling_convention != LAYEC_DEFAULTCC);

            assert(arr_count(parameter_types) == arr_count(type.node->type_function.parameter_types));
            return layec_function_type(context, return_type, parameter_types, calling_convention, type.node->type_function.varargs_style == LAYE_VARARGS_C);
        }

        case LAYE_NODE_TYPE_POINTER:
        case LAYE_NODE_TYPE_BUFFER: {
            return layec_ptr_type(context);
        }

        case LAYE_NODE_TYPE_ARRAY: {
            layec_type* element_type = laye_convert_type(type.node->type_container.element_type);
            assert(element_type != NULL);

            int64_t length = 1;
            assert(arr_count(type.node->type_container.length_values) > 0);
            for (int64_t i = 0, count = arr_count(type.node->type_container.length_values); i < count; i++) {
                laye_node* length_value = type.node->type_container.length_values[i];
                assert(length_value != NULL);
                assert(length_value->kind == LAYE_NODE_EVALUATED_CONSTANT);
                assert(length_value->evaluated_constant.result.kind == LAYEC_EVAL_INT);
                length *= length_value->evaluated_constant.result.int_value;
            }

            return layec_array_type(context, length, element_type);
        }

        case LAYE_NODE_TYPE_STRUCT: {
            for (int64_t i = 0, count = arr_count(context->_all_struct_types); i < count; i++) {
                if (context->_all_struct_types[i].node == type.node) {
                    return context->_all_struct_types[i].type;
                }
            }

            int64_t field_count = arr_count(type.node->type_struct.fields);
            dynarr(layec_type*) fields = NULL;
            arr_set_count(fields, field_count);

            for (int64_t i = 0; i < field_count; i++) {
                fields[i] = laye_convert_type(type.node->type_struct.fields[i].type);
            }

            layec_type* struct_type = layec_struct_type(context, type.node->type_struct.name, fields);
            assert(struct_type != NULL);

            struct cached_struct_type t = {
                .node = type.node,
                .type = struct_type,
            };
            arr_push(context->_all_struct_types, t);

            return struct_type;
        }
    }
}

static layec_value* laye_generate_node(layec_builder* builder, laye_node* node) {
    assert(builder != NULL);
    assert(node != NULL);

    layec_module* module = layec_builder_get_module(builder);
    assert(module != NULL);

    layec_context* context = layec_builder_get_context(builder);
    assert(context != NULL);

    layec_value* function = layec_builder_get_function(builder);

    switch (node->kind) {
        default: {
            fprintf(stderr, "for node kind %s\n", laye_node_kind_to_cstring(node->kind));
            assert(false && "unimplemented Laye node in laye_generate_node");
            return NULL;
        }

        case LAYE_NODE_DECL_BINDING: {
            layec_type* type_to_alloca = laye_convert_type(node->declared_type);
            int64_t element_count = 1;

            layec_value* alloca = layec_build_alloca(builder, node->location, type_to_alloca, element_count);
            assert(alloca != NULL);
            assert(layec_type_is_ptr(layec_value_get_type(alloca)));

            if (node->decl_binding.initializer != NULL) {
                layec_value* initial_value = laye_generate_node(builder, node->decl_binding.initializer);
                assert(initial_value != NULL);

                layec_build_store(builder, node->location, alloca, initial_value);
            } else {
                int64_t size_in_bytes = layec_type_size_in_bytes(type_to_alloca);
                layec_value* zero_const = layec_int_constant(context, node->location, layec_int_type(context, 8), 0);
                layec_value* byte_count = layec_int_constant(context, node->location, layec_int_type(context, context->target->laye.size_of_int), size_in_bytes);
                layec_build_builtin_memset(builder, node->location, alloca, zero_const, byte_count);
            }

            node->ir_value = alloca;
            return layec_void_constant(context);
        }

        case LAYE_NODE_IF: {
            bool is_expr = !(laye_type_is_void(node->type) || laye_type_is_noreturn(node->type));

            dynarr(layec_value*) pass_blocks = NULL;
            dynarr(layec_value*) condition_blocks = NULL;
            layec_value* fail_block = NULL;
            layec_value* continue_block = NULL;

            layec_value* phi_value = NULL;

            assert(arr_count(node->_if.conditions) == arr_count(node->_if.passes));
            for (int64_t i = 0, count = arr_count(node->_if.conditions); i < count; i++) {
                if (i > 0) {
                    layec_value* cond_block = layec_function_append_block(function, SV_EMPTY);
                    assert(cond_block != NULL);
                    arr_push(condition_blocks, cond_block);
                }

                layec_value* block = layec_function_append_block(function, SV_EMPTY);
                assert(block != NULL);
                arr_push(pass_blocks, block);
            }
            assert(arr_count(node->_if.conditions) == arr_count(pass_blocks));
            assert(arr_count(node->_if.conditions) - 1 == arr_count(condition_blocks));

            if (node->_if.fail != NULL) {
                fail_block = layec_function_append_block(function, SV_EMPTY);
                assert(fail_block != NULL);
            }

            if (!laye_type_is_noreturn(node->type)) {
                continue_block = layec_function_append_block(function, SV_EMPTY);
                assert(continue_block != NULL);
            }

            if (is_expr) {
                layec_value* phi_block = continue_block;
                if (phi_block == NULL) {
                    phi_block = fail_block;
                }

                assert(phi_block != NULL);

                layec_value* current_block = layec_builder_get_insert_block(builder);
                assert(current_block != NULL);

                layec_builder_position_at_end(builder, phi_block);
                phi_value = layec_build_phi(builder, node->location, laye_convert_type(node->type));

                layec_builder_position_at_end(builder, current_block);
            }

            for (int64_t i = 0, count = arr_count(node->_if.conditions); i < count; i++) {
                if (i > 0) {
                    layec_builder_position_at_end(builder, condition_blocks[i - 1]);
                }

                layec_value* condition_value = laye_generate_node(builder, node->_if.conditions[i]);
                assert(condition_value != NULL);

                layec_type* condition_type = layec_value_get_type(condition_value);
                assert(condition_type != NULL);
                assert(layec_type_is_integer(condition_type));

                layec_value* block = pass_blocks[i];
                assert(block != NULL);

                layec_value* else_block = NULL;
                if (i + 1 < count) {
                    else_block = condition_blocks[i];
                } else if (fail_block != NULL) {
                    else_block = fail_block;
                } else {
                    else_block = continue_block;
                }

                assert(else_block != NULL);
                layec_build_branch_conditional(builder, node->_if.conditions[i]->location, condition_value, block, else_block);

                layec_builder_position_at_end(builder, block);
                layec_value* pass_value = laye_generate_node(builder, node->_if.passes[i]);
                assert(pass_value != NULL);
                layec_value* from_block = layec_builder_get_insert_block(builder);
                assert(from_block != NULL);

                if (!laye_type_is_noreturn(node->_if.passes[i]->type)) {
                    assert(continue_block != NULL);
                    if (!layec_block_is_terminated(layec_builder_get_insert_block(builder))) {
                        layec_build_branch(builder, node->_if.passes[i]->location, continue_block);
                    }
                }

                if (is_expr) {
                    assert(phi_value != NULL);
                    layec_phi_add_incoming_value(phi_value, pass_value, from_block);
                }
            }

            if (is_expr) {
                assert(phi_value != NULL);
                assert(layec_phi_incoming_value_count(phi_value) == arr_count(pass_blocks));
            }

            if (fail_block != NULL) {
                assert(node->_if.fail != NULL);
                layec_builder_position_at_end(builder, fail_block);
                layec_value* fail_value = laye_generate_node(builder, node->_if.fail);
                assert(fail_value != NULL);
                layec_value* from_block = layec_builder_get_insert_block(builder);
                assert(from_block != NULL);

                if (!laye_type_is_noreturn(node->_if.fail->type)) {
                    assert(continue_block != NULL);
                    if (!layec_block_is_terminated(layec_builder_get_insert_block(builder))) {
                        layec_build_branch(builder, node->_if.fail->location, continue_block);
                    }
                }

                if (is_expr) {
                    assert(phi_value != NULL);
                    layec_phi_add_incoming_value(phi_value, fail_value, from_block);
                    assert(layec_phi_incoming_value_count(phi_value) == arr_count(pass_blocks) + 1);
                }
            }

            arr_free(condition_blocks);
            arr_free(pass_blocks);

            if (continue_block != NULL) {
                layec_builder_position_at_end(builder, continue_block);
            }

            layec_value* result_value = NULL;
            if (is_expr) {
                result_value = phi_value;
            } else {
                result_value = layec_void_constant(context);
            }

            return result_value;
        }

        case LAYE_NODE_FOR: {
            assert(node->_for.pass != NULL);

            bool has_breaks = node->_for.has_breaks;
            bool has_continues = node->_for.has_continues;

            bool has_initializer = node->_for.initializer != NULL && node->_for.initializer->kind != LAYE_NODE_XYZZY;
            bool has_increment = node->_for.increment != NULL && node->_for.increment->kind != LAYE_NODE_XYZZY;
            bool has_always_true_condition = !has_breaks && node->_for.condition == NULL || (node->_for.condition->kind == LAYE_NODE_EVALUATED_CONSTANT && node->_for.condition->evaluated_constant.result.bool_value);

            bool requires_join_block = has_breaks || !laye_type_is_noreturn(node->type);
            
            #if 0
            bool is_infinite_for = node->_for.initializer == NULL && node->_for.condition == NULL && node->_for.increment == NULL;
            bool is_effectively_infinite_for = !has_initializer && has_always_true_condition && !has_increment && node->_for.fail == NULL;

            if (is_infinite_for || is_effectively_infinite_for) {
                assert(node->_for.fail == NULL);

                layec_value* body_block = layec_function_append_block(function, SV_EMPTY);
                assert(body_block != NULL);

                layec_build_branch(builder, node->location, body_block);
                layec_builder_position_at_end(builder, body_block);
                laye_generate_node(builder, node->_for.pass);

                if (!laye_type_is_noreturn(node->_for.pass->type)) {
                    assert(!layec_block_is_terminated(layec_builder_get_insert_block(builder)));
                    layec_build_branch(builder, node->location, body_block);
                }

                assert(layec_block_is_terminated(layec_builder_get_insert_block(builder)));
                return layec_void_constant(context);
            }
            #endif

            // 1. handle the initializer
            if (has_initializer) {
                laye_generate_node(builder, node->_for.initializer);

                if (laye_type_is_noreturn(node->_for.initializer->type)) {
                    assert(layec_block_is_terminated(layec_builder_get_insert_block(builder)));
                    return layec_void_constant(context);
                }
            }

            layec_value* for_early_condition_block = NULL;
            if (!has_always_true_condition && node->_for.fail != NULL) {
                for_early_condition_block = layec_function_append_block(function, SV_EMPTY);
                assert(for_early_condition_block != NULL);
            }

            layec_value* for_condition_block = NULL;
            if (!has_always_true_condition) {
                for_condition_block = layec_function_append_block(function, SV_EMPTY);
                assert(for_condition_block != NULL);
            }

            layec_value* for_pass_block = layec_function_append_block(function, SV_EMPTY);
            assert(for_pass_block != NULL);
            
            layec_value* for_increment_block = NULL;
            if (has_increment && !laye_type_is_noreturn(node->_for.pass->type)) {
                for_increment_block = layec_function_append_block(function, SV_EMPTY);
                assert(for_increment_block != NULL);
            }

            layec_value* for_fail_block = NULL;
            if (!has_always_true_condition && node->_for.fail != NULL) {
                for_fail_block = layec_function_append_block(function, SV_EMPTY);
                assert(for_fail_block != NULL);
            }

            layec_value* for_join_block = NULL;
            if (requires_join_block) {
                for_join_block = layec_function_append_block(function, SV_EMPTY);
                assert(for_join_block != NULL);
            }

            // 1.5. assign the correct blocks to the syntax nodes for later lookup by break/continue
            if (node->_for.has_continues) {
                if (for_increment_block != NULL) {
                    node->_for.continue_target_block = for_increment_block;
                } else if (for_condition_block != NULL) {
                    node->_for.continue_target_block = for_condition_block;
                } else {
                    node->_for.continue_target_block = for_pass_block;
                }
                
                assert(node->_for.continue_target_block != NULL);
            }

            if (node->_for.has_breaks) {
                node->_for.break_target_block = for_join_block;
                assert(node->_for.break_target_block != NULL);
            }

            // 2. early condition for branching to `else`
            if (!has_always_true_condition && node->_for.fail != NULL) {
                assert(for_early_condition_block != NULL);
                assert(for_fail_block != NULL);

                layec_build_branch(builder, node->_for.condition->location, for_early_condition_block);
                layec_builder_position_at_end(builder, for_early_condition_block);

                layec_value* early_condition_value = laye_generate_node(builder, node->_for.condition);
                assert(early_condition_value != NULL);

                if (laye_type_is_noreturn(node->_for.condition->type)) {
                    assert(layec_block_is_terminated(layec_builder_get_insert_block(builder)));
                    return layec_void_constant(context);
                }

                layec_build_branch_conditional(builder, node->_for.condition->location, early_condition_value, for_pass_block, for_fail_block);
            }

            // 3. regular condition for looping
            if (has_always_true_condition) {
                //assert(node->_for.condition != NULL && "condition == NULL should be an infinite loop, not a C style while/for loop");
                layec_location condition_location = node->_for.condition != NULL ? node->_for.condition->location : node->location;
                layec_build_branch(builder, condition_location, for_pass_block);
            } else {
                assert(for_condition_block != NULL);

                layec_build_branch(builder, node->_for.condition->location, for_condition_block);
                layec_builder_position_at_end(builder, for_condition_block);

                layec_value* condition_value = laye_generate_node(builder, node->_for.condition);
                assert(condition_value != NULL);

                if (!requires_join_block) {
                    assert(layec_block_is_terminated(layec_builder_get_insert_block(builder)));
                    return layec_void_constant(context);
                }

                layec_build_branch_conditional(builder, node->_for.condition->location, condition_value, for_pass_block, for_join_block);
            }

            // 4. handle incrementing and returning to the condition (or pass if the condition is always true)
            if (has_increment && !laye_type_is_noreturn(node->_for.pass->type)) {
                assert(for_increment_block != NULL);

                layec_builder_position_at_end(builder, for_increment_block);
                laye_generate_node(builder, node->_for.increment);

                if (laye_type_is_noreturn(node->_for.condition->type)) {
                    assert(layec_block_is_terminated(layec_builder_get_insert_block(builder)));
                } else {
                    if (has_always_true_condition) {
                        layec_build_branch(builder, node->_for.increment->location, for_pass_block);
                    } else {
                        layec_build_branch(builder, node->_for.increment->location, for_condition_block);
                    }
                }
            }

            // 5. generate the "pass" loop body
            layec_builder_position_at_end(builder, for_pass_block);
            laye_generate_node(builder, node->_for.pass);

            if (laye_type_is_noreturn(node->_for.pass->type)) {
                assert(layec_block_is_terminated(layec_builder_get_insert_block(builder)));
            } else {
                if (!layec_block_is_terminated(layec_builder_get_insert_block(builder))) {
                    if (for_increment_block != NULL) {
                        layec_build_branch(builder, node->_for.pass->location, for_increment_block);
                    } else {
                        if (has_always_true_condition) {
                            layec_build_branch(builder, node->_for.pass->location, for_pass_block);
                        } else {
                            layec_build_branch(builder, node->_for.pass->location, for_condition_block);
                        }
                    }
                }
            }

            // 6. generate the "fail" `else` body
            if (!has_always_true_condition && node->_for.fail != NULL) {
                assert(for_fail_block != NULL);

                layec_builder_position_at_end(builder, for_fail_block);
                laye_generate_node(builder, node->_for.fail);

                if (!requires_join_block) {
                    assert(layec_block_is_terminated(layec_builder_get_insert_block(builder)));
                } else {
                    layec_build_branch(builder, node->_for.fail->location, for_join_block);
                }
            }

            // 7. the loop is done, continue with the remaining code : )
            if (laye_type_is_noreturn(node->type)) {
                assert(layec_block_is_terminated(layec_builder_get_insert_block(builder)));
                return layec_void_constant(context);
            } else {
                assert(for_join_block != NULL);
                layec_builder_position_at_end(builder, for_join_block);
            }

            return layec_void_constant(context);
        }

        case LAYE_NODE_WHILE: {
            assert(node->_while.pass != NULL);

            bool has_breaks = node->_while.has_breaks;
            bool has_continues = node->_while.has_continues;

            bool has_always_true_condition = !has_breaks && node->_while.condition == NULL || (node->_while.condition->kind == LAYE_NODE_EVALUATED_CONSTANT && node->_while.condition->evaluated_constant.result.bool_value);

            bool requires_join_block = has_breaks || !laye_type_is_noreturn(node->type);

            layec_value* while_early_condition_block = NULL;
            if (!has_always_true_condition && node->_while.fail != NULL) {
                while_early_condition_block = layec_function_append_block(function, SV_EMPTY);
                assert(while_early_condition_block != NULL);
            }

            layec_value* while_condition_block = NULL;
            if (!has_always_true_condition) {
                while_condition_block = layec_function_append_block(function, SV_EMPTY);
                assert(while_condition_block != NULL);
            }

            layec_value* while_pass_block = layec_function_append_block(function, SV_EMPTY);
            assert(while_pass_block != NULL);

            layec_value* while_fail_block = NULL;
            if (!has_always_true_condition && node->_while.fail != NULL) {
                while_fail_block = layec_function_append_block(function, SV_EMPTY);
                assert(while_fail_block != NULL);
            }

            layec_value* while_join_block = NULL;
            if (requires_join_block) {
                while_join_block = layec_function_append_block(function, SV_EMPTY);
                assert(while_join_block != NULL);
            }

            // 1.5. assign the correct blocks to the syntax nodes for later lookup by break/continue
            if (node->_while.has_continues) {
                if (while_condition_block != NULL) {
                    node->_while.continue_target_block = while_condition_block;
                } else {
                    node->_while.continue_target_block = while_pass_block;
                }
                
                assert(node->_while.continue_target_block != NULL);
            }

            if (node->_while.has_breaks) {
                node->_while.break_target_block = while_join_block;
                assert(node->_while.break_target_block != NULL);
            }

            // 2. early condition for branching to `else`
            if (!has_always_true_condition && node->_while.fail != NULL) {
                assert(while_early_condition_block != NULL);
                assert(while_fail_block != NULL);

                layec_build_branch(builder, node->_while.condition->location, while_early_condition_block);
                layec_builder_position_at_end(builder, while_early_condition_block);

                layec_value* early_condition_value = laye_generate_node(builder, node->_while.condition);
                assert(early_condition_value != NULL);

                if (laye_type_is_noreturn(node->_while.condition->type)) {
                    assert(layec_block_is_terminated(layec_builder_get_insert_block(builder)));
                    return layec_void_constant(context);
                }

                layec_build_branch_conditional(builder, node->_while.condition->location, early_condition_value, while_pass_block, while_fail_block);
            }

            // 3. regular condition for looping
            if (has_always_true_condition) {
                //assert(node->_while.condition != NULL && "condition == NULL should be an infinite loop, not a C style while/for loop");
                layec_location condition_location = node->_while.condition != NULL ? node->_while.condition->location : node->location;
                layec_build_branch(builder, condition_location, while_pass_block);
            } else {
                assert(while_condition_block != NULL);

                layec_build_branch(builder, node->_while.condition->location, while_condition_block);
                layec_builder_position_at_end(builder, while_condition_block);

                layec_value* condition_value = laye_generate_node(builder, node->_while.condition);
                assert(condition_value != NULL);

                if (!requires_join_block) {
                    assert(layec_block_is_terminated(layec_builder_get_insert_block(builder)));
                    return layec_void_constant(context);
                }

                layec_build_branch_conditional(builder, node->_while.condition->location, condition_value, while_pass_block, while_join_block);
            }

            // 4. generate the "pass" loop body
            layec_builder_position_at_end(builder, while_pass_block);
            laye_generate_node(builder, node->_while.pass);

            if (laye_type_is_noreturn(node->_while.pass->type)) {
                assert(layec_block_is_terminated(layec_builder_get_insert_block(builder)));
            } else {
                if (!layec_block_is_terminated(layec_builder_get_insert_block(builder))) {
                    if (has_always_true_condition) {
                        layec_build_branch(builder, node->_while.pass->location, while_pass_block);
                    } else {
                        layec_build_branch(builder, node->_while.pass->location, while_condition_block);
                    }
                }
            }

            // 5. generate the "fail" `else` body
            if (!has_always_true_condition && node->_while.fail != NULL) {
                assert(while_fail_block != NULL);

                layec_builder_position_at_end(builder, while_fail_block);
                laye_generate_node(builder, node->_while.fail);

                if (!requires_join_block) {
                    assert(layec_block_is_terminated(layec_builder_get_insert_block(builder)));
                } else {
                    layec_build_branch(builder, node->_while.fail->location, while_join_block);
                }
            }

            // 6. the loop is done, continue with the remaining code : )
            if (laye_type_is_noreturn(node->type)) {
                assert(layec_block_is_terminated(layec_builder_get_insert_block(builder)));
                return layec_void_constant(context);
            } else {
                assert(while_join_block != NULL);
                layec_builder_position_at_end(builder, while_join_block);
            }

            return layec_void_constant(context);
        }

        case LAYE_NODE_RETURN: {
            if (node->_return.value == NULL) {
                return layec_build_return_void(builder, node->location);
            }

            layec_value* return_value = laye_generate_node(builder, node->_return.value);
            assert(return_value != NULL);
            return layec_build_return(builder, node->location, return_value);
        }

        case LAYE_NODE_YIELD: {
            layec_value* yield_value = laye_generate_node(builder, node->yield.value);
            assert(yield_value != NULL);
            return yield_value;
        }

        case LAYE_NODE_BREAK: {
            assert(node->_break.target_node != NULL);
            switch (node->_break.target_node->kind) {
                default: assert(false && "unreachable"); return NULL;

                case LAYE_NODE_FOR: {
                    assert(node->_break.target_node->_for.break_target_block != NULL);
                    return layec_build_branch(builder, node->location, node->_break.target_node->_for.break_target_block);
                }

                case LAYE_NODE_FOREACH: {
                    assert(node->_break.target_node->foreach.break_target_block != NULL);
                    return layec_build_branch(builder, node->location, node->_break.target_node->foreach.break_target_block);
                }

                case LAYE_NODE_WHILE: {
                    assert(node->_break.target_node->_while.break_target_block != NULL);
                    return layec_build_branch(builder, node->location, node->_break.target_node->_while.break_target_block);
                }

                case LAYE_NODE_DOWHILE: {
                    assert(node->_break.target_node->dowhile.break_target_block != NULL);
                    return layec_build_branch(builder, node->location, node->_break.target_node->dowhile.break_target_block);
                }
            }

            assert(false && "unreachable");
            return NULL;
        }

        case LAYE_NODE_CONTINUE: {
            assert(node->_continue.target_node != NULL);
            switch (node->_continue.target_node->kind) {
                default: assert(false && "unreachable"); return NULL;

                case LAYE_NODE_FOR: {
                    assert(node->_continue.target_node->_for.continue_target_block != NULL);
                    return layec_build_branch(builder, node->location, node->_continue.target_node->_for.continue_target_block);
                }

                case LAYE_NODE_FOREACH: {
                    assert(node->_continue.target_node->foreach.continue_target_block != NULL);
                    return layec_build_branch(builder, node->location, node->_continue.target_node->foreach.continue_target_block);
                }

                case LAYE_NODE_WHILE: {
                    assert(node->_continue.target_node->_while.continue_target_block != NULL);
                    return layec_build_branch(builder, node->location, node->_continue.target_node->_while.continue_target_block);
                }

                case LAYE_NODE_DOWHILE: {
                    assert(node->_continue.target_node->dowhile.continue_target_block != NULL);
                    return layec_build_branch(builder, node->location, node->_continue.target_node->dowhile.continue_target_block);
                }
            }

            assert(false && "unreachable");
            return NULL;
        }

        case LAYE_NODE_XYZZY: {
            return layec_build_nop(builder, node->location);
        }

        case LAYE_NODE_ASSIGNMENT: {
            layec_value* lhs_value = laye_generate_node(builder, node->assignment.lhs);
            assert(lhs_value != NULL);
            assert(layec_type_is_ptr(layec_value_get_type(lhs_value)));
            layec_value* rhs_value = laye_generate_node(builder, node->assignment.rhs);
            assert(rhs_value != NULL);
            return layec_build_store(builder, node->location, lhs_value, rhs_value);
        }

        case LAYE_NODE_COMPOUND: {
            layec_value* result_value = NULL;

            for (int64_t i = 0, count = arr_count(node->compound.children); i < count; i++) {
                laye_node* child = node->compound.children[i];
                assert(child != NULL);

                layec_value* child_value = laye_generate_node(builder, child);
                assert(child_value != NULL);

                if (child->kind == LAYE_NODE_YIELD) {
                    result_value = child_value;
                }

                if (child->kind == LAYE_NODE_RETURN || child->kind == LAYE_NODE_BREAK || child->kind == LAYE_NODE_CONTINUE || child->kind == LAYE_NODE_YIELD) {
                    break;
                }

                if (laye_type_is_noreturn(child->type)) {
                    break;
                }
            }

            // TODO(local): this (can be) an expression, so it really should return stuff, even if discarded.
            // we wanted it to be `yield`, so maybe if there's no yields it's semantically invalid, then we can
            // generate phis for every yield target???

            if (laye_type_is_noreturn(node->type)) {
                if (!layec_block_is_terminated(layec_builder_get_insert_block(builder))) {
                    layec_build_unreachable(builder, (layec_location){0});
                }
            }

            if (result_value == NULL) {
                result_value = layec_void_constant(context);
            }

            return result_value;
        }

        case LAYE_NODE_CAST: {
            laye_type from = node->cast.operand->type;
            laye_type to = node->type;

            layec_type* cast_type = laye_convert_type(node->type);
            layec_value* operand = laye_generate_node(builder, node->cast.operand);

            switch (node->cast.kind) {
                default: {
                    if (laye_type_equals(from, to, LAYE_MUT_IGNORE)) {
                        return operand;
                    }

                    if (
                        (laye_type_is_reference(from) || laye_type_is_pointer(from) || laye_type_is_buffer(from)) &&
                        (laye_type_is_reference(to) || laye_type_is_pointer(to) || laye_type_is_buffer(to))
                    ) {
                        return operand;
                    }

                    if (laye_type_is_int(from) && laye_type_is_int(to)) {
                        int64_t from_sz = laye_type_size_in_bits(from);
                        int64_t to_sz = laye_type_size_in_bits(to);

                        if (from_sz == to_sz) {
                            return layec_build_bitcast(builder, node->location, operand, cast_type);
                        } else if (from_sz < to_sz) {
                            if (laye_type_is_signed_int(from)) {
                                return layec_build_sign_extend(builder, node->location, operand, cast_type);
                            } else {
                                return layec_build_zero_extend(builder, node->location, operand, cast_type);
                            }
                        } else if (from_sz > to_sz) {
                            return layec_build_truncate(builder, node->location, operand, cast_type);
                        }
                    }

                    assert(false && "todo irgen cast");
                    return NULL;
                }

                case LAYE_CAST_REFERENCE_TO_LVALUE:
                case LAYE_CAST_LVALUE_TO_REFERENCE: {
                    return operand;
                }

                case LAYE_CAST_LVALUE_TO_RVALUE: {
                    return layec_build_load(builder, node->location, operand, cast_type);
                }
            }
        }

        case LAYE_NODE_UNARY: {
            layec_value* operand_value = laye_generate_node(builder, node->unary.operand);
            assert(operand_value != NULL);

            switch (node->unary.operator.kind) {
                default: {
                    fprintf(stderr, "for token kind %s\n", laye_token_kind_to_cstring(node->unary.operator.kind));
                    assert(false && "unimplemented unary operator in irgen");
                    return NULL;
                }

                case '+': {
                    return operand_value;
                }

                case '-': {
                    return layec_build_neg(builder, node->location, operand_value);
                }

                case '~': {
                    return layec_build_compl(builder, node->location, operand_value);
                }

                case '&':
                case '*': {
                    return operand_value;
                }

                case LAYE_TOKEN_NOT: {
                    layec_type* operand_type = layec_value_get_type(operand_value);
                    assert(layec_type_is_integer(operand_type));
                    return layec_build_eq(builder, node->location, layec_int_constant(context, node->location, operand_type, 0), operand_value);
                }
            }
        }

        case LAYE_NODE_BINARY: {
            bool is_short_circuit = node->binary.operator.kind == LAYE_TOKEN_AND || node->binary.operator.kind == LAYE_TOKEN_OR;

            bool are_signed_ints = laye_type_is_signed_int(node->binary.lhs->type) && laye_type_is_signed_int(node->binary.rhs->type);
            bool are_floats = laye_type_is_float(node->binary.lhs->type) && laye_type_is_float(node->binary.rhs->type);
            bool are_signed = are_signed_ints || are_floats;

            layec_value* lhs_value = laye_generate_node(builder, node->binary.lhs);
            assert(lhs_value != NULL);

            layec_value* rhs_value = NULL;
            if (is_short_circuit) {
                bool is_or = node->binary.operator.kind == LAYE_TOKEN_OR;

                layec_type* bool_type = layec_int_type(context, 1);

                layec_value* lhs_block = layec_builder_get_insert_block(builder);
                assert(lhs_block != NULL);
                layec_value* rhs_block = layec_function_append_block(function, SV_EMPTY);
                assert(rhs_block != NULL);
                layec_value* merge_block = layec_function_append_block(function, SV_EMPTY);
                assert(merge_block != NULL);

                if (is_or) {
                    layec_build_branch_conditional(builder, node->location, lhs_value, merge_block, rhs_block);
                } else {
                    layec_build_branch_conditional(builder, node->location, lhs_value, rhs_block, merge_block);
                }

                layec_builder_position_at_end(builder, rhs_block);
                rhs_value = laye_generate_node(builder, node->binary.rhs);
                assert(rhs_value != NULL);
                layec_build_branch(builder, node->location, merge_block);

                layec_builder_position_at_end(builder, merge_block);
                layec_value* phi = layec_build_phi(builder, node->location, bool_type);
                layec_phi_add_incoming_value(phi, lhs_value, lhs_block);
                layec_phi_add_incoming_value(phi, rhs_value, rhs_block);

                return phi;
            }

            rhs_value = laye_generate_node(builder, node->binary.rhs);
            assert(rhs_value != NULL);

            switch (node->binary.operator.kind) {
                default: {
                    fprintf(stderr, "for token kind %s\n", laye_token_kind_to_cstring(node->unary.operator.kind));
                    assert(false && "unimplemented binary operator in irgen");
                    return NULL;
                }

                case LAYE_TOKEN_AND:
                case LAYE_TOKEN_OR: {
                    assert(false);
                }

                case LAYE_TOKEN_XOR: {
                    return layec_build_ne(builder, node->location, lhs_value, rhs_value);
                }

                case LAYE_TOKEN_PLUS: {
                    return layec_build_add(builder, node->location, lhs_value, rhs_value);
                }

                case LAYE_TOKEN_MINUS: {
                    return layec_build_sub(builder, node->location, lhs_value, rhs_value);
                }

                case LAYE_TOKEN_STAR: {
                    return layec_build_mul(builder, node->location, lhs_value, rhs_value);
                }

                case LAYE_TOKEN_SLASH: {
                    if (are_signed) {
                        return layec_build_sdiv(builder, node->location, lhs_value, rhs_value);
                    } else {
                        return layec_build_udiv(builder, node->location, lhs_value, rhs_value);
                    }
                }

                case LAYE_TOKEN_PERCENT: {
                    if (are_signed) {
                        return layec_build_smod(builder, node->location, lhs_value, rhs_value);
                    } else {
                        return layec_build_umod(builder, node->location, lhs_value, rhs_value);
                    }
                }

                case LAYE_TOKEN_AMPERSAND: {
                    return layec_build_and(builder, node->location, lhs_value, rhs_value);
                }

                case LAYE_TOKEN_PIPE: {
                    return layec_build_or(builder, node->location, lhs_value, rhs_value);
                }

                case LAYE_TOKEN_TILDE: {
                    return layec_build_xor(builder, node->location, lhs_value, rhs_value);
                }

                case LAYE_TOKEN_LESSLESS: {
                    return layec_build_shl(builder, node->location, lhs_value, rhs_value);
                }

                case LAYE_TOKEN_GREATERGREATER: {
                    if (are_signed) {
                        return layec_build_sar(builder, node->location, lhs_value, rhs_value);
                    } else {
                        return layec_build_shr(builder, node->location, lhs_value, rhs_value);
                    }
                }

                case LAYE_TOKEN_EQUALEQUAL: return layec_build_eq(builder, node->location, lhs_value, rhs_value);
                case LAYE_TOKEN_BANGEQUAL: return layec_build_ne(builder, node->location, lhs_value, rhs_value);

                case LAYE_TOKEN_LESS: {
                    if (are_signed) {
                        return layec_build_slt(builder, node->location, lhs_value, rhs_value);
                    } else {
                        return layec_build_ult(builder, node->location, lhs_value, rhs_value);
                    }
                }
                
                case LAYE_TOKEN_LESSEQUAL: {
                    if (are_signed) {
                        return layec_build_sle(builder, node->location, lhs_value, rhs_value);
                    } else {
                        return layec_build_ule(builder, node->location, lhs_value, rhs_value);
                    }
                }

                case LAYE_TOKEN_GREATER: {
                    if (are_signed) {
                        return layec_build_sgt(builder, node->location, lhs_value, rhs_value);
                    } else {
                        return layec_build_ugt(builder, node->location, lhs_value, rhs_value);
                    }
                }
                
                case LAYE_TOKEN_GREATEREQUAL: {
                    if (are_signed) {
                        return layec_build_sge(builder, node->location, lhs_value, rhs_value);
                    } else {
                        return layec_build_uge(builder, node->location, lhs_value, rhs_value);
                    }
                }
            }
        }

        case LAYE_NODE_NAMEREF: {
            assert(node->nameref.referenced_declaration != NULL);
            assert(node->nameref.referenced_declaration->ir_value != NULL);
            return node->nameref.referenced_declaration->ir_value;
        }

        case LAYE_NODE_CALL: {
            layec_value* callee = laye_generate_node(builder, node->call.callee);
            assert(callee != NULL);

            layec_type* callee_type = layec_value_get_type(callee);
            if (layec_type_is_ptr(callee_type)) {
                assert(false && "gotta get the function type, not just the pointer");
            }

            dynarr(layec_value*) argument_values = NULL;
            for (int64_t i = 0, count = arr_count(node->call.arguments); i < count; i++) {
                layec_value* argument_value = laye_generate_node(builder, node->call.arguments[i]);
                arr_push(argument_values, argument_value);
                assert(argument_values[i] != NULL);
            }

            return layec_build_call(builder, node->location, callee, callee_type, argument_values, SV_EMPTY);
        }

        case LAYE_NODE_INDEX: {
            layec_value* value = laye_generate_node(builder, node->index.value);
            assert(value != NULL);

            dynarr(layec_value*) indices = NULL;
            for (int64_t i = 0, count = arr_count(node->index.indices); i < count; i++) {
                layec_value* index_value = laye_generate_node(builder, node->index.indices[i]);
                arr_push(indices, index_value);
                assert(indices[i] != NULL);
            }

            assert(layec_type_is_ptr(layec_value_get_type(value)));
            layec_type* underlying_type = laye_convert_type(node->index.value->type);

            if (layec_type_is_array(underlying_type)) {
                layec_type* element_type = layec_type_element_type(underlying_type);
                assert(element_type != NULL);

                layec_value* element_size_value = layec_int_constant(context, node->index.value->type.node->location, layec_int_type(context, 64), layec_type_size_in_bytes(element_type));
                assert(element_size_value != NULL);

                laye_type laye_array_type = node->index.value->type;
                assert(laye_array_type.node != NULL);
                assert(laye_type_is_array(laye_array_type));

                assert(arr_count(indices) == arr_count(laye_array_type.node->type_container.length_values));

                layec_value* calc_index_value = indices[arr_count(indices) - 1];
                int64_t current_stride = 1;

                for (int64_t i = arr_count(indices) - 2; i >= 0; i--) {
                    laye_node* length_value = laye_array_type.node->type_container.length_values[i + 1];
                    assert(length_value != NULL);
                    assert(length_value->kind == LAYE_NODE_EVALUATED_CONSTANT);
                    assert(length_value->evaluated_constant.result.kind == LAYEC_EVAL_INT);
                    int64_t next_length = length_value->evaluated_constant.result.int_value;
                    assert(next_length >= 0);

                    current_stride *= next_length;
                    layec_value* stride_constant = layec_int_constant(context, length_value->location, layec_int_type(context, context->target->laye.size_of_int), current_stride);
                    layec_value* curr_index_value = layec_build_mul(builder, node->index.indices[i]->location, indices[i], stride_constant);

                    calc_index_value = layec_build_add(builder, node->index.indices[i]->location, calc_index_value, curr_index_value);
                }

                arr_free(indices);
                
                calc_index_value = layec_build_mul(builder, node->location, calc_index_value, element_size_value);
                return layec_build_ptradd(builder, node->location, value, calc_index_value);
            } else {
                fprintf(stderr, "for layec_type %s\n", layec_type_kind_to_cstring(layec_type_get_kind(underlying_type)));
                assert(false && "unsupported indexable type");
                return NULL;
            }
        }

        case LAYE_NODE_MEMBER: {
            layec_value* address = laye_generate_node(builder, node->member.value);
            assert(address != NULL);
            assert(layec_type_is_ptr(layec_value_get_type(address)));

            int64_t member_offset = node->member.member_offset;
            assert(member_offset >= 0);

            layec_value* offset = layec_int_constant(context, node->location, layec_int_type(context, 64), member_offset);
            assert(offset != NULL);

            return layec_build_ptradd(builder, node->location, address, offset);
        }

        case LAYE_NODE_LITBOOL: {
            layec_type* type = laye_convert_type(node->type);
            assert(type != NULL);
            assert(layec_type_is_integer(type));
            return layec_int_constant(context, node->location, type, node->litbool.value ? 1 : 0);
        }

        case LAYE_NODE_LITINT: {
            layec_type* type = laye_convert_type(node->type);
            assert(type != NULL);
            assert(layec_type_is_integer(type));
            return layec_int_constant(context, node->location, type, node->litint.value);
        }

        case LAYE_NODE_LITSTRING: {
            layec_type* type = laye_convert_type(node->type);
            // asserts are only to validate that we're expecting a pointer value
            assert(type != NULL);
            assert(layec_type_is_ptr(type));
            return layec_module_create_global_string_ptr(module, node->location, node->litstring.value);
        }

        case LAYE_NODE_EVALUATED_CONSTANT: {
            layec_type* type = laye_convert_type(node->type);
            assert(type != NULL);

            if (node->evaluated_constant.result.kind == LAYEC_EVAL_INT) {
                assert(layec_type_is_integer(type));
                return layec_int_constant(context, node->location, type, node->evaluated_constant.result.int_value);
            } else if (node->evaluated_constant.result.kind == LAYEC_EVAL_STRING) {
                // assert is only to validate that we're expecting a pointer value
                assert(layec_type_is_ptr(type));
                return layec_module_create_global_string_ptr(module, node->location, node->evaluated_constant.result.string_value);
            } else {
                assert(false && "unsupported/unimplemented constant kind in irgen");
                return NULL;
            }
        }
    }

    assert(false && "unreachable");
}
