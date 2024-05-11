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

#include "laye.h"

#include <assert.h>

typedef enum laye_builtin_runtime_function {
    LAYE_RUNTIME_ASSERT_FUNCTION,
} laye_builtin_runtime_function;

typedef enum laye_irvalue_kv_kind {
    LAYE_IRKV_NODE,
    LAYE_IRKV_BUILTIN,
} laye_irvalue_kv_kind;

typedef struct laye_irvalue_kv {
    laye_irvalue_kv_kind kind;

    laye_module* module;
    union {
        laye_node* node;
        laye_builtin_runtime_function builtin;
    };

    lyir_value* value;
} laye_irvalue_kv;

typedef struct laye_irgen {
    laye_context* context;
    lca_da(laye_irvalue_kv) ir_values;
} laye_irgen;

// NOTE(local): this pointer can and will move, it should never be stored
static laye_irvalue_kv* laye_irgen_irvalue_kv_get_for_node(laye_irgen* irgen, laye_module* module, laye_node* node) {
    for (int64_t i = 0, count = lca_da_count(irgen->ir_values); i < count; i++) {
        laye_irvalue_kv* kv = &irgen->ir_values[i];
        if (kv->kind == LAYE_IRKV_NODE && kv->module == module && kv->node == node)
            return kv;
    }

    laye_irvalue_kv kv = {
        .kind = LAYE_IRKV_NODE,
        .module = module,
        .node = node,
    };

    lca_da_push(irgen->ir_values, kv);
    return lca_da_back(irgen->ir_values);
}

static laye_irvalue_kv* laye_irgen_irvalue_kv_get_for_builtin(laye_irgen* irgen, laye_module* module, laye_builtin_runtime_function builtin) {
    for (int64_t i = 0, count = lca_da_count(irgen->ir_values); i < count; i++) {
        laye_irvalue_kv* kv = &irgen->ir_values[i];
        if (kv->kind == LAYE_IRKV_BUILTIN && kv->module == module && kv->builtin == builtin)
            return kv;
    }

    laye_irvalue_kv kv = {
        .kind = LAYE_IRKV_BUILTIN,
        .module = module,
        .builtin = builtin,
    };

    lca_da_push(irgen->ir_values, kv);
    return lca_da_back(irgen->ir_values);
}

static lyir_value* laye_irgen_ir_value_get(laye_irgen* irgen, laye_module* module, laye_node* node) {
    return laye_irgen_irvalue_kv_get_for_node(irgen, module, node)->value;
}

static lyir_value* laye_irgen_ir_value_get_builtin(laye_irgen* irgen, laye_module* module, laye_builtin_runtime_function builtin) {
    return laye_irgen_irvalue_kv_get_for_builtin(irgen, module, builtin)->value;
}

static void laye_irgen_ir_value_set(laye_irgen* irgen, laye_module* module, laye_node* node, lyir_value* value) {
    assert(value != NULL);
    laye_irgen_irvalue_kv_get_for_node(irgen, module, node)->value = value;
}

static void laye_irgen_ir_value_set_builtin(laye_irgen* irgen, laye_module* module, laye_builtin_runtime_function builtin, lyir_value* value) {
    assert(value != NULL);
    laye_irgen_irvalue_kv_get_for_builtin(irgen, module, builtin)->value = value;
}

static lyir_type* laye_convert_type(laye_type type);
static lyir_value* laye_generate_node(laye_irgen* irgen, lyir_builder* builder, laye_node* node);

static void laye_irgen_generate_declaration(laye_irgen* irgen, laye_module* module, laye_node* node) {
    assert(irgen != NULL);
    assert(module != NULL);

    lyir_module* ir_module = module->ir_module;
    assert(ir_module != NULL);

    assert(node != NULL);

    if (laye_irgen_ir_value_get(irgen, module, node) != NULL) {
        return;
    }

    if (node->kind == LAYE_NODE_DECL_FUNCTION) {
        lca_string_view function_name = node->attributes.foreign_name.count != 0 ? node->attributes.foreign_name : node->declared_name;

        assert(laye_type_is_function(node->declared_type));
        lyir_type* ir_function_type = laye_convert_type(node->declared_type);
        assert(ir_function_type != NULL);
        assert(lyir_type_is_function(ir_function_type));

        lyir_linkage function_linkage;
        if (node->decl_function.body == NULL) {
            if (node->attributes.linkage == LYIR_LINK_EXPORTED) {
                function_linkage = LYIR_LINK_REEXPORTED;
            } else {
                function_linkage = LYIR_LINK_IMPORTED;
            }
        } else {
            if (node->attributes.linkage == LYIR_LINK_EXPORTED) {
                function_linkage = LYIR_LINK_EXPORTED;
            } else {
                function_linkage = LYIR_LINK_INTERNAL;
            }
        }

        lca_da(lyir_value*) parameters = NULL;
        for (int64_t i = 0, count = lca_da_count(node->decl_function.parameter_declarations); i < count; i++) {
            laye_node* parameter_node = node->decl_function.parameter_declarations[i];

            lyir_type* parameter_type = lyir_function_type_parameter_type_get_at_index(ir_function_type, i);
            lyir_value* ir_parameter = lyir_value_parameter_create(ir_module, parameter_node->location, parameter_type, parameter_node->declared_name, i);

            laye_irgen_ir_value_set(irgen, module, parameter_node, ir_parameter);
            // parameter_node->ir_value = ir_parameter;
            lca_da_push(parameters, ir_parameter);
        }

        lyir_value* ir_function = lyir_module_create_function(
            ir_module,
            node->location,
            function_name,
            ir_function_type,
            parameters,
            function_linkage
        );

        assert(ir_function != NULL);
        laye_irgen_ir_value_set(irgen, module, node, ir_function);
        // node->ir_value = ir_function;
    }
}

static void laye_irgen_generate_imported_function_declarations(laye_irgen* irgen, laye_module* module, laye_symbol* from_namespace) {
    assert(module != NULL);

    lyir_module* ir_module = module->ir_module;
    assert(ir_module != NULL);

    assert(from_namespace != NULL);
    assert(from_namespace->kind == LAYE_SYMBOL_NAMESPACE);

    for (int64_t symbol_index = 0, symbol_count = lca_da_count(from_namespace->symbols); symbol_index < symbol_count; symbol_index++) {
        laye_symbol* symbol = from_namespace->symbols[symbol_index];
        assert(symbol != NULL);

        if (symbol->kind == LAYE_SYMBOL_NAMESPACE) {
            laye_irgen_generate_imported_function_declarations(irgen, module, symbol);
            continue;
        }

        assert(symbol->kind == LAYE_SYMBOL_ENTITY);
        for (int64_t node_index = 0, node_count = lca_da_count(symbol->nodes); node_index < node_count; node_index++) {
            laye_node* node = symbol->nodes[node_index];
            assert(node != NULL);

            laye_irgen_generate_declaration(irgen, module, node);
            // assert(node->ir_value != NULL);
        }
    }
}

static lyir_value* laye_irgen_get_runtime_assert_function(laye_irgen* irgen, laye_module* module) {
    assert(irgen != NULL);
    assert(module != NULL);

    laye_irvalue_kv* kv = laye_irgen_irvalue_kv_get_for_builtin(irgen, module, LAYE_RUNTIME_ASSERT_FUNCTION);
    if (kv->value == NULL) {
        laye_context* context = module->context;

        lca_da(lyir_type*) parameter_types = NULL;
        lca_da_push(parameter_types, laye_convert_type(LTY(context->laye_types.i8_buffer)));
        lca_da_push(parameter_types, laye_convert_type(LTY(context->laye_types.i8_buffer)));
        lca_da_push(parameter_types, laye_convert_type(LTY(context->laye_types._int)));
        lca_da_push(parameter_types, laye_convert_type(LTY(context->laye_types._int)));
        lca_da_push(parameter_types, laye_convert_type(LTY(context->laye_types._int)));
        lca_da_push(parameter_types, laye_convert_type(LTY(context->laye_types.i8_buffer)));

        lyir_type* function_type = lyir_function_type(context->lyir_context, lyir_void_type(context->lyir_context), parameter_types, LYIR_CCC, false);
        kv->value = lyir_module_create_function(
            module->ir_module,
            (lyir_location){0},
            LCA_SV_CONSTANT("__laye_assert_fail"),
            function_type,
            NULL,
            LYIR_LINK_REEXPORTED
        );
    }

    assert(kv->value != NULL);
    return kv->value;
}

