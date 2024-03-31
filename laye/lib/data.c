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

laye_symbol* laye_symbol_create(laye_module* module, laye_symbol_kind kind, lca_string_view name) {
    if (kind == LAYE_SYMBOL_ENTITY) {
        assert(name.count > 0);
    }
    
    laye_symbol* symbol = lca_arena_push(module->arena, sizeof *symbol);
    assert(symbol != NULL);
    symbol->kind = kind;
    symbol->name = name;
    lca_da_push(module->_all_symbols, symbol);
    return symbol;
}

laye_symbol* laye_symbol_lookup(laye_symbol* symbol_namespace, lca_string_view name) {
    assert(symbol_namespace != NULL);
    assert(symbol_namespace->kind == LAYE_SYMBOL_NAMESPACE);

    for (int64_t i = 0, count = lca_da_count(symbol_namespace->symbols); i < count; i++) {
        laye_symbol* lookup = symbol_namespace->symbols[i];
        if (lca_string_view_equals(name, lookup->name)) {
            return lookup;
        }
    }

    return NULL;
}

void laye_symbol_destroy(laye_symbol* symbol) {
    if (symbol == NULL) return;

    if (symbol->kind == LAYE_SYMBOL_ENTITY) {
        lca_da_free(symbol->nodes);
    } else {
        lca_da_free(symbol->symbols);
    }
}

void laye_module_destroy(laye_module* module) {
    if (module == NULL) return;

    assert(module->context != NULL);
    lca_allocator allocator = module->context->allocator;

    for (int64_t i = 0, count = lca_da_count(module->_all_tokens); i < count; i++) {
        laye_token token = module->_all_tokens[i];
        lca_da_free(token.leading_trivia);
        lca_da_free(token.trailing_trivia);
    }

    for (int64_t i = 0, count = lca_da_count(module->_all_nodes); i < count; i++) {
        laye_node* node = module->_all_nodes[i];
        assert(node != NULL);
        laye_node_destroy(node);
    }

    for (int64_t i = 0, count = lca_da_count(module->_all_scopes); i < count; i++) {
        laye_scope* scope = module->_all_scopes[i];
        assert(scope != NULL);
        laye_scope_destroy(scope);
    }

    for (int64_t i = 0, count = lca_da_count(module->_all_symbols); i < count; i++) {
        laye_symbol* symbol = module->_all_symbols[i];
        assert(symbol != NULL);
        laye_symbol_destroy(symbol);
    }

    lca_da_free(module->_all_tokens);
    lca_da_free(module->_all_nodes);
    lca_da_free(module->_all_scopes);
    lca_da_free(module->_all_symbols);

    lca_da_free(module->top_level_nodes);
    //lca_da_free(module->imports);

    lca_arena_destroy(module->arena);

    *module = (laye_module){0};
    lca_deallocate(allocator, module);
}

lyir_source laye_module_get_source(laye_module* module) {
    assert(module != NULL);
    assert(module->context != NULL);
    assert(module->sourceid >= 0);
    return lyir_context_get_source(module->context, module->sourceid);
}

laye_scope* laye_scope_create(laye_module* module, laye_scope* parent) {
    assert(module != NULL);
    assert(module->arena != NULL);
    laye_scope* scope = lca_arena_push(module->arena, sizeof *scope);
    assert(scope != NULL);
    scope->module = module;
    scope->parent = parent;
    lca_da_push(module->_all_scopes, scope);
    return scope;
}

void laye_scope_destroy(laye_scope* scope) {
    if (scope == NULL) return;
    lca_da_free(scope->type_declarations);
    lca_da_free(scope->value_declarations);
    *scope = (laye_scope){0};
}

void laye_scope_declare(laye_scope* scope, laye_node* declaration) {
    laye_scope_declare_aliased(scope, declaration, declaration->declared_name);
}

