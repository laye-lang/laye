#include <assert.h>

#include "layec.h"

void laye_module_destroy(laye_module* module) {
    if (module == NULL) return;

    assert(module->context != NULL);
    lca_allocator allocator = module->context->allocator;

    for (int64_t i = 0, count = arr_count(module->_all_tokens); i < count; i++) {
        laye_token token = module->_all_tokens[i];
        arr_free(token.leading_trivia);
        arr_free(token.trailing_trivia);
    }

    for (int64_t i = 0, count = arr_count(module->_all_nodes); i < count; i++) {
        laye_node* node = module->_all_nodes[i];
        assert(node != NULL);
        laye_node_destroy(node);
    }

    for (int64_t i = 0, count = arr_count(module->_all_scopes); i < count; i++) {
        laye_scope* scope = module->_all_scopes[i];
        assert(scope != NULL);
        laye_scope_destroy(scope);
    }

    arr_free(module->_all_tokens);
    arr_free(module->_all_nodes);
    arr_free(module->_all_scopes);

    arr_free(module->top_level_nodes);

    lca_arena_destroy(module->arena);

    *module = (laye_module){};
    lca_deallocate(allocator, module);
}

layec_source laye_module_get_source(laye_module* module) {
    assert(module != NULL);
    assert(module->context != NULL);
    assert(module->sourceid >= 0);
    return layec_context_get_source(module->context, module->sourceid);
}

laye_scope* laye_scope_create(laye_module* module, laye_scope* parent) {
    assert(module != NULL);
    assert(module->arena != NULL);
    laye_scope* scope = lca_arena_push(module->arena, sizeof *scope);
    assert(scope != NULL);
    scope->module = module;
    scope->parent = parent;
    arr_push(module->_all_scopes, scope);
    return scope;
}

void laye_scope_destroy(laye_scope* scope) {
    if (scope == NULL) return;
    arr_free(scope->type_declarations);
    arr_free(scope->value_declarations);
    *scope = (laye_scope){};
}

void laye_scope_declare(laye_scope* scope, laye_node* declaration) {
    assert(scope != NULL);
    assert(declaration != NULL);
    assert(laye_node_is_decl(declaration));
    assert(declaration->kind != LAYE_NODE_DECL_OVERLOADS);

    laye_module* module = scope->module;
    assert(module != NULL);

    bool is_type_declaration = declaration->kind == LAYE_NODE_DECL_STRUCT || declaration->kind == LAYE_NODE_DECL_ENUM || declaration->kind == LAYE_NODE_DECL_ALIAS || declaration->kind == LAYE_NODE_DECL_TEMPLATE_TYPE;
    dynarr(laye_node*)* entity_namespace = is_type_declaration ? &scope->type_declarations : &scope->value_declarations;
    assert(entity_namespace != NULL);

    if (!is_type_declaration) {
        for (int64_t i = 0, count = arr_count(*entity_namespace); i < count; i++) {
            laye_node* existing_declaration = (*entity_namespace)[i];
            assert(existing_declaration != NULL);

            if (
                string_equals(existing_declaration->declared_name, declaration->declared_name) &&
                (declaration->kind != LAYE_NODE_DECL_FUNCTION || existing_declaration->kind != LAYE_NODE_DECL_FUNCTION)
            ) {
                assert(module->context != NULL);
                layec_write_error(module->context, declaration->location, "redeclaration of '%.*s' in this scope.", STR_EXPAND(declaration->declared_name));
                return;
            }
        }
    }

    arr_push(*entity_namespace, declaration);
}

laye_node* laye_node_create(laye_module* module, laye_node_kind kind, layec_location location, laye_node* type) {
    assert(module != NULL);
    assert(module->arena != NULL);
    assert(type != NULL);
    assert(laye_node_is_type(type));
    laye_node* node = lca_arena_push(module->arena, sizeof *node);
    assert(node != NULL);
    arr_push(module->_all_nodes, node);
    node->module = module;
    node->kind = kind;
    node->location = location;
    node->type = type;
    return node;
}

laye_node* laye_node_create_in_context(layec_context* context, laye_node_kind kind, laye_node* type) {
    assert(context != NULL);
    if (kind != LAYE_NODE_TYPE_TYPE) assert(type != NULL);
    laye_node* node = lca_allocate(context->allocator, sizeof *node);
    assert(node != NULL);
    node->kind = kind;
    node->type = type;
    return node;
}