void laye_generate_ir(laye_context* context) {
    assert(context != NULL);

    laye_irgen irgen = {
        .context = context,
    };

    for (int64_t i = 0, module_count = lca_da_count(context->laye_modules); i < module_count; i++) {
        laye_module* module = context->laye_modules[i];
        assert(module != NULL);

        lyir_source source = lyir_context_get_source(module->context->lyir_context, module->sourceid);

        lyir_module* ir_module = lyir_module_create(module->context->lyir_context, lca_string_as_view(source.name));
        assert(ir_module != NULL);
        lca_da_push(module->context->lyir_context->ir_modules, ir_module);

        module->ir_module = ir_module;
    }

    for (int64_t i = 0, module_count = lca_da_count(context->laye_modules); i < module_count; i++) {
        laye_module* module = context->laye_modules[i];
        assert(module != NULL);

        lyir_module* ir_module = module->ir_module;
        assert(ir_module != NULL);

        // 1. Top-level type generation
    }

    for (int64_t i = 0, module_count = lca_da_count(context->laye_modules); i < module_count; i++) {
        laye_module* module = context->laye_modules[i];
        assert(module != NULL);

        lyir_module* ir_module = module->ir_module;
        assert(ir_module != NULL);

        // 2. Top-level function generation
        laye_irgen_generate_imported_function_declarations(&irgen, module, module->imports);

        for (int64_t i = 0, count = lca_da_count(module->top_level_nodes); i < count; i++) {
            laye_node* top_level_node = module->top_level_nodes[i];
            assert(top_level_node != NULL);

            laye_irgen_generate_declaration(&irgen, module, top_level_node);
            // assert(top_level_node->ir_value != NULL);
        }
    }

    for (int64_t i = 0, module_count = lca_da_count(context->laye_modules); i < module_count; i++) {
        laye_module* module = context->laye_modules[i];
        assert(module != NULL);

        lyir_module* ir_module = module->ir_module;
        assert(ir_module != NULL);

        // 3. Generate function bodies
        lyir_builder* builder = lyir_builder_create(module->context->lyir_context);

        for (int64_t i = 0, count = lca_da_count(module->top_level_nodes); i < count; i++) {
            laye_node* top_level_node = module->top_level_nodes[i];
            assert(top_level_node != NULL);

            if (top_level_node->kind == LAYE_NODE_DECL_FUNCTION) {
                lyir_value* function = laye_irgen_ir_value_get(&irgen, module, top_level_node);
                // layec_value* function = top_level_node->ir_value;
                assert(function != NULL);
                assert(lyir_value_is_function(function));

                if (top_level_node->decl_function.body == NULL) {
                    continue;
                }

                lyir_value* entry_block = lyir_value_function_block_append(function, LCA_SV_CONSTANT("entry"));
                assert(entry_block != NULL);
                lyir_builder_position_at_end(builder, entry_block);

                // insert declarations for the parameters for the function
                lyir_type* function_type = lyir_value_type_get(function);

                for (int64_t i = 0, count = lyir_function_type_parameter_count_get(function_type); i < count; i++) {
                    laye_node* parameter_node = top_level_node->decl_function.parameter_declarations[i];
                    lyir_type* parameter_type = lyir_function_type_parameter_type_get_at_index(function_type, i);

                    lyir_value* alloca = lyir_build_alloca(builder, parameter_node->location, parameter_type, 1);
                    assert(alloca != NULL);
                    assert(lyir_type_is_ptr(lyir_value_type_get(alloca)));

                    lyir_value* ir_parameter = laye_irgen_ir_value_get(&irgen, module, parameter_node);
                    assert(ir_parameter != NULL);
                    // assert(parameter_node->ir_value != NULL);
                    lyir_value* store = lyir_build_store(builder, parameter_node->location, alloca, ir_parameter);
                    assert(store != NULL);

                    laye_irgen_ir_value_set(&irgen, module, parameter_node, alloca);
                }

                // generate the function body
                laye_generate_node(&irgen, builder, top_level_node->decl_function.body);

                lyir_builder_reset(builder);
            }
        }

        lyir_builder_destroy(builder);
    }

    lca_da_free(irgen.ir_values);
}

static lyir_type* laye_convert_type(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    // laye_module* module = type->module;
    // assert(module != NULL);
    laye_context* context = type.node->context;
    assert(context != NULL);

    switch (type.node->kind) {
        default: {
            fprintf(stderr, "for node kind %s\n", laye_node_kind_to_cstring(type.node->kind));
            assert(false && "unimplemented type kind in laye_convert_type");
            return NULL;
        }

        case LAYE_NODE_TYPE_VOID:
        case LAYE_NODE_TYPE_NORETURN: {
            return lyir_void_type(context->lyir_context);
        }

        case LAYE_NODE_TYPE_BOOL: {
            return lyir_int_type(context->lyir_context, 1);
        }

        case LAYE_NODE_TYPE_INT: {
            return lyir_int_type(context->lyir_context, type.node->type_primitive.bit_width);
        }

        case LAYE_NODE_TYPE_FLOAT: {
            return lyir_float_type(context->lyir_context, type.node->type_primitive.bit_width);
        }

        case LAYE_NODE_TYPE_FUNCTION: {
            assert(type.node->type_function.return_type.node != NULL);
            lyir_type* return_type = laye_convert_type(type.node->type_function.return_type);
            assert(return_type != NULL);

            lca_da(lyir_type*) parameter_types = NULL;
            for (int64_t i = 0, count = lca_da_count(type.node->type_function.parameter_types); i < count; i++) {
                laye_type pt = type.node->type_function.parameter_types[i];
                assert(pt.node != NULL);

                lyir_type* parameter_type = laye_convert_type(pt);
                assert(parameter_type != NULL);

                lca_da_push(parameter_types, parameter_type);
            }

            lyir_calling_convention calling_convention = type.node->type_function.calling_convention;
            assert(calling_convention != LYIR_DEFAULTCC);

            assert(lca_da_count(parameter_types) == lca_da_count(type.node->type_function.parameter_types));
            return lyir_function_type(context->lyir_context, return_type, parameter_types, calling_convention, type.node->type_function.varargs_style == LAYE_VARARGS_C);
        }

        case LAYE_NODE_TYPE_POINTER:
        case LAYE_NODE_TYPE_REFERENCE:
        case LAYE_NODE_TYPE_BUFFER: {
            return lyir_ptr_type(context->lyir_context);
        }

        case LAYE_NODE_TYPE_ARRAY: {
            lyir_type* element_type = laye_convert_type(type.node->type_container.element_type);
            assert(element_type != NULL);

            int64_t length = 1;
            assert(lca_da_count(type.node->type_container.length_values) > 0);
            for (int64_t i = 0, count = lca_da_count(type.node->type_container.length_values); i < count; i++) {
                laye_node* length_value = type.node->type_container.length_values[i];
                assert(length_value != NULL);
                assert(length_value->kind == LAYE_NODE_EVALUATED_CONSTANT);
                assert(length_value->evaluated_constant.result.kind == LYIR_EVAL_INT);
                length *= length_value->evaluated_constant.result.int_value;
            }

            return lyir_array_type(context->lyir_context, length, element_type);
        }

        case LAYE_NODE_TYPE_STRUCT: {
            for (int64_t i = 0, count = lca_da_count(context->_all_struct_types); i < count; i++) {
                if (context->_all_struct_types[i].node == type.node) {
                    return context->_all_struct_types[i].type;
                }
            }

            int64_t field_count = lca_da_count(type.node->type_struct.fields);
            lca_da(lyir_struct_member) fields = NULL;
            lca_da_count_set(fields, field_count);

            for (int64_t i = 0; i < field_count; i++) {
                fields[i] = (lyir_struct_member){
                    .type = laye_convert_type(type.node->type_struct.fields[i].type),
                };
            }

            lyir_type* struct_type = lyir_struct_type(context->lyir_context, type.node->type_struct.name, fields);
            assert(struct_type != NULL);

            struct cached_struct_type t = {
                .node = type.node,
                .type = struct_type,
            };
            lca_da_push(context->_all_struct_types, t);

            return struct_type;
        }
    }
}