void laye_scope_declare_aliased(laye_scope* scope, laye_node* declaration, lca_string_view alias) {
    assert(scope != NULL);
    assert(declaration != NULL);
    assert(laye_node_is_decl(declaration));
    assert(declaration->kind != LAYE_NODE_DECL_OVERLOADS);
    assert(alias.count != 0);

    laye_module* module = scope->module;
    assert(module != NULL);

    bool is_type_declaration = declaration->kind == LAYE_NODE_DECL_STRUCT || declaration->kind == LAYE_NODE_DECL_ENUM || declaration->kind == LAYE_NODE_DECL_ALIAS || declaration->kind == LAYE_NODE_DECL_TEMPLATE_TYPE;
    lca_da(laye_aliased_node)* entity_namespace = is_type_declaration ? &scope->type_declarations : &scope->value_declarations;
    assert(entity_namespace != NULL);

    if (!is_type_declaration) {
        for (int64_t i = 0, count = lca_da_count(*entity_namespace); i < count; i++) {
            laye_aliased_node entry = (*entity_namespace)[i];
            
            lca_string_view existing_name = entry.name;
            laye_node* existing_declaration = entry.node;
            assert(existing_declaration != NULL);

            if (
                lca_string_view_equals(existing_name, alias) &&
                (declaration->kind != LAYE_NODE_DECL_FUNCTION || existing_declaration->kind != LAYE_NODE_DECL_FUNCTION)
            ) {
                assert(module->context != NULL);
                lyir_write_error(module->context, declaration->location, "redeclaration of '%.*s' in this scope.", LCA_STR_EXPAND(alias));
                return;
            }
        }
    }

    lca_da_push(*entity_namespace, ((laye_aliased_node){
        .name = alias,
        .node = declaration,
    }));
}

static laye_node* laye_scope_lookup_from(laye_scope* scope, lca_da(laye_aliased_node) declarations, lca_string_view name) {
    assert(scope != NULL);
    assert(scope->module != NULL);

    for (int64_t i = 0, count = lca_da_count(declarations); i < count; i++) {
        laye_aliased_node entry = declarations[i];
        assert(entry.node != NULL);

        if (lca_string_view_equals(entry.name, name))
            return entry.node;
    }

    return NULL;
}

laye_node* laye_scope_lookup_value(laye_scope* scope, lca_string_view value_name) {
    assert(scope != NULL);
    return laye_scope_lookup_from(scope, scope->value_declarations, value_name);
}

laye_node* laye_scope_lookup_type(laye_scope* scope, lca_string_view type_name) {
    assert(scope != NULL);
    return laye_scope_lookup_from(scope, scope->type_declarations, type_name);
}

laye_node* laye_node_create(laye_module* module, laye_node_kind kind, lyir_location location, laye_type type) {
    assert(module != NULL);
    assert(module->arena != NULL);
    assert(module->context != NULL);
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    laye_node* node = lca_arena_push(module->arena, sizeof *node);
    assert(node != NULL);
    lca_da_push(module->_all_nodes, node);
    node->module = module;
    node->context = module->context;
    node->kind = kind;
    node->location = location;
    node->type = type;
    return node;
}

laye_node* laye_node_create_in_context(laye_context* context, laye_node_kind kind, laye_type type) {
    assert(context != NULL);
    if (kind != LAYE_NODE_TYPE_TYPE) assert(type.node != NULL);
    laye_node* node = lca_allocate(context->allocator, sizeof *node);
    assert(node != NULL);
    node->context = context;
    node->kind = kind;
    node->type = type;
    return node;
}