void laye_node_destroy(laye_node* node) {
    if (node == NULL) return;

    arr_free(node->template_parameters);
    arr_free(node->attribute_nodes);

    switch (node->kind) {
        default: break;

        case LAYE_NODE_DECL_IMPORT: {
            arr_free(node->decl_import.imported_names);
        } break;

        case LAYE_NODE_DECL_OVERLOADS: {
            arr_free(node->decl_overloads.declarations);
        } break;

        case LAYE_NODE_DECL_FUNCTION: {
            arr_free(node->decl_function.parameter_declarations);
        } break;

        case LAYE_NODE_DECL_STRUCT: {
            arr_free(node->decl_struct.field_declarations);
            arr_free(node->decl_struct.variant_declarations);
        } break;

        case LAYE_NODE_DECL_ENUM: {
            arr_free(node->decl_enum.variants);
        } break;

        case LAYE_NODE_COMPOUND: {
            arr_free(node->compound.children);
        } break;

        case LAYE_NODE_SWITCH: {
            arr_free(node->_switch.cases);
        } break;

        case LAYE_NODE_NAMEREF:
        case LAYE_NODE_TYPE_NAMEREF: {
            arr_free(node->nameref.pieces);
            arr_free(node->nameref.template_arguments);
        } break;

        case LAYE_NODE_INDEX: {
            arr_free(node->index.indices);
        } break;

        case LAYE_NODE_CALL: {
            arr_free(node->call.arguments);
        } break;

        case LAYE_NODE_CTOR: {
            arr_free(node->ctor.initializers);
        } break;

        case LAYE_NODE_NEW: {
            arr_free(node->new.arguments);
            arr_free(node->new.initializers);
        } break;

        case LAYE_NODE_TYPE_NILABLE:
        case LAYE_NODE_TYPE_ARRAY:
        case LAYE_NODE_TYPE_SLICE:
        case LAYE_NODE_TYPE_REFERENCE:
        case LAYE_NODE_TYPE_POINTER:
        case LAYE_NODE_TYPE_BUFFER: {
            arr_free(node->type_container.length_values);
        } break;

        case LAYE_NODE_TYPE_FUNCTION: {
            arr_free(node->type_function.parameter_types);
        } break;

        case LAYE_NODE_TYPE_STRUCT: {
            arr_free(node->type_struct.fields);
            arr_free(node->type_struct.variants);
        } break;

        case LAYE_NODE_TYPE_ENUM: {
            arr_free(node->type_enum.variants);
        } break;
    }

    *node = (laye_node){};
    // don't free the node, since it's arena allocated
}

void laye_node_set_sema_in_progress(laye_node* node) {
    assert(node != NULL);
    node->sema_state = LAYEC_SEMA_IN_PROGRESS;
}

void laye_node_set_sema_errored(laye_node* node) {
    assert(node != NULL);
    node->sema_state = LAYEC_SEMA_ERRORED;
}

void laye_node_set_sema_ok(laye_node* node) {
    assert(node != NULL);
    node->sema_state = LAYEC_SEMA_OK;
}

bool laye_node_is_sema_in_progress(laye_node* node) {
    assert(node != NULL);
    return node->sema_state == LAYEC_SEMA_IN_PROGRESS;
}

bool laye_node_is_sema_ok(laye_node* node) {
    assert(node != NULL);
    return node->sema_state == LAYEC_SEMA_OK;
}

bool laye_node_is_sema_ok_or_errored(laye_node* node) {
    assert(node != NULL);
    return node->sema_state == LAYEC_SEMA_OK || node->sema_state == LAYEC_SEMA_ERRORED;
}

bool laye_node_has_noreturn_semantics(laye_node* node) {
    assert(node != NULL);
    assert(node->type != NULL);
    return laye_type_is_noreturn(node->type);
}

bool laye_decl_is_exported(laye_node* decl) {
    assert(decl != NULL);
    assert(laye_node_is_decl(decl));
    return decl->attributes.linkage == LAYEC_LINK_EXPORTED || decl->attributes.linkage == LAYEC_LINK_REEXPORTED;
}