static void laye_generate_ctor(laye_irgen* irgen, lyir_builder* builder, laye_node* ctor, lyir_value* address, bool zero_init) {
    assert(irgen != NULL);
    assert(builder != NULL);
    assert(ctor != NULL);
    assert(address != NULL);

    lyir_context* context = lyir_builder_context_get(builder);
    assert(context != NULL);

    laye_type struct_type = ctor->type;
    assert(struct_type.node != NULL);
    assert(struct_type.node->kind == LAYE_NODE_TYPE_STRUCT || struct_type.node->kind == LAYE_NODE_TYPE_ARRAY);

    bool is_struct_ctor = struct_type.node->kind == LAYE_NODE_TYPE_STRUCT;
    lca_da(laye_node*) inits = ctor->ctor.initializers;
    lca_da(int64_t) offsets = ctor->ctor.calculated_offsets;

    assert(lca_da_count(inits) == lca_da_count(offsets));

    lyir_type* intptr_type = lyir_int_type(context, context->target->size_of_pointer);
    assert(intptr_type != NULL);

    if (zero_init) {
        int64_t size_in_bytes = laye_type_size_in_bytes(struct_type);
        lyir_value* zero_const = lyir_int_constant_create(context, ctor->location, lyir_int_type(context, 8), 0);
        lyir_value* byte_count = lyir_int_constant_create(context, ctor->location, lyir_int_type(context, context->target->size_of_pointer), size_in_bytes);
        lyir_build_builtin_memset(builder, ctor->location, address, zero_const, byte_count);
    }

    for (int64_t i = 0, count = lca_da_count(inits); i < count; i++) {
        laye_node* init_node = inits[i]->member_initializer.value;

        int64_t offset = offsets[i];
        lyir_value* offset_value = lyir_int_constant_create(context, inits[i]->location, intptr_type, offset);

        lyir_value* init_address = lyir_build_ptradd(builder, inits[i]->location, address, offset_value);
        
        if (init_node->kind == LAYE_NODE_CTOR) {
            laye_generate_ctor(irgen, builder, init_node, init_address, false);
        } else {
            lyir_value* init_value = laye_generate_node(irgen, builder, init_node);
            assert(init_value != NULL);

            lyir_build_store(builder, inits[i]->location, init_address, init_value);
        }
    }
}