void laye_node_destroy(laye_node* node) {
    if (node == NULL) return;

    lca_da_free(node->template_parameters);
    lca_da_free(node->attribute_nodes);

    switch (node->kind) {
        default: break;

        case LAYE_NODE_DECL_IMPORT: {
            lca_da_free(node->decl_import.import_queries);
        } break;

        case LAYE_NODE_IMPORT_QUERY: {
            lca_da_free(node->import_query.pieces);
        } break;

        case LAYE_NODE_DECL_OVERLOADS: {
            lca_da_free(node->decl_overloads.declarations);
        } break;

        case LAYE_NODE_DECL_FUNCTION: {
            lca_da_free(node->decl_function.parameter_declarations);
        } break;

        case LAYE_NODE_DECL_STRUCT: {
            lca_da_free(node->decl_struct.field_declarations);
            lca_da_free(node->decl_struct.variant_declarations);
        } break;

        case LAYE_NODE_DECL_ENUM: {
            lca_da_free(node->decl_enum.variants);
        } break;

        case LAYE_NODE_DECL_TEST: {
            lca_da_free(node->decl_test.nameref.pieces);
            lca_da_free(node->decl_test.nameref.template_arguments);
        } break;

        case LAYE_NODE_IF: {
            lca_da_free(node->_if.conditions);
            lca_da_free(node->_if.passes);
        } break;

        case LAYE_NODE_COMPOUND: {
            lca_da_free(node->compound.children);
        } break;

        case LAYE_NODE_SWITCH: {
            lca_da_free(node->_switch.cases);
        } break;

        case LAYE_NODE_NAMEREF:
        case LAYE_NODE_TYPE_NAMEREF: {
            lca_da_free(node->nameref.pieces);
            lca_da_free(node->nameref.template_arguments);
        } break;

        case LAYE_NODE_INDEX: {
            lca_da_free(node->index.indices);
        } break;

        case LAYE_NODE_CALL: {
            lca_da_free(node->call.arguments);
        } break;

        case LAYE_NODE_CTOR: {
            lca_da_free(node->ctor.initializers);
        } break;

        case LAYE_NODE_NEW: {
            lca_da_free(node->new.arguments);
            lca_da_free(node->new.initializers);
        } break;

        case LAYE_NODE_TYPE_NILABLE:
        case LAYE_NODE_TYPE_ARRAY:
        case LAYE_NODE_TYPE_SLICE:
        case LAYE_NODE_TYPE_REFERENCE:
        case LAYE_NODE_TYPE_POINTER:
        case LAYE_NODE_TYPE_BUFFER: {
            lca_da_free(node->type_container.length_values);
        } break;

        case LAYE_NODE_TYPE_FUNCTION: {
            lca_da_free(node->type_function.parameter_types);
        } break;

        case LAYE_NODE_TYPE_STRUCT: {
            lca_da_free(node->type_struct.fields);
            lca_da_free(node->type_struct.variants);
        } break;

        case LAYE_NODE_TYPE_ENUM: {
            lca_da_free(node->type_enum.variants);
        } break;
    }

    *node = (laye_node){0};
    // don't free the node, since it's arena allocated
}

void laye_node_set_sema_in_progress(laye_node* node) {
    assert(node != NULL);
    node->sema_state = LYIR_SEMA_IN_PROGRESS;
}

void laye_node_set_sema_errored(laye_node* node) {
    assert(node != NULL);
    node->sema_state = LYIR_SEMA_ERRORED;
}

void laye_node_set_sema_ok(laye_node* node) {
    assert(node != NULL);
    node->sema_state = LYIR_SEMA_OK;
}

bool laye_node_is_sema_in_progress(laye_node* node) {
    assert(node != NULL);
    return node->sema_state == LYIR_SEMA_IN_PROGRESS;
}

bool laye_node_is_sema_ok(laye_node* node) {
    assert(node != NULL);
    return node->sema_state == LYIR_SEMA_OK;
}

bool laye_node_is_sema_ok_or_errored(laye_node* node) {
    assert(node != NULL);
    return node->sema_state == LYIR_SEMA_OK || node->sema_state == LYIR_SEMA_ERRORED;
}

bool laye_node_has_noreturn_semantics(laye_node* node) {
    assert(node != NULL);
    assert(node->type.node != NULL);
    return laye_type_is_noreturn(node->type);
}

bool laye_decl_is_exported(laye_node* decl) {
    assert(decl != NULL);
    assert(laye_node_is_decl(decl));
    return decl->attributes.linkage == LYIR_LINK_EXPORTED || decl->attributes.linkage == LYIR_LINK_REEXPORTED;
}

bool laye_decl_is_template(laye_node* decl) {
    assert(decl != NULL);
    assert(laye_node_is_decl(decl));
    return lca_da_count(decl->template_parameters) > 0;
}

laye_type laye_expr_type(laye_node* expr) {
    assert(expr != NULL);
    assert(expr->type.node != NULL);
    return expr->type;
}

