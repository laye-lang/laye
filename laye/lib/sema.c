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

#define NOTY ((laye_type){0})

typedef struct laye_sema {
    laye_context* context;
    lyir_dependency_graph* dependencies;

    laye_node* current_function;
    laye_node* current_yield_target;
} laye_sema;

static bool laye_sema_analyse_type(laye_sema* sema, laye_type* type);
static bool laye_sema_analyse_node(laye_sema* sema, laye_node** node, laye_type expected_type);
static bool laye_sema_analyse_node_and_discard(laye_sema* sema, laye_node** node);

static void laye_sema_discard(laye_sema* sema, laye_node** node);
static bool laye_sema_has_side_effects(laye_sema* sema, laye_node* node);

static bool laye_sema_convert(laye_sema* sema, laye_node** node, laye_type to);
static void laye_sema_convert_or_error(laye_sema* sema, laye_node** node, laye_type to);
static void laye_sema_convert_to_c_varargs_or_error(laye_sema* sema, laye_node** node);
static bool laye_sema_convert_to_common_type(laye_sema* sema, laye_node** a, laye_node** b);
static int laye_sema_try_convert(laye_sema* sema, laye_node** node, laye_type to);

static void laye_sema_wrap_with_cast(laye_sema* sema, laye_node** node, laye_type type, laye_cast_kind cast_kind);
static void laye_sema_insert_pointer_to_integer_cast(laye_sema* sema, laye_node** node);
static void laye_sema_insert_implicit_cast(laye_sema* sema, laye_node** node, laye_type to);
static void laye_sema_lvalue_to_rvalue(laye_sema* sema, laye_node** node, bool strip_ref);
static bool laye_sema_implicit_dereference(laye_sema* sema, laye_node** node);
static bool laye_sema_implicit_de_reference(laye_sema* sema, laye_node** node);

static laye_type laye_sema_get_pointer_to_type(laye_sema* sema, laye_type element_type, bool is_modifiable);
static laye_type laye_sema_get_buffer_of_type(laye_sema* sema, laye_type element_type, bool is_modifiable);
static laye_type laye_sema_get_reference_to_type(laye_sema* sema, laye_type element_type, bool is_modifiable);

static laye_node* laye_create_constant_node(laye_sema* sema, laye_node* node, lyir_evaluated_constant eval_result);

// TODO(local): redeclaration of a name as an import namespace should be a semantic error. They can't participate in overload resolution,
// so should just be disallowed for simplicity.

static void laye_sema_set_errored(laye_node* node) {
    assert(node);
    node->dependence |= LAYE_DEPENDENCE_ERROR_DEPENDENT;
    node->sema_state = LYIR_SEMA_DONE;
}

static bool laye_sema_is_errored(laye_node* node) {
    assert(node);
    return (node->dependence & LAYE_DEPENDENCE_ERROR) != 0;
}

static laye_node* laye_sema_lookup_entity(laye_module* from_module, laye_nameref nameref, bool is_type_entity) {
    assert(from_module != NULL);
    assert(from_module->context != NULL);

    laye_context* laye_context = from_module->context;
    assert(laye_context != NULL);

    lyir_context* lyir_context = laye_context->lyir_context;
    assert(lyir_context != NULL);

    laye_scope* search_scope = nameref.scope;
    assert(search_scope != NULL);

    assert(lca_da_count(nameref.pieces) >= 1);
    lca_string_view first_name = nameref.pieces[0].string_value;

    assert(nameref.kind == LAYE_NAMEREF_DEFAULT && "only the default representation of a nameref is supported at this time");

    laye_node* found_declaration = NULL;

    while (search_scope != NULL) {
        laye_node* lookup = is_type_entity ? laye_scope_lookup_type(search_scope, first_name) : laye_scope_lookup_value(search_scope, first_name);
        if (lookup != NULL) {
            found_declaration = lookup;
            break;
        }

        search_scope = search_scope->parent;
    }

    if (found_declaration == NULL) {
        laye_symbol* search_namespace = from_module->imports;
        assert(search_namespace != NULL); // TODO(local): is this actually true?
        assert(search_namespace->kind == LAYE_SYMBOL_NAMESPACE);

        for (int64_t name_index = 0, name_count = lca_da_count(nameref.pieces); name_index < name_count; name_index++) {
            // bool is_last_name = name_index == name_count - 1;
            laye_token name_piece_token = nameref.pieces[name_index];
            lca_string_view name_piece = name_piece_token.string_value;

            laye_symbol* symbol_matching = NULL;
            for (int64_t symbol_index = 0, symbol_count = lca_da_count(search_namespace->symbols); symbol_index < symbol_count && symbol_matching == NULL; symbol_index++) {
                laye_symbol* symbol_imported = search_namespace->symbols[symbol_index];
                assert(symbol_imported != NULL);

                if (lca_string_view_equals(symbol_imported->name, name_piece)) {
                    symbol_matching = symbol_imported;
                }
            }

            if (symbol_matching == NULL) {
                lyir_write_error(
                    lyir_context,
                    name_piece_token.location,
                    "Unable to resolve identifier '%.*s' in this context.",
                    LCA_STR_EXPAND(name_piece)
                );
                return NULL;
            }

            if (symbol_matching->kind == LAYE_SYMBOL_ENTITY) {
                if (name_index == name_count - 1) {
                    assert(lca_da_count(symbol_matching->nodes) > 0 && "the symbol exists, so it should have at least one node in it");
                    assert(lca_da_count(symbol_matching->nodes) == 1 && "no support for overloads just yet");
                    return symbol_matching->nodes[0];
                }

                // TODO(local): resolve variants within types
                lyir_write_error(
                    lyir_context,
                    name_piece_token.location,
                    "Entity '%.*s' is not a namespace in this context.",
                    LCA_STR_EXPAND(name_piece)
                );
                return NULL;
            } else {
                assert(symbol_matching->kind == LAYE_SYMBOL_NAMESPACE);
                search_namespace = symbol_matching;
            }
        }
    }

    return found_declaration;
}

static laye_node* laye_sema_lookup_value_declaration(laye_module* from_module, laye_nameref nameref) {
    return laye_sema_lookup_entity(from_module, nameref, false);
}

static laye_node* laye_sema_lookup_type_declaration(laye_module* from_module, laye_nameref nameref) {
    return laye_sema_lookup_entity(from_module, nameref, true);
}

static void laye_generate_dependencies_for_node(lyir_dependency_graph* graph, laye_node* dep_parent, laye_node* node) {
    assert(graph != NULL);
    assert(node != NULL);
    assert(node->module != NULL);

    switch (node->kind) {
        default: {
            fprintf(stderr, "for node kind %s\n", laye_node_kind_to_cstring(node->kind));
            assert(false && "unimplemented dependency generation");
        } break;

        case LAYE_NODE_DECL_STRUCT: {
            for (int64_t i = 0, count = lca_da_count(node->decl_struct.field_declarations); i < count; i++) {
                laye_node* field_decl = node->decl_struct.field_declarations[i];
                laye_generate_dependencies_for_node(graph, dep_parent, field_decl->declared_type.node);
            }
        } break;

        case LAYE_NODE_TYPE_NORETURN:
        case LAYE_NODE_TYPE_VOID:
        case LAYE_NODE_TYPE_INT:
        case LAYE_NODE_TYPE_FLOAT:
        case LAYE_NODE_TYPE_BOOL: {
        } break;

        case LAYE_NODE_TYPE_ARRAY:
        case LAYE_NODE_TYPE_REFERENCE:
        case LAYE_NODE_TYPE_POINTER:
        case LAYE_NODE_TYPE_BUFFER:
        case LAYE_NODE_TYPE_SLICE: {
            laye_generate_dependencies_for_node(graph, dep_parent, node->type_container.element_type.node);
        } break;

        case LAYE_NODE_TYPE_NAMEREF: {
            laye_node* referenced_decl_node = node->nameref.referenced_declaration;

            if (referenced_decl_node == NULL) {
                referenced_decl_node = laye_sema_lookup_type_declaration(node->module, node->nameref);
            }

            lyir_depgraph_add_dependency(graph, dep_parent, referenced_decl_node);
        } break;
    }
}

static void laye_generate_dependencies_for_module(lyir_dependency_graph* graph, laye_module* module) {
    assert(graph != NULL);
    assert(module != NULL);

    if (module->dependencies_generated) {
        return;
    }

    for (int64_t i = 0, count = lca_da_count(module->top_level_nodes); i < count; i++) {
        laye_node* top_level_node = module->top_level_nodes[i];
        assert(top_level_node != NULL);

        switch (top_level_node->kind) {
            default: {
                fprintf(stderr, "for node kind %s\n", laye_node_kind_to_cstring(top_level_node->kind));
                assert(false && "unreachable");
            } break;

            case LAYE_NODE_DECL_BINDING: {
                lyir_depgraph_ensure_tracked(graph, top_level_node);
            } break;

            case LAYE_NODE_DECL_IMPORT: {
            } break;

            case LAYE_NODE_DECL_FUNCTION: {
                // TODO(local): generate dependencies
                lyir_depgraph_ensure_tracked(graph, top_level_node);

                laye_generate_dependencies_for_node(graph, top_level_node, top_level_node->decl_function.return_type.node);
                for (int64_t i = 0, count = lca_da_count(top_level_node->decl_function.parameter_declarations); i < count; i++) {
                    laye_generate_dependencies_for_node(graph, top_level_node, top_level_node->decl_function.parameter_declarations[i]->declared_type.node);
                }
            } break;

            case LAYE_NODE_DECL_STRUCT: {
                // TODO(local): generate dependencies
                lyir_depgraph_ensure_tracked(graph, top_level_node);
                laye_generate_dependencies_for_node(graph, top_level_node, top_level_node);
            } break;

            case LAYE_NODE_DECL_TEST: {
                lyir_depgraph_ensure_tracked(graph, top_level_node);
            } break;
        }
    }

    module->dependencies_generated = true;
}

static void laye_sema_resolve_top_level_types(laye_sema* sema, laye_node** node);

static int64_t maxi(int64_t a, int64_t b) {
    return a > b ? a : b;
}

static bool is_identifier_char(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c >= 256;
}

static lca_string_view import_string_to_laye_identifier_string(laye_node* import_node) {
    assert(import_node != NULL);
    assert(import_node->module != NULL);
    assert(import_node->module->context != NULL);

    laye_context* laye_context = import_node->module->context;
    assert(laye_context != NULL);

    lyir_context* lyir_context = laye_context->lyir_context;
    assert(lyir_context != NULL);

    lca_string_view module_name = import_node->decl_import.import_alias.string_value;
    if (module_name.count == 0) {
        module_name = lca_string_as_view(lyir_context_get_source(lyir_context, import_node->decl_import.referenced_module->sourceid).name);

        int64_t last_slash_index = maxi(lca_string_view_last_index_of(module_name, '/'), lca_string_view_last_index_of(module_name, '\\'));
        if (last_slash_index >= 0) {
            module_name.data += (last_slash_index + 1);
            module_name.count -= (last_slash_index + 1);
        }

        int64_t first_dot_index = lca_string_view_index_of(module_name, '.');
        if (first_dot_index >= 0) {
            module_name.count = first_dot_index;
        }

        if (module_name.count == 0) {
            lyir_write_error(lyir_context, import_node->decl_import.module_name.location, "Could not implicitly create a valid Laye identifier from the module file path.");
        } else {
            for (int64_t j = 0; j < module_name.count; j++) {
                if (!is_identifier_char(module_name.data[j])) {
                    lyir_write_error(lyir_context, import_node->decl_import.module_name.location, "Could not implicitly create a valid Laye identifier from the module file path.");
                    break;
                }
            }
        }

        // lyir_write_note(context, import_node->decl_import.module_name.location, "calculated module name: '%.*s'\n", LCA_STR_EXPAND(module_name));
    }

    return module_name;
}

static void laye_sema_add_symbol_shallow_copy(laye_module* module, laye_symbol* namespace_symbol, laye_symbol* symbol) {
    laye_symbol* existing_symbol = laye_symbol_lookup(namespace_symbol, symbol->name);
    if (existing_symbol == NULL) {
        existing_symbol = laye_symbol_create(module, symbol->kind, symbol->name);
        lca_da_push(namespace_symbol->symbols, existing_symbol);
    }

    assert(existing_symbol != NULL);
    assert(existing_symbol->kind == symbol->kind);

    if (symbol->kind == LAYE_SYMBOL_NAMESPACE) {
        assert(existing_symbol->kind == LAYE_SYMBOL_NAMESPACE);
        // this node should also be freshly created
        assert(lca_da_count(existing_symbol->symbols) == 0);

        for (int64_t j = 0, j_count = lca_da_count(symbol->symbols); j < j_count; j++) {
            lca_da_push(existing_symbol->symbols, symbol->symbols[j]);
        }
    } else {
        assert(existing_symbol->kind == LAYE_SYMBOL_ENTITY);

        for (int64_t j = 0, j_count = lca_da_count(symbol->nodes); j < j_count; j++) {
            lca_da_push(existing_symbol->nodes, symbol->nodes[j]);
        }
    }
}