bool laye_decl_is_template(laye_node* decl) {
    assert(decl != NULL);
    assert(laye_node_is_decl(decl));
    return arr_count(decl->template_parameters) > 0;
}

laye_node* laye_expr_type(laye_node* expr) {
    assert(expr != NULL);
    assert(expr->type != NULL);
    return expr->type;
}

bool laye_expr_evaluate(laye_node* expr, layec_evaluated_constant* out_constant, bool is_required) {
    assert(expr != NULL);
    assert(out_constant != NULL);
    assert(!is_required || laye_node_is_sema_ok(expr) && "cannot evaluate ill-formed or unchecked expression");

    switch (expr->kind) {
        default: return false;

        case LAYE_NODE_SIZEOF: {
            int size_in_bytes = 0;

            laye_node* query = expr->_sizeof.query;
            if (laye_node_is_type(query)) {
                size_in_bytes = laye_type_size_in_bytes(query);
            } else {
                assert(query->type != NULL);
                size_in_bytes = laye_type_size_in_bytes(query->type);
            }

            out_constant->kind = LAYEC_EVAL_INT;
            out_constant->int_value = (int64_t)size_in_bytes;
            return true;
        }

        case LAYE_NODE_LITBOOL: {
            out_constant->kind = LAYEC_EVAL_BOOL;
            out_constant->bool_value = expr->litbool.value;
            return true;
        }

        case LAYE_NODE_LITINT: {
            out_constant->kind = LAYEC_EVAL_INT;
            out_constant->int_value = expr->litint.value;
            return true;
        }

        case LAYE_NODE_LITFLOAT: {
            out_constant->kind = LAYEC_EVAL_FLOAT;
            out_constant->float_value = expr->litfloat.value;
            return true;
        }

        case LAYE_NODE_LITSTRING: {
            out_constant->kind = LAYEC_EVAL_STRING;
            out_constant->string_value = expr->litstring.value;
            return true;
        }
    }
}

bool laye_expr_is_lvalue(laye_node* expr) {
    assert(expr != NULL);
    return expr->value_category == LAYEC_LVALUE;
}

bool laye_expr_is_modifiable_lvalue(laye_node* expr) {
    assert(expr != NULL);
    assert(expr->type != NULL);
    return expr->value_category == LAYEC_LVALUE && expr->type->type_is_modifiable;
}

void laye_expr_set_lvalue(laye_node* expr, bool is_lvalue) {
    assert(expr != NULL);
    expr->value_category = is_lvalue ? LAYEC_LVALUE : LAYEC_RVALUE;
}

int align_padding(int bits, int align) {
    assert(align > 0);
    return (align - (bits % align)) % align;
}

int align_to(int bits, int align) {
    return bits + align_padding(bits, align);
}

int laye_type_size_in_bytes(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    int size_in_bits = laye_type_size_in_bits(type);
    return align_to(size_in_bits, 8);
}

int laye_type_size_in_bits(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    switch (type->kind) {
        default: return 0;

        case LAYE_NODE_TYPE_BOOL: return 8;

        case LAYE_NODE_TYPE_INT:
        case LAYE_NODE_TYPE_FLOAT: {
            return type->type_primitive.bit_width;
        }

        case LAYE_NODE_TYPE_ERROR_PAIR: {
            assert(false && "todo: error pair type");
        }
    }
    return 0;
}

int laye_type_align_in_bytes(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    switch (type->kind) {
        default: return 1;

        case LAYE_NODE_TYPE_BOOL: return 1;

        case LAYE_NODE_TYPE_INT:
        case LAYE_NODE_TYPE_FLOAT: {
            return align_to(type->type_primitive.bit_width, 8) / 8;
        }

        case LAYE_NODE_TYPE_ERROR_PAIR: {
            assert(false && "todo: error pair type");
        }

        case LAYE_NODE_NAMEREF: {
            if (type->nameref.referenced_type == NULL) {
                return 1;
            }

            assert(type->nameref.referenced_type != NULL);
            return laye_type_align_in_bytes(type->nameref.referenced_type);
        }

        case LAYE_NODE_TYPE_OVERLOADS: return 1;

        case LAYE_NODE_TYPE_NILABLE: {
            assert(type->type_container.element_type != NULL);
            return laye_type_align_in_bytes(type->type_container.element_type);
        }
    }
    return 0;
}