bool laye_expr_evaluate(laye_node* expr, lyir_evaluated_constant* out_constant, bool is_required) {
    assert(expr != NULL);
    assert(out_constant != NULL);
    assert(!is_required || laye_node_is_sema_ok(expr) && "cannot evaluate ill-formed or unchecked expression");

    switch (expr->kind) {
        default: return false;

        case LAYE_NODE_SIZEOF: {
            int size_in_bytes = 0;

            laye_node* query = expr->_sizeof.query;
            if (laye_node_is_type(query)) {
                size_in_bytes = laye_type_size_in_bytes(LTY(query));
            } else {
                assert(query->type.node != NULL);
                size_in_bytes = laye_type_size_in_bytes(query->type);
            }

            out_constant->kind = LYIR_EVAL_INT;
            out_constant->int_value = (int64_t)size_in_bytes;
            return true;
        }

        case LAYE_NODE_LITBOOL: {
            out_constant->kind = LYIR_EVAL_BOOL;
            out_constant->bool_value = expr->litbool.value;
            return true;
        }

        case LAYE_NODE_LITINT: {
            out_constant->kind = LYIR_EVAL_INT;
            out_constant->int_value = expr->litint.value;
            return true;
        }

        case LAYE_NODE_LITFLOAT: {
            out_constant->kind = LYIR_EVAL_FLOAT;
            out_constant->float_value = expr->litfloat.value;
            return true;
        }

        case LAYE_NODE_LITSTRING: {
            out_constant->kind = LYIR_EVAL_STRING;
            out_constant->string_value = expr->litstring.value;
            return true;
        }

#if false
        case LAYE_NODE_COMPOUND: {
            if (lca_da_count(expr->compound.children) == 1 && expr->compound.children[0]->kind == LAYE_NODE_YIELD) {
                return laye_expr_evaluate(expr->compound.children[0]->yield.value, out_constant, is_required);
            }

            return false;
        }
#endif
    }
}

bool laye_expr_is_lvalue(laye_node* expr) {
    assert(expr != NULL);
    return expr->value_category == LYIR_LVALUE;
}

bool laye_expr_is_modifiable_lvalue(laye_node* expr) {
    assert(expr != NULL);
    assert(expr->type.node != NULL);
    return expr->value_category == LYIR_LVALUE && expr->type.is_modifiable;
}

void laye_expr_set_lvalue(laye_node* expr, bool is_lvalue) {
    assert(expr != NULL);
    expr->value_category = is_lvalue ? LYIR_LVALUE : LYIR_RVALUE;
}

int align_padding(int bits, int align) {
    assert(align > 0);
    return (align - (bits % align)) % align;
}

int align_to(int bits, int align) {
    return bits + align_padding(bits, align);
}

int laye_type_size_in_bytes(laye_type type) {
    int size_in_bits = laye_type_size_in_bits(type);
    return align_to(size_in_bits, 8) / 8;
}

int laye_type_size_in_bits(laye_type type) {
    assert(type.node != NULL);
    assert(type.node->context != NULL);
    assert(laye_node_is_type(type.node));
    lyir_context* context = type.node->context;

    switch (type.node->kind) {
        default: {
            fprintf(stderr, "for type %s\n", laye_node_kind_to_cstring(type.node->kind));
            assert(false && "unreachable");
            return 0;
        }

        case LAYE_NODE_TYPE_VOID:
        case LAYE_NODE_TYPE_NORETURN: {
            return 0;
        }

        case LAYE_NODE_TYPE_BOOL:
        case LAYE_NODE_TYPE_INT:
        case LAYE_NODE_TYPE_FLOAT: {
            assert(type.node->type_primitive.bit_width > 0);
            return type.node->type_primitive.bit_width;
        }

        case LAYE_NODE_TYPE_REFERENCE:
        case LAYE_NODE_TYPE_POINTER:
        case LAYE_NODE_TYPE_BUFFER: {
            return context->target->size_of_pointer;
        }

        case LAYE_NODE_TYPE_ERROR_PAIR: {
            assert(false && "todo: error pair type");
        }

        case LAYE_NODE_TYPE_ARRAY: {
            int element_size = laye_type_size_in_bytes(type.node->type_container.element_type);// * 8;
            int64_t constant_value = 1;

            for (int64_t i = 0, count = lca_da_count(type.node->type_container.length_values); i < count; i++) {
                laye_node* length_value = type.node->type_container.length_values[i];
                if (length_value->kind == LAYE_NODE_EVALUATED_CONSTANT && length_value->evaluated_constant.result.kind == LYIR_EVAL_INT) {
                    constant_value *= length_value->evaluated_constant.result.int_value;
                }
            }

            return (int)(element_size * constant_value);
        }

        case LAYE_NODE_TYPE_STRUCT: {
            if (type.node->type_struct.cached_size != 0) {
                return type.node->type_struct.cached_size * 8;
            }

            // NOTE(local): Sema is responsible for caching, any time before sema
            // we don't usually care about the size that often. this is most likely
            // for debug use. Fine to not cache it.
            int size = 0;
            // NOTE(local): generation of this struct should include padding, so we don't consider it here
            for (int64_t i = 0, count = lca_da_count(type.node->type_struct.fields); i < count; i++) {
                size += laye_type_size_in_bits(type.node->type_struct.fields[i].type);
            }
            // TODO(local): include largest variant
            assert(lca_da_count(type.node->type_struct.variants) == 0);

            return size;
        }
    }
}