static void laye_sema_resolve_import_query(laye_context* laye_context, laye_module* module, laye_module* queried_module, laye_node* query, bool export) {
    assert(laye_context != NULL);
    assert(module != NULL);
    assert(queried_module != NULL);

    lyir_context* lyir_context = laye_context->lyir_context;
    assert(lyir_context != NULL);

    laye_module* search_module = queried_module;
    laye_symbol* search_namespace = search_module->exports;

    if (query->import_query.is_wildcard) {
        for (int64_t i = 0, count = lca_da_count(search_namespace->symbols); i < count; i++) {
            laye_symbol* exported_symbol = search_namespace->symbols[i];
            assert(exported_symbol != NULL);
            assert(exported_symbol->name.count > 0);

            laye_symbol* imported_symbol = laye_symbol_lookup(module->imports, exported_symbol->name);
            if (imported_symbol == NULL) {
                imported_symbol = laye_symbol_create(module, exported_symbol->kind, exported_symbol->name);
                assert(imported_symbol != NULL);
                lca_da_push(module->imports->symbols, imported_symbol);
            } else {
                if (exported_symbol->kind == LAYE_SYMBOL_NAMESPACE) {
                    lyir_write_error(lyir_context, query->location, "Wildcard imports symbol '%.*s', which is a namespace. This symbol has already been declared, and namespace names cannot be overloaded.");
                    continue;
                }

                if (imported_symbol->kind == LAYE_SYMBOL_NAMESPACE) {
                    lyir_write_error(lyir_context, query->location, "Wildcard imports symbol '%.*s', which was previously imported as a namespace. Namespace names cannot be overloaded.");
                    continue;
                }
            }

            assert(imported_symbol != NULL);

            if (exported_symbol->kind == LAYE_SYMBOL_NAMESPACE) {
                assert(imported_symbol->kind == LAYE_SYMBOL_NAMESPACE);
                // this node should also be freshly created
                assert(lca_da_count(imported_symbol->symbols) == 0);

                for (int64_t j = 0, j_count = lca_da_count(exported_symbol->symbols); j < j_count; j++) {
                    lca_da_push(imported_symbol->symbols, exported_symbol->symbols[j]);
                }
            } else {
                assert(imported_symbol->kind == LAYE_SYMBOL_ENTITY);

                for (int64_t j = 0, j_count = lca_da_count(exported_symbol->nodes); j < j_count; j++) {
                    lca_da_push(imported_symbol->nodes, exported_symbol->nodes[j]);
                }
            }

            if (export) {
                laye_sema_add_symbol_shallow_copy(module, module->exports, imported_symbol);
            }
        }
    } else {
        assert(lca_da_count(query->import_query.pieces) > 0);

        laye_symbol* resolved_symbol = NULL;
        for (int64_t i = 0, count = lca_da_count(query->import_query.pieces); i < count; i++) {
            bool is_last_name_in_path = i == count - 1;

            laye_token search_token = query->import_query.pieces[i];
            lca_string_view search_name = search_token.string_value;

            assert(search_namespace != NULL);

            if (search_namespace->kind == LAYE_SYMBOL_ENTITY) {
                assert(i > 0);
                laye_token previous_search_token = query->import_query.pieces[i - 1];
                lyir_write_error(
                    lyir_context,
                    previous_search_token.location,
                    "The imported name '%.*s' does not resolve to a namespace. Cannot search it for a child entity named '%.*s'.",
                    LCA_STR_EXPAND(previous_search_token.string_value),
                    LCA_STR_EXPAND(search_name)
                );
                break;
            }

            assert(search_namespace->kind == LAYE_SYMBOL_NAMESPACE);

            laye_symbol* found_lookup_symbol = NULL;
            for (int64_t j = 0, j_count = lca_da_count(search_namespace->symbols); j < j_count; j++) {
                laye_symbol* search_symbol = search_namespace->symbols[j];
                assert(search_symbol != NULL);

                if (lca_string_view_equals(search_symbol->name, search_name)) {
                    found_lookup_symbol = search_symbol;
                    break;
                }
            }

            if (found_lookup_symbol == NULL) {
                lyir_write_error(
                    lyir_context,
                    search_token.location,
                    "The name '%.*s' does not exist in this context.",
                    LCA_STR_EXPAND(search_name)
                );
                break;
            }

            if (is_last_name_in_path) {
                resolved_symbol = found_lookup_symbol;
            } else {
                search_namespace = found_lookup_symbol;
            }
        }

        if (resolved_symbol == NULL) {
            // if all went well, we reported an error. just leave
            return;
        }

        lca_string_view query_result_name = query->import_query.alias.string_value;
        if (query_result_name.count == 0) {
            query_result_name = query->import_query.pieces[lca_da_count(query->import_query.pieces) - 1].string_value;
        }

        laye_symbol* imported_symbol = laye_symbol_lookup(module->imports, query_result_name);
        if (imported_symbol == NULL) {
            imported_symbol = laye_symbol_create(module, resolved_symbol->kind, query_result_name);
            assert(imported_symbol != NULL);
            lca_da_push(module->imports->symbols, imported_symbol);
        } else {
            if (resolved_symbol->kind == LAYE_SYMBOL_NAMESPACE) {
                lyir_write_error(lyir_context, query->location, "Query imports symbol '%.*s', which is a namespace. This symbol has already been declared, and namespace names cannot be overloaded.");
                return;
            }

            if (imported_symbol->kind == LAYE_SYMBOL_NAMESPACE) {
                lyir_write_error(lyir_context, query->location, "Query imports symbol '%.*s', which was previously imported as a namespace. Namespace names cannot be overloaded.");
                return;
            }
        }

        if (resolved_symbol->kind == LAYE_SYMBOL_NAMESPACE) {
            assert(imported_symbol->kind == LAYE_SYMBOL_NAMESPACE);
            // this node should also be freshly created
            assert(lca_da_count(imported_symbol->symbols) == 0);

            for (int64_t j = 0, j_count = lca_da_count(resolved_symbol->symbols); j < j_count; j++) {
                lca_da_push(imported_symbol->symbols, resolved_symbol->symbols[j]);
            }
        } else {
            assert(imported_symbol->kind == LAYE_SYMBOL_ENTITY);

            for (int64_t j = 0, j_count = lca_da_count(resolved_symbol->nodes); j < j_count; j++) {
                lca_da_push(imported_symbol->nodes, resolved_symbol->nodes[j]);
            }
        }

        /*

        // populate the module's scope with the imported entities.
        for (int64_t k = 0, k_count = lca_da_count(query->import_query.imported_entities); k < k_count; k++) {
            laye_node* entity = query->import_query.imported_entities[k];
            assert(laye_node_is_decl(entity));
            assert(entity->declared_name.count > 0);
            assert(entity->declared_name.data != NULL);

            if (query->import_query.alias.string_value.count > 0)
                laye_scope_declare_aliased(module->scope, entity, query->import_query.alias.string_value);
            else laye_scope_declare(module->scope, entity);
        }

        // add imported modules to the same import module array as import declarations with namespaces.
        for (int64_t k = 0, k_count = lca_da_count(query->import_query.imported_modules); k < k_count; k++) {
            lca_da_push(module->imports, query->import_query.imported_modules[k]);
        }

        */
    }
}

static lca_string laye_sema_get_module_import_file_path(laye_context* laye_context, lca_string_view relative_module_path, lca_string_view import_name) {
    // first try to find the file based on the relative directory of the module requesting it
    int64_t last_slash_index = maxi(lca_string_view_last_index_of(relative_module_path, '/'), lca_string_view_last_index_of(relative_module_path, '\\'));

    lca_string lookup_path = lca_string_create(lca_default_allocator);
    if (last_slash_index < 0) {
        lca_string_append_format(&lookup_path, "./");
    } else {
        relative_module_path.count = last_slash_index;
        lca_string_append_format(&lookup_path, "%.*s", LCA_STR_EXPAND(relative_module_path));
    }

    lca_string_path_append_view(&lookup_path, import_name);
    if (lcat_file_exists(lca_string_as_cstring(lookup_path))) {
        return lookup_path;
    }

    for (int64_t include_index = 0, include_count = lca_da_count(laye_context->include_directories); include_index < include_count; include_index++) {
        lca_string_view include_path = laye_context->include_directories[include_index];

        memset(lookup_path.data, 0, (size_t)lookup_path.count);
        lookup_path.count = 0;

        lca_string_append_format(&lookup_path, "%.*s", LCA_STR_EXPAND(include_path));
        lca_string_path_append_view(&lookup_path, import_name);
        if (lcat_file_exists(lca_string_as_cstring(lookup_path))) {
            return lookup_path;
        }
    }

    lca_string_destroy(&lookup_path);
    return (lca_string){0};
}

static void laye_sema_resolve_module_import_declarations(laye_context* laye_context, lyir_dependency_graph* import_graph, laye_module* module) {
    assert(laye_context != NULL);
    assert(module != NULL);

    if (module->has_handled_imports) {
        return;
    }

    lyir_context* lyir_context = laye_context->lyir_context;
    assert(lyir_context != NULL);

    module->has_handled_imports = true;

    lyir_depgraph_ensure_tracked(import_graph, module);

    for (int64_t i = 0, count = lca_da_count(module->top_level_nodes); i < count; i++) {
        laye_node* top_level_node = module->top_level_nodes[i];
        assert(top_level_node != NULL);

        switch (top_level_node->kind) {
            default: break; // nothing else is an import declaration, just ignore them in this step

            case LAYE_NODE_DECL_IMPORT: {
                laye_token module_name_token = top_level_node->decl_import.module_name;
                if (module_name_token.kind == LAYE_TOKEN_IDENT) {
                    laye_sema_set_errored(top_level_node);
                    lyir_write_error(lyir_context, module_name_token.location, "Currently, module names cannot be identifiers; this syntax is reserved for future features that are not implemented yet.");
                }

                lyir_source source = lyir_context_get_source(lyir_context, module->sourceid);
                lca_string lookup_path = laye_sema_get_module_import_file_path(laye_context, lca_string_as_view(source.name), module_name_token.string_value);

                if (lookup_path.count == 0) {
                    laye_sema_set_errored(top_level_node);
                    lyir_write_error(lyir_context, module_name_token.location, "Cannot find module file to import: '%.*s'", LCA_STR_EXPAND(module_name_token.string_value));
                    continue;
                }

                lyir_sourceid sourceid = lyir_context_get_or_add_source_from_file(lyir_context, lca_string_as_view(lookup_path));
                lca_string_destroy(&lookup_path);

                laye_module* found = NULL;
                for (int64_t i = 0, count = lca_da_count(laye_context->laye_modules); i < count && found == NULL; i++) {
                    laye_module* module = laye_context->laye_modules[i];
                    if (module->sourceid == sourceid) {
                        found = module;
                    }
                }

                if (found == NULL) {
                    found = laye_parse(laye_context, sourceid);
                }

                assert(found != NULL);

                lyir_depgraph_add_dependency(import_graph, module, found);
                top_level_node->decl_import.referenced_module = found;

                laye_sema_resolve_module_import_declarations(laye_context, import_graph, found);
            } break;
        }
    }
}

static void laye_sema_build_module_symbol_tables(laye_module* module) {
    assert(module != NULL);

    laye_context* laye_context = module->context;
    assert(module->context != NULL);

    lyir_context* lyir_context = laye_context->lyir_context;
    assert(lyir_context != NULL);

    assert(module->exports == NULL);
    module->exports = laye_symbol_create(module, LAYE_SYMBOL_NAMESPACE, LCA_SV_EMPTY);
    assert(module->exports != NULL);

    assert(module->imports == NULL);
    module->imports = laye_symbol_create(module, LAYE_SYMBOL_NAMESPACE, LCA_SV_EMPTY);
    assert(module->imports != NULL);

    for (int64_t i = 0, count = lca_da_count(module->top_level_nodes); i < count; i++) {
        laye_node* top_level_node = module->top_level_nodes[i];
        if (laye_sema_is_errored(top_level_node)) {
            continue;
        }

        switch (top_level_node->kind) {
            default: break;

            case LAYE_NODE_DECL_IMPORT: {
                assert(top_level_node->decl_import.referenced_module != NULL);
                bool is_export_import = top_level_node->attributes.linkage == LYIR_LINK_EXPORTED;

                if (lca_da_count(top_level_node->decl_import.import_queries) == 0) {
                    lca_string_view module_name = import_string_to_laye_identifier_string(top_level_node);
                    assert(module_name.count > 0);

                    if (laye_symbol_lookup(module->imports, module_name) != NULL) {
                        lyir_write_error(lyir_context, top_level_node->location, "Redeclaration of name '%.*s'.", LCA_STR_EXPAND(module_name));
                    } else {
                        laye_symbol* import_scope = laye_symbol_create(module, LAYE_SYMBOL_NAMESPACE, module_name);
                        assert(import_scope != NULL);

                        lca_da_push(module->imports->symbols, import_scope);

                        if (is_export_import) {
                            assert(laye_symbol_lookup(module->exports, module_name) == NULL && "somehow, this module already exports something with the same name");
                            lca_da_push(module->exports->symbols, import_scope);
                        }

                        // shallow-copy all of the referenced module's exports into this new scope for our own imports (and potentially exports)
                        laye_module* referenced_module = top_level_node->decl_import.referenced_module;
                        assert(referenced_module != NULL);
                        assert(referenced_module->exports != NULL);

                        for (int64_t export_index = 0, export_count = lca_da_count(referenced_module->exports->symbols); export_index < export_count; export_index++) {
                            laye_symbol* imported_symbol = referenced_module->exports->symbols[export_index];
                            assert(imported_symbol != NULL);
                            assert(laye_symbol_lookup(import_scope, imported_symbol->name) == NULL);
                            lca_da_push(import_scope->symbols, imported_symbol);
                        }
                    }
                } else {
                    // no import namespaces, populate this scope directly
                    lca_da(laye_node*) queries = top_level_node->decl_import.import_queries;
                    for (int64_t j = 0, j_count = lca_da_count(queries); j < j_count; j++) {
                        laye_node* query = queries[j];
                        assert(query != NULL);

                        laye_sema_resolve_import_query(laye_context, query->module, top_level_node->decl_import.referenced_module, query, is_export_import);
                    }
                }
            } break;

            case LAYE_NODE_DECL_ALIAS:
            case LAYE_NODE_DECL_BINDING:
            case LAYE_NODE_DECL_ENUM:
            case LAYE_NODE_DECL_FUNCTION:
            case LAYE_NODE_DECL_STRUCT: {
                if (top_level_node->attributes.linkage != LYIR_LINK_EXPORTED) {
                    break;
                }

                laye_symbol* export_symbol = laye_symbol_lookup(module->exports, top_level_node->declared_name);
                if (export_symbol != NULL) {
                    if (export_symbol->kind == LAYE_SYMBOL_NAMESPACE) {
                        lyir_write_error(lyir_context, top_level_node->location, "Redeclaration of symbol '%.*s', previously declared as a namespace.", LCA_STR_EXPAND(top_level_node->declared_name));
                        break;
                    }
                } else {
                    export_symbol = laye_symbol_create(module, LAYE_SYMBOL_ENTITY, top_level_node->declared_name);
                    lca_da_push(module->exports->symbols, export_symbol);
                }

                assert(export_symbol != NULL);
                assert(export_symbol->kind == LAYE_SYMBOL_ENTITY);

                lca_da_push(export_symbol->nodes, top_level_node);
            } break;
        }
    }
}

