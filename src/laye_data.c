#include "layec.h"

#include <assert.h>

void laye_module_destroy(laye_module* module) {
    if (module == NULL) return;

    assert(module->context != NULL);
    lca_allocator allocator = module->context->allocator;

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
    return scope;
}

void laye_scope_declare(laye_scope* scope, laye_node* declaration) {
    assert(scope != NULL);
    assert(declaration != NULL);
    assert(laye_node_is_decl(declaration));
    assert(declaration->kind != LAYE_NODE_OVERLOADS);

    laye_module* module = scope->module;
    assert(module != NULL);

    bool is_type_declaration = declaration->kind == LAYE_NODE_STRUCT || declaration->kind == LAYE_NODE_ENUM || declaration->kind == LAYE_NODE_ALIAS || declaration->kind == LAYE_NODE_TEMPLATE_TYPE;
    dynarr(laye_node*)* entity_namespace = is_type_declaration ? &scope->type_declarations : &scope->value_declarations;
    assert(entity_namespace != NULL);

    if (!is_type_declaration) {
        for (int64_t i = 0, count = arr_count(*entity_namespace); i < count; i++) {
            laye_node* existing_declaration = (*entity_namespace)[i];
            assert(existing_declaration != NULL);

            if (
                string_equals(existing_declaration->decl.declared_name, declaration->decl.declared_name) &&
                (declaration->kind != LAYE_NODE_FUNCTION || existing_declaration->kind != LAYE_NODE_FUNCTION)
            ) {
                assert(module->context != NULL);
                layec_write_error(module->context, declaration->location, "redeclaration of '%.*s' in this scope.", STR_EXPAND(declaration->decl.declared_name));
                return;
            }
        }
    }

    arr_push(*entity_namespace, declaration);
}

laye_node* laye_node_create(laye_module* module, laye_node_kind kind, layec_location location) {
    assert(module != NULL);
    assert(module->arena != NULL);
    assert(!laye_node_kind_is_expr(kind) && "call laye_expr_create instead, please");
    laye_node* node = lca_arena_push(module->arena, sizeof *node);
    assert(node != NULL);
    node->kind = kind;
    node->location = location;
    return node;
}

laye_node* laye_expr_create(laye_module* module, laye_node_kind kind, layec_location location, laye_node* type) {
    assert(module != NULL);
    assert(module->arena != NULL);
    assert(laye_node_kind_is_expr(kind));
    assert(type != NULL);
    assert(laye_node_is_type(type));
    laye_node* expr = lca_arena_push(module->arena, sizeof *expr);
    assert(expr != NULL);
    expr->kind = kind;
    expr->location = location;
    expr->expr.type = type;
    return expr;
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
    assert(false && "TODO: implement");
    switch (node->kind) {
        default: return false;
    }
}

bool laye_decl_is_exported(laye_node* decl) {
    assert(decl != NULL);
    assert(laye_node_is_decl(decl));
    return decl->decl.linkage == LAYEC_LINK_EXPORTED || decl->decl.linkage == LAYEC_LINK_REEXPORTED;
}

bool laye_decl_is_template(laye_node* decl) {
    assert(decl != NULL);
    assert(laye_node_is_stmt(decl));
    return arr_count(decl->decl.template_parameters) > 0;
}

laye_node* laye_expr_type(laye_node* expr) {
    assert(expr != NULL);
    assert(laye_node_is_expr(expr));
    assert(expr->expr.type != NULL);
    return expr->expr.type;
}

bool laye_expr_evaluate(laye_node* expr, layec_evaluated_constant* out_constant, bool is_required) {
    assert(expr != NULL);
    assert(laye_node_is_expr(expr));
    assert(out_constant != NULL);
    assert(!is_required || laye_node_is_sema_ok(expr) && "cannot evaluate ill-formed or unchecked expression");
    
    switch (expr->kind) {
        default: return false;

        case LAYE_NODE_LITBOOL: {
            out_constant->kind = LAYEC_EVAL_BOOL;
            out_constant->bool_value = expr->expr.litbool.value;
            return true;
        }

        case LAYE_NODE_LITINT: {
            out_constant->kind = LAYEC_EVAL_INT;
            out_constant->int_value = expr->expr.litint.value;
            return true;
        }

        case LAYE_NODE_LITFLOAT: {
            out_constant->kind = LAYEC_EVAL_FLOAT;
            out_constant->float_value = expr->expr.litfloat.value;
            return true;
        }

        case LAYE_NODE_LITSTRING: {
            out_constant->kind = LAYEC_EVAL_STRING;
            out_constant->string_value = expr->expr.litstring.value;
            return true;
        }
    }
}

bool laye_expr_is_lvalue(laye_node* expr) {
    assert(expr != NULL);
    assert(laye_node_is_expr(expr));
    return expr->expr.value_category == LAYEC_LVALUE;
}

bool laye_expr_is_modifiable_lvalue(laye_node* expr) {
    assert(expr != NULL);
    assert(laye_node_is_expr(expr));
    assert(expr->expr.type != NULL);
    return expr->expr.value_category == LAYEC_LVALUE && expr->expr.type->type.is_modifiable;
}

void laye_expr_set_lvalue(laye_node* expr, bool is_lvalue) {
    assert(expr != NULL);
    assert(laye_node_is_expr(expr));
    expr->expr.value_category = is_lvalue ? LAYEC_LVALUE : LAYEC_RVALUE;
}

int laye_type_size_in_bytes(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    int size_in_bits = laye_type_size_in_bytes(type);
    return (size_in_bits + 7) / 8;
}

int laye_type_size_in_bits(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    assert(false && "TODO: implement");
    return 0;
}

int laye_type_align_in_bytes(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    assert(false && "TODO: implement");
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
    return type->kind == LAYE_NODE_TYPE_INT && type->type.primitive.is_signed;
}

bool laye_type_is_unsigned_int(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    return type->kind == LAYE_NODE_TYPE_INT && !type->type.primitive.is_signed;
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
    return type->kind == LAYE_NODE_TYPE_NAMEREF;
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
        type = type->type.container.element_type;
        assert(type != NULL);
        assert(laye_node_is_type(type));
    }

    return type;
}

laye_node* laye_type_strip_references(laye_node* type) {
    assert(type != NULL);
    assert(laye_node_is_type(type));

    while (type->kind == LAYE_NODE_TYPE_REFERENCE) {
        type = type->type.container.element_type;
        assert(type != NULL);
        assert(laye_node_is_type(type));
    }

    return type;
}

bool laye_type_equals(laye_node* a, laye_node* b) {
    if (a == b) return true;
    if (a->kind != b->kind) return false;
    assert(false && "TODO: implement");
    return 0;
}

int laye_type_array_rank(laye_node* array_type) {
    assert(array_type != NULL);
    assert(laye_type_is_array(array_type));
    return (int)arr_count(array_type->type.container.length_values);
}