int laye_type_align_in_bytes(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    assert(type.node->context != NULL);
    lyir_context* context = type.node->context;

    switch (type.node->kind) {
        default: return 1;

        case LAYE_NODE_TYPE_VOID:
        case LAYE_NODE_TYPE_NORETURN: {
            return 1;
        }

        case LAYE_NODE_TYPE_BOOL: return 1;

        case LAYE_NODE_TYPE_INT:
        case LAYE_NODE_TYPE_FLOAT: {
            return align_to(type.node->type_primitive.bit_width, 8) / 8;
        }

        case LAYE_NODE_TYPE_ERROR_PAIR: {
            assert(false && "todo: error pair type");
        }

        case LAYE_NODE_NAMEREF: {
            if (type.node->nameref.referenced_type == NULL) {
                return 1;
            }

            assert(type.node->nameref.referenced_type != NULL);
            return laye_type_align_in_bytes(LTY(type.node->nameref.referenced_type));
        }

        case LAYE_NODE_TYPE_OVERLOADS: return 1;

        case LAYE_NODE_TYPE_NILABLE: {
            assert(type.node->type_container.element_type.node != NULL);
            return laye_type_align_in_bytes(LTY(type.node->type_container.element_type.node));
        }

        case LAYE_NODE_TYPE_REFERENCE:
        case LAYE_NODE_TYPE_POINTER:
        case LAYE_NODE_TYPE_BUFFER: {
            return align_to(context->target->align_of_pointer, 8) / 8;
        }

        case LAYE_NODE_TYPE_STRUCT: {
            if (type.node->type_struct.cached_align != 0) {
                return type.node->type_struct.cached_align;// * 8;
            }

            // NOTE(local): Sema is responsible for caching, any time before sema
            // we don't usually care about the size that often. this is most likely
            // for debug use. Fine to not cache it.
            int align = 1;
            for (int64_t i = 0, count = lca_da_count(type.node->type_struct.fields); i < count; i++) {
                int f_align = laye_type_align_in_bytes(type.node->type_struct.fields[i].type);
                if (f_align > align) {
                    align = f_align;
                }
            }
            // TODO(local): include largest variant
            assert(lca_da_count(type.node->type_struct.variants) == 0);

            return align;
        }
    }
    return 0;
}

bool laye_type_is_poison(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_POISON;
}

bool laye_type_is_void(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_VOID;
}

bool laye_type_is_noreturn(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_NORETURN;
}

bool laye_type_is_bool(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_BOOL;
}

bool laye_type_is_int(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_INT;
}

bool laye_type_is_signed_int(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_INT && type.node->type_primitive.is_signed;
}

bool laye_type_is_unsigned_int(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_INT && !type.node->type_primitive.is_signed;
}

bool laye_type_is_float(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_FLOAT;
}

bool laye_type_is_numeric(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_INT || type.node->kind == LAYE_NODE_TYPE_FLOAT;
}

bool laye_type_is_template_parameter(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_TEMPLATE_PARAMETER;
}

bool laye_type_is_error_pair(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_ERROR_PAIR;
}

bool laye_type_is_nameref(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_NAMEREF;
}

bool laye_type_is_overload(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_OVERLOADS;
}

bool laye_type_is_nilable(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_NILABLE;
}

bool laye_type_is_array(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_ARRAY;
}