void laye_analyse(laye_context* laye_context) {
    assert(laye_context != NULL);
    assert(laye_context->laye_dependencies != NULL);

    lyir_context* lyir_context = laye_context->lyir_context;
    assert(lyir_context != NULL);

    laye_sema sema = {
        .context = laye_context,
        .dependencies = laye_context->laye_dependencies,
    };

    lyir_dependency_graph* import_graph = lyir_dependency_graph_create_in_context(lyir_context);

    // Step 1 of generating semantic symbols is making sure we have access to all of the modules we want to use.
    // We walk through all import declarations recursively for all modules and resolve them to a valid module pointer.
    // This may involve parsing all new source files if they haven't been parsed yet.
    // NOTHING ELSE HAPPENS IN THIS STAGE
    for (int64_t i = 0, count = lca_da_count(laye_context->laye_modules); i < count; i++) {
        laye_sema_resolve_module_import_declarations(laye_context, import_graph, laye_context->laye_modules[i]);
    }

    lyir_dependency_order_result import_order_result = lyir_dependency_graph_get_ordered_entities(import_graph);
    if (import_order_result.status == LYIR_DEP_CYCLE) {
        laye_module* from = (laye_module*)import_order_result.from;
        laye_module* to = (laye_module*)import_order_result.to;

        lyir_write_error(
            lyir_context,
            (lyir_location){.sourceid = from->sourceid},
            "Cyclic dependency detected. module '%.*s' depends on %.*s, and vice versa. Eventually this will be supported, but the import resolution is currently not graunular enough.",
            LCA_STR_EXPAND(lyir_context_get_source(lyir_context, from->sourceid).name),
            LCA_STR_EXPAND(lyir_context_get_source(lyir_context, to->sourceid).name)
        );

        return;
    }

    assert(import_order_result.status == LYIR_DEP_OK);
    lca_da(laye_module*) ordered_modules = (lca_da(laye_module*))import_order_result.ordered_entities;
    assert(lca_da_count(ordered_modules) == lca_da_count(laye_context->laye_modules));

    // Step 2 is building the import/export symbol tables for each module.
    // (in dependency order)
    for (int64_t i = 0, count = lca_da_count(ordered_modules); i < count; i++) {
        laye_module* module = ordered_modules[i];
        // fprintf(stderr, "module: %.*s\n", LCA_STR_EXPAND(layec_context_get_source(context, module->sourceid).name));
        laye_sema_build_module_symbol_tables(module);

        assert(module->imports != NULL);
        assert(module->exports != NULL);
    }

    lca_da_free(ordered_modules);

    // TODO(local): somewhere in here, before sema is done, we have to check for redeclared symbols.
    // probably after top level types.

    // return;

    for (int64_t i = 0, count = lca_da_count(laye_context->laye_modules); i < count; i++) {
        laye_generate_dependencies_for_module(sema.dependencies, laye_context->laye_modules[i]);
    }

    lyir_dependency_order_result order_result = lyir_dependency_graph_get_ordered_entities(sema.dependencies);
    if (order_result.status == LYIR_DEP_CYCLE) {
        lyir_write_error(
            lyir_context,
            ((laye_node*)order_result.from)->location,
            "Cyclic dependency detected. %.*s depends on %.*s, and vice versa.",
            LCA_STR_EXPAND(((laye_node*)order_result.from)->declared_name),
            LCA_STR_EXPAND(((laye_node*)order_result.to)->declared_name)
        );

        lyir_write_note(
            lyir_context,
            ((laye_node*)order_result.to)->location,
            "%.*s declared here.",
            LCA_STR_EXPAND(((laye_node*)order_result.to)->declared_name)
        );

        return;
    }

    assert(order_result.status == LYIR_DEP_OK);
    lca_da(laye_node*) ordered_nodes = (lca_da(laye_node*))order_result.ordered_entities;

    // for (int64_t i = 0, count = lca_da_count(ordered_nodes); i < count; i++) {
    //     fprintf(stderr, ">>  %s :: %.*s\n", laye_node_kind_to_cstring(ordered_nodes[i]->kind), LCA_STR_EXPAND(ordered_nodes[i]->declared_name));
    // }

    for (int64_t i = 0, count = lca_da_count(ordered_nodes); i < count; i++) {
        laye_node* node = ordered_nodes[i];
        assert(node != NULL);
        // fprintf(stderr, ANSI_COLOR_BLUE "%016lX\n", (size_t)node);
        laye_sema_resolve_top_level_types(&sema, &node);
        assert(node != NULL);
    }

    for (int64_t i = 0, count = lca_da_count(ordered_nodes); i < count; i++) {
        laye_node* node = ordered_nodes[i];
        assert(node != NULL);
        laye_sema_analyse_node(&sema, &node, NOTY);
        assert(node != NULL);
    }

    lca_da_free(ordered_nodes);
}

static laye_node* laye_sema_build_struct_type(laye_sema* sema, laye_node* node, laye_node* parent_struct) {
    assert(sema != NULL);
    assert(node != NULL);
    if (parent_struct != NULL) {
        assert(parent_struct->kind == LAYE_NODE_TYPE_STRUCT);
    }

    laye_context* context = sema->context;
    assert(context != NULL);

    lyir_context* lyir_context = context->lyir_context;
    assert(lyir_context != NULL);

    laye_node* struct_type = laye_node_create(node->module, LAYE_NODE_TYPE_STRUCT, node->location, LTY(context->laye_types.type));
    assert(struct_type != NULL);
    struct_type->type_struct.name = node->declared_name;
    struct_type->type_struct.parent_struct_type = parent_struct;

    for (int64_t i = 0, count = lca_da_count(node->decl_struct.field_declarations); i < count; i++) {
        laye_node* field_node = node->decl_struct.field_declarations[i];
        assert(field_node != NULL);
        assert(field_node->kind == LAYE_NODE_DECL_STRUCT_FIELD);

        (void)laye_sema_analyse_type(sema, &field_node->declared_type);

        lyir_evaluated_constant constant_initial_value = {0};
        if (field_node->decl_struct_field.initializer != NULL) {
            if (laye_sema_analyse_node(sema, &field_node->decl_struct_field.initializer, field_node->declared_type)) {
                laye_sema_convert_or_error(sema, &field_node->decl_struct_field.initializer, field_node->declared_type);

                if (!laye_expr_evaluate(field_node->decl_struct_field.initializer, &constant_initial_value, true)) {
                    // make sure it's still zero'd
                    constant_initial_value = (lyir_evaluated_constant){0};
                    lyir_write_error(lyir_context, field_node->decl_struct_field.initializer->location, "Could not evaluate field initializer. Nontrivial compile-time execution is not currently supported.");
                }
            }
        }

        laye_struct_type_field field = {
            .type = field_node->declared_type,
            .name = field_node->declared_name,
            .initial_value = constant_initial_value,
        };

        lca_da_push(struct_type->type_struct.fields, field);
    }

    for (int64_t i = 0, count = lca_da_count(node->decl_struct.variant_declarations); i < count; i++) {
        laye_node* variant_node = node->decl_struct.variant_declarations[i];
        assert(variant_node != NULL);
        assert(variant_node->kind == LAYE_NODE_DECL_STRUCT);

        laye_node* variant_type = laye_sema_build_struct_type(sema, variant_node, struct_type);
        assert(variant_type != NULL);
        assert(variant_type->kind == LAYE_NODE_TYPE_STRUCT);

        laye_struct_type_variant variant = {
            .type = LTY(variant_type),
            .name = variant_node->declared_name,
        };

        lca_da_push(struct_type->type_struct.variants, variant);
    }

    return struct_type;
}

static void laye_sema_resolve_top_level_types(laye_sema* sema, laye_node** node_ref) {
    laye_node* node = *node_ref;

    assert(sema != NULL);
    assert(node != NULL);
    assert(node != NULL);
    assert(node->module != NULL);

    laye_context* context = sema->context;
    assert(context != NULL);
    assert(node->module->context == sema->context);

    lyir_context* lyir_context = context->lyir_context;
    assert(lyir_context != NULL);

    switch (node->kind) {
        default: {
            fprintf(stderr, "on node kind %s\n", laye_node_kind_to_cstring(node->kind));
            assert(false && "unreachable");
        } break;

        case LAYE_NODE_DECL_FUNCTION: {
            assert(node->decl_function.return_type.node != NULL);
            if (!laye_sema_analyse_type(sema, &node->decl_function.return_type)) {
                laye_sema_set_errored(node);
                node->type = LTY(context->laye_types.poison);
            }

            for (int64_t i = 0, count = lca_da_count(node->decl_function.parameter_declarations); i < count; i++) {
                assert(node->decl_function.parameter_declarations[i] != NULL);
                assert(node->decl_function.parameter_declarations[i]->declared_type.node != NULL);
                if (!laye_sema_analyse_type(sema, &node->decl_function.parameter_declarations[i]->declared_type)) {
                    laye_sema_set_errored(node);
                    node->type = LTY(context->laye_types.poison);
                }
            }

            assert(node->declared_type.node != NULL);
            assert(laye_type_is_function(node->declared_type));
            if (!laye_sema_analyse_type(sema, &node->declared_type)) {
                laye_sema_set_errored(node);
                node->type = LTY(context->laye_types.poison);
            }

            bool is_declared_main = lca_string_view_equals(LCA_SV_CONSTANT("main"), node->declared_name);
            bool has_foreign_name = node->attributes.foreign_name.count != 0;
            bool has_body = lca_da_count(node->decl_function.body) != 0;

            if (is_declared_main && !has_foreign_name) {
                node->attributes.calling_convention = LYIR_CCC;
                node->attributes.linkage = LYIR_LINK_EXPORTED;
                node->attributes.mangling = LYIR_MANGLE_NONE;

                node->declared_type.node->type_function.calling_convention = LYIR_CCC;

                if (!has_body) {
                    // TODO(local): should we allow declarations of main?
                }
            }
        } break;

        case LAYE_NODE_DECL_STRUCT: {
            node->declared_type = LTY(laye_sema_build_struct_type(sema, node, NULL));
            assert(node->declared_type.node != NULL);
            assert(node->declared_type.node->kind == LAYE_NODE_TYPE_STRUCT);
            laye_sema_analyse_type(sema, &node->declared_type);
            assert(node->declared_type.node != NULL);
            assert(node->declared_type.node->kind == LAYE_NODE_TYPE_STRUCT);
            node->sema_state = LYIR_SEMA_DONE;
        } break;

        case LAYE_NODE_DECL_BINDING:
        case LAYE_NODE_DECL_TEST: {
            return;
        } break;
    }

    assert(node->declared_type.node != NULL);
    *node_ref = node;
}

static laye_node* wrap_yieldable_value_in_compound(laye_sema* sema, laye_node** value_ref, laye_type expected_type) {
    assert(sema != NULL);
    assert(value_ref != NULL);

    laye_context* context = sema->context;
    assert(context != NULL);

    lyir_context* lyir_context = context->lyir_context;
    assert(lyir_context != NULL);

    laye_sema_analyse_node(sema, value_ref, expected_type);
    laye_node* value = *value_ref;
    assert(value != NULL);

    laye_node* yield_node = laye_node_create(value->module, LAYE_NODE_YIELD, value->location, LTY(context->laye_types._void));
    assert(yield_node != NULL);
    yield_node->compiler_generated = true;
    yield_node->yield.value = value;

    laye_node* compound_node = laye_node_create(value->module, LAYE_NODE_COMPOUND, value->location, LTY(context->laye_types._void));
    assert(compound_node != NULL);
    compound_node->compiler_generated = true;
    lca_da_push(compound_node->compound.children, yield_node);

    return compound_node;
}

static bool laye_sema_analyse_type(laye_sema* sema, laye_type* type) {
    if (!laye_sema_analyse_node(sema, &type->node, NOTY)) {
        return false;
    }

    if (laye_type_is_nameref(*type)) {
        assert(type->node->nameref.referenced_type != NULL);
        *type = (laye_type){
            .node = type->node->nameref.referenced_type,
            .source_node = type->node,
            .is_modifiable = type->is_modifiable,
        };
        laye_sema_analyse_type(sema, type);
    } else if (laye_type_is_alias(*type)) {
        laye_type underlying_type = type->node->type_alias.underlying_type;
        assert(underlying_type.node != NULL);
        *type = laye_type_with_source(underlying_type.node, type->node, type->is_modifiable | underlying_type.is_modifiable);
        laye_sema_analyse_type(sema, type);
    }

    return true;
}

static laye_node* laye_node_instantiate_template(laye_sema* sema, lyir_location instantiation_location, laye_node* templated_node, lca_da(laye_template_arg) arguments) {
    assert(sema != NULL);
    assert(templated_node != NULL);

    laye_context* laye_context = sema->context;
    assert(laye_context != NULL);

    //

    assert(false && "laye_node_instantiate_template is not implemented");
    return NULL;
}

static laye_struct_type_field laye_sema_create_padding_field(laye_sema* sema, laye_module* module, lyir_location location, int padding_bytes);

