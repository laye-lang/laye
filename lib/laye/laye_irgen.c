#include <assert.h>

#include "layec.h"
#include "laye.h"

static layec_type* laye_convert_type(laye_node* type);
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
            string function_name = top_level_node->attributes.foreign_name.count != 0 ?
                top_level_node->attributes.foreign_name :
                top_level_node->declared_name;

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
                string_as_view(function_name),
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

static layec_type* laye_convert_type(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    //laye_module* module = type->module;
    //assert(module != NULL);
    layec_context* context = type->context;
    assert(context != NULL);

    switch (type->kind) {
        default: {
            fprintf(stderr, "for node kind %s\n", laye_node_kind_to_cstring(type->kind));
            assert(false && "unimplemented type kind in laye_convert_type");
            return NULL;
        }

        case LAYE_NODE_TYPE_VOID:
        case LAYE_NODE_TYPE_NORETURN: {
            return layec_void_type(context);
        }

        case LAYE_NODE_TYPE_BOOL:
        case LAYE_NODE_TYPE_INT: {
            return layec_int_type(context, type->type_primitive.bit_width);
        }

        case LAYE_NODE_TYPE_FUNCTION: {
            assert(type->type_function.return_type != NULL);
            layec_type* return_type = laye_convert_type(type->type_function.return_type);
            assert(return_type != NULL);

            dynarr(layec_type*) parameter_types = NULL;
            for (int64_t i = 0, count = arr_count(type->type_function.parameter_types); i < count; i++) {
                laye_node* parameter_type_node = type->type_function.parameter_types[i];
                assert(parameter_type_node != NULL);

                layec_type* parameter_type = laye_convert_type(parameter_type_node);
                assert(parameter_type != NULL);

                arr_push(parameter_types, parameter_type);
            }

            layec_calling_convention calling_convention = type->type_function.calling_convention;
            assert(calling_convention != LAYEC_DEFAULTCC);

            assert(arr_count(parameter_types) == arr_count(type->type_function.parameter_types));
            return layec_function_type(context, return_type, parameter_types, calling_convention, type->type_function.varargs_style == LAYE_VARARGS_C);
        }

        case LAYE_NODE_TYPE_POINTER:
        case LAYE_NODE_TYPE_BUFFER: {
            return layec_ptr_type(context);
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
            layec_type* declared_type = laye_convert_type(node->declared_type);
            layec_value* alloca = layec_build_alloca(builder, node->location, declared_type);
            assert(alloca != NULL);
            assert(layec_type_is_ptr(layec_value_get_type(alloca)));

            if (node->decl_binding.initializer != NULL) {
                layec_value* initial_value = laye_generate_node(builder, node->decl_binding.initializer);
                assert(initial_value != NULL);

                layec_build_store(builder, node->location, alloca, initial_value);
            }

            node->ir_value = alloca;
            return layec_void_constant(context);
        }

        case LAYE_NODE_IF: {
            assert((laye_type_is_void(node->type) || laye_type_is_noreturn(node->type)) && "expression if is not yet supported in IR gen");

            dynarr(layec_value*) pass_blocks = NULL;
            dynarr(layec_value*) condition_blocks = NULL;
            layec_value* fail_block = NULL;
            layec_value* continue_block = NULL;

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

            for (int64_t i = 0, count = arr_count(node->_if.conditions); i < count; i++) {
                if (i > 0) {
                    layec_builder_position_at_end(builder, condition_blocks[i - 1]);
                }

                layec_value* condition_value = laye_generate_node(builder, node->_if.conditions[i]);
                assert(condition_value != NULL);

                layec_type* condition_type = layec_value_get_type(condition_value);
                assert(condition_type != NULL);
                assert(layec_type_is_integer(condition_type));

                layec_value* condition = layec_build_ne(builder, node->_if.conditions[i]->location, condition_value, layec_int_constant(context, (layec_location){0}, condition_type, 0));
                assert(condition != NULL);
                assert(layec_type_is_integer(layec_value_get_type(condition)));

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
                layec_build_branch(builder, node->_if.conditions[i]->location, condition, block, else_block);

                layec_builder_position_at_end(builder, block);
                layec_value* pass_value = laye_generate_node(builder, node->_if.passes[i]);
            }

            arr_free(condition_blocks);
            arr_free(pass_blocks);

            if (fail_block != NULL) {
                assert(node->_if.fail != NULL);
                layec_builder_position_at_end(builder, fail_block);
                layec_value* fail_value = laye_generate_node(builder, node->_if.fail);
            }

            if (continue_block != NULL) {
                layec_builder_position_at_end(builder, continue_block);
            }

            // TODO(local): return phi in expression context, I assume
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

        case LAYE_NODE_XYZZY: {
            return layec_build_nop(builder, node->location);
        }

        case LAYE_NODE_COMPOUND: {
            for (int64_t i = 0, count = arr_count(node->compound.children); i < count; i++) {
                laye_node* child = node->compound.children[i];
                assert(child != NULL);
                layec_value* child_value = laye_generate_node(builder, child);
                assert(child_value != NULL);
            }

            // TODO(local): this (can be) an expression, so it really should return stuff, even if discarded.
            // we wanted it to be `yield`, so maybe if there's no yields it's semantically invalid, then we can
            // generate phis for every yield target???

            if (laye_type_is_noreturn(node->type)) {
                if (!layec_block_is_terminated(layec_builder_get_insert_block(builder))) {
                    layec_build_unreachable(builder, (layec_location){0});
                }
            }

            return layec_void_constant(context);
        }

        case LAYE_NODE_CAST: {
            layec_type* cast_type = laye_convert_type(node->type);
            layec_value* operand = laye_generate_node(builder, node->cast.operand);

            switch (node->cast.kind) {
                default: {
                    assert(false && "todo irgen cast");
                    return NULL;
                }

                case LAYE_CAST_LVALUE_TO_RVALUE: {
                    return layec_build_load(builder, node->location, operand, cast_type);
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
