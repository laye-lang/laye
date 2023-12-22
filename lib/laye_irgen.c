#include "layec.h"

#include <assert.h>

static layec_type* laye_convert_type(laye_node* type);

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
        }
    }

    // 3. Generate function bodies

    return ir_module;
}

static layec_type* laye_convert_type(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    laye_module* module = type->module;
    assert(module != NULL);
    layec_context* context = module->context;
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
    }
}