static bool laye_sema_analyse_node(laye_sema* sema, laye_node** node_ref, laye_type expected_type) {
    laye_node* node = *node_ref;

    assert(sema != NULL);
    assert(node_ref != NULL);
    assert(node != NULL);
    if (!laye_node_is_type(node)) {
        assert(node->module != NULL);
        assert(node->module->context == sema->context);
    }
    assert(node->type.node != NULL);

    if (expected_type.node != NULL) {
        assert(expected_type.node->sema_state == LYIR_SEMA_DONE);
    }

    if (node->sema_state == LYIR_SEMA_DONE || laye_sema_is_errored(node)) {
        return node->sema_state == LYIR_SEMA_DONE;
    }

    if (node->sema_state == LYIR_SEMA_IN_PROGRESS) {
        assert(false && "node already in progress");
        return false;
    }

    laye_context* laye_context = sema->context;
    assert(laye_context != NULL);

    lyir_context* lyir_context = laye_context->lyir_context;
    assert(lyir_context != NULL);

    node->sema_state = LYIR_SEMA_IN_PROGRESS;
    if (node->type.node->kind != LAYE_NODE_TYPE_VAR) {
        laye_sema_analyse_type(sema, &node->type);
    }

    switch (node->kind) {
        default: {
            fprintf(stderr, "on node kind %s\n", laye_node_kind_to_cstring(node->kind));
            assert(false && "unreachable");
        } break;

        case LAYE_NODE_DECL_FUNCTION: {
            laye_node* prev_function = sema->current_function;
            sema->current_function = node;

            for (int64_t i = 0, count = lca_da_count(node->decl_function.parameter_declarations); i < count; i++) {
                laye_sema_analyse_node(sema, &node->decl_function.parameter_declarations[i], NOTY);
            }

            if (node->decl_function.body != NULL) {
                assert(node->decl_function.body->kind == LAYE_NODE_COMPOUND);
                laye_sema_analyse_node(sema, &node->decl_function.body, NOTY);

                if (!laye_type_is_noreturn(node->decl_function.body->type)) {
                    if (laye_type_is_void(node->decl_function.return_type)) {
                        laye_node* implicit_return = laye_node_create(node->module, LAYE_NODE_RETURN, node->decl_function.body->location, LTY(laye_context->laye_types.noreturn));
                        assert(implicit_return != NULL);
                        implicit_return->sema_state = LYIR_SEMA_DONE;
                        implicit_return->compiler_generated = true;
                        lca_da_push(node->decl_function.body->compound.children, implicit_return);
                        node->decl_function.body->type = LTY(laye_context->laye_types.noreturn);
                    } else if (laye_type_is_noreturn(node->decl_function.return_type)) {
                        lyir_write_error(lyir_context, node->location, "Control flow reaches the end of a `noreturn` function.");
                    } else {
                        lyir_write_error(lyir_context, node->location, "Not all code paths return a value.");
                    }
                }

            }

            sema->current_function = prev_function;
        } break;

        case LAYE_NODE_DECL_BINDING: {
            bool infer = false;
            if (node->declared_type.node->kind == LAYE_NODE_TYPE_VAR) {
                infer = true;
                node->declared_type.node->sema_state = LYIR_SEMA_DONE;

                if (node->decl_binding.initializer == NULL) {
                    node->declared_type = LTY(sema->context->laye_types.poison);
                    laye_sema_set_errored(node);
                    lyir_write_error(
                        lyir_context,
                        node->declared_type.node->location,
                        "Cannot infer the type of a declaration without an initializer."
                    );

                    goto done_with_decl_binding;
                }
            } else if (!laye_sema_analyse_type(sema, &node->declared_type)) {
                laye_sema_set_errored(node);
                break;
            }

            assert(node->declared_type.node != NULL);
            if (node->decl_binding.initializer != NULL) {
                laye_type expected_type = {0};
                if (!infer) {
                    expected_type = node->declared_type;
                }

                if (laye_sema_analyse_node(sema, &node->decl_binding.initializer, expected_type)) {
                    if (!infer) {
                        assert(expected_type.node != NULL);
                        laye_sema_convert_or_error(sema, &node->decl_binding.initializer, expected_type);
                    }
                }

                if (infer) {
                    assert(node->decl_binding.initializer->type.node != NULL);
                    node->declared_type = node->decl_binding.initializer->type;
                }
            }

        done_with_decl_binding:;
            assert(node->declared_type.node != NULL);
            assert(node->declared_type.node->kind != LAYE_NODE_TYPE_VAR);
            if (node->decl_binding.initializer != NULL) {
                assert(node->decl_binding.initializer->type.node != NULL);
                assert(node->decl_binding.initializer->type.node->kind != LAYE_NODE_TYPE_VAR);
            }
        } break;

        case LAYE_NODE_DECL_STRUCT: {
            assert(node->declared_type.node != NULL);
            assert(node->declared_type.node->kind == LAYE_NODE_TYPE_STRUCT);
        } break;

        case LAYE_NODE_DECL_FUNCTION_PARAMETER: {
            laye_sema_analyse_type(sema, &node->declared_type);
            if (node->decl_function_parameter.default_value != NULL) {
                // TODO: Analyse default value
                // node->decl_function_parameter.default_value
            }
        } break;

        case LAYE_NODE_DECL_TEST: {
            // assert(!sema->is_in_test);
            // sema->is_in_test = true;

            if (node->decl_test.is_named) {
                assert(lca_da_count(node->decl_test.nameref.pieces) > 0);
                node->decl_test.referenced_decl_node = laye_sema_lookup_value_declaration(node->module, node->decl_test.nameref);
                if (node->decl_test.referenced_decl_node == NULL) {
                    laye_sema_set_errored(node);
                }
            }

            assert(node->decl_test.body != NULL);
            if (!laye_sema_analyse_node_and_discard(sema, &node->decl_test.body)) {
                laye_sema_set_errored(node);
            }

            // sema->is_in_test = false;
        } break;

        case LAYE_NODE_ASSERT: {
            assert(node->_assert.condition != NULL);
            if (!laye_sema_analyse_node(sema, &node->_assert.condition, LTY(laye_context->laye_types._bool))) {
                laye_sema_set_errored(node);
                break;
            }

            laye_sema_convert_or_error(sema, &node->_assert.condition, LTY(laye_context->laye_types._bool));
        } break;

        case LAYE_NODE_IF: {
            bool is_expression = node->_if.is_expr;
            bool is_noreturn = true;

            assert(lca_da_count(node->_if.conditions) == lca_da_count(node->_if.passes));
            for (int64_t i = 0, count = lca_da_count(node->_if.conditions); i < count; i++) {
                if (laye_sema_analyse_node(sema, &node->_if.conditions[i], LTY(laye_context->laye_types._bool))) {
                    laye_sema_convert_or_error(sema, &node->_if.conditions[i], LTY(laye_context->laye_types._bool));
                } else {
                    laye_sema_set_errored(node);
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
                    laye_sema_set_errored(node);
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
                    laye_sema_set_errored(node);
                }

                sema->current_yield_target = prev_yield_target;

                if (!laye_type_is_noreturn(node->_if.fail->type)) {
                    is_noreturn = false;
                }
            } else {
                is_noreturn = false;
            }

            if (is_noreturn) {
                node->type = LTY(laye_context->laye_types.noreturn);
            } else {
                if (is_expression) {
                    node->type = expected_type;
                } else {
                    assert(node->type.node != NULL);
                    assert(laye_type_is_void(node->type));
                }
            }
        } break;

        case LAYE_NODE_FOR: {
            if (node->_for.initializer != NULL) {
                if (!laye_sema_analyse_node(sema, &node->_for.initializer, NOTY)) {
                    laye_sema_set_errored(node);
                }
            }

            bool is_condition_always_true = false;
            if (node->_for.condition == NULL) {
                is_condition_always_true = true;
            } else {
                if (!laye_sema_analyse_node(sema, &node->_for.condition, LTY(laye_context->laye_types._bool))) {
                    laye_sema_set_errored(node);
                } else {
                    // laye_sema_lvalue_to_rvalue(sema, &node->_for.condition, true);
                    laye_sema_convert_or_error(sema, &node->_for.condition, LTY(laye_context->laye_types._bool));

                    lyir_evaluated_constant condition_constant;
                    if (laye_expr_evaluate(node->_for.condition, &condition_constant, false) && condition_constant.kind == LYIR_EVAL_BOOL && condition_constant.bool_value) {
                        laye_node* eval_condition = laye_node_create(node->module, LAYE_NODE_EVALUATED_CONSTANT, node->_for.condition->location, LTY(laye_context->laye_types._bool));
                        assert(eval_condition != NULL);
                        eval_condition->compiler_generated = true;
                        eval_condition->evaluated_constant.expr = node->_for.condition;
                        eval_condition->evaluated_constant.result = condition_constant;
                        node->_for.condition = eval_condition;
                        is_condition_always_true = true;
                    }
                }
            }

            if (node->_for.increment != NULL) {
                if (!laye_sema_analyse_node(sema, &node->_for.increment, NOTY)) {
                    laye_sema_set_errored(node);
                }
            }

            if (node->_for.has_breaks) {
                is_condition_always_true = false;
            }

            if (!laye_sema_analyse_node(sema, &node->_for.pass, NOTY)) {
                laye_sema_set_errored(node);
            }

            if (node->_for.fail != NULL) {
                if (!laye_sema_analyse_node(sema, &node->_for.fail, NOTY)) {
                    laye_sema_set_errored(node);
                }
            }

            // TODO(local): if there is a `break` within the body anywhere, then this is not true
            if (is_condition_always_true) {
                node->type = LTY(laye_context->laye_types.noreturn);
            }
        } break;

        case LAYE_NODE_FOREACH: {
            if (!laye_sema_analyse_node(sema, &node->foreach.iterable, NOTY)) {
                laye_sema_set_errored(node);
            }

            laye_type iterable_type = node->foreach.iterable->type;
            assert(iterable_type.node != NULL);

            if (laye_type_is_array(iterable_type)) {
                if (node->foreach.index_binding != NULL) {
                    node->foreach.index_binding->declared_type = LTY(laye_context->laye_types._int);
                    if (!laye_sema_analyse_node(sema, &node->foreach.index_binding, NOTY)) {
                        laye_sema_set_errored(node);
                    }
                }

                laye_type element_reference_type = LTY(laye_node_create(node->module, LAYE_NODE_TYPE_REFERENCE, node->foreach.element_binding->location, LTY(laye_context->laye_types.type)));
                assert(element_reference_type.node != NULL);
                element_reference_type.node->compiler_generated = true;
                element_reference_type.node->type_container.element_type = iterable_type.node->type_container.element_type;
                node->foreach.element_binding->declared_type = element_reference_type;
                if (!laye_sema_analyse_node(sema, &node->foreach.element_binding, NOTY)) {
                    laye_sema_set_errored(node);
                }
            } else {
                if (node->foreach.index_binding != NULL) {
                    node->foreach.index_binding->declared_type = LTY(laye_context->laye_types.poison);
                }

                node->foreach.element_binding->declared_type = LTY(laye_context->laye_types.poison);

                if (node->foreach.iterable->kind != LAYE_NODE_TYPE_POISON) {
                    lca_string type_string = lca_string_create(laye_context->allocator);
                    laye_type_print_to_string(iterable_type, &type_string, laye_context->use_color);
                    lyir_write_error(lyir_context, node->foreach.iterable->location, "Cannot iterate over type %.*s.");
                    lca_string_destroy(&type_string);
                }
            }

            if (!laye_sema_analyse_node(sema, &node->foreach.pass, NOTY)) {
                laye_sema_set_errored(node);
            }
        } break;

        case LAYE_NODE_WHILE: {
            bool is_condition_always_true = false;
            if (node->_while.condition == NULL) {
                is_condition_always_true = true;
            } else {
                if (!laye_sema_analyse_node(sema, &node->_while.condition, LTY(laye_context->laye_types._bool))) {
                    laye_sema_set_errored(node);
                } else {
                    // laye_sema_lvalue_to_rvalue(sema, &node->_while.condition, true);
                    laye_sema_convert_or_error(sema, &node->_while.condition, LTY(laye_context->laye_types._bool));

                    lyir_evaluated_constant condition_constant;
                    if (laye_expr_evaluate(node->_while.condition, &condition_constant, false) && condition_constant.kind == LYIR_EVAL_BOOL && condition_constant.bool_value) {
                        laye_node* eval_condition = laye_node_create(node->module, LAYE_NODE_EVALUATED_CONSTANT, node->_while.condition->location, LTY(laye_context->laye_types._bool));
                        assert(eval_condition != NULL);
                        eval_condition->compiler_generated = true;
                        eval_condition->evaluated_constant.expr = node->_while.condition;
                        eval_condition->evaluated_constant.result = condition_constant;
                        node->_while.condition = eval_condition;
                        is_condition_always_true = true;
                    }
                }
            }

            if (node->_while.has_breaks) {
                is_condition_always_true = false;
            }

            if (!laye_sema_analyse_node(sema, &node->_while.pass, NOTY)) {
                laye_sema_set_errored(node);
            }

            if (node->_while.fail != NULL) {
                if (!laye_sema_analyse_node(sema, &node->_while.fail, NOTY)) {
                    laye_sema_set_errored(node);
                }
            }

            // TODO(local): if there is a `break` within the body anywhere, then this is not true
            if (is_condition_always_true) {
                node->type = LTY(laye_context->laye_types.noreturn);
            }
        } break;

        case LAYE_NODE_RETURN: {
            assert(sema->current_function != NULL);
            assert(sema->current_function->type.node != NULL);
            assert(laye_node_is_type(sema->current_function->type.node));
            assert(laye_type_is_function(sema->current_function->declared_type));

            assert(laye_type_is_noreturn(node->type));
            // node->type = LTY( context->laye_types.noreturn);

            laye_type expected_return_type = sema->current_function->declared_type.node->type_function.return_type;
            assert(expected_return_type.node != NULL);
            assert(laye_node_is_type(expected_return_type.node));

            if (node->_return.value != NULL) {
                laye_sema_analyse_node(sema, &node->_return.value, expected_return_type);
                laye_sema_lvalue_to_rvalue(sema, &node->_return.value, true);
                if (laye_type_is_void(expected_return_type) || laye_type_is_noreturn(expected_return_type)) {
                    lyir_write_error(lyir_context, node->location, "Cannot return a value from a `void` or `noreturn` function.");
                } else {
                    laye_sema_convert_or_error(sema, &node->_return.value, expected_return_type);
                }
            } else {
                if (!laye_type_is_void(expected_return_type) && !laye_type_is_noreturn(expected_return_type)) {
                    lyir_write_error(lyir_context, node->location, "Must return a value from a non-void function.");
                }
            }
        } break;

        case LAYE_NODE_YIELD: {
            laye_sema_analyse_node(sema, &node->yield.value, expected_type);

            if (sema->current_yield_target == NULL) {
                lyir_write_error(lyir_context, node->location, "Must yield a value from a yieldable block.");
            } else {
                if (expected_type.node != NULL) {
                    laye_sema_convert_or_error(sema, &node->yield.value, expected_type);
                }

                assert(sema->current_yield_target->kind == LAYE_NODE_COMPOUND);
                sema->current_yield_target->type = node->yield.value->type;

                if (sema->current_yield_target->compound.is_expr && laye_expr_is_lvalue(node->yield.value)) {
                    laye_expr_set_lvalue(sema->current_yield_target, true);
                }
            }
        } break;

        case LAYE_NODE_BREAK: {
        } break;

        case LAYE_NODE_CONTINUE: {
        } break;

        case LAYE_NODE_XYZZY: {
        } break;

        case LAYE_NODE_ASSIGNMENT: {
            laye_sema_analyse_node(sema, &node->assignment.lhs, NOTY);
            laye_sema_implicit_de_reference(sema, &node->assignment.lhs);
            assert(node->assignment.lhs->type.node != NULL);

            laye_sema_analyse_node(sema, &node->assignment.rhs, node->assignment.lhs->type);
            laye_sema_lvalue_to_rvalue(sema, &node->assignment.rhs, true);
            assert(node->assignment.rhs->type.node != NULL);

            if (!laye_expr_is_lvalue(node->assignment.lhs)) {
                lyir_write_error(lyir_context, node->assignment.lhs->location, "Cannot assign to a non-lvalue.");
                laye_sema_set_errored(node);
            } else {
                laye_type nonref_target_type = laye_type_strip_references(node->assignment.lhs->type);
                laye_sema_convert_or_error(sema, &node->assignment.rhs, nonref_target_type);
            }

            if (!node->assignment.lhs->type.is_modifiable && !laye_type_is_poison(node->assignment.lhs->type)) {
                lyir_write_error(lyir_context, node->assignment.lhs->location, "Left-hand side of assignment is not mutable.");
                laye_sema_set_errored(node);
            }

            if (node->assignment.lhs->sema_state != LYIR_SEMA_DONE || node->assignment.rhs->sema_state != LYIR_SEMA_DONE) {
                laye_sema_set_errored(node);
            }
        } break;

        case LAYE_NODE_COMPOUND: {
            bool is_expression = node->compound.is_expr;

            laye_node* prev_yield_target = sema->current_yield_target;

            if (is_expression) {
                // assert(expected_type != NULL);
                sema->current_yield_target = node;
            }

            for (int64_t i = 0, count = lca_da_count(node->compound.children); i < count; i++) {
                laye_node** child_ref = &node->compound.children[i];
                assert(*child_ref != NULL);

                if ((*child_ref)->kind == LAYE_NODE_YIELD) {
                    laye_sema_analyse_node(sema, child_ref, expected_type);
                } else {
                    laye_sema_analyse_node(sema, child_ref, NOTY);
                }

                laye_node* child = *child_ref;
                if (laye_type_is_noreturn(child->type)) {
                    node->type = LTY(laye_context->laye_types.noreturn);
                }
            }

            sema->current_yield_target = prev_yield_target;
        } break;

        case LAYE_NODE_EVALUATED_CONSTANT: break;

        case LAYE_NODE_CALL: {
            laye_sema_analyse_node(sema, &node->call.callee, NOTY);

            for (int64_t i = 0, count = lca_da_count(node->call.arguments); i < count; i++) {
                laye_node** argument_node_ref = &node->call.arguments[i];
                assert(*argument_node_ref != NULL);
                laye_sema_analyse_node(sema, argument_node_ref, NOTY);
                laye_sema_lvalue_to_rvalue(sema, argument_node_ref, false);
            }

            laye_type callee_type = node->call.callee->type;
            assert(callee_type.node->sema_state == LYIR_SEMA_DONE || laye_sema_is_errored(callee_type.node));

            switch (callee_type.node->kind) {
                default: {
                    fprintf(stderr, "on node kind %s\n", laye_node_kind_to_cstring(callee_type.node->kind));
                    assert(false && "todo callee type");
                } break;

                case LAYE_NODE_TYPE_POISON: {
                    node->type = LTY(laye_context->laye_types.poison);
                } break;

                case LAYE_NODE_TYPE_FUNCTION: {
                    assert(callee_type.node->type_function.return_type.node != NULL);
                    node->type = callee_type.node->type_function.return_type;

                    int64_t param_count = lca_da_count(callee_type.node->type_function.parameter_types);

                    if (callee_type.node->type_function.varargs_style == LAYE_VARARGS_NONE) {
                        if (lca_da_count(node->call.arguments) != param_count) {
                            laye_sema_set_errored(node);
                            lyir_write_error(
                                lyir_context,
                                node->location,
                                "Expected %lld arguments to call, got %lld.",
                                param_count,
                                lca_da_count(node->call.arguments)
                            );
                            break;
                        }

                        for (int64_t i = 0, count = lca_da_count(node->call.arguments); i < count; i++) {
                            laye_sema_convert_or_error(sema, &node->call.arguments[i], callee_type.node->type_function.parameter_types[i]);
                        }
                    } else if (callee_type.node->type_function.varargs_style == LAYE_VARARGS_C) {
                        if (lca_da_count(node->call.arguments) < param_count) {
                            laye_sema_set_errored(node);
                            lyir_write_error(
                                lyir_context,
                                node->location,
                                "Expected at least %lld arguments to call, got %lld.",
                                param_count,
                                lca_da_count(node->call.arguments)
                            );
                            break;
                        }

                        for (int64_t i = 0, count = lca_da_count(node->call.arguments); i < count; i++) {
                            if (i < param_count) {
                                laye_sema_convert_or_error(sema, &node->call.arguments[i], callee_type.node->type_function.parameter_types[i]);
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
            laye_sema_analyse_node(sema, &node->index.value, NOTY);
            laye_sema_implicit_de_reference(sema, &node->index.value);

            // bool is_lvalue = laye_node_is_lvalue(node->index.value);
            laye_expr_set_lvalue(node, true);

            for (int64_t i = 0, count = lca_da_count(node->index.indices); i < count; i++) {
                laye_node** index_node_ref = &node->index.indices[i];
                assert(*index_node_ref != NULL);
                laye_sema_analyse_node(sema, index_node_ref, LTY(laye_context->laye_types._int));
                // laye_sema_convert_or_error(sema, index_node_ref, LTY( context->laye_types._int));

                if (laye_type_is_int((*index_node_ref)->type)) {
                    if (laye_type_is_signed_int((*index_node_ref)->type)) {
                        laye_sema_convert_or_error(sema, index_node_ref, LTY(laye_context->laye_types._int));
                        laye_sema_insert_implicit_cast(sema, index_node_ref, LTY(laye_context->laye_types._uint));
                    } else {
                        laye_sema_convert_or_error(sema, index_node_ref, LTY(laye_context->laye_types._uint));
                    }
                } else {
                    lyir_write_error(lyir_context, (*index_node_ref)->location, "Indices must be of integer type or convertible to an integer.");
                }
            }

            laye_type value_type = node->index.value->type;
            assert(value_type.node->sema_state == LYIR_SEMA_DONE || laye_sema_is_errored(value_type.node));

            switch (value_type.node->kind) {
                default: {
                    lca_string type_string = lca_string_create(laye_context->allocator);
                    laye_type_print_to_string(value_type, &type_string, laye_context->use_color);
                    lyir_write_error(lyir_context, node->index.value->location, "Cannot index type %.*s.", LCA_STR_EXPAND(type_string));
                    lca_string_destroy(&type_string);
                    node->type = LTY(laye_context->laye_types.poison);
                } break;

                case LAYE_NODE_TYPE_ARRAY: {
                    if (lca_da_count(node->index.indices) != lca_da_count(value_type.node->type_container.length_values)) {
                        lca_string type_string = lca_string_create(laye_context->allocator);
                        laye_type_print_to_string(value_type, &type_string, laye_context->use_color);
                        lyir_write_error(
                            lyir_context,
                            node->location,
                            "Expected %lld indices to type %.*s, got %lld.",
                            lca_da_count(value_type.node->type_container.length_values),
                            LCA_STR_EXPAND(type_string),
                            lca_da_count(node->index.indices)
                        );
                        lca_string_destroy(&type_string);
                    }

                    node->type = value_type.node->type_container.element_type;
                } break;

                    /*
                    case LAYE_NODE_TYPE_SLICE: {
                        if (lca_da_count(node->index.indices) != 1) {
                            layec_write_error(context, node->location, "Slice types require exactly one index.");
                        }

                        node->type = value_type.node->type_container.element_type;
                    } break;
                    */

                case LAYE_NODE_TYPE_BUFFER: {
                    laye_sema_lvalue_to_rvalue(sema, &node->index.value, true);

                    if (lca_da_count(node->index.indices) != 1) {
                        lyir_write_error(lyir_context, node->location, "Buffer types require exactly one index.");
                    }

                    node->type = value_type.node->type_container.element_type;
                } break;
            }

            assert(node->type.node != NULL);
            assert(node->type.node->kind != LAYE_NODE_INVALID);
        } break;

        case LAYE_NODE_MEMBER: {
            laye_sema_analyse_node(sema, &node->member.value, NOTY);
            assert(node->member.value != NULL);

            laye_type value_type = node->member.value->type;
            assert(value_type.node->sema_state == LYIR_SEMA_DONE || laye_sema_is_errored(value_type.node));

            laye_expr_set_lvalue(node, laye_expr_is_lvalue(node->member.value));
            if (!laye_expr_is_lvalue(node->member.value)) {
                laye_sema_set_errored(node);
                lyir_write_error(lyir_context, node->member.value->location, "Expression must be a modifiable lvalue.");
                break;
            }

            laye_sema_implicit_dereference(sema, &node->member.value);
            value_type = node->member.value->type;

            lyir_location member_location = node->member.field_name.location;
            lca_string_view member_name = node->member.field_name.string_value;

            switch (value_type.node->kind) {
                default: {
                    laye_sema_set_errored(node);
                    node->type = LTY(laye_context->laye_types.poison);
                    lca_string type_string = lca_string_create(laye_context->allocator);
                    laye_type_print_to_string(value_type, &type_string, laye_context->use_color);
                    lyir_write_error(
                        lyir_context,
                        node->location,
                        "Cannot index type %.*s.",
                        LCA_STR_EXPAND(type_string)
                    );
                    lca_string_destroy(&type_string);
                } break;

                case LAYE_NODE_TYPE_STRUCT: {
                    laye_node* struct_type_node = value_type.node;

                    int64_t member_index = laye_type_struct_field_index_by_name(value_type, member_name);
                    if (member_index < 0) {
                        laye_sema_set_errored(node);
                        node->type = LTY(laye_context->laye_types.poison);
                        lyir_write_error(lyir_context, node->location, "No such member '%.*s'.", LCA_STR_EXPAND(member_name));
                        break;
                    }

                    node->member.member_offset = laye_type_struct_field_offset_bytes(value_type, member_index);
                    node->type = value_type.node->type_struct.fields[member_index].type;
                } break;
            }
        } break;

        case LAYE_NODE_NAMEREF: {
            laye_node* referenced_decl_node = node->nameref.referenced_declaration;

            if (referenced_decl_node == NULL) {
                referenced_decl_node = laye_sema_lookup_value_declaration(node->module, node->nameref);
                if (referenced_decl_node == NULL) {
                    laye_sema_set_errored(node);
                    node->type = LTY(laye_context->laye_types.poison);
                    break;
                }
            }

            if (lca_da_count(node->nameref.template_arguments) != 0) {
                if (lca_da_count(referenced_decl_node->template_parameters) != lca_da_count(node->nameref.template_arguments)) {
                    laye_sema_set_errored(node);
                    lyir_write_error(
                        lyir_context,
                        node->location,
                        "Expected %ld template arguments, but got %ld.",
                        lca_da_count(referenced_decl_node->template_parameters),
                        lca_da_count(node->nameref.template_arguments)
                    );
                }

                referenced_decl_node = laye_node_instantiate_template(sema, node->location, referenced_decl_node, node->nameref.template_arguments);
            }

            assert(referenced_decl_node != NULL);
            assert(laye_node_is_decl(referenced_decl_node));
            node->nameref.referenced_declaration = referenced_decl_node;
            assert(referenced_decl_node->declared_type.node != NULL);
            node->type = referenced_decl_node->declared_type;

            switch (referenced_decl_node->kind) {
                default: {
                    fprintf(stderr, "on node kind %s\n", laye_node_kind_to_cstring(referenced_decl_node->kind));
                    assert(false && "todo nameref unknown declaration");
                } break;

                case LAYE_NODE_DECL_FUNCTION: {
                } break;

                case LAYE_NODE_DECL_FUNCTION_PARAMETER: {
                    laye_expr_set_lvalue(node, true);
                } break;

                case LAYE_NODE_DECL_BINDING: {
                    laye_expr_set_lvalue(node, true);
                } break;

                case LAYE_NODE_DECL_STRUCT: {
                    lyir_write_error(lyir_context, node->location, "Cannot use a struct as a value.");
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

            laye_type type_to = node->type;
            laye_type type_from = node->cast.operand->type;

            if (node->cast.kind == LAYE_CAST_HARD) {
                // hard casts between numbers are allowed
                if (laye_type_is_numeric(type_from) && laye_type_is_numeric(type_to)) {
                    break;
                }

                // hard casts from pointer to pointer are allowed
                if (laye_type_is_pointer(type_from) && laye_type_is_pointer(type_to)) {
                    break;
                }

                // hard casts from buffer to buffer are allowed
                if (laye_type_is_buffer(type_from) && laye_type_is_buffer(type_to)) {
                    break;
                }
            }

            lca_string from_type_string = lca_string_create(laye_context->allocator);
            laye_type_print_to_string(type_from, &from_type_string, laye_context->use_color);

            lca_string to_type_string = lca_string_create(laye_context->allocator);
            laye_type_print_to_string(type_to, &to_type_string, laye_context->use_color);

            laye_sema_set_errored(node);
            lyir_write_error(
                lyir_context,
                node->location,
                "Expression of type %.*s is not convertible to %.*s",
                LCA_STR_EXPAND(from_type_string),
                LCA_STR_EXPAND(to_type_string)
            );

            lca_string_destroy(&to_type_string);
            lca_string_destroy(&from_type_string);
        } break;

        case LAYE_NODE_CTOR: {
            bool has_usable_type = true;

            lyir_location type_location = node->type.node->location;
            if (node->type.node->kind == LAYE_NODE_TYPE_VAR) {
                if (expected_type.node != NULL) {
                    node->type = expected_type;
                } else {
                    has_usable_type = false;
                    laye_sema_set_errored(node);
                    node->type = LTY(laye_context->laye_types.poison);
                    lyir_write_error(
                        lyir_context,
                        node->location,
                        "Unable to infer the type of this constructor in this context."
                    );
                }
            } else if (!laye_sema_analyse_type(sema, &node->type)) {
                laye_sema_set_errored(node);
                node->type = LTY(laye_context->laye_types.poison);
                // continue to analyze the initializers, though
            }

            laye_type ctor_type = {0};
            // TODO(local): is this how we have to determine if something is a struct type????
            if (has_usable_type && node->type.node->kind == LAYE_NODE_TYPE_STRUCT) {
                ctor_type = node->type;
            } else {
                has_usable_type = false;
                lyir_write_error(
                    lyir_context,
                    type_location,
                    "Can only construct a struct or array type."
                );
            }

            bool is_struct_ctor = ctor_type.node->kind == LAYE_NODE_TYPE_STRUCT;
            if (has_usable_type) {
                assert(ctor_type.node != NULL);
                assert(ctor_type.node->kind == LAYE_NODE_TYPE_STRUCT || ctor_type.node->kind == LAYE_NODE_TYPE_ARRAY);

                if (ctor_type.node->kind == LAYE_NODE_TYPE_ARRAY) {
                    lyir_write_error(
                        lyir_context,
                        type_location,
                        "Sorry, can only construct a struct type for the time being."
                    );
                }
            } else {
                assert(ctor_type.node == NULL);
            }

            for (int64_t i = 0, init_index = 0, count = lca_da_count(node->ctor.initializers); i < count; i++) {
                laye_node* init = node->ctor.initializers[i];
                assert(init != NULL);

                int64_t calculated_offset = -1;

                if (init->member_initializer.kind == LAYE_MEMBER_INIT_NONE) {
                    if (!has_usable_type) {
                        continue;
                    }

                    if (is_struct_ctor) {
                        if (init_index >= lca_da_count(ctor_type.node->type_struct.fields)) {
                            laye_sema_set_errored(node);
                            lyir_write_error(
                                lyir_context,
                                init->location,
                                "Too many initializers."
                            );
                        } else {
                            laye_struct_type_field field = ctor_type.node->type_struct.fields[init_index];
                            assert(field.type.node != NULL);

                            laye_sema_analyse_node(sema, &init->member_initializer.value, field.type);
                            laye_sema_convert_or_error(sema, &init->member_initializer.value, field.type);

                            calculated_offset = laye_type_struct_field_offset_bytes(ctor_type, init_index);
                        }
                    } else {
                        fprintf(stderr, "for ctor type %s\n", laye_node_kind_to_cstring(ctor_type.node->kind));
                        assert(false && "unimplmented constructor type");
                    }

                    init_index++;
                } else {
                    laye_sema_analyse_node(sema, &init->member_initializer.value, NOTY);

                    lyir_location designator_location = {0};
                    if (init->member_initializer.kind == LAYE_MEMBER_INIT_INDEXED) {
                        designator_location = init->member_initializer.index->location;
                    } else {
                        designator_location = init->member_initializer.name.location;
                    }

                    laye_sema_set_errored(node);
                    lyir_write_error(
                        lyir_context,
                        designator_location,
                        "Currently, initializer designations are not supported."
                    );
                }

                lca_da_push(node->ctor.calculated_offsets, calculated_offset);
            }

            if (node->sema_state == LYIR_SEMA_DONE) {
                assert(lca_da_count(node->ctor.calculated_offsets) == lca_da_count(node->ctor.initializers) && "did not generate the correct number of pre-calculated initializer offsets for the number of ctor member initializers");
            }
        } break;

        case LAYE_NODE_UNARY: {
            if (!laye_sema_analyse_node(sema, &node->unary.operand, NOTY)) {
                laye_sema_set_errored(node);
                node->type = LTY(laye_context->laye_types.poison);
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
                        lyir_write_error(lyir_context, node->location, "Expression must have an arithmetic type.");
                        laye_sema_set_errored(node);
                        node->type = LTY(laye_context->laye_types.poison);
                        break;
                    }

                    node->type = node->unary.operand->type;
                } break;

                case '~': {
                    laye_sema_implicit_dereference(sema, &node->unary.operand);
                    laye_sema_lvalue_to_rvalue(sema, &node->unary.operand, true);

                    if (!laye_type_is_int(node->unary.operand->type)) {
                        lyir_write_error(lyir_context, node->location, "Expression must have an integer type.");
                        laye_sema_set_errored(node);
                        node->type = LTY(laye_context->laye_types.poison);
                        break;
                    }

                    node->type = node->unary.operand->type;
                } break;

                case '&': {
                    if (!laye_expr_is_lvalue(node->unary.operand)) {
                        lyir_write_error(lyir_context, node->location, "Cannot take the address of a non-lvalue expression.");
                        laye_sema_set_errored(node);
                        node->type = LTY(laye_context->laye_types.poison);
                        break;
                    }

                    assert(laye_type_is_pointer(node->type)); // from the parser, already constructed
                    node->type.node->type_container.element_type = node->unary.operand->type;
                } break;

                case '*': {
                    laye_sema_lvalue_to_rvalue(sema, &node->unary.operand, true);

                    laye_type value_type_noref = laye_type_strip_references(node->unary.operand->type);
                    assert(value_type_noref.node != NULL);
                    if (!laye_type_is_pointer(value_type_noref)) {
                        goto cannot_dereference_type;
                    }

                    if (!laye_sema_convert(sema, &node->unary.operand, value_type_noref)) {
                        laye_sema_set_errored(node);
                        node->type = LTY(laye_context->laye_types.poison);
                        break;
                    }

                    laye_type pointer_type = value_type_noref;
                    if (laye_type_is_void(pointer_type.node->type_container.element_type) || laye_type_is_noreturn(pointer_type.node->type_container.element_type)) {
                        goto cannot_dereference_type;
                    }

                    node->type = pointer_type.node->type_container.element_type;
                    laye_expr_set_lvalue(node, true);
                    break;

                cannot_dereference_type:;
                    lca_string type_string = lca_string_create(lca_default_allocator);
                    laye_type_print_to_string(node->unary.operand->type, &type_string, laye_context->use_color);
                    lyir_write_error(lyir_context, node->location, "Cannot dereference type %.*s.", LCA_STR_EXPAND(type_string));
                    lca_string_destroy(&type_string);
                    laye_sema_set_errored(node);
                    node->type = LTY(laye_context->laye_types.poison);
                } break;

                case LAYE_TOKEN_NOT: {
                    laye_sema_implicit_dereference(sema, &node->unary.operand);
                    laye_sema_lvalue_to_rvalue(sema, &node->unary.operand, true);

                    node->type = LTY(laye_context->laye_types._bool);
                    laye_sema_convert_or_error(sema, &node->unary.operand, node->type);
                } break;
            }
        } break;

        case LAYE_NODE_BINARY: {
            if (!laye_sema_analyse_node(sema, &node->binary.lhs, NOTY) || !laye_sema_analyse_node(sema, &node->binary.rhs, NOTY)) {
                laye_sema_set_errored(node);
                node->type = LTY(laye_context->laye_types.poison);
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
                    node->type = LTY(laye_context->laye_types._bool);

                    laye_sema_implicit_dereference(sema, &node->binary.lhs);
                    laye_sema_implicit_dereference(sema, &node->binary.rhs);

                    laye_sema_lvalue_to_rvalue(sema, &node->binary.lhs, true);
                    laye_sema_lvalue_to_rvalue(sema, &node->binary.rhs, true);

                    laye_sema_convert_or_error(sema, &node->binary.lhs, LTY(laye_context->laye_types._bool));
                    laye_sema_convert_or_error(sema, &node->binary.rhs, LTY(laye_context->laye_types._bool));
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
                    bool is_additive_operation = 
                        node->binary.operator.kind == LAYE_TOKEN_PLUS ||
                        node->binary.operator.kind == LAYE_TOKEN_MINUS;
                    // clang-format on

                    laye_sema_implicit_dereference(sema, &node->binary.lhs);
                    laye_sema_implicit_dereference(sema, &node->binary.rhs);

                    laye_sema_lvalue_to_rvalue(sema, &node->binary.lhs, true);
                    laye_sema_lvalue_to_rvalue(sema, &node->binary.rhs, true);

                    laye_type lhs_type = node->binary.lhs->type;
                    assert(lhs_type.node != NULL);
                    laye_type rhs_type = node->binary.rhs->type;
                    assert(rhs_type.node != NULL);

                    if (laye_type_is_numeric(lhs_type) && laye_type_is_numeric(rhs_type)) {
                        if (!laye_sema_convert_to_common_type(sema, &node->binary.lhs, &node->binary.rhs)) {
                            goto cannot_arith_types;
                        }

                        node->type = node->binary.lhs->type;
                    } else if (is_additive_operation && (laye_type_is_int(lhs_type) || laye_type_is_buffer(lhs_type)) && (laye_type_is_int(rhs_type) || laye_type_is_buffer(rhs_type))) {
                        if (laye_type_is_buffer(lhs_type) && laye_type_is_buffer(rhs_type)) {
                            goto cannot_arith_types;
                        }

                        node->type = laye_type_is_buffer(lhs_type) ? lhs_type : rhs_type;
                    } else {
                        // TODO(local): pointer arith
                        goto cannot_arith_types;
                    }

                    assert(node->type.node->kind != LAYE_NODE_TYPE_UNKNOWN);
                    break;

                cannot_arith_types:;
                    laye_sema_set_errored(node);
                    node->type = LTY(laye_context->laye_types.poison);

                    lca_string lhs_type_string = lca_string_create(lca_default_allocator);
                    lca_string rhs_type_string = lca_string_create(lca_default_allocator);

                    laye_type_print_to_string(lhs_type, &lhs_type_string, laye_context->use_color);
                    laye_type_print_to_string(rhs_type, &rhs_type_string, laye_context->use_color);

                    lyir_write_error(
                        lyir_context,
                        node->location,
                        "Cannot perform arithmetic on %.*s and %.*s.",
                        LCA_STR_EXPAND(lhs_type_string),
                        LCA_STR_EXPAND(rhs_type_string)
                    );

                    lca_string_destroy(&rhs_type_string);
                    lca_string_destroy(&lhs_type_string);
                } break;

                case LAYE_TOKEN_EQUALEQUAL:
                case LAYE_TOKEN_BANGEQUAL:
                case LAYE_TOKEN_LESS:
                case LAYE_TOKEN_LESSEQUAL:
                case LAYE_TOKEN_GREATER:
                case LAYE_TOKEN_GREATEREQUAL: {
                    bool is_equality_compare = node->binary.operator.kind == LAYE_TOKEN_EQUALEQUAL || node->binary.operator.kind == LAYE_TOKEN_BANGEQUAL;

                    node->type = LTY(laye_context->laye_types._bool);

                    laye_sema_implicit_dereference(sema, &node->binary.lhs);
                    laye_sema_implicit_dereference(sema, &node->binary.rhs);

                    laye_sema_lvalue_to_rvalue(sema, &node->binary.lhs, true);
                    laye_sema_lvalue_to_rvalue(sema, &node->binary.rhs, true);

                    laye_type lhs_type = node->binary.lhs->type;
                    assert(lhs_type.node != NULL);
                    laye_type rhs_type = node->binary.rhs->type;
                    assert(rhs_type.node != NULL);

                    if (laye_type_is_numeric(lhs_type) && laye_type_is_numeric(rhs_type)) {
                        if (!laye_sema_convert_to_common_type(sema, &node->binary.lhs, &node->binary.rhs)) {
                            goto cannot_compare_types;
                        }
                    } else if (laye_type_is_bool(lhs_type) && laye_type_is_bool(rhs_type)) {
                        // xyzzy;
                    } else if (laye_type_is_pointer(lhs_type) && laye_type_is_pointer(rhs_type)) {
                        if (!is_equality_compare || !laye_type_equals(lhs_type.node->type_container.element_type, rhs_type.node->type_container.element_type, LAYE_MUT_IGNORE)) {
                            goto cannot_compare_types;
                        }
                    } else if (laye_type_is_buffer(lhs_type) && laye_type_is_buffer(rhs_type)) {
                        if (!laye_type_equals(lhs_type.node->type_container.element_type, rhs_type.node->type_container.element_type, LAYE_MUT_IGNORE)) {
                            goto cannot_compare_types;
                        }
                    } else {
                        goto cannot_compare_types;
                    }

                    break;

                cannot_compare_types:;
                    laye_sema_set_errored(node);
                    node->type = LTY(laye_context->laye_types.poison);

                    lca_string lhs_type_string = lca_string_create(lca_default_allocator);
                    lca_string rhs_type_string = lca_string_create(lca_default_allocator);

                    laye_type_print_to_string(lhs_type, &lhs_type_string, laye_context->use_color);
                    laye_type_print_to_string(rhs_type, &rhs_type_string, laye_context->use_color);

                    lyir_write_error(
                        lyir_context,
                        node->location,
                        "Cannot compare %.*s and %.*s.",
                        LCA_STR_EXPAND(lhs_type_string),
                        LCA_STR_EXPAND(rhs_type_string)
                    );

                    lca_string_destroy(&rhs_type_string);
                    lca_string_destroy(&lhs_type_string);
                } break;
            }
        } break;

        case LAYE_NODE_LITBOOL: {
            // assert we populated this at parse time
            assert(node->type.node != NULL);
            if (expected_type.node != NULL) {
                laye_sema_convert_or_error(sema, node_ref, expected_type);
            }
            assert(laye_type_is_bool(node->type));
        } break;

        case LAYE_NODE_LITINT: {
            // assert we populated this at parse time
            assert(node->type.node != NULL);
            if (expected_type.node != NULL) {
                laye_sema_convert_or_error(sema, node_ref, expected_type);
            }
            assert(laye_type_is_int(node->type));
        } break;

        case LAYE_NODE_LITSTRING: {
            // assert we populated this at parse time
            assert(node->type.node != NULL);
            if (expected_type.node != NULL) {
                laye_sema_convert_or_error(sema, node_ref, expected_type);
            }
            assert(laye_type_is_buffer(node->type));
        } break;

        case LAYE_NODE_TYPE_VAR: {
            laye_sema_set_errored(node);
            node->type = LTY(laye_context->laye_types.poison);
            lyir_write_error(lyir_context, node->location, "Cannot infer a type here.");
        } break;

        case LAYE_NODE_TYPE_NAMEREF: {
            laye_node* referenced_decl_node = node->nameref.referenced_declaration;

            if (referenced_decl_node == NULL) {
                referenced_decl_node = laye_sema_lookup_type_declaration(node->module, node->nameref);
                if (referenced_decl_node == NULL) {
                    laye_sema_set_errored(node);
                    node->type = LTY(laye_context->laye_types.poison);
                    break;
                }
            }

            assert(referenced_decl_node != NULL);
            assert(referenced_decl_node->sema_state == LYIR_SEMA_DONE || laye_sema_is_errored(referenced_decl_node));
            assert(laye_node_is_decl(referenced_decl_node));
            node->nameref.referenced_declaration = referenced_decl_node;
            assert(referenced_decl_node->declared_type.node != NULL);
            node->nameref.referenced_type = referenced_decl_node->declared_type.node;

            switch (referenced_decl_node->kind) {
                default: {
                    fprintf(stderr, "on node kind %s\n", laye_node_kind_to_cstring(referenced_decl_node->kind));
                    assert(false && "todo nameref unknown declaration");
                } break;

                case LAYE_NODE_DECL_FUNCTION: {
                    lyir_write_error(lyir_context, node->location, "Cannot use a function as a type.");
                } break;

                case LAYE_NODE_DECL_BINDING: {
                    lyir_write_error(lyir_context, node->location, "Cannot use a variable as a type.");
                } break;

                case LAYE_NODE_DECL_STRUCT: {
                } break;

                case LAYE_NODE_DECL_ENUM: {
                } break;

                case LAYE_NODE_DECL_ALIAS: {
                } break;
            }
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
            if (!laye_sema_analyse_type(sema, &node->type_container.element_type)) {
                laye_sema_set_errored(node);
            }
        } break;

        case LAYE_NODE_TYPE_FUNCTION: {
            if (node->type_function.calling_convention == LYIR_DEFAULTCC) {
                node->type_function.calling_convention = LYIR_LAYECC;
            }

            assert(node->type_function.return_type.node != NULL);
            if (!laye_sema_analyse_type(sema, &node->type_function.return_type)) {
                laye_sema_set_errored(node);
                node->type = LTY(laye_context->laye_types.poison);
            }

            for (int64_t i = 0, count = lca_da_count(node->type_function.parameter_types); i < count; i++) {
                if (!laye_sema_analyse_type(sema, &node->type_function.parameter_types[i])) {
                    laye_sema_set_errored(node);
                    node->type = LTY(laye_context->laye_types.poison);
                }
            }
        } break;

        case LAYE_NODE_TYPE_ARRAY: {
            if (!laye_sema_analyse_type(sema, &node->type_container.element_type)) {
                laye_sema_set_errored(node);
            }

            for (int64_t i = 0, count = lca_da_count(node->type_container.length_values); i < count; i++) {
                if (!laye_sema_analyse_node(sema, &node->type_container.length_values[i], NOTY)) {
                    laye_sema_set_errored(node);
                    continue;
                }

                lyir_evaluated_constant constant_value = {0};
                if (!laye_expr_evaluate(node->type_container.length_values[i], &constant_value, true)) {
                    lyir_write_error(lyir_context, node->type_container.length_values[i]->location, "Array length value must be a compile-time known integer value. This expression was unable to be evaluated at compile time.");
                    laye_sema_set_errored(node);
                    continue;
                }

                if (constant_value.kind != LYIR_EVAL_INT) {
                    lyir_write_error(lyir_context, node->type_container.length_values[i]->location, "Array length value must be a compile-time known integer value. This expression did not evaluate to an integer.");
                    laye_sema_set_errored(node);
                    continue;
                }

                laye_node* evaluated_constant = laye_node_create(node->module, LAYE_NODE_EVALUATED_CONSTANT, node->type_container.length_values[i]->location, LTY(laye_context->laye_types._int));
                assert(evaluated_constant != NULL);
                evaluated_constant->evaluated_constant.expr = node->type_container.length_values[i];
                evaluated_constant->evaluated_constant.result = constant_value;

                node->type_container.length_values[i] = evaluated_constant;
            }
        } break;

        case LAYE_NODE_TYPE_STRUCT: {
            // TODO(local): This needs to handle padding and shit
            assert(node->type.node->kind == LAYE_NODE_TYPE_TYPE);

            int current_size = 0;
            int current_align = 1;

            int padding_bytes = 0;

            for (int64_t i = 0; i < lca_da_count(node->type_struct.fields); i++) {
                laye_sema_analyse_type(sema, &node->type_struct.fields[i].type);
                assert(node->type_struct.fields[i].type.node != NULL);

                int f_size = laye_type_size_in_bytes(node->type_struct.fields[i].type);
                assert(f_size > 0);
                int f_align = laye_type_align_in_bytes(node->type_struct.fields[i].type);
                assert(f_align > 0);

                if (f_align > current_align) {
                    current_align = f_align;
                }

                padding_bytes = (current_align - (current_size % current_align)) % current_align;
                if (padding_bytes > 0) {
                    laye_struct_type_field padding_field = laye_sema_create_padding_field(sema, node->module, node->location, padding_bytes);
                    lca_da_insert(node->type_struct.fields, i, padding_field);
                    i++;
                }

                current_size += padding_bytes;
                current_size += f_size;
            }

            padding_bytes = (current_align - (current_size % current_align)) % current_align;
            if (padding_bytes > 0) {
                laye_struct_type_field padding_field = laye_sema_create_padding_field(sema, node->module, node->location, padding_bytes);
                lca_da_push(node->type_struct.fields, padding_field);
            }

            current_size += padding_bytes;

            node->type_struct.cached_size = current_size;
            node->type_struct.cached_align = current_align;
        } break;
    }

    assert(node != NULL);
    if (node->sema_state == LYIR_SEMA_IN_PROGRESS) {
        node->sema_state = LYIR_SEMA_DONE;
    }

#if false
    if (expected_type != NULL && node->kind != LAYE_NODE_YIELD && node->sema_state == LAYEC_SEMA_OK) {
        assert(laye_node_is_type(expected_type));
        laye_sema_convert_or_error(sema, &node, expected_type);
    }
#endif

    assert(node->sema_state == LYIR_SEMA_DONE || laye_sema_is_errored(node));
    assert(node->type.node != NULL);
    assert(node->type.node->kind != LAYE_NODE_INVALID);
    assert(node->type.node->kind != LAYE_NODE_TYPE_UNKNOWN);

    *node_ref = node;
    return node->sema_state == LYIR_SEMA_DONE;
}

static laye_struct_type_field laye_sema_create_padding_field(laye_sema* sema, laye_module* module, lyir_location location, int padding_bytes) {
    laye_context* laye_context = module->context;
    assert(laye_context != NULL);

    laye_type padding_type = LTY(laye_node_create(module, LAYE_NODE_TYPE_ARRAY, location, LTY(laye_context->laye_types.type)));
    assert(padding_type.node != NULL);
    padding_type.node->type_container.element_type = LTY(laye_context->laye_types.i8);

    laye_node* constant_value = laye_node_create(module, LAYE_NODE_LITINT, location, LTY(laye_context->laye_types._int));
    assert(constant_value != NULL);
    constant_value->litint.value = padding_bytes;

    laye_sema_analyse_node(sema, &constant_value, constant_value->type);

    lyir_evaluated_constant eval_result = {0};
    laye_expr_evaluate(constant_value, &eval_result, true);
    constant_value = laye_create_constant_node(sema, constant_value, eval_result);
    assert(constant_value->kind == LAYE_NODE_EVALUATED_CONSTANT);

    lca_da_push(padding_type.node->type_container.length_values, constant_value);

    laye_struct_type_field padding_field = {
        .type = padding_type,
    };

    return padding_field;
}

static bool laye_sema_analyse_node_and_discard(laye_sema* sema, laye_node** node) {
    assert(sema != NULL);
    assert(node != NULL);
    assert(*node != NULL);
    if (!laye_sema_analyse_node(sema, node, NOTY)) return false;
    laye_sema_discard(sema, node);
    return true;
}

static void laye_sema_discard(laye_sema* sema, laye_node** node_ref) {
    assert(sema != NULL);
    assert(node_ref != NULL);

    laye_context* laye_context = sema->context;
    assert(laye_context != NULL);

    laye_node* node = *node_ref;
    assert(node != NULL);
    assert(node->type.node != NULL);

    if (node->kind == LAYE_NODE_CALL) {
        // TODO(local): check discardable nature of the callee
    }

    if (laye_type_is_void(node->type) || laye_type_is_noreturn(node->type)) {
        return;
    }

    laye_sema_insert_implicit_cast(sema, node_ref, LTY(laye_context->laye_types._void));
}

static bool laye_sema_has_side_effects(laye_sema* sema, laye_node* node) {
    assert(sema != NULL);
    assert(node != NULL);

    laye_context* laye_context = sema->context;
    assert(laye_context != NULL);

    // TODO(local): calculate if something is pure or not
    return true;
}

enum {
    LAYE_CONVERT_CONTAINS_ERRORS = -2,
    LAYE_CONVERT_IMPOSSIBLE = -1,
    LAYE_CONVERT_NOOP = 0,
};

static laye_node* laye_create_constant_node(laye_sema* sema, laye_node* node, lyir_evaluated_constant eval_result) {
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

static int laye_sema_convert_impl(laye_sema* sema, laye_node** node_ref, laye_type to, bool perform_conversion) {
    assert(sema != NULL);
    assert(node_ref != NULL);
    assert(to.node != NULL);
    assert(laye_node_is_type(to.node));

    laye_context* laye_context = sema->context;
    assert(laye_context != NULL);

    lyir_context* lyir_context = laye_context->lyir_context;
    assert(lyir_context != NULL);

    laye_node* node = *node_ref;
    assert(node != NULL);

    laye_type from = (*node_ref)->type;
    assert(from.node != NULL);
    assert(laye_node_is_type(from.node));

    // these are copies, so effectively ignore the outermost mutability
    from.is_modifiable = false;
    to.is_modifiable = false;

    if (laye_type_is_poison(from) || laye_type_is_poison(to)) {
        return LAYE_CONVERT_NOOP;
    }

    if (laye_sema_is_errored(from.node) || laye_sema_is_errored(to.node)) {
        return LAYE_CONVERT_CONTAINS_ERRORS;
    }

    assert(from.node->sema_state == LYIR_SEMA_DONE);
    assert(to.node->sema_state == LYIR_SEMA_DONE);

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

        if (laye_type_equals(from.node->type_container.element_type, to.node->type_container.element_type, LAYE_MUT_CONVERTIBLE)) {
            if (from.is_modifiable == to.is_modifiable || !to.is_modifiable)
                return LAYE_CONVERT_NOOP;
        }

        // TODO(local): struct variants, arrays->element

        return LAYE_CONVERT_IMPOSSIBLE;
    }

    if (perform_conversion) {
        laye_sema_lvalue_to_rvalue(sema, node_ref, true);
        node = *node_ref;
        from = node->type;
    } else {
        from = laye_type_strip_references(node->type);
    }

    // these are copies, so effectively ignore the outermost mutability
    from.is_modifiable = false;
    to.is_modifiable = false;

    if (laye_type_equals(from, to, LAYE_MUT_CONVERTIBLE)) {
        return LAYE_CONVERT_NOOP;
    }

    if (laye_type_is_pointer(from) && laye_type_is_reference(to)) {
        if (laye_type_equals(from.node->type_container.element_type, to.node->type_container.element_type, LAYE_MUT_CONVERTIBLE)) {
            if (from.is_modifiable == to.is_modifiable || !to.is_modifiable)
                return LAYE_CONVERT_NOOP;
        }

        return LAYE_CONVERT_IMPOSSIBLE;
    }

    if (laye_type_is_pointer(from) && laye_type_is_pointer(to)) {
        if (laye_type_equals(from.node->type_container.element_type, to.node->type_container.element_type, LAYE_MUT_CONVERTIBLE)) {
            if (from.is_modifiable == to.is_modifiable || !to.is_modifiable)
                return LAYE_CONVERT_NOOP;
        }

        return LAYE_CONVERT_IMPOSSIBLE;
    }

    int to_size = laye_type_size_in_bits(to);

    lyir_evaluated_constant eval_result = {0};
    if (laye_expr_evaluate(node, &eval_result, false)) {
        if (eval_result.kind == LYIR_EVAL_INT && laye_type_is_numeric(to)) {
            if (laye_type_is_float(to)) {
                eval_result.float_value = eval_result.int_value;
                eval_result.kind = LYIR_EVAL_FLOAT;
                goto eval_float;
            }

            int sig_bits = lyir_get_significant_bits(eval_result.int_value);
            if (sig_bits <= to_size) {
                if (perform_conversion) {
                    laye_sema_insert_implicit_cast(sema, node_ref, to);
                    *node_ref = laye_create_constant_node(sema, *node_ref, eval_result);
                }

                return score;
            }
        } else if (eval_result.kind == LYIR_EVAL_FLOAT && laye_type_is_float(to)) {
        eval_float:;
            assert(laye_type_is_float(to)); // because of the goto
            if (perform_conversion) {
                laye_sema_insert_implicit_cast(sema, node_ref, to);
                *node_ref = laye_create_constant_node(sema, *node_ref, eval_result);
            }
            return score;
        } else if (eval_result.kind == LYIR_EVAL_STRING) {
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

    if (laye_type_is_int(from) && laye_type_is_float(to)) {
        if (perform_conversion) {
            laye_sema_insert_implicit_cast(sema, node_ref, to);
        }

        return 1 + score;
    }

    if (laye_type_is_float(from) && laye_type_is_float(to)) {
        int from_size = laye_type_size_in_bits(from);

        if (from_size <= to_size) {
            if (perform_conversion) {
                laye_sema_insert_implicit_cast(sema, node_ref, to);
            }

            return 1 + score;
        }

        return LAYE_CONVERT_IMPOSSIBLE;
    }

    /*
    if (laye_type_is_int(from) && laye_type_is_float(to)) {
        int from_size = laye_type_size_in_bits(from);

        if (from_size <= to_size) {
            if (perform_conversion) {
                laye_sema_insert_implicit_cast(sema, node_ref, to);
            }

            return 1 + score;
        }

        return LAYE_CONVERT_IMPOSSIBLE;
    }
    */

    return LAYE_CONVERT_IMPOSSIBLE;
}

static bool laye_sema_convert(laye_sema* sema, laye_node** node, laye_type to) {
    assert(node != NULL);
    assert(*node != NULL);

    if (laye_sema_is_errored(*node)) {
        return true;
    }

    return laye_sema_convert_impl(sema, node, to, true) >= 0;
}

static void laye_sema_convert_or_error(laye_sema* sema, laye_node** node, laye_type to) {
    laye_context* laye_context = sema->context;
    assert(laye_context != NULL);

    lyir_context* lyir_context = laye_context->lyir_context;
    assert(lyir_context != NULL);

    if (!laye_sema_convert(sema, node, to)) {
        lca_string from_type_string = lca_string_create(laye_context->allocator);
        laye_type_print_to_string((*node)->type, &from_type_string, laye_context->use_color);

        lca_string to_type_string = lca_string_create(laye_context->allocator);
        laye_type_print_to_string(to, &to_type_string, laye_context->use_color);

        laye_sema_set_errored(*node);
        lyir_write_error(
            lyir_context,
            (*node)->location,
            "Expression of type %.*s is not convertible to %.*s",
            LCA_STR_EXPAND(from_type_string),
            LCA_STR_EXPAND(to_type_string)
        );

        lca_string_destroy(&to_type_string);
        lca_string_destroy(&from_type_string);
    }
}

static void laye_sema_convert_to_c_varargs_or_error(laye_sema* sema, laye_node** node) {
    assert(sema != NULL);

    laye_context* laye_context = sema->context;
    assert(laye_context != NULL);

    lyir_context* lyir_context = laye_context->lyir_context;
    assert(lyir_context != NULL);

    laye_node* varargs_type = NULL;
    laye_sema_lvalue_to_rvalue(sema, node, true);

    int type_size = laye_type_size_in_bits((*node)->type);

    if (laye_type_is_int((*node)->type)) {
        if (type_size < lyir_context->target->ffi.size_of_int) {
            laye_type ffi_int_type = LTY(laye_node_create((*node)->module, LAYE_NODE_TYPE_INT, (*node)->location, LTY(laye_context->laye_types.type)));
            assert(ffi_int_type.node != NULL);
            ffi_int_type.node->type_primitive.is_signed = laye_type_is_signed_int((*node)->type);
            ffi_int_type.node->type_primitive.bit_width = lyir_context->target->ffi.size_of_int;
            laye_sema_analyse_type(sema, &ffi_int_type);
            laye_sema_insert_implicit_cast(sema, node, ffi_int_type);
            laye_sema_analyse_node(sema, node, NOTY);
            return;
        }
    }

    if (laye_type_is_float((*node)->type)) {
        if (type_size < lyir_context->target->ffi.size_of_double) {
            laye_type ffi_double_type = LTY(laye_node_create((*node)->module, LAYE_NODE_TYPE_FLOAT, (*node)->location, LTY(laye_context->laye_types.type)));
            assert(ffi_double_type.node != NULL);
            ffi_double_type.node->type_primitive.bit_width = lyir_context->target->ffi.size_of_double;
            laye_sema_analyse_type(sema, &ffi_double_type);
            laye_sema_insert_implicit_cast(sema, node, ffi_double_type);
            laye_sema_analyse_node(sema, node, NOTY);
            return;
        }
    }

    if (type_size <= lyir_context->target->size_of_pointer) {
        return; // fine
    }

    lca_string type_string = lca_string_create(lca_default_allocator);
    laye_type_print_to_string((*node)->type, &type_string, laye_context->use_color);
    lyir_write_error(lyir_context, (*node)->location, "Cannot convert type %.*s to a type correct for C varargs.", LCA_STR_EXPAND(type_string));
    lca_string_destroy(&type_string);

    laye_sema_set_errored(*node);
    (*node)->type = LTY(laye_context->laye_types.poison);
}

static bool laye_sema_convert_to_common_type(laye_sema* sema, laye_node** a, laye_node** b) {
    assert(a != NULL);
    assert(*a != NULL);
    assert(b != NULL);
    assert(*b != NULL);

    int a2b_score = laye_sema_try_convert(sema, a, (*b)->type);
    int b2a_score = laye_sema_try_convert(sema, b, (*a)->type);

    if (a2b_score >= 0 && (a2b_score <= b2a_score || b2a_score < 0)) {
        return laye_sema_convert(sema, a, (*b)->type);
    }

    return laye_sema_convert(sema, b, (*a)->type);
}

static int laye_sema_try_convert(laye_sema* sema, laye_node** node, laye_type to) {
    return laye_sema_convert_impl(sema, node, to, false);
}

static void laye_sema_wrap_with_cast(laye_sema* sema, laye_node** node, laye_type type, laye_cast_kind cast_kind) {
    assert(sema != NULL);
    assert(node != NULL);
    assert(*node != NULL);
    assert((*node)->module != NULL);
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));

    laye_context* laye_context = sema->context;
    assert(laye_context != NULL);

    lyir_context* lyir_context = laye_context->lyir_context;
    assert(lyir_context != NULL);

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
    assert(node != NULL);
    assert(*node != NULL);

    laye_context* laye_context = sema->context;
    assert(laye_context != NULL);

    lyir_context* lyir_context = laye_context->lyir_context;
    assert(lyir_context != NULL);

    if (laye_type_is_pointer((*node)->type) || laye_type_is_buffer((*node)->type)) {
        laye_sema_wrap_with_cast(sema, node, LTY(laye_context->laye_types._int), LAYE_CAST_IMPLICIT);
    }
}

static void laye_sema_insert_implicit_cast(laye_sema* sema, laye_node** node, laye_type to) {
    laye_sema_wrap_with_cast(sema, node, to, LAYE_CAST_IMPLICIT);
}

static void laye_sema_lvalue_to_rvalue(laye_sema* sema, laye_node** node, bool strip_ref) {
    assert(sema != NULL);
    assert(node != NULL);
    assert(*node != NULL);
    assert((*node)->module != NULL);

    if (laye_sema_is_errored(*node)) return;

    laye_context* laye_context = sema->context;
    assert(laye_context != NULL);

    lyir_context* lyir_context = laye_context->lyir_context;
    assert(lyir_context != NULL);

    if (laye_node_is_lvalue(*node)) {
        laye_sema_wrap_with_cast(sema, node, (*node)->type, LAYE_CAST_LVALUE_TO_RVALUE);
    }

    if (strip_ref && laye_type_is_reference((*node)->type)) {
        laye_sema_wrap_with_cast(sema, node, (*node)->type.node->type_container.element_type, LAYE_CAST_REFERENCE_TO_LVALUE);
        laye_sema_lvalue_to_rvalue(sema, node, false);
    }
}

static bool laye_sema_implicit_dereference(laye_sema* sema, laye_node** node) {
    assert(sema != NULL);
    assert(node != NULL);
    assert(*node != NULL);
    assert((*node)->module != NULL);
    assert((*node)->type.node != NULL);

    laye_context* laye_context = sema->context;
    assert(laye_context != NULL);

    lyir_context* lyir_context = laye_context->lyir_context;
    assert(lyir_context != NULL);

    if (laye_type_is_reference((*node)->type)) {
        laye_sema_lvalue_to_rvalue(sema, node, false);
        laye_sema_wrap_with_cast(sema, node, (*node)->type.node->type_container.element_type, LAYE_CAST_REFERENCE_TO_LVALUE);
    }

    assert(node != NULL);
    assert(*node != NULL);
    assert((*node)->type.node != NULL);

    while (laye_type_is_pointer((*node)->type)) {
        assert((*node)->module != NULL);
        laye_node* deref_node = laye_node_create((*node)->module, LAYE_NODE_UNARY, (*node)->location, (*node)->type.node->type_container.element_type);
        assert(deref_node != NULL);
        deref_node->compiler_generated = true;
        deref_node->unary.operand = *node;
        deref_node->unary.operator=(laye_token){
            .kind = '*',
            .location = (*node)->location,
        };
        deref_node->unary.operator.location.length = 0;

        bool deref_analyse_result = laye_sema_analyse_node(sema, &deref_node, NOTY);
        assert(deref_analyse_result);

        *node = deref_node;

        assert(node != NULL);
        assert(*node != NULL);
        assert((*node)->type.node != NULL);
    }

    return laye_expr_is_lvalue(*node);
}

static bool laye_sema_implicit_de_reference(laye_sema* sema, laye_node** node) {
    assert(sema != NULL);
    assert(node != NULL);
    assert(*node != NULL);
    assert((*node)->module != NULL);
    assert((*node)->type.node != NULL);

    laye_context* laye_context = sema->context;
    assert(laye_context != NULL);

    lyir_context* lyir_context = laye_context->lyir_context;
    assert(lyir_context != NULL);

    if (laye_type_is_reference((*node)->type)) {
        laye_sema_lvalue_to_rvalue(sema, node, false);
        laye_sema_wrap_with_cast(sema, node, (*node)->type.node->type_container.element_type, LAYE_CAST_REFERENCE_TO_LVALUE);
        // laye_expr_set_lvalue(*node, true);
    }

    return laye_expr_is_lvalue(*node);
}

static laye_type laye_sema_get_pointer_to_type(laye_sema* sema, laye_type element_type, bool is_modifiable) {
    assert(sema != NULL);
    assert(element_type.node != NULL);
    assert(element_type.node->module != NULL);
    assert(laye_node_is_type(element_type.node));

    laye_context* laye_context = sema->context;
    assert(laye_context != NULL);

    lyir_context* lyir_context = laye_context->lyir_context;
    assert(lyir_context != NULL);

    laye_node* type = laye_node_create(element_type.node->module, LAYE_NODE_TYPE_POINTER, element_type.node->location, LTY(laye_context->laye_types.type));
    assert(type != NULL);

    type->type_container.element_type = element_type;
    return LTY(type);
}

static laye_type laye_sema_get_buffer_of_type(laye_sema* sema, laye_type element_type, bool is_modifiable) {
    assert(sema != NULL);
    assert(element_type.node != NULL);
    assert(element_type.node->module != NULL);
    assert(laye_node_is_type(element_type.node));

    laye_context* laye_context = sema->context;
    assert(laye_context != NULL);

    lyir_context* lyir_context = laye_context->lyir_context;
    assert(lyir_context != NULL);

    laye_node* type = laye_node_create(element_type.node->module, LAYE_NODE_TYPE_BUFFER, element_type.node->location, LTY(laye_context->laye_types.type));
    assert(type != NULL);

    type->type_container.element_type = element_type;
    return LTY(type);
}

static laye_type laye_sema_get_reference_to_type(laye_sema* sema, laye_type element_type, bool is_modifiable) {
    assert(sema != NULL);
    assert(element_type.node != NULL);
    assert(element_type.node->module != NULL);
    assert(laye_node_is_type(element_type.node));

    laye_context* laye_context = sema->context;
    assert(laye_context != NULL);

    lyir_context* lyir_context = laye_context->lyir_context;
    assert(lyir_context != NULL);

    laye_node* type = laye_node_create(element_type.node->module, LAYE_NODE_TYPE_REFERENCE, element_type.node->location, LTY(laye_context->laye_types.type));
    assert(type != NULL);

    type->type_container.element_type = element_type;
    return LTY(type);
}