static lyir_value* laye_generate_node(laye_irgen* irgen, lyir_builder* builder, laye_node* node) {
    assert(irgen != NULL);
    assert(builder != NULL);
    assert(node != NULL);
    assert(node->sema_state == LYIR_SEMA_OK);

    lyir_module* module = lyir_builder_module_get(builder);
    assert(module != NULL);

    laye_context* laye_context = irgen->context;
    assert(laye_context != NULL);

    lyir_context* context = lyir_builder_context_get(builder);
    assert(context != NULL);

    lyir_value* function = lyir_builder_function_get(builder);

    switch (node->kind) {
        default: {
            fprintf(stderr, "for node kind %s\n", laye_node_kind_to_cstring(node->kind));
            assert(false && "unimplemented Laye node in laye_generate_node");
            return NULL;
        }

        case LAYE_NODE_DECL_BINDING: {
            lyir_type* type_to_alloca = laye_convert_type(node->declared_type);
            int64_t element_count = 1;

            lyir_value* alloca = lyir_build_alloca(builder, node->location, type_to_alloca, element_count);
            assert(alloca != NULL);
            assert(lyir_type_is_ptr(lyir_value_type_get(alloca)));

            if (node->decl_binding.initializer != NULL) {
                if (node->decl_binding.initializer->kind == LAYE_NODE_CTOR) {
                    laye_generate_ctor(irgen, builder, node->decl_binding.initializer, alloca, true);
                } else {
                    lyir_value* initial_value = laye_generate_node(irgen, builder, node->decl_binding.initializer);
                    assert(initial_value != NULL);

                    lyir_build_store(builder, node->location, alloca, initial_value);
                }
            } else {
                int64_t size_in_bytes = lyir_type_size_in_bytes(type_to_alloca);
                lyir_value* zero_const = lyir_int_constant_create(context, node->location, lyir_int_type(context, 8), 0);
                lyir_value* byte_count = lyir_int_constant_create(context, node->location, lyir_int_type(context, context->target->size_of_pointer), size_in_bytes);
                lyir_build_builtin_memset(builder, node->location, alloca, zero_const, byte_count);
            }

            laye_irgen_ir_value_set(irgen, node->module, node, alloca);
            // node->ir_value = alloca;
            return lyir_void_constant_create(context);
        }

        case LAYE_NODE_ASSERT: {
            // TODO(local): generate slightly different code for tests, eventually
            assert(node->_assert.condition != NULL);
            lyir_value* condition = laye_generate_node(irgen, builder, node->_assert.condition);

            lyir_value* message_global_string = NULL;
            if (node->_assert.message.kind == LAYE_TOKEN_LITSTRING) {
                message_global_string = lyir_module_create_global_string_ptr(module, node->_assert.message.location, node->_assert.message.string_value);
            } else {
                message_global_string = lyir_int_constant_create(context, node->location, lyir_ptr_type(context), 0);
            }

            assert(message_global_string != NULL);
            assert(lyir_type_is_ptr(lyir_value_type_get(message_global_string)));

            lyir_value* assert_fail_block = lyir_value_function_block_append(function, LCA_SV_EMPTY);
            lyir_value* assert_after_block = lyir_value_function_block_append(function, LCA_SV_EMPTY);

            lyir_build_branch_conditional(builder, node->_assert.condition->location, condition, assert_after_block, assert_fail_block);

            lyir_builder_position_at_end(builder, assert_fail_block);

            lyir_value* runtime_assert_function = laye_irgen_get_runtime_assert_function(irgen, node->module);
            assert(runtime_assert_function != NULL);

            lyir_source source = lyir_context_get_source(context, node->location.sourceid);

            lyir_location condition_location = node->_assert.condition->location;
            lca_string_view condition_source_text = lca_string_slice(source.text, condition_location.offset, condition_location.length);
            lyir_value* condition_global_string = lyir_module_create_global_string_ptr(module, condition_location, condition_source_text);

            lyir_value* file_name_global_string = lyir_module_create_global_string_ptr(module, condition_location, lca_string_as_view(source.name));

            lca_da(lyir_value*) arguments = NULL;
            lca_da_push(arguments, condition_global_string);
            lca_da_push(arguments, file_name_global_string);
            lca_da_push(arguments, (lyir_int_constant_create(context, node->location, laye_convert_type(LTY(laye_context->laye_types._int)), node->location.offset)));
            lca_da_push(arguments, (lyir_int_constant_create(context, node->location, laye_convert_type(LTY(laye_context->laye_types._int)), 0)));
            lca_da_push(arguments, (lyir_int_constant_create(context, node->location, laye_convert_type(LTY(laye_context->laye_types._int)), 0)));
            lca_da_push(arguments, message_global_string);

            lyir_type* runtime_assert_function_type = lyir_value_type_get(runtime_assert_function);
            assert(lyir_function_type_parameter_count_get(runtime_assert_function_type) == lca_da_count(arguments));

            lyir_build_call(builder, node->location, runtime_assert_function, runtime_assert_function_type, arguments, LCA_SV_EMPTY);
            lyir_value* unreachable_value = lyir_build_unreachable(builder, node->location);

            lyir_builder_position_at_end(builder, assert_after_block);
            return unreachable_value;
        }

        case LAYE_NODE_IF: {
            bool is_expr = !(laye_type_is_void(node->type) || laye_type_is_noreturn(node->type));

            lca_da(lyir_value*) pass_blocks = NULL;
            lca_da(lyir_value*) condition_blocks = NULL;
            lyir_value* fail_block = NULL;
            lyir_value* continue_block = NULL;

            lyir_value* phi_value = NULL;

            assert(lca_da_count(node->_if.conditions) == lca_da_count(node->_if.passes));
            for (int64_t i = 0, count = lca_da_count(node->_if.conditions); i < count; i++) {
                if (i > 0) {
                    lyir_value* cond_block = lyir_value_function_block_append(function, LCA_SV_EMPTY);
                    assert(cond_block != NULL);
                    lca_da_push(condition_blocks, cond_block);
                }

                lyir_value* block = lyir_value_function_block_append(function, LCA_SV_EMPTY);
                assert(block != NULL);
                lca_da_push(pass_blocks, block);
            }
            assert(lca_da_count(node->_if.conditions) == lca_da_count(pass_blocks));
            assert(lca_da_count(node->_if.conditions) - 1 == lca_da_count(condition_blocks));

            if (node->_if.fail != NULL) {
                fail_block = lyir_value_function_block_append(function, LCA_SV_EMPTY);
                assert(fail_block != NULL);
            }

            if (!laye_type_is_noreturn(node->type)) {
                continue_block = lyir_value_function_block_append(function, LCA_SV_EMPTY);
                assert(continue_block != NULL);
            }

            if (is_expr) {
                lyir_value* phi_block = continue_block;
                if (phi_block == NULL) {
                    phi_block = fail_block;
                }

                assert(phi_block != NULL);

                lyir_value* current_block = lyir_builder_insert_block_get(builder);
                assert(current_block != NULL);

                lyir_builder_position_at_end(builder, phi_block);
                phi_value = lyir_build_phi(builder, node->location, laye_convert_type(node->type));

                lyir_builder_position_at_end(builder, current_block);
            }

            for (int64_t i = 0, count = lca_da_count(node->_if.conditions); i < count; i++) {
                if (i > 0) {
                    lyir_builder_position_at_end(builder, condition_blocks[i - 1]);
                }

                lyir_value* condition_value = laye_generate_node(irgen, builder, node->_if.conditions[i]);
                assert(condition_value != NULL);

                lyir_type* condition_type = lyir_value_type_get(condition_value);
                assert(condition_type != NULL);
                assert(lyir_type_is_integer(condition_type));

                lyir_value* block = pass_blocks[i];
                assert(block != NULL);

                lyir_value* else_block = NULL;
                if (i + 1 < count) {
                    else_block = condition_blocks[i];
                } else if (fail_block != NULL) {
                    else_block = fail_block;
                } else {
                    else_block = continue_block;
                }

                assert(else_block != NULL);
                lyir_build_branch_conditional(builder, node->_if.conditions[i]->location, condition_value, block, else_block);

                lyir_builder_position_at_end(builder, block);
                lyir_value* pass_value = laye_generate_node(irgen, builder, node->_if.passes[i]);
                assert(pass_value != NULL);
                lyir_value* from_block = lyir_builder_insert_block_get(builder);
                assert(from_block != NULL);

                if (!laye_type_is_noreturn(node->_if.passes[i]->type)) {
                    assert(continue_block != NULL);
                    if (!lyir_value_block_is_terminated(lyir_builder_insert_block_get(builder))) {
                        lyir_build_branch(builder, node->_if.passes[i]->location, continue_block);
                    }
                }

                if (is_expr) {
                    assert(phi_value != NULL);
                    lyir_value_phi_incoming_value_add(phi_value, pass_value, from_block);
                }
            }

            if (is_expr) {
                assert(phi_value != NULL);
                assert(lyir_value_phi_incoming_value_count_get(phi_value) == lca_da_count(pass_blocks));
            }

            if (fail_block != NULL) {
                assert(node->_if.fail != NULL);
                lyir_builder_position_at_end(builder, fail_block);
                lyir_value* fail_value = laye_generate_node(irgen, builder, node->_if.fail);
                assert(fail_value != NULL);
                lyir_value* from_block = lyir_builder_insert_block_get(builder);
                assert(from_block != NULL);

                if (!laye_type_is_noreturn(node->_if.fail->type)) {
                    assert(continue_block != NULL);
                    if (!lyir_value_block_is_terminated(lyir_builder_insert_block_get(builder))) {
                        lyir_build_branch(builder, node->_if.fail->location, continue_block);
                    }
                }

                if (is_expr) {
                    assert(phi_value != NULL);
                    lyir_value_phi_incoming_value_add(phi_value, fail_value, from_block);
                    assert(lyir_value_phi_incoming_value_count_get(phi_value) == lca_da_count(pass_blocks) + 1);
                }
            }

            lca_da_free(condition_blocks);
            lca_da_free(pass_blocks);

            if (continue_block != NULL) {
                lyir_builder_position_at_end(builder, continue_block);
            }

            lyir_value* result_value = NULL;
            if (is_expr) {
                result_value = phi_value;
            } else {
                result_value = lyir_void_constant_create(context);
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

                layec_value* body_block = lyir_value_function_block_append(function, LCA_SV_EMPTY);
                assert(body_block != NULL);

                lyir_build_branch(builder, node->location, body_block);
                lyir_builder_position_at_end(builder, body_block);
                laye_generate_node(irgen, builder, node->_for.pass);

                if (!laye_type_is_noreturn(node->_for.pass->type)) {
                    assert(!lyir_value_block_is_terminated(lyir_builder_insert_block_get(builder)));
                    lyir_build_branch(builder, node->location, body_block);
                }

                assert(lyir_value_block_is_terminated(lyir_builder_insert_block_get(builder)));
                return lyir_void_constant_create(context);
            }
#endif

            // 1. handle the initializer
            if (has_initializer) {
                laye_generate_node(irgen, builder, node->_for.initializer);

                if (laye_type_is_noreturn(node->_for.initializer->type)) {
                    assert(lyir_value_block_is_terminated(lyir_builder_insert_block_get(builder)));
                    return lyir_void_constant_create(context);
                }
            }

            lyir_value* for_early_condition_block = NULL;
            if (!has_always_true_condition && node->_for.fail != NULL) {
                for_early_condition_block = lyir_value_function_block_append(function, LCA_SV_EMPTY);
                assert(for_early_condition_block != NULL);
            }

            lyir_value* for_condition_block = NULL;
            if (!has_always_true_condition) {
                for_condition_block = lyir_value_function_block_append(function, LCA_SV_EMPTY);
                assert(for_condition_block != NULL);
            }

            lyir_value* for_pass_block = lyir_value_function_block_append(function, LCA_SV_EMPTY);
            assert(for_pass_block != NULL);

            lyir_value* for_increment_block = NULL;
            if (has_increment && !laye_type_is_noreturn(node->_for.pass->type)) {
                for_increment_block = lyir_value_function_block_append(function, LCA_SV_EMPTY);
                assert(for_increment_block != NULL);
            }

            lyir_value* for_fail_block = NULL;
            if (!has_always_true_condition && node->_for.fail != NULL) {
                for_fail_block = lyir_value_function_block_append(function, LCA_SV_EMPTY);
                assert(for_fail_block != NULL);
            }

            lyir_value* for_join_block = NULL;
            if (requires_join_block) {
                for_join_block = lyir_value_function_block_append(function, LCA_SV_EMPTY);
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

                lyir_build_branch(builder, node->_for.condition->location, for_early_condition_block);
                lyir_builder_position_at_end(builder, for_early_condition_block);

                lyir_value* early_condition_value = laye_generate_node(irgen, builder, node->_for.condition);
                assert(early_condition_value != NULL);

                if (laye_type_is_noreturn(node->_for.condition->type)) {
                    assert(lyir_value_block_is_terminated(lyir_builder_insert_block_get(builder)));
                    return lyir_void_constant_create(context);
                }

                lyir_build_branch_conditional(builder, node->_for.condition->location, early_condition_value, for_pass_block, for_fail_block);
            }

            // 3. regular condition for looping
            if (has_always_true_condition) {
                // assert(node->_for.condition != NULL && "condition == NULL should be an infinite loop, not a C style while/for loop");
                lyir_location condition_location = node->_for.condition != NULL ? node->_for.condition->location : node->location;
                lyir_build_branch(builder, condition_location, for_pass_block);
            } else {
                assert(for_condition_block != NULL);

                lyir_build_branch(builder, node->_for.condition->location, for_condition_block);
                lyir_builder_position_at_end(builder, for_condition_block);

                lyir_value* condition_value = laye_generate_node(irgen, builder, node->_for.condition);
                assert(condition_value != NULL);

                if (!requires_join_block) {
                    assert(lyir_value_block_is_terminated(lyir_builder_insert_block_get(builder)));
                    return lyir_void_constant_create(context);
                }

                lyir_build_branch_conditional(builder, node->_for.condition->location, condition_value, for_pass_block, for_join_block);
            }

            // 4. handle incrementing and returning to the condition (or pass if the condition is always true)
            if (has_increment && !laye_type_is_noreturn(node->_for.pass->type)) {
                assert(for_increment_block != NULL);

                lyir_builder_position_at_end(builder, for_increment_block);
                laye_generate_node(irgen, builder, node->_for.increment);

                if (laye_type_is_noreturn(node->_for.condition->type)) {
                    assert(lyir_value_block_is_terminated(lyir_builder_insert_block_get(builder)));
                } else {
                    if (has_always_true_condition) {
                        lyir_build_branch(builder, node->_for.increment->location, for_pass_block);
                    } else {
                        lyir_build_branch(builder, node->_for.increment->location, for_condition_block);
                    }
                }
            }

            // 5. generate the "pass" loop body
            lyir_builder_position_at_end(builder, for_pass_block);
            laye_generate_node(irgen, builder, node->_for.pass);

            if (laye_type_is_noreturn(node->_for.pass->type)) {
                assert(lyir_value_block_is_terminated(lyir_builder_insert_block_get(builder)));
            } else {
                if (!lyir_value_block_is_terminated(lyir_builder_insert_block_get(builder))) {
                    if (for_increment_block != NULL) {
                        lyir_build_branch(builder, node->_for.pass->location, for_increment_block);
                    } else {
                        if (has_always_true_condition) {
                            lyir_build_branch(builder, node->_for.pass->location, for_pass_block);
                        } else {
                            lyir_build_branch(builder, node->_for.pass->location, for_condition_block);
                        }
                    }
                }
            }

            // 6. generate the "fail" `else` body
            if (!has_always_true_condition && node->_for.fail != NULL) {
                assert(for_fail_block != NULL);

                lyir_builder_position_at_end(builder, for_fail_block);
                laye_generate_node(irgen, builder, node->_for.fail);

                if (!requires_join_block) {
                    assert(lyir_value_block_is_terminated(lyir_builder_insert_block_get(builder)));
                } else {
                    lyir_build_branch(builder, node->_for.fail->location, for_join_block);
                }
            }

            // 7. the loop is done, continue with the remaining code : )
            if (laye_type_is_noreturn(node->type)) {
                assert(lyir_value_block_is_terminated(lyir_builder_insert_block_get(builder)));
                return lyir_void_constant_create(context);
            } else {
                assert(for_join_block != NULL);
                lyir_builder_position_at_end(builder, for_join_block);
            }

            return lyir_void_constant_create(context);
        }

        case LAYE_NODE_WHILE: {
            assert(node->_while.pass != NULL);

            bool has_breaks = node->_while.has_breaks;
            bool has_continues = node->_while.has_continues;

            bool has_always_true_condition = !has_breaks && node->_while.condition == NULL || (node->_while.condition->kind == LAYE_NODE_EVALUATED_CONSTANT && node->_while.condition->evaluated_constant.result.bool_value);

            bool requires_join_block = has_breaks || !laye_type_is_noreturn(node->type);

            lyir_value* while_early_condition_block = NULL;
            if (!has_always_true_condition && node->_while.fail != NULL) {
                while_early_condition_block = lyir_value_function_block_append(function, LCA_SV_EMPTY);
                assert(while_early_condition_block != NULL);
            }

            lyir_value* while_condition_block = NULL;
            if (!has_always_true_condition) {
                while_condition_block = lyir_value_function_block_append(function, LCA_SV_EMPTY);
                assert(while_condition_block != NULL);
            }

            lyir_value* while_pass_block = lyir_value_function_block_append(function, LCA_SV_EMPTY);
            assert(while_pass_block != NULL);

            lyir_value* while_fail_block = NULL;
            if (!has_always_true_condition && node->_while.fail != NULL) {
                while_fail_block = lyir_value_function_block_append(function, LCA_SV_EMPTY);
                assert(while_fail_block != NULL);
            }

            lyir_value* while_join_block = NULL;
            if (requires_join_block) {
                while_join_block = lyir_value_function_block_append(function, LCA_SV_EMPTY);
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

                lyir_build_branch(builder, node->_while.condition->location, while_early_condition_block);
                lyir_builder_position_at_end(builder, while_early_condition_block);

                lyir_value* early_condition_value = laye_generate_node(irgen, builder, node->_while.condition);
                assert(early_condition_value != NULL);

                if (laye_type_is_noreturn(node->_while.condition->type)) {
                    assert(lyir_value_block_is_terminated(lyir_builder_insert_block_get(builder)));
                    return lyir_void_constant_create(context);
                }

                lyir_build_branch_conditional(builder, node->_while.condition->location, early_condition_value, while_pass_block, while_fail_block);
            }

            // 3. regular condition for looping
            if (has_always_true_condition) {
                // assert(node->_while.condition != NULL && "condition == NULL should be an infinite loop, not a C style while/for loop");
                lyir_location condition_location = node->_while.condition != NULL ? node->_while.condition->location : node->location;
                lyir_build_branch(builder, condition_location, while_pass_block);
            } else {
                assert(while_condition_block != NULL);

                lyir_build_branch(builder, node->_while.condition->location, while_condition_block);
                lyir_builder_position_at_end(builder, while_condition_block);

                lyir_value* condition_value = laye_generate_node(irgen, builder, node->_while.condition);
                assert(condition_value != NULL);

                if (!requires_join_block) {
                    assert(lyir_value_block_is_terminated(lyir_builder_insert_block_get(builder)));
                    return lyir_void_constant_create(context);
                }

                lyir_build_branch_conditional(builder, node->_while.condition->location, condition_value, while_pass_block, while_join_block);
            }

            // 4. generate the "pass" loop body
            lyir_builder_position_at_end(builder, while_pass_block);
            laye_generate_node(irgen, builder, node->_while.pass);

            if (laye_type_is_noreturn(node->_while.pass->type)) {
                assert(lyir_value_block_is_terminated(lyir_builder_insert_block_get(builder)));
            } else {
                if (!lyir_value_block_is_terminated(lyir_builder_insert_block_get(builder))) {
                    if (has_always_true_condition) {
                        lyir_build_branch(builder, node->_while.pass->location, while_pass_block);
                    } else {
                        lyir_build_branch(builder, node->_while.pass->location, while_condition_block);
                    }
                }
            }

            // 5. generate the "fail" `else` body
            if (!has_always_true_condition && node->_while.fail != NULL) {
                assert(while_fail_block != NULL);

                lyir_builder_position_at_end(builder, while_fail_block);
                laye_generate_node(irgen, builder, node->_while.fail);

                if (!requires_join_block) {
                    assert(lyir_value_block_is_terminated(lyir_builder_insert_block_get(builder)));
                } else {
                    lyir_build_branch(builder, node->_while.fail->location, while_join_block);
                }
            }

            // 6. the loop is done, continue with the remaining code : )
            if (laye_type_is_noreturn(node->type)) {
                assert(lyir_value_block_is_terminated(lyir_builder_insert_block_get(builder)));
                return lyir_void_constant_create(context);
            } else {
                assert(while_join_block != NULL);
                lyir_builder_position_at_end(builder, while_join_block);
            }

            return lyir_void_constant_create(context);
        }

        case LAYE_NODE_RETURN: {
            if (node->_return.value == NULL) {
                return lyir_build_return_void(builder, node->location);
            }

            lyir_value* return_value = laye_generate_node(irgen, builder, node->_return.value);
            assert(return_value != NULL);
            return lyir_build_return(builder, node->location, return_value);
        }

        case LAYE_NODE_YIELD: {
            lyir_value* yield_value = laye_generate_node(irgen, builder, node->yield.value);
            assert(yield_value != NULL);
            return yield_value;
        }

        case LAYE_NODE_BREAK: {
            assert(node->_break.target_node != NULL);
            switch (node->_break.target_node->kind) {
                default: assert(false && "unreachable"); return NULL;

                case LAYE_NODE_FOR: {
                    assert(node->_break.target_node->_for.break_target_block != NULL);
                    return lyir_build_branch(builder, node->location, node->_break.target_node->_for.break_target_block);
                }

                case LAYE_NODE_FOREACH: {
                    assert(node->_break.target_node->foreach.break_target_block != NULL);
                    return lyir_build_branch(builder, node->location, node->_break.target_node->foreach.break_target_block);
                }

                case LAYE_NODE_WHILE: {
                    assert(node->_break.target_node->_while.break_target_block != NULL);
                    return lyir_build_branch(builder, node->location, node->_break.target_node->_while.break_target_block);
                }

                case LAYE_NODE_DOWHILE: {
                    assert(node->_break.target_node->dowhile.break_target_block != NULL);
                    return lyir_build_branch(builder, node->location, node->_break.target_node->dowhile.break_target_block);
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
                    return lyir_build_branch(builder, node->location, node->_continue.target_node->_for.continue_target_block);
                }

                case LAYE_NODE_FOREACH: {
                    assert(node->_continue.target_node->foreach.continue_target_block != NULL);
                    return lyir_build_branch(builder, node->location, node->_continue.target_node->foreach.continue_target_block);
                }

                case LAYE_NODE_WHILE: {
                    assert(node->_continue.target_node->_while.continue_target_block != NULL);
                    return lyir_build_branch(builder, node->location, node->_continue.target_node->_while.continue_target_block);
                }

                case LAYE_NODE_DOWHILE: {
                    assert(node->_continue.target_node->dowhile.continue_target_block != NULL);
                    return lyir_build_branch(builder, node->location, node->_continue.target_node->dowhile.continue_target_block);
                }
            }

            assert(false && "unreachable");
            return NULL;
        }

        case LAYE_NODE_XYZZY: {
            return lyir_build_nop(builder, node->location);
        }

        case LAYE_NODE_ASSIGNMENT: {
            lyir_value* lhs_value = laye_generate_node(irgen, builder, node->assignment.lhs);
            assert(lhs_value != NULL);
            assert(lyir_type_is_ptr(lyir_value_type_get(lhs_value)));
            lyir_value* rhs_value = laye_generate_node(irgen, builder, node->assignment.rhs);
            assert(rhs_value != NULL);
            return lyir_build_store(builder, node->location, lhs_value, rhs_value);
        }

        case LAYE_NODE_COMPOUND: {
            lyir_value* result_value = NULL;

            for (int64_t i = 0, count = lca_da_count(node->compound.children); i < count; i++) {
                laye_node* child = node->compound.children[i];
                assert(child != NULL);

                lyir_value* child_value = laye_generate_node(irgen, builder, child);
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
                if (!lyir_value_block_is_terminated(lyir_builder_insert_block_get(builder))) {
                    lyir_build_unreachable(builder, (lyir_location){0});
                }
            }

            if (result_value == NULL) {
                result_value = lyir_void_constant_create(context);
            }

            return result_value;
        }

        case LAYE_NODE_CTOR: {
            assert(false && "should never hit ctor in this case");
        } break;

        case LAYE_NODE_CAST: {
            laye_type from = node->cast.operand->type;
            laye_type to = node->type;

            lyir_type* cast_type = laye_convert_type(node->type);
            lyir_value* operand = laye_generate_node(irgen, builder, node->cast.operand);

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
                            return lyir_build_bitcast(builder, node->location, operand, cast_type);
                        } else if (from_sz < to_sz) {
                            if (laye_type_is_signed_int(from)) {
                                return lyir_build_sign_extend(builder, node->location, operand, cast_type);
                            } else {
                                return lyir_build_zero_extend(builder, node->location, operand, cast_type);
                            }
                        } else if (from_sz > to_sz) {
                            return lyir_build_truncate(builder, node->location, operand, cast_type);
                        }
                    }

                    if (laye_type_is_float(from) && laye_type_is_float(to)) {
                        int64_t from_sz = laye_type_size_in_bits(from);
                        int64_t to_sz = laye_type_size_in_bits(to);

                        if (from_sz < to_sz) {
                            return lyir_build_fpext(builder, node->location, operand, cast_type);
                        } else if (from_sz > to_sz) {
                            return lyir_build_fptrunc(builder, node->location, operand, cast_type);
                        }
                    }

                    if (laye_type_is_int(from) && laye_type_is_float(to)) {
                        if (laye_type_is_unsigned_int(from)) {
                            return lyir_build_uitofp(builder, node->location, operand, laye_convert_type(to));
                        } else {
                            assert(laye_type_is_signed_int(from));
                            return lyir_build_sitofp(builder, node->location, operand, laye_convert_type(to));
                        }
                    }

                    if (laye_type_is_float(from) && laye_type_is_int(to)) {
                        int64_t from_sz = laye_type_size_in_bits(from);
                        int64_t to_sz = laye_type_size_in_bits(to);
                        assert(false && "fp -> int cast irgen");
                    }

                    assert(false && "todo irgen cast");
                    return NULL;
                }

                case LAYE_CAST_REFERENCE_TO_LVALUE:
                case LAYE_CAST_LVALUE_TO_REFERENCE: {
                    return operand;
                }

                case LAYE_CAST_LVALUE_TO_RVALUE: {
                    return lyir_build_load(builder, node->location, operand, cast_type);
                }
            }
        }

        case LAYE_NODE_UNARY: {
            lyir_value* operand_value = laye_generate_node(irgen, builder, node->unary.operand);
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
                    return lyir_build_neg(builder, node->location, operand_value);
                }

                case '~': {
                    return lyir_build_compl(builder, node->location, operand_value);
                }

                case '&':
                case '*': {
                    return operand_value;
                }

                case LAYE_TOKEN_NOT: {
                    lyir_type* operand_type = lyir_value_type_get(operand_value);
                    assert(lyir_type_is_integer(operand_type));
                    return lyir_build_icmp_eq(builder, node->location, lyir_int_constant_create(context, node->location, operand_type, 0), operand_value);
                }
            }
        }

        case LAYE_NODE_BINARY: {
            bool is_short_circuit = node->binary.operator.kind == LAYE_TOKEN_AND || node->binary.operator.kind == LAYE_TOKEN_OR;

            bool are_signed_ints = laye_type_is_signed_int(node->binary.lhs->type) && laye_type_is_signed_int(node->binary.rhs->type);
            bool are_floats = laye_type_is_float(node->binary.lhs->type) && laye_type_is_float(node->binary.rhs->type);
            bool are_signed = are_signed_ints || are_floats;

            lyir_value* lhs_value = laye_generate_node(irgen, builder, node->binary.lhs);
            assert(lhs_value != NULL);

            lyir_value* rhs_value = NULL;
            if (is_short_circuit) {
                bool is_or = node->binary.operator.kind == LAYE_TOKEN_OR;

                lyir_type* bool_type = lyir_int_type(context, 1);

                lyir_value* lhs_block = lyir_builder_insert_block_get(builder);
                assert(lhs_block != NULL);
                lyir_value* rhs_block = lyir_value_function_block_append(function, LCA_SV_EMPTY);
                assert(rhs_block != NULL);
                lyir_value* merge_block = lyir_value_function_block_append(function, LCA_SV_EMPTY);
                assert(merge_block != NULL);

                if (is_or) {
                    lyir_build_branch_conditional(builder, node->location, lhs_value, merge_block, rhs_block);
                } else {
                    lyir_build_branch_conditional(builder, node->location, lhs_value, rhs_block, merge_block);
                }

                lyir_builder_position_at_end(builder, rhs_block);
                rhs_value = laye_generate_node(irgen, builder, node->binary.rhs);
                assert(rhs_value != NULL);
                lyir_build_branch(builder, node->location, merge_block);

                lyir_builder_position_at_end(builder, merge_block);
                lyir_value* phi = lyir_build_phi(builder, node->location, bool_type);
                lyir_value_phi_incoming_value_add(phi, lhs_value, lhs_block);
                lyir_value_phi_incoming_value_add(phi, rhs_value, rhs_block);

                return phi;
            }

            rhs_value = laye_generate_node(irgen, builder, node->binary.rhs);
            assert(rhs_value != NULL);

            switch (node->binary.operator.kind) {
                default: {
                    fprintf(stderr, "for token kind %s\n", laye_token_kind_to_cstring(node->unary.operator.kind));
                    assert(false && "unimplemented binary operator in irgen");
                    return NULL;
                }

                case LAYE_TOKEN_AND:
                case LAYE_TOKEN_OR: {
                    // already implemented above
                    assert(false);
                }

                case LAYE_TOKEN_XOR: {
                    assert(lyir_type_is_integer(lyir_value_type_get(lhs_value)));
                    return lyir_build_icmp_ne(builder, node->location, lhs_value, rhs_value);
                }

                case LAYE_TOKEN_PLUS: {
                    if (laye_type_is_buffer(node->binary.lhs->type)) {
                        lyir_type* element_type = laye_convert_type(node->binary.lhs->type.node->type_container.element_type);
                        int64_t element_size_in_bytes = lyir_type_size_in_bytes(element_type);
                        rhs_value = lyir_build_mul(builder, node->binary.rhs->location, rhs_value, lyir_int_constant_create(context, node->binary.rhs->location, lyir_int_type(context, lyir_type_size_in_bits(lyir_value_type_get(rhs_value))), element_size_in_bytes));
                        return lyir_build_ptradd(builder, node->location, lhs_value, rhs_value);
                    }

                    if (laye_type_is_buffer(node->binary.rhs->type)) {
                        lyir_type* element_type = laye_convert_type(node->binary.rhs->type.node->type_container.element_type);
                        int64_t element_size_in_bytes = lyir_type_size_in_bytes(element_type);
                        lhs_value = lyir_build_mul(builder, node->binary.lhs->location, lhs_value, lyir_int_constant_create(context, node->binary.lhs->location, lyir_int_type(context, lyir_type_size_in_bits(lyir_value_type_get(lhs_value))), element_size_in_bytes));
                        return lyir_build_ptradd(builder, node->location, rhs_value, lhs_value);
                    }

                    if (are_floats) {
                        return lyir_build_fadd(builder, node->location, lhs_value, rhs_value);
                    }

                    assert(lyir_type_is_integer(lyir_value_type_get(lhs_value)));
                    return lyir_build_add(builder, node->location, lhs_value, rhs_value);
                }

                case LAYE_TOKEN_MINUS: {
                    if (laye_type_is_buffer(node->binary.lhs->type)) {
                        assert(lyir_type_is_integer(lyir_value_type_get(rhs_value)));
                        lyir_type* element_type = laye_convert_type(node->binary.lhs->type.node->type_container.element_type);
                        int64_t element_size_in_bytes = lyir_type_size_in_bytes(element_type);
                        rhs_value = lyir_build_mul(builder, node->binary.rhs->location, rhs_value, lyir_int_constant_create(context, node->binary.rhs->location, lyir_int_type(context, lyir_type_size_in_bits(lyir_value_type_get(rhs_value))), element_size_in_bytes));
                        lyir_value* negated = lyir_build_sub(builder, node->binary.rhs->location, lyir_int_constant_create(context, node->binary.rhs->location, lyir_int_type(context, lyir_type_size_in_bits(lyir_value_type_get(rhs_value))), 0), rhs_value);
                        return lyir_build_ptradd(builder, node->location, lhs_value, negated);
                    }

                    if (laye_type_is_buffer(node->binary.rhs->type)) {
                        assert(lyir_type_is_integer(lyir_value_type_get(lhs_value)));
                        lyir_type* element_type = laye_convert_type(node->binary.rhs->type.node->type_container.element_type);
                        int64_t element_size_in_bytes = lyir_type_size_in_bytes(element_type);
                        lhs_value = lyir_build_mul(builder, node->binary.lhs->location, lhs_value, lyir_int_constant_create(context, node->binary.lhs->location, lyir_int_type(context, lyir_type_size_in_bits(lyir_value_type_get(lhs_value))), element_size_in_bytes));
                        lyir_value* negated = lyir_build_sub(builder, node->binary.lhs->location, lyir_int_constant_create(context, node->binary.lhs->location, lyir_int_type(context, lyir_type_size_in_bits(lyir_value_type_get(lhs_value))), 0), lhs_value);
                        return lyir_build_ptradd(builder, node->location, rhs_value, negated);
                    }

                    if (are_floats) {
                        return lyir_build_fsub(builder, node->location, lhs_value, rhs_value);
                    }

                    assert(lyir_type_is_integer(lyir_value_type_get(lhs_value)));
                    return lyir_build_sub(builder, node->location, lhs_value, rhs_value);
                }

                case LAYE_TOKEN_STAR: {
                    if (are_floats) {
                        return lyir_build_fmul(builder, node->location, lhs_value, rhs_value);
                    }

                    assert(lyir_type_is_integer(lyir_value_type_get(lhs_value)));
                    return lyir_build_mul(builder, node->location, lhs_value, rhs_value);
                }

                case LAYE_TOKEN_SLASH: {
                    if (are_floats) {
                        return lyir_build_fdiv(builder, node->location, lhs_value, rhs_value);
                    }

                    assert(lyir_type_is_integer(lyir_value_type_get(lhs_value)));
                    if (are_signed) {
                        return lyir_build_sdiv(builder, node->location, lhs_value, rhs_value);
                    } else {
                        return lyir_build_udiv(builder, node->location, lhs_value, rhs_value);
                    }
                }

                case LAYE_TOKEN_PERCENT: {
                    if (are_floats) {
                        return lyir_build_fmod(builder, node->location, lhs_value, rhs_value);
                    }

                    assert(lyir_type_is_integer(lyir_value_type_get(lhs_value)));
                    if (are_signed) {
                        return lyir_build_smod(builder, node->location, lhs_value, rhs_value);
                    } else {
                        return lyir_build_umod(builder, node->location, lhs_value, rhs_value);
                    }
                }

                case LAYE_TOKEN_AMPERSAND: {
                    assert(lyir_type_is_integer(lyir_value_type_get(lhs_value)));
                    return lyir_build_and(builder, node->location, lhs_value, rhs_value);
                }

                case LAYE_TOKEN_PIPE: {
                    assert(lyir_type_is_integer(lyir_value_type_get(lhs_value)));
                    return lyir_build_or(builder, node->location, lhs_value, rhs_value);
                }

                case LAYE_TOKEN_TILDE: {
                    assert(lyir_type_is_integer(lyir_value_type_get(lhs_value)));
                    return lyir_build_xor(builder, node->location, lhs_value, rhs_value);
                }

                case LAYE_TOKEN_LESSLESS: {
                    assert(lyir_type_is_integer(lyir_value_type_get(lhs_value)));
                    return lyir_build_shl(builder, node->location, lhs_value, rhs_value);
                }

                case LAYE_TOKEN_GREATERGREATER: {
                    assert(lyir_type_is_integer(lyir_value_type_get(lhs_value)));
                    if (are_signed) {
                        return lyir_build_sar(builder, node->location, lhs_value, rhs_value);
                    } else {
                        return lyir_build_shr(builder, node->location, lhs_value, rhs_value);
                    }
                }

                case LAYE_TOKEN_EQUALEQUAL: {
                    if (are_floats) {
                        return lyir_build_fcmp_oeq(builder, node->location, lhs_value, rhs_value);
                    }

                    assert(lyir_type_is_integer(lyir_value_type_get(lhs_value)));
                    return lyir_build_icmp_eq(builder, node->location, lhs_value, rhs_value);
                }

                case LAYE_TOKEN_BANGEQUAL: {
                    if (are_floats) {
                        return lyir_build_fcmp_one(builder, node->location, lhs_value, rhs_value);
                    }

                    assert(lyir_type_is_integer(lyir_value_type_get(lhs_value)));
                    return lyir_build_icmp_ne(builder, node->location, lhs_value, rhs_value);
                }

                case LAYE_TOKEN_LESS: {
                    if (are_floats) {
                        return lyir_build_fcmp_olt(builder, node->location, lhs_value, rhs_value);
                    }

                    assert(lyir_type_is_integer(lyir_value_type_get(lhs_value)));
                    if (are_signed) {
                        return lyir_build_icmp_slt(builder, node->location, lhs_value, rhs_value);
                    } else {
                        return lyir_build_icmp_ult(builder, node->location, lhs_value, rhs_value);
                    }
                }

                case LAYE_TOKEN_LESSEQUAL: {
                    if (are_floats) {
                        return lyir_build_fcmp_ole(builder, node->location, lhs_value, rhs_value);
                    }

                    assert(lyir_type_is_integer(lyir_value_type_get(lhs_value)));
                    if (are_signed) {
                        return lyir_build_icmp_sle(builder, node->location, lhs_value, rhs_value);
                    } else {
                        return lyir_build_icmp_ule(builder, node->location, lhs_value, rhs_value);
                    }
                }

                case LAYE_TOKEN_GREATER: {
                    if (are_floats) {
                        return lyir_build_fcmp_ogt(builder, node->location, lhs_value, rhs_value);
                    }

                    assert(lyir_type_is_integer(lyir_value_type_get(lhs_value)));
                    if (are_signed) {
                        return lyir_build_icmp_sgt(builder, node->location, lhs_value, rhs_value);
                    } else {
                        return lyir_build_icmp_ugt(builder, node->location, lhs_value, rhs_value);
                    }
                }

                case LAYE_TOKEN_GREATEREQUAL: {
                    if (are_floats) {
                        return lyir_build_fcmp_oge(builder, node->location, lhs_value, rhs_value);
                    }

                    assert(lyir_type_is_integer(lyir_value_type_get(lhs_value)));
                    if (are_signed) {
                        return lyir_build_icmp_sge(builder, node->location, lhs_value, rhs_value);
                    } else {
                        return lyir_build_icmp_uge(builder, node->location, lhs_value, rhs_value);
                    }
                }
            }
        }

        case LAYE_NODE_NAMEREF: {
            assert(node->nameref.referenced_declaration != NULL);
            lyir_value* ir_value_referenced = laye_irgen_ir_value_get(irgen, node->module, node->nameref.referenced_declaration);
            assert(ir_value_referenced != NULL);
            // assert(node->nameref.referenced_declaration->ir_value != NULL);
            return ir_value_referenced;
        }

        case LAYE_NODE_CALL: {
            lyir_value* callee = laye_generate_node(irgen, builder, node->call.callee);
            assert(callee != NULL);

            lyir_type* callee_type = lyir_value_type_get(callee);
            if (lyir_type_is_ptr(callee_type)) {
                assert(false && "gotta get the function type, not just the pointer");
            }

            lca_da(lyir_value*) argument_values = NULL;
            for (int64_t i = 0, count = lca_da_count(node->call.arguments); i < count; i++) {
                lyir_value* argument_value = laye_generate_node(irgen, builder, node->call.arguments[i]);
                lca_da_push(argument_values, argument_value);
                assert(argument_values[i] != NULL);
            }

            return lyir_build_call(builder, node->location, callee, callee_type, argument_values, LCA_SV_EMPTY);
        }

        case LAYE_NODE_INDEX: {
            lyir_value* value = laye_generate_node(irgen, builder, node->index.value);
            assert(value != NULL);

            lca_da(lyir_value*) indices = NULL;
            for (int64_t i = 0, count = lca_da_count(node->index.indices); i < count; i++) {
                lyir_value* index_value = laye_generate_node(irgen, builder, node->index.indices[i]);
                lca_da_push(indices, index_value);
                assert(indices[i] != NULL);
            }

            assert(lyir_type_is_ptr(lyir_value_type_get(value)));

            laye_type lhs_type = node->index.value->type;
            assert(lhs_type.node != NULL);

            lyir_type* lhs_ir_type = laye_convert_type(lhs_type);

            if (laye_type_is_array(lhs_type)) {
                lyir_type* element_type = lyir_type_element_type_get(lhs_ir_type);
                assert(element_type != NULL);

                lyir_value* element_size_value = lyir_int_constant_create(context, lhs_type.node->location, lyir_int_type(context, 64), lyir_type_size_in_bytes(element_type));
                assert(element_size_value != NULL);

                assert(lca_da_count(indices) == lca_da_count(lhs_type.node->type_container.length_values));

                lyir_value* calc_index_value = indices[lca_da_count(indices) - 1];
                int64_t current_stride = 1;

                for (int64_t i = lca_da_count(indices) - 2; i >= 0; i--) {
                    laye_node* length_value = lhs_type.node->type_container.length_values[i + 1];
                    assert(length_value != NULL);
                    assert(length_value->kind == LAYE_NODE_EVALUATED_CONSTANT);
                    assert(length_value->evaluated_constant.result.kind == LYIR_EVAL_INT);
                    int64_t next_length = length_value->evaluated_constant.result.int_value;
                    assert(next_length >= 0);

                    current_stride *= next_length;
                    lyir_value* stride_constant = lyir_int_constant_create(context, length_value->location, lyir_int_type(context, context->target->size_of_pointer), current_stride);
                    lyir_value* curr_index_value = lyir_build_mul(builder, node->index.indices[i]->location, indices[i], stride_constant);

                    calc_index_value = lyir_build_add(builder, node->index.indices[i]->location, calc_index_value, curr_index_value);
                }

                lca_da_free(indices);

                calc_index_value = lyir_build_mul(builder, node->location, calc_index_value, element_size_value);
                return lyir_build_ptradd(builder, node->location, value, calc_index_value);
            } else if (laye_type_is_buffer(lhs_type)) {
                lyir_type* element_type = laye_convert_type(lhs_type.node->type_container.element_type);
                assert(element_type != NULL);

                lyir_value* element_size_value = lyir_int_constant_create(context, lhs_type.node->location, lyir_int_type(context, 64), lyir_type_size_in_bytes(element_type));
                assert(element_size_value != NULL);

                assert(lca_da_count(indices) == 1);
                lyir_value* calc_index_value = indices[0];

                lca_da_free(indices);

                calc_index_value = lyir_build_mul(builder, node->location, calc_index_value, element_size_value);
                return lyir_build_ptradd(builder, node->location, value, calc_index_value);
            } else {
                fprintf(stderr, "for layec_type %s\n", lyir_type_kind_to_cstring(lyir_type_kind_get(lhs_ir_type)));
                assert(false && "unsupported indexable type");
                return NULL;
            }
        }

        case LAYE_NODE_MEMBER: {
            lyir_value* address = laye_generate_node(irgen, builder, node->member.value);
            assert(address != NULL);
            assert(lyir_type_is_ptr(lyir_value_type_get(address)));

            int64_t member_offset = node->member.member_offset;
            assert(member_offset >= 0);

            lyir_value* offset = lyir_int_constant_create(context, node->location, lyir_int_type(context, 64), member_offset);
            assert(offset != NULL);

            return lyir_build_ptradd(builder, node->location, address, offset);
        }

        case LAYE_NODE_LITBOOL: {
            lyir_type* type = laye_convert_type(node->type);
            assert(type != NULL);
            assert(lyir_type_is_integer(type));
            return lyir_int_constant_create(context, node->location, type, node->litbool.value ? 1 : 0);
        }

        case LAYE_NODE_LITINT: {
            lyir_type* type = laye_convert_type(node->type);
            assert(type != NULL);
            assert(lyir_type_is_integer(type));
            return lyir_int_constant_create(context, node->location, type, node->litint.value);
        }

        case LAYE_NODE_LITSTRING: {
            lyir_type* type = laye_convert_type(node->type);
            // asserts are only to validate that we're expecting a pointer value
            assert(type != NULL);
            assert(lyir_type_is_ptr(type));
            return lyir_module_create_global_string_ptr(module, node->location, node->litstring.value);
        }

        case LAYE_NODE_EVALUATED_CONSTANT: {
            lyir_type* type = laye_convert_type(node->type);
            assert(type != NULL);

            if (node->evaluated_constant.result.kind == LYIR_EVAL_INT) {
                assert(lyir_type_is_integer(type));
                return lyir_int_constant_create(context, node->location, type, node->evaluated_constant.result.int_value);
            } else if (node->evaluated_constant.result.kind == LYIR_EVAL_FLOAT) {
                assert(lyir_type_is_float(type));
                return lyir_float_constant_create(context, node->location, type, node->evaluated_constant.result.float_value);
            } else if (node->evaluated_constant.result.kind == LYIR_EVAL_STRING) {
                // assert is only to validate that we're expecting a pointer value
                assert(lyir_type_is_ptr(type));
                return lyir_module_create_global_string_ptr(module, node->location, node->evaluated_constant.result.string_value);
            } else {
                assert(false && "unsupported/unimplemented constant kind in irgen");
                return NULL;
            }
        }
    }

    assert(false && "unreachable");
}