bool laye_type_is_poison(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_POISON;
}

bool laye_type_is_void(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_VOID;
}

bool laye_type_is_noreturn(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_NORETURN;
}

bool laye_type_is_bool(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_BOOL;
}

bool laye_type_is_int(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_INT;
}

bool laye_type_is_signed_int(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_INT && type->type_primitive.is_signed;
}

bool laye_type_is_unsigned_int(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_INT && !type->type_primitive.is_signed;
}

bool laye_type_is_float(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_FLOAT;
}

bool laye_type_is_template_parameter(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_TEMPLATE_PARAMETER;
}

bool laye_type_is_error_pair(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_ERROR_PAIR;
}

bool laye_type_is_nameref(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_NAMEREF;
}

bool laye_type_is_overload(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_OVERLOADS;
}

bool laye_type_is_nilable(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_NILABLE;
}

bool laye_type_is_array(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_ARRAY;
}

bool laye_type_is_slice(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_SLICE;
}

bool laye_type_is_reference(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_REFERENCE;
}

bool laye_type_is_pointer(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_POINTER;
}

bool laye_type_is_buffer(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_BUFFER;
}

bool laye_type_is_function(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_FUNCTION;
}

bool laye_type_is_struct(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_STRUCT;
}

bool laye_type_is_variant(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_VARIANT;
}

bool laye_type_is_enum(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_ENUM;
}

bool laye_type_is_alias(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_ALIAS;
}

bool laye_type_is_strict_alias(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_STRICT_ALIAS;
}

laye_node* laye_type_strip_pointers_and_references(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));

    while (
        type->kind == LAYE_NODE_TYPE_POINTER ||
        type->kind == LAYE_NODE_TYPE_REFERENCE
    ) {
        type = type->type_container.element_type;
        assert(type != NULL);
        assert(laye_node_is_type(type));
    }

    return type;
}

laye_node* laye_type_strip_references(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));

    while (type->kind == LAYE_NODE_TYPE_REFERENCE) {
        type = type->type_container.element_type;
        assert(type != NULL);
        assert(laye_node_is_type(type));
    }

    return type;
}