bool laye_type_is_slice(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_SLICE;
}

bool laye_type_is_reference(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_REFERENCE;
}

bool laye_type_is_pointer(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_POINTER;
}

bool laye_type_is_buffer(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_BUFFER;
}

bool laye_type_is_function(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_FUNCTION;
}

bool laye_type_is_struct(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_STRUCT;
}

bool laye_type_is_variant(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_VARIANT;
}

bool laye_type_is_enum(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_ENUM;
}

bool laye_type_is_alias(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_ALIAS;
}

bool laye_type_is_strict_alias(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    return type.node->kind == LAYE_NODE_TYPE_STRICT_ALIAS;
}

laye_type laye_type_qualify(laye_node* type_node, bool is_modifiable) {
    return (laye_type){
        .node = type_node,
        .is_modifiable = is_modifiable,
    };
}

laye_type laye_type_add_qualifiers(laye_type type, bool is_modifiable) {
    type.is_modifiable |= is_modifiable;
    return type;
}

laye_type laye_type_with_source(laye_node* type_node, laye_node* source_node, bool is_modifiable) {
    return (laye_type){
        .node = type_node,
        .source_node = source_node,
        .is_modifiable = is_modifiable,
    };
}

bool laye_node_kind_is_decl(laye_node_kind kind) {
    return kind >= LAYE_NODE_DECL_IMPORT && kind <= LAYE_NODE_DECL_TEST;
}

bool laye_node_kind_is_type(laye_node_kind kind) {
    return kind >= LAYE_NODE_TYPE_POISON && kind <= LAYE_NODE_TYPE_STRICT_ALIAS;
}

bool laye_node_is_decl(laye_node* node) {
    assert(node != NULL);
    return laye_node_kind_is_decl(node->kind);
}

bool laye_node_is_type(laye_node* node) {
    assert(node != NULL);
    return laye_node_kind_is_type(node->kind);
}

bool laye_node_is_lvalue(laye_node* node) {
    assert(node != NULL);
    return node->value_category == LYIR_LVALUE;
}

bool laye_node_is_rvalue(laye_node* node) {
    assert(node != NULL);
    return node->value_category == LYIR_RVALUE;
}

bool laye_node_is_modifiable_lvalue(laye_node* node) {
    assert(node != NULL);
    return laye_node_is_lvalue(node) && laye_type_is_modifiable(node->type);
}

bool laye_type_is_modifiable(laye_type type) {
    return type.is_modifiable;
}

laye_type laye_type_strip_pointers_and_references(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));

    while (
        type.node->kind == LAYE_NODE_TYPE_POINTER ||
        type.node->kind == LAYE_NODE_TYPE_REFERENCE
    ) {
        type = type.node->type_container.element_type;
        assert(type.node != NULL);
        assert(laye_node_is_type(type.node));
    }

    return type;
}

laye_type laye_type_strip_references(laye_type type) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));

    while (type.node->kind == LAYE_NODE_TYPE_REFERENCE) {
        type = type.node->type_container.element_type;
        assert(type.node != NULL);
        assert(laye_node_is_type(type.node));
    }

    return type;
}

bool laye_type_equals(laye_type a_type, laye_type b_type, laye_mut_compare mut_compare) {
    laye_node* a = a_type.node;
    laye_node* b = b_type.node;

    assert(a != NULL);
    assert(b != NULL);
    assert(laye_node_is_type(a));
    assert(laye_node_is_type(b));

    if (a == b) return true;

    switch (mut_compare) {
        default: assert(false);

        case LAYE_MUT_IGNORE: break;

        case LAYE_MUT_EQUAL: {
            if (a_type.is_modifiable != b_type.is_modifiable) return false;
        } break;

        case LAYE_MUT_CONVERTIBLE: {
            bool convertible = a_type.is_modifiable == b_type.is_modifiable || !b_type.is_modifiable;
            if (!convertible) return false;
        } break;
    }

    if (laye_type_is_nameref(a_type)) {
        assert(a->nameref.referenced_type != NULL);
        return laye_type_equals(laye_type_qualify(a->nameref.referenced_type, a_type.is_modifiable), b_type, mut_compare);
    }

    if (laye_type_is_nameref(b_type)) {
        assert(b->nameref.referenced_type != NULL);
        return laye_type_equals(a_type, laye_type_qualify(b->nameref.referenced_type, b_type.is_modifiable), mut_compare);
    }

    assert(!laye_type_is_nameref(a_type));
    assert(!laye_type_is_nameref(b_type));

    if (laye_type_is_alias(a_type)) {
        assert(a->type_alias.underlying_type.node != NULL);
        return laye_type_equals(laye_type_add_qualifiers(a->type_alias.underlying_type, a_type.is_modifiable), b_type, mut_compare);
    }

    if (laye_type_is_alias(b_type)) {
        assert(b->type_alias.underlying_type.node != NULL);
        return laye_type_equals(a_type, laye_type_add_qualifiers(b->type_alias.underlying_type, b_type.is_modifiable), mut_compare);
    }

    assert(!laye_type_is_alias(a_type));
    assert(!laye_type_is_alias(b_type));

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
            assert(a->type_error_pair.value_type.node != NULL);
            assert(b->type_error_pair.value_type.node != NULL);

            if (a->type_error_pair.error_type.node != NULL) {
                if (
                    b->type_error_pair.error_type.node == NULL ||
                    !laye_type_equals(a->type_error_pair.error_type, b->type_error_pair.error_type, mut_compare)
                ) {
                    return false;
                }
            } else if (b->type_error_pair.error_type.node != NULL)
                return false;

            return laye_type_equals(a->type_error_pair.value_type, b->type_error_pair.value_type, mut_compare);
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
            assert(a->type_container.element_type.node != NULL);
            assert(b->type_container.element_type.node != NULL);
            return laye_type_equals(a->type_container.element_type, b->type_container.element_type, mut_compare);
        }

        case LAYE_NODE_TYPE_ARRAY: {
            assert(a->type_container.element_type.node != NULL);
            assert(b->type_container.element_type.node != NULL);

            if (lca_da_count(a->type_container.length_values) != lca_da_count(b->type_container.length_values))
                return false;

            for (int64_t i = 0, count = lca_da_count(a->type_container.length_values); i < count; i++) {
                laye_node* a_expr = a->type_container.length_values[i];
                laye_node* b_expr = b->type_container.length_values[i];

                assert(a_expr != NULL);
                assert(b_expr != NULL);

                // can only compare equality of arrays if the length values have been resolved.
                // otherwise, they are considered unequal by default.
                if (a_expr->kind != LAYE_NODE_EVALUATED_CONSTANT || b_expr->kind != LAYE_NODE_EVALUATED_CONSTANT)
                    return false;

                // we don't want to consider erroneous evaluations, only integers are allowed
                if (a_expr->evaluated_constant.result.kind != LYIR_EVAL_INT)
                    return false;
                if (b_expr->evaluated_constant.result.kind != LYIR_EVAL_INT)
                    return false;

                if (!lyir_evaluated_constant_equals(a_expr->evaluated_constant.result, b_expr->evaluated_constant.result))
                    return false;
            }

            return laye_type_equals(a->type_container.element_type, b->type_container.element_type, mut_compare);
        }

        case LAYE_NODE_TYPE_FUNCTION: {
            assert(a->type_function.return_type.node != NULL);

            if (a->type_function.calling_convention != b->type_function.calling_convention)
                return false;
            if (a->type_function.varargs_style != b->type_function.varargs_style)
                return false;

            if (lca_da_count(a->type_function.parameter_types) != lca_da_count(b->type_function.parameter_types))
                return false;

            if (!laye_type_equals(a->type_function.return_type, b->type_function.return_type, mut_compare))
                return false;

            for (int64_t i = 0, count = lca_da_count(a->type_function.parameter_types); i < count; i++) {
                laye_type a_param_type = a->type_function.parameter_types[i];
                laye_type b_param_type = b->type_function.parameter_types[i];

                assert(a_param_type.node != NULL);
                assert(b_param_type.node != NULL);

                if (!laye_type_equals(a_param_type, b_param_type, mut_compare))
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

int laye_type_array_rank(laye_type array_type) {
    assert(array_type.node != NULL);
    assert(laye_type_is_array(array_type));
    return (int)lca_da_count(array_type.node->type_container.length_values);
}