bool laye_type_equals(laye_node* a, laye_node* b) {
    assert(a != NULL);
    assert(b != NULL);
    assert(laye_node_is_type(a));
    assert(laye_node_is_type(b));

    if (a == b) return true;
    if (a->type_is_modifiable != b->type_is_modifiable) return false;

    if (laye_type_is_nameref(a)) {
        assert(a->nameref.referenced_type != NULL);
        return laye_type_equals(a->nameref.referenced_type, b);
    }

    if (laye_type_is_nameref(b)) {
        assert(b->nameref.referenced_type != NULL);
        return laye_type_equals(a, b->nameref.referenced_type);
    }

    assert(!laye_type_is_nameref(a));
    assert(!laye_type_is_nameref(b));

    if (laye_type_is_alias(a)) {
        assert(a->type_alias.underlying_type != NULL);
        return laye_type_equals(a->type_alias.underlying_type, b);
    }

    if (laye_type_is_alias(b)) {
        assert(b->type_alias.underlying_type != NULL);
        return laye_type_equals(a, b->type_alias.underlying_type);
    }

    assert(!laye_type_is_alias(a));
    assert(!laye_type_is_alias(b));

    if (a->kind != b->kind) return false;

    switch (a->kind) {
        default: return false;

        case LAYE_NODE_TYPE_ALIAS:
        case LAYE_NODE_NAMEREF: {
            assert(false && "unreachable");
            return false;
        }

        case LAYE_NODE_TYPE_POISON:
        case LAYE_NODE_TYPE_VOID:
        case LAYE_NODE_TYPE_NORETURN:
        case LAYE_NODE_TYPE_BOOL: {
            return true;
        }

        case LAYE_NODE_TEMPLATE_PARAMETER: {
            assert(a->type_template_parameter.declaration != NULL);
            assert(b->type_template_parameter.declaration != NULL);
            return a->type_template_parameter.declaration == b->type_template_parameter.declaration;
        }

        case LAYE_NODE_TYPE_INT:
        case LAYE_NODE_TYPE_FLOAT: {
            if (a->type_primitive.is_platform_specified)
                return b->type_primitive.is_platform_specified;
            if (a->kind == LAYE_NODE_TYPE_INT && (a->type_primitive.is_signed != b->type_primitive.is_signed))
                return false;
            return a->type_primitive.bit_width == b->type_primitive.bit_width;
        }

        case LAYE_NODE_TYPE_ERROR_PAIR: {
            assert(a->type_error_pair.value_type != NULL);
            assert(b->type_error_pair.value_type != NULL);

            if (a->type_error_pair.error_type != NULL) {
                if (
                    b->type_error_pair.error_type == NULL ||
                    !laye_type_equals(a->type_error_pair.error_type, b->type_error_pair.error_type)
                ) {
                    return false;
                }
            } else if (b->type_error_pair.error_type != NULL)
                return false;

            return laye_type_equals(a->type_error_pair.value_type, b->type_error_pair.value_type);
        }

        // an overload set never has a unique type, and they're never associated with
        // dedicated entities, so they're never equal. just thow the world works.
        case LAYE_NODE_TYPE_OVERLOADS: {
            return false;
        }

        case LAYE_NODE_TYPE_NILABLE:
        case LAYE_NODE_TYPE_SLICE:
        case LAYE_NODE_TYPE_REFERENCE:
        case LAYE_NODE_TYPE_POINTER:
        case LAYE_NODE_TYPE_BUFFER: {
            assert(a->type_container.element_type != NULL);
            assert(b->type_container.element_type != NULL);
            return laye_type_equals(a->type_container.element_type, b->type_container.element_type);
        }

        case LAYE_NODE_TYPE_ARRAY: {
            assert(a->type_container.element_type != NULL);
            assert(b->type_container.element_type != NULL);

            if (arr_count(a->type_container.length_values) != arr_count(b->type_container.length_values))
                return false;

            for (int64_t i = 0, count = arr_count(a->type_container.length_values); i < count; i++) {
                laye_node* a_expr = a->type_container.length_values[i];
                laye_node* b_expr = b->type_container.length_values[i];

                assert(a_expr != NULL);
                assert(b_expr != NULL);

                // can only compare equality of arrays if the length values have been resolved.
                // otherwise, they are considered unequal by default.
                if (a_expr->kind != LAYE_NODE_EVALUATED_CONSTANT || b_expr->kind != LAYE_NODE_EVALUATED_CONSTANT)
                    return false;

                // we don't want to consider erroneous evaluations, only integers are allowed
                if (a_expr->evaluated_constant.result.kind != LAYEC_EVAL_INT)
                    return false;
                if (b_expr->evaluated_constant.result.kind != LAYEC_EVAL_INT)
                    return false;

                if (!layec_evaluated_constant_equals(a_expr->evaluated_constant.result, b_expr->evaluated_constant.result))
                    return false;
            }

            return laye_type_equals(a->type_container.element_type, b->type_container.element_type);
        }

        case LAYE_NODE_TYPE_FUNCTION: {
            assert(a->type_function.return_type != NULL);

            if (a->type_function.calling_convention != b->type_function.calling_convention)
                return false;
            if (a->type_function.varargs_style != b->type_function.varargs_style)
                return false;

            if (arr_count(a->type_function.parameter_types) != arr_count(b->type_function.parameter_types))
                return false;

            if (!laye_type_equals(a->type_function.return_type, b->type_function.return_type))
                return false;

            for (int64_t i = 0, count = arr_count(a->type_function.parameter_types); i < count; i++) {
                laye_node* a_type = a->type_function.parameter_types[i];
                laye_node* b_type = b->type_function.parameter_types[i];

                assert(a_type != NULL);
                assert(b_type != NULL);

                if (!laye_type_equals(a_type, b_type))
                    return false;
            }

            return true;
        }

        // the underlying types for each of these declarations is only equal
        // if they're identical, which is checked at the top of the function.
        // any other case is never equal.
        case LAYE_NODE_TYPE_STRUCT:
        case LAYE_NODE_TYPE_VARIANT:
        case LAYE_NODE_TYPE_ENUM:
        case LAYE_NODE_TYPE_STRICT_ALIAS: {
            return false;
        }
    }
}

int laye_type_array_rank(laye_node* array_type) {
    assert(array_type != NULL);
    assert(laye_type_is_array(array_type));
    return (int)arr_count(array_type->type_container.length_values);
}
