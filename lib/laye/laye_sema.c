#include "laye.h"
#include "layec.h"

#include <assert.h>

#define NOTY ((laye_type){0})

typedef struct laye_sema {
    layec_context* context;
    layec_dependency_graph* dependencies;

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

static laye_node* laye_create_constant_node(laye_sema* sema, laye_node* node, layec_evaluated_constant eval_result);

// TODO(local): redeclaration of a name as an import namespace should be a semantic error. They can't participate in overload resolution,
// so should just be disallowed for simplicity.

static laye_node* laye_sema_lookup_entity(laye_sema* sema, laye_module* from_module, laye_nameref nameref, bool is_type_entity) {
    assert(sema != NULL);
    assert(from_module != NULL);
    assert(from_module->context != NULL);

    laye_scope* search_scope = nameref.scope;
    assert(search_scope != NULL);

    assert(arr_count(nameref.pieces) >= 1);
    string_view first_name = nameref.pieces[0].string_value;

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
        laye_module* search_module = from_module;

        for (int64_t name_index = 0, name_count = arr_count(nameref.pieces); name_index < name_count; name_index++) {
            //bool is_last_name = name_index == name_count - 1;


        }
    }

    return NULL;
}

// TODO(local): combine lookup_value and lookup_type, mostly to help with import query resolution
static laye_node* laye_sema_lookup_value_declaration(laye_sema* sema, laye_module* from_module, laye_nameref nameref) {
    assert(sema != NULL);
    assert(from_module != NULL);
    assert(from_module->context != NULL);

    laye_scope* search_scope = nameref.scope;
    assert(search_scope != NULL);

    assert(arr_count(nameref.pieces) >= 1);
    string_view first_name = nameref.pieces[0].string_value;

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
        assert(false && "todo laye_sema_lookup_value_declaration search through imports");
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

static laye_node* laye_sema_lookup_type_declaration(laye_sema* sema, laye_module* from_module, laye_nameref nameref) {
    assert(sema != NULL);
    assert(from_module != NULL);
    assert(from_module->context != NULL);

    laye_scope* search_scope = nameref.scope;
    assert(search_scope != NULL);

    assert(arr_count(nameref.pieces) >= 1);
    string_view first_name = nameref.pieces[0].string_value;

    laye_node* found_declaration = NULL;

    while (search_scope != NULL) {
        laye_node* lookup = laye_scope_lookup_type(search_scope, first_name);
        if (lookup != NULL) {
            found_declaration = lookup;
            break;
        }

        search_scope = search_scope->parent;
    }

    laye_module* search_module = from_module;
    int64_t name_index = found_declaration == NULL ? 0 : 1;

    while (found_declaration == NULL && name_index < arr_count(nameref.pieces)) {
        assert(false && "todo laye_sema_lookup_type_declaration search through imports");
    }

    if (found_declaration == NULL) {
        assert(name_index >= arr_count(nameref.pieces));
        layec_write_error(
            from_module->context,
            arr_back(nameref.pieces)->location,
            "'%.*s' is not a type name in this context.",
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

            case LAYE_NODE_DECL_IMPORT: {
            } break;

            case LAYE_NODE_DECL_FUNCTION: {
                // TODO(local): generate dependencies
                layec_depgraph_ensure_tracked(graph, top_level_node);
            } break;

            case LAYE_NODE_DECL_STRUCT: {
                // TODO(local): generate dependencies
                layec_depgraph_ensure_tracked(graph, top_level_node);
            } break;
        }
    }

    module->dependencies_generated = true;
}

static void laye_sema_resolve_top_level_types(laye_sema* sema, laye_node** node);

static int64_t maxi(int64_t a, int64_t b) {
    return a > b ? a : b;
}

static void laye_handle_module_imports(layec_context* context, laye_module* module) {
    assert(context != NULL);
    assert(module != NULL);

    if (module->has_handled_imports) {
        return;
    }

    module->has_handled_imports = true;

    for (int64_t i = 0, count = arr_count(module->top_level_nodes); i < count; i++) {
        laye_node* top_level_node = module->top_level_nodes[i];
        if (top_level_node->kind == LAYE_NODE_DECL_IMPORT) {
            laye_token module_name_token = top_level_node->decl_import.module_name;
            if (module_name_token.kind == LAYE_TOKEN_IDENT) {
                layec_write_error(context, module_name_token.location, "Currently, module names cannot be identifiers; this syntax is reserved for future features that are not implemented yet.");
            }

            string_view module_name = module_name_token.string_value;

            layec_source source = layec_context_get_source(context, module->sourceid);

            string_view this_file_dir = string_as_view(source.name);
            int64_t last_slash_index = maxi(string_view_last_index_of(this_file_dir, '/'), string_view_last_index_of(this_file_dir, '\\'));

            string lookup_path = string_create(default_allocator);
            if (last_slash_index < 0) {
                lca_string_append_format(&lookup_path, "./");
            } else {
                this_file_dir.count = last_slash_index;
                string_append_format(&lookup_path, "%.*s", STR_EXPAND(this_file_dir));
            }

            string_path_append_view(&lookup_path, module_name);
            //fprintf(stderr, "%.*s\n", STR_EXPAND(lookup_path));
            if (!lca_plat_file_exists(string_as_cstring(lookup_path))) {
                layec_write_error(context, module_name_token.location, "Cannot find module file to import: '%.*s'", STR_EXPAND(module_name));
                continue;
            }

            layec_sourceid sourceid = layec_context_get_or_add_source_from_file(context, string_as_view(lookup_path));
            string_destroy(&lookup_path);

            laye_module* found = NULL;
            for (int64_t i = 0, count = arr_count(context->laye_modules); i < count && found == NULL; i++) {
                laye_module* module = context->laye_modules[i];
                if (module->sourceid == sourceid) {
                    found = module;
                }
            }

            if (found != NULL) {
                top_level_node->decl_import.referenced_module = found;
                // already loaded, should be doing things
                continue;
            }

            found = laye_parse(context, sourceid);
            assert(found != NULL);

            top_level_node->decl_import.referenced_module = found;
            laye_handle_module_imports(context, found);
        }
    }
}

typedef struct laye_sema_aliased_node {
    string_view alias;
    laye_node* node;
} laye_sema_aliased_node;

static void laye_sema_resolve_import_query(layec_context* context, laye_module* module, laye_node* query) {
    assert(context != NULL);
    assert(module != NULL);
    // `module` is not the current module, but the module we are querying into.

    /*
    string_view alias = entities[i].alias;
    if (alias.count > 0) {
        laye_node* proxy = laye_node_create(module, LAYE_NODE_DECL_PROXY, entity->location, entity->type);
        assert(proxy != NULL);
        proxy->proxl_declaration = entity;
        proxy->declared_name = alias;
        proxy->declared_type = entity->declared_type;
        entity = proxy;
    }
    */
    
    //assert(false && "unimplemented");
}

static bool is_identifier_char(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c >= 256;
}

static void laye_populate_module_import_scopes(layec_context* context, laye_module* module) {
    assert(context != NULL);
    assert(module != NULL);

    for (int64_t i = 0, count = arr_count(module->top_level_nodes); i < count; i++) {
        laye_node* top_level_node = module->top_level_nodes[i];
        if (top_level_node->kind == LAYE_NODE_DECL_IMPORT) {
            assert(top_level_node->decl_import.referenced_module != NULL);

            if (arr_count(top_level_node->decl_import.import_queries) == 0) {
                string_view module_name = top_level_node->decl_import.import_alias.string_value;
                if (module_name.count == 0) {
                    module_name = string_as_view(layec_context_get_source(context, top_level_node->decl_import.referenced_module->sourceid).name);

                    int64_t last_slash_index = maxi(string_view_last_index_of(module_name, '/'), string_view_last_index_of(module_name, '\\'));
                    if (last_slash_index >= 0) {
                        module_name.data += (last_slash_index + 1);
                        module_name.count -= (last_slash_index + 1);
                    }

                    int64_t first_dot_index = string_view_index_of(module_name, '.');
                    if (first_dot_index >= 0) {
                        module_name.count = first_dot_index;
                    }

                    if (module_name.count == 0) {
                        layec_write_error(context, top_level_node->decl_import.module_name.location, "Could not implicitly create a valid Laye identifier from the module file path.");
                    } else {
                        for (int64_t j = 0; j < module_name.count; j++) {
                            if (!is_identifier_char(module_name.data[j])) {
                                layec_write_error(context, top_level_node->decl_import.module_name.location, "Could not implicitly create a valid Laye identifier from the module file path.");
                                break;
                            }
                        }
                    }

                    //layec_write_note(context, top_level_node->decl_import.module_name.location, "calculated module name: '%.*s'\n", STR_EXPAND(module_name));
                }

                laye_module_import module_import_info = {
                    .referenced_module = top_level_node->decl_import.referenced_module,
                    .name = module_name,
                };

                arr_push(module->imports, module_import_info);
            } else {
                // no import namespaces, populate this scope directly
                dynarr(laye_node*) queries = top_level_node->decl_import.import_queries;
                for (int64_t j = 0, j_count = arr_count(queries); j < j_count; j++) {
                    laye_node* query = queries[j];
                    laye_sema_resolve_import_query(context, top_level_node->decl_import.referenced_module, query);

                    // populate the module's scope with the imported entities.
                    for (int64_t k = 0, k_count = arr_count(query->import_query.imported_entities); k < k_count; k++) {
                        laye_node* entity = query->import_query.imported_entities[k];
                        assert(laye_node_is_decl(entity));
                        assert(entity->declared_name.count > 0);
                        assert(entity->declared_name.data != NULL);

                        laye_scope_declare(module->scope, entity);
                    }

                    // add imported modules to the same import module array as import declarations with namespaces.
                    for (int64_t k = 0, k_count = arr_count(query->import_query.imported_modules); k < k_count; k++) {
                        arr_push(module->imports, query->import_query.imported_modules[k]);
                    }
                }
            }
        }
    }
}

void laye_analyse(layec_context* context) {
    assert(context != NULL);
    assert(context->laye_dependencies != NULL);

    laye_sema sema = {
        .context = context,
        .dependencies = context->laye_dependencies,
    };

    for (int64_t i = 0, count = arr_count(context->laye_modules); i < count; i++) {
        laye_handle_module_imports(context, context->laye_modules[i]);
    }

    for (int64_t i = 0, count = arr_count(context->laye_modules); i < count; i++) {
        laye_populate_module_import_scopes(context, context->laye_modules[i]);
    }

    // TODO(local): somewhere in here, before sema is done, we have to check for redeclared symbols.
    // probably after top level types.

    //return;

    for (int64_t i = 0, count = arr_count(context->laye_modules); i < count; i++) {
        laye_generate_dependencies_for_module(sema.dependencies, context->laye_modules[i]);
    }

    layec_dependency_order_result order_result = layec_dependency_graph_get_ordered_entities(sema.dependencies);
    if (order_result.status == LAYEC_DEP_CYCLE) {
        layec_write_error(
            context,
            ((laye_node*)order_result.from)->location,
            "Cyclic dependency detected. %.*s depends on %.*s, and vice versa.",
            STR_EXPAND(((laye_node*)order_result.from)->declared_name),
            STR_EXPAND(((laye_node*)order_result.to)->declared_name)
        );

        layec_write_note(
            context,
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
        laye_sema_analyse_node(&sema, &node, NOTY);
        assert(node != NULL);
    }

    arr_free(ordered_nodes);
}

static laye_node* laye_sema_build_struct_type(laye_sema* sema, laye_node* node, laye_node* parent_struct) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node != NULL);
    if (parent_struct != NULL) {
        assert(parent_struct->kind == LAYE_NODE_TYPE_STRUCT);
    }

    laye_node* struct_type = laye_node_create(node->module, LAYE_NODE_TYPE_STRUCT, node->location, LTY(sema->context->laye_types.type));
    assert(struct_type != NULL);
    struct_type->type_struct.name = node->declared_name;
    struct_type->type_struct.parent_struct_type = parent_struct;

    for (int64_t i = 0, count = arr_count(node->decl_struct.field_declarations); i < count; i++) {
        laye_node* field_node = node->decl_struct.field_declarations[i];
        assert(field_node != NULL);
        assert(field_node->kind == LAYE_NODE_DECL_STRUCT_FIELD);

        (void)laye_sema_analyse_type(sema, &field_node->declared_type);

        layec_evaluated_constant constant_initial_value = {0};
        if (field_node->decl_struct_field.initializer != NULL) {
            if (laye_sema_analyse_node(sema, &field_node->decl_struct_field.initializer, field_node->declared_type)) {
                laye_sema_convert_or_error(sema, &field_node->decl_struct_field.initializer, field_node->declared_type);

                if (!laye_expr_evaluate(field_node->decl_struct_field.initializer, &constant_initial_value, true)) {
                    // make sure it's still zero'd
                    constant_initial_value = (layec_evaluated_constant){0};
                    layec_write_error(sema->context, field_node->decl_struct_field.initializer->location, "Could not evaluate field initializer. Nontrivial compile-time execution is not currently supported.");
                }
            }
        }

        laye_struct_type_field field = {
            .type = field_node->declared_type,
            .name = field_node->declared_name,
            .initial_value = constant_initial_value,
        };

        arr_push(struct_type->type_struct.fields, field);
    }

    for (int64_t i = 0, count = arr_count(node->decl_struct.variant_declarations); i < count; i++) {
        laye_node* variant_node = node->decl_struct.variant_declarations[i];
        assert(variant_node != NULL);
        assert(variant_node->kind == LAYE_NODE_DECL_STRUCT);

        laye_node* variant_type = laye_sema_build_struct_type(sema, variant_node, struct_type);
        assert(variant_type != NULL);
        assert(variant_type->kind == LAYE_NODE_TYPE_STRUCT);

        laye_struct_type_variant variant = {
            .type = variant_type,
            .name = variant_node->declared_name,
        };

        arr_push(struct_type->type_struct.variants, variant);
    }

    return struct_type;
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
            assert(node->decl_function.return_type.node != NULL);
            if (!laye_sema_analyse_type(sema, &node->decl_function.return_type)) {
                node->sema_state = LAYEC_SEMA_ERRORED;
                node->type = LTY(sema->context->laye_types.poison);
            }

            for (int64_t i = 0, count = arr_count(node->decl_function.parameter_declarations); i < count; i++) {
                assert(node->decl_function.parameter_declarations[i] != NULL);
                assert(node->decl_function.parameter_declarations[i]->declared_type.node != NULL);
                if (!laye_sema_analyse_type(sema, &node->decl_function.parameter_declarations[i]->declared_type)) {
                    node->sema_state = LAYEC_SEMA_ERRORED;
                    node->type = LTY(sema->context->laye_types.poison);
                }
            }

            assert(node->declared_type.node != NULL);
            assert(laye_type_is_function(node->declared_type));
            if (!laye_sema_analyse_type(sema, &node->declared_type)) {
                node->sema_state = LAYEC_SEMA_ERRORED;
                node->type = LTY(sema->context->laye_types.poison);
            }

            bool is_declared_main = string_view_equals(SV_CONSTANT("main"), node->declared_name);
            bool has_foreign_name = node->attributes.foreign_name.count != 0;
            bool has_body = arr_count(node->decl_function.body) != 0;

            if (is_declared_main && !has_foreign_name) {
                node->attributes.calling_convention = LAYEC_CCC;
                node->attributes.linkage = LAYEC_LINK_EXPORTED;
                node->attributes.mangling = LAYEC_MANGLE_NONE;

                node->declared_type.node->type_function.calling_convention = LAYEC_CCC;

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
        } break;
    }

    *node_ref = node;
}

static laye_node* wrap_yieldable_value_in_compound(laye_sema* sema, laye_node** value_ref, laye_type expected_type) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(value_ref != NULL);

    laye_sema_analyse_node(sema, value_ref, expected_type);
    laye_node* value = *value_ref;
    assert(value != NULL);

    laye_node* yield_node = laye_node_create(value->module, LAYE_NODE_YIELD, value->location, LTY(sema->context->laye_types._void));
    assert(yield_node != NULL);
    yield_node->compiler_generated = true;
    yield_node->yield.value = value;

    laye_node* compound_node = laye_node_create(value->module, LAYE_NODE_COMPOUND, value->location, LTY(sema->context->laye_types._void));
    assert(compound_node != NULL);
    compound_node->compiler_generated = true;
    arr_push(compound_node->compound.children, yield_node);

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
static laye_struct_type_field laye_sema_create_padding_field(laye_sema* sema, laye_module* module, layec_location location, int padding_bytes);

static bool laye_sema_analyse_node(laye_sema* sema, laye_node** node_ref, laye_type expected_type) {
    laye_node* node = *node_ref;

    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node_ref != NULL);
    assert(node != NULL);
    if (!laye_node_is_type(node)) {
        assert(node->module != NULL);
        assert(node->module->context == sema->context);
    }
    assert(node->type.node != NULL);

    if (expected_type.node != NULL) {
        assert(expected_type.node->sema_state == LAYEC_SEMA_OK);
    }

    if (node->sema_state == LAYEC_SEMA_OK || node->sema_state == LAYEC_SEMA_ERRORED) {
        return node->sema_state == LAYEC_SEMA_OK;
    }

    if (node->sema_state == LAYEC_SEMA_IN_PROGRESS) {
        assert(false && "node already in progress");
        return false;
    }

    node->sema_state = LAYEC_SEMA_IN_PROGRESS;
    laye_sema_analyse_type(sema, &node->type);

    switch (node->kind) {
        default: {
            fprintf(stderr, "on node kind %s\n", laye_node_kind_to_cstring(node->kind));
            assert(false && "unreachable");
        } break;

        case LAYE_NODE_DECL_FUNCTION: {
            laye_node* prev_function = sema->current_function;
            sema->current_function = node;

            if (node->decl_function.body != NULL) {
                assert(node->decl_function.body->kind == LAYE_NODE_COMPOUND);
                laye_sema_analyse_node(sema, &node->decl_function.body, NOTY);

                if (!laye_type_is_noreturn(node->decl_function.body->type)) {
                    if (laye_type_is_void(node->decl_function.return_type)) {
                        laye_node* implicit_return = laye_node_create(node->module, LAYE_NODE_RETURN, node->decl_function.body->location, LTY(sema->context->laye_types.noreturn));
                        assert(implicit_return != NULL);
                        implicit_return->compiler_generated = true;
                        arr_push(node->decl_function.body->compound.children, implicit_return);
                        node->decl_function.body->type = LTY(sema->context->laye_types.noreturn);
                    } else if (laye_type_is_noreturn(node->decl_function.return_type)) {
                        layec_write_error(sema->context, node->location, "Control flow reaches the end of a `noreturn` function.");
                    } else {
                        layec_write_error(sema->context, node->location, "Not all code paths return a value.");
                    }
                }
            }

            sema->current_function = prev_function;
        } break;

        case LAYE_NODE_DECL_BINDING: {
            laye_sema_analyse_type(sema, &node->declared_type);
            if (node->decl_binding.initializer != NULL) {
                if (laye_sema_analyse_node(sema, &node->decl_binding.initializer, node->declared_type)) {
                    laye_sema_convert_or_error(sema, &node->decl_binding.initializer, node->declared_type);
                }
            }
        } break;

        case LAYE_NODE_DECL_STRUCT: {
            assert(node->declared_type.node != NULL);
            assert(node->declared_type.node->kind == LAYE_NODE_TYPE_STRUCT);
        } break;

        case LAYE_NODE_IF: {
            bool is_expression = node->_if.is_expr;
            bool is_noreturn = true;

            assert(arr_count(node->_if.conditions) == arr_count(node->_if.passes));
            for (int64_t i = 0, count = arr_count(node->_if.conditions); i < count; i++) {
                if (laye_sema_analyse_node(sema, &node->_if.conditions[i], LTY(sema->context->laye_types._bool))) {
                    laye_sema_convert_or_error(sema, &node->_if.conditions[i], LTY(sema->context->laye_types._bool));
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
                node->type = LTY(sema->context->laye_types.noreturn);
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
                    node->sema_state = LAYEC_SEMA_ERRORED;
                }
            }

            bool is_condition_always_true = false;
            if (node->_for.condition == NULL) {
                is_condition_always_true = true;
            } else {
                if (!laye_sema_analyse_node(sema, &node->_for.condition, LTY(sema->context->laye_types._bool))) {
                    node->sema_state = LAYEC_SEMA_ERRORED;
                } else {
                    // laye_sema_lvalue_to_rvalue(sema, &node->_for.condition, true);
                    laye_sema_convert_or_error(sema, &node->_for.condition, LTY(sema->context->laye_types._bool));

                    layec_evaluated_constant condition_constant;
                    if (laye_expr_evaluate(node->_for.condition, &condition_constant, false) && condition_constant.kind == LAYEC_EVAL_BOOL && condition_constant.bool_value) {
                        laye_node* eval_condition = laye_node_create(node->module, LAYE_NODE_EVALUATED_CONSTANT, node->_for.condition->location, LTY(sema->context->laye_types._bool));
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
                    node->sema_state = LAYEC_SEMA_ERRORED;
                }
            }

            if (node->_for.has_breaks) {
                is_condition_always_true = false;
            }

            if (!laye_sema_analyse_node(sema, &node->_for.pass, NOTY)) {
                node->sema_state = LAYEC_SEMA_ERRORED;
            }

            if (node->_for.fail != NULL) {
                if (!laye_sema_analyse_node(sema, &node->_for.fail, NOTY)) {
                    node->sema_state = LAYEC_SEMA_ERRORED;
                }
            }

            // TODO(local): if there is a `break` within the body anywhere, then this is not true
            if (is_condition_always_true) {
                node->type = LTY(sema->context->laye_types.noreturn);
            }
        } break;

        case LAYE_NODE_FOREACH: {
            if (!laye_sema_analyse_node(sema, &node->foreach.iterable, NOTY)) {
                node->sema_state = LAYEC_SEMA_ERRORED;
            }

            laye_type iterable_type = node->foreach.iterable->type;
            assert(iterable_type.node != NULL);

            if (laye_type_is_array(iterable_type)) {
                if (node->foreach.index_binding != NULL) {
                    node->foreach.index_binding->declared_type = LTY(sema->context->laye_types._int);
                    if (!laye_sema_analyse_node(sema, &node->foreach.index_binding, NOTY)) {
                        node->sema_state = LAYEC_SEMA_ERRORED;
                    }
                }

                laye_type element_reference_type = LTY(laye_node_create(node->module, LAYE_NODE_TYPE_REFERENCE, node->foreach.element_binding->location, LTY(sema->context->laye_types.type)));
                assert(element_reference_type.node != NULL);
                element_reference_type.node->compiler_generated = true;
                element_reference_type.node->type_container.element_type = iterable_type.node->type_container.element_type;
                node->foreach.element_binding->declared_type = element_reference_type;
                if (!laye_sema_analyse_node(sema, &node->foreach.element_binding, NOTY)) {
                    node->sema_state = LAYEC_SEMA_ERRORED;
                }
            } else {
                if (node->foreach.index_binding != NULL) {
                    node->foreach.index_binding->declared_type = LTY(sema->context->laye_types.poison);
                }

                node->foreach.element_binding->declared_type = LTY(sema->context->laye_types.poison);

                if (node->foreach.iterable->kind != LAYE_NODE_TYPE_POISON) {
                    string type_string = string_create(sema->context->allocator);
                    laye_type_print_to_string(iterable_type, &type_string, sema->context->use_color);
                    layec_write_error(sema->context, node->foreach.iterable->location, "Cannot iterate over type %.*s.");
                    string_destroy(&type_string);
                }
            }

            if (!laye_sema_analyse_node(sema, &node->foreach.pass, NOTY)) {
                node->sema_state = LAYEC_SEMA_ERRORED;
            }
        } break;

        case LAYE_NODE_WHILE: {
            bool is_condition_always_true = false;
            if (node->_while.condition == NULL) {
                is_condition_always_true = true;
            } else {
                if (!laye_sema_analyse_node(sema, &node->_while.condition, LTY(sema->context->laye_types._bool))) {
                    node->sema_state = LAYEC_SEMA_ERRORED;
                } else {
                    // laye_sema_lvalue_to_rvalue(sema, &node->_while.condition, true);
                    laye_sema_convert_or_error(sema, &node->_while.condition, LTY(sema->context->laye_types._bool));

                    layec_evaluated_constant condition_constant;
                    if (laye_expr_evaluate(node->_while.condition, &condition_constant, false) && condition_constant.kind == LAYEC_EVAL_BOOL && condition_constant.bool_value) {
                        laye_node* eval_condition = laye_node_create(node->module, LAYE_NODE_EVALUATED_CONSTANT, node->_while.condition->location, LTY(sema->context->laye_types._bool));
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
                node->sema_state = LAYEC_SEMA_ERRORED;
            }

            if (node->_while.fail != NULL) {
                if (!laye_sema_analyse_node(sema, &node->_while.fail, NOTY)) {
                    node->sema_state = LAYEC_SEMA_ERRORED;
                }
            }

            // TODO(local): if there is a `break` within the body anywhere, then this is not true
            if (is_condition_always_true) {
                node->type = LTY(sema->context->laye_types.noreturn);
            }
        } break;

        case LAYE_NODE_RETURN: {
            assert(sema->current_function != NULL);
            assert(sema->current_function->type.node != NULL);
            assert(laye_node_is_type(sema->current_function->type.node));
            assert(laye_type_is_function(sema->current_function->declared_type));

            assert(laye_type_is_noreturn(node->type));
            // node->type = LTY( sema->context->laye_types.noreturn);

            laye_type expected_return_type = sema->current_function->declared_type.node->type_function.return_type;
            assert(expected_return_type.node != NULL);
            assert(laye_node_is_type(expected_return_type.node));

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
                layec_write_error(sema->context, node->assignment.lhs->location, "Cannot assign to a non-lvalue.");
                node->sema_state = LAYEC_SEMA_ERRORED;
            } else {
                laye_type nonref_target_type = laye_type_strip_references(node->assignment.lhs->type);
                laye_sema_convert_or_error(sema, &node->assignment.rhs, nonref_target_type);
            }

            if (!node->assignment.lhs->type.is_modifiable && !laye_type_is_poison(node->assignment.lhs->type)) {
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
                    laye_sema_analyse_node(sema, child_ref, NOTY);
                }

                laye_node* child = *child_ref;
                if (laye_type_is_noreturn(child->type)) {
                    node->type = LTY(sema->context->laye_types.noreturn);
                }
            }

            sema->current_yield_target = prev_yield_target;
        } break;

        case LAYE_NODE_EVALUATED_CONSTANT: break;

        case LAYE_NODE_CALL: {
            laye_sema_analyse_node(sema, &node->call.callee, NOTY);

            for (int64_t i = 0, count = arr_count(node->call.arguments); i < count; i++) {
                laye_node** argument_node_ref = &node->call.arguments[i];
                assert(*argument_node_ref != NULL);
                laye_sema_analyse_node(sema, argument_node_ref, NOTY);
            }

            laye_type callee_type = node->call.callee->type;
            assert(callee_type.node->sema_state == LAYEC_SEMA_OK || callee_type.node->sema_state == LAYEC_SEMA_ERRORED);

            switch (callee_type.node->kind) {
                default: {
                    fprintf(stderr, "on node kind %s\n", laye_node_kind_to_cstring(callee_type.node->kind));
                    assert(false && "todo callee type");
                } break;

                case LAYE_NODE_TYPE_POISON: {
                    node->type = LTY(sema->context->laye_types.poison);
                } break;

                case LAYE_NODE_TYPE_FUNCTION: {
                    assert(callee_type.node->type_function.return_type.node != NULL);
                    node->type = callee_type.node->type_function.return_type;

                    int64_t param_count = arr_count(callee_type.node->type_function.parameter_types);

                    if (callee_type.node->type_function.varargs_style == LAYE_VARARGS_NONE) {
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
                            laye_sema_convert_or_error(sema, &node->call.arguments[i], callee_type.node->type_function.parameter_types[i]);
                        }
                    } else if (callee_type.node->type_function.varargs_style == LAYE_VARARGS_C) {
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

            for (int64_t i = 0, count = arr_count(node->index.indices); i < count; i++) {
                laye_node** index_node_ref = &node->index.indices[i];
                assert(*index_node_ref != NULL);
                laye_sema_analyse_node(sema, index_node_ref, LTY(sema->context->laye_types._int));
                // laye_sema_convert_or_error(sema, index_node_ref, LTY( sema->context->laye_types._int));

                if (laye_type_is_int((*index_node_ref)->type)) {
                    if (laye_type_is_signed_int((*index_node_ref)->type)) {
                        laye_sema_convert_or_error(sema, index_node_ref, LTY(sema->context->laye_types._int));
                        laye_sema_insert_implicit_cast(sema, index_node_ref, LTY(sema->context->laye_types._uint));
                    } else {
                        laye_sema_convert_or_error(sema, index_node_ref, LTY(sema->context->laye_types._uint));
                    }
                } else {
                    layec_write_error(sema->context, (*index_node_ref)->location, "Indices must be of integer type or convertible to an integer.");
                }
            }

            laye_type value_type = node->index.value->type;
            assert(value_type.node->sema_state == LAYEC_SEMA_OK || value_type.node->sema_state == LAYEC_SEMA_ERRORED);

            switch (value_type.node->kind) {
                default: {
                    string type_string = string_create(sema->context->allocator);
                    laye_type_print_to_string(value_type, &type_string, sema->context->use_color);
                    layec_write_error(sema->context, node->index.value->location, "Cannot index type %.*s.", STR_EXPAND(type_string));
                    string_destroy(&type_string);
                    node->type = LTY(sema->context->laye_types.poison);
                } break;

                case LAYE_NODE_TYPE_ARRAY: {
                    if (arr_count(node->index.indices) != arr_count(value_type.node->type_container.length_values)) {
                        string type_string = string_create(sema->context->allocator);
                        laye_type_print_to_string(value_type, &type_string, sema->context->use_color);
                        layec_write_error(
                            sema->context,
                            node->location,
                            "Expected %lld indices to type %.*s, got %lld.",
                            arr_count(value_type.node->type_container.length_values),
                            STR_EXPAND(type_string),
                            arr_count(node->index.indices)
                        );
                        string_destroy(&type_string);
                    }

                    node->type = value_type.node->type_container.element_type;
                } break;
            }

            assert(node->type.node != NULL);
            assert(node->type.node->kind != LAYE_NODE_INVALID);

            bool is_lvalue = laye_node_is_lvalue(node->index.value);
            laye_expr_set_lvalue(node, is_lvalue);
        } break;

        case LAYE_NODE_MEMBER: {
            laye_sema_analyse_node(sema, &node->member.value, NOTY);
            assert(node->member.value != NULL);

            laye_type value_type = node->member.value->type;
            assert(value_type.node->sema_state == LAYEC_SEMA_OK || value_type.node->sema_state == LAYEC_SEMA_ERRORED);

            laye_expr_set_lvalue(node, laye_expr_is_lvalue(node->member.value));
            if (!laye_expr_is_lvalue(node->member.value)) {
                node->sema_state = LAYEC_SEMA_ERRORED;
                layec_write_error(sema->context, node->member.value->location, "Expression must be a modifiable lvalue.");
                break;
            }

            layec_location member_location = node->member.field_name.location;
            string_view member_name = node->member.field_name.string_value;

            switch (value_type.node->kind) {
                default: {
                    node->sema_state = LAYEC_SEMA_ERRORED;
                    node->type = LTY(sema->context->laye_types.poison);
                    string type_string = string_create(sema->context->allocator);
                    laye_type_print_to_string(value_type, &type_string, sema->context->use_color);
                    layec_write_error(
                        sema->context,
                        node->location,
                        "Cannot index type %.*s.",
                        STR_EXPAND(type_string)
                    );
                    string_destroy(&type_string);
                } break;

                case LAYE_NODE_TYPE_STRUCT: {
                    laye_node* struct_type_node = value_type.node;

                    int64_t member_offset = 0;
                    laye_type member_type = {0};

                    for (int64_t i = 0, count = arr_count(struct_type_node->type_struct.fields); i < count; i++) {
                        laye_struct_type_field f = struct_type_node->type_struct.fields[i];
                        if (string_view_equals(member_name, f.name)) {
                            member_type = f.type;
                            break;
                        } else {
                            member_offset += laye_type_size_in_bytes(f.type);
                        }
                    }

                    if (member_offset >= laye_type_size_in_bytes(value_type)) {
                        node->sema_state = LAYEC_SEMA_ERRORED;
                        node->type = LTY(sema->context->laye_types.poison);
                        layec_write_error(sema->context, node->location, "No such member '%.*s'.", STR_EXPAND(member_name));
                        break;
                    }

                    node->member.member_offset = member_offset;
                    node->type = member_type;
                } break;
            }
        } break;

        case LAYE_NODE_NAMEREF: {
            laye_node* referenced_decl_node = node->nameref.referenced_declaration;

            if (referenced_decl_node == NULL) {
                referenced_decl_node = laye_sema_lookup_value_declaration(sema, node->module, node->nameref);
                if (referenced_decl_node == NULL) {
                    node->sema_state = LAYEC_SEMA_ERRORED;
                    node->type = LTY(sema->context->laye_types.poison);
                    break;
                }
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

                case LAYE_NODE_DECL_BINDING: {
                    laye_expr_set_lvalue(node, true);
                } break;

                case LAYE_NODE_DECL_STRUCT: {
                    layec_write_error(sema->context, node->location, "Cannot use a struct as a value.");
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
            }
            
            string from_type_string = string_create(sema->context->allocator);
            laye_type_print_to_string(type_from, &from_type_string, sema->context->use_color);

            string to_type_string = string_create(sema->context->allocator);
            laye_type_print_to_string(type_to, &to_type_string, sema->context->use_color);

            node->sema_state = LAYEC_SEMA_ERRORED;
            layec_write_error(
                sema->context,
                node->location,
                "Expression of type %.*s is not convertible to %.*s",
                STR_EXPAND(from_type_string),
                STR_EXPAND(to_type_string)
            );

            string_destroy(&to_type_string);
            string_destroy(&from_type_string);
        } break;

        case LAYE_NODE_UNARY: {
            if (!laye_sema_analyse_node(sema, &node->unary.operand, NOTY)) {
                node->sema_state = LAYEC_SEMA_ERRORED;
                node->type = LTY(sema->context->laye_types.poison);
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
                        node->type = LTY(sema->context->laye_types.poison);
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
                        node->type = LTY(sema->context->laye_types.poison);
                        break;
                    }

                    node->type = node->unary.operand->type;
                } break;

                case '&': {
                    if (!laye_expr_is_lvalue(node->unary.operand)) {
                        layec_write_error(sema->context, node->location, "Cannot take the address of a non-lvalue expression.");
                        node->sema_state = LAYEC_SEMA_ERRORED;
                        node->type = LTY(sema->context->laye_types.poison);
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
                        node->sema_state = LAYEC_SEMA_ERRORED;
                        node->type = LTY(sema->context->laye_types.poison);
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
                    string type_string = string_create(default_allocator);
                    laye_type_print_to_string(node->unary.operand->type, &type_string, sema->context->use_color);
                    layec_write_error(sema->context, node->location, "Cannot dereference type %.*s.", STR_EXPAND(type_string));
                    string_destroy(&type_string);
                    node->sema_state = LAYEC_SEMA_ERRORED;
                    node->type = LTY(sema->context->laye_types.poison);
                } break;

                case LAYE_TOKEN_NOT: {
                    laye_sema_implicit_dereference(sema, &node->unary.operand);
                    laye_sema_lvalue_to_rvalue(sema, &node->unary.operand, true);

                    node->type = LTY(sema->context->laye_types._bool);
                    laye_sema_convert_or_error(sema, &node->unary.operand, node->type);
                } break;
            }
        } break;

        case LAYE_NODE_BINARY: {
            if (!laye_sema_analyse_node(sema, &node->binary.lhs, NOTY) || !laye_sema_analyse_node(sema, &node->binary.rhs, NOTY)) {
                node->sema_state = LAYEC_SEMA_ERRORED;
                node->type = LTY(sema->context->laye_types.poison);
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
                    node->type = LTY(sema->context->laye_types._bool);

                    laye_sema_implicit_dereference(sema, &node->binary.lhs);
                    laye_sema_implicit_dereference(sema, &node->binary.rhs);

                    laye_sema_lvalue_to_rvalue(sema, &node->binary.lhs, true);
                    laye_sema_lvalue_to_rvalue(sema, &node->binary.rhs, true);

                    laye_sema_convert_or_error(sema, &node->binary.lhs, LTY(sema->context->laye_types._bool));
                    laye_sema_convert_or_error(sema, &node->binary.rhs, LTY(sema->context->laye_types._bool));
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

                    laye_type lhs_type = node->binary.lhs->type;
                    assert(lhs_type.node != NULL);
                    laye_type rhs_type = node->binary.rhs->type;
                    assert(rhs_type.node != NULL);

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

                    assert(node->type.node->kind != LAYE_NODE_TYPE_UNKNOWN);
                    break;

                cannot_arith_types:;
                    node->sema_state = LAYEC_SEMA_ERRORED;
                    node->type = LTY(sema->context->laye_types.poison);

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

                    node->type = LTY(sema->context->laye_types._bool);

                    laye_sema_implicit_dereference(sema, &node->binary.lhs);
                    laye_sema_implicit_dereference(sema, &node->binary.rhs);

                    laye_sema_lvalue_to_rvalue(sema, &node->binary.lhs, true);
                    laye_sema_lvalue_to_rvalue(sema, &node->binary.rhs, true);

                    laye_type lhs_type = node->binary.lhs->type;
                    assert(lhs_type.node != NULL);
                    laye_type rhs_type = node->binary.rhs->type;
                    assert(rhs_type.node != NULL);

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
                    node->sema_state = LAYEC_SEMA_ERRORED;
                    node->type = LTY(sema->context->laye_types.poison);

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

        case LAYE_NODE_TYPE_NAMEREF: {
            laye_node* referenced_decl_node = node->nameref.referenced_declaration;

            if (referenced_decl_node == NULL) {
                referenced_decl_node = laye_sema_lookup_type_declaration(sema, node->module, node->nameref);
                if (referenced_decl_node == NULL) {
                    node->sema_state = LAYEC_SEMA_ERRORED;
                    node->type = LTY(sema->context->laye_types.poison);
                    break;
                }
            }

            assert(referenced_decl_node != NULL);
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
                    layec_write_error(sema->context, node->location, "Cannot use a function as a type.");
                } break;

                case LAYE_NODE_DECL_BINDING: {
                    layec_write_error(sema->context, node->location, "Cannot use a variable as a type.");
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
                node->sema_state = LAYEC_SEMA_ERRORED;
            }
        } break;

        case LAYE_NODE_TYPE_FUNCTION: {
            if (node->type_function.calling_convention == LAYEC_DEFAULTCC) {
                node->type_function.calling_convention = LAYEC_LAYECC;
            }

            assert(node->type_function.return_type.node != NULL);
            if (!laye_sema_analyse_type(sema, &node->type_function.return_type)) {
                node->sema_state = LAYEC_SEMA_ERRORED;
                node->type = LTY(sema->context->laye_types.poison);
            }

            for (int64_t i = 0, count = arr_count(node->type_function.parameter_types); i < count; i++) {
                if (!laye_sema_analyse_type(sema, &node->type_function.parameter_types[i])) {
                    node->sema_state = LAYEC_SEMA_ERRORED;
                    node->type = LTY(sema->context->laye_types.poison);
                }
            }
        } break;

        case LAYE_NODE_TYPE_ARRAY: {
            if (!laye_sema_analyse_type(sema, &node->type_container.element_type)) {
                node->sema_state = LAYEC_SEMA_ERRORED;
            }

            for (int64_t i = 0, count = arr_count(node->type_container.length_values); i < count; i++) {
                if (!laye_sema_analyse_node(sema, &node->type_container.length_values[i], NOTY)) {
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

                laye_node* evaluated_constant = laye_node_create(node->module, LAYE_NODE_EVALUATED_CONSTANT, node->type_container.length_values[i]->location, LTY(sema->context->laye_types._int));
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

            for (int64_t i = 0; i < arr_count(node->type_struct.fields); i++) {
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
                    arr_insert(node->type_struct.fields, i, padding_field);
                    i++;
                }

                current_size += padding_bytes;
                current_size += f_size;
            }

            padding_bytes = (current_align - (current_size % current_align)) % current_align;
            if (padding_bytes > 0) {
                laye_struct_type_field padding_field = laye_sema_create_padding_field(sema, node->module, node->location, padding_bytes);
                arr_push(node->type_struct.fields, padding_field);
            }

            current_size += padding_bytes;

            node->type_struct.cached_size = current_size;
            node->type_struct.cached_align = current_align;
        } break;
    }

    assert(node != NULL);
    if (node->sema_state == LAYEC_SEMA_IN_PROGRESS) {
        node->sema_state = LAYEC_SEMA_OK;
    }

#if false
    if (expected_type != NULL && node->kind != LAYE_NODE_YIELD && node->sema_state == LAYEC_SEMA_OK) {
        assert(laye_node_is_type(expected_type));
        laye_sema_convert_or_error(sema, &node, expected_type);
    }
#endif

    assert(node->sema_state == LAYEC_SEMA_OK || node->sema_state == LAYEC_SEMA_ERRORED);
    assert(node->type.node != NULL);
    assert(node->type.node->kind != LAYE_NODE_INVALID);
    assert(node->type.node->kind != LAYE_NODE_TYPE_UNKNOWN);

    *node_ref = node;
    return node->sema_state == LAYEC_SEMA_OK;
}

static laye_struct_type_field laye_sema_create_padding_field(laye_sema* sema, laye_module* module, layec_location location, int padding_bytes) {
    laye_type padding_type = LTY(laye_node_create(module, LAYE_NODE_TYPE_ARRAY, location, LTY(sema->context->laye_types.type)));
    assert(padding_type.node != NULL);
    padding_type.node->type_container.element_type = LTY(sema->context->laye_types.i8);

    laye_node* constant_value = laye_node_create(module, LAYE_NODE_LITINT, location, LTY(sema->context->laye_types._int));
    assert(constant_value != NULL);
    constant_value->litint.value = padding_bytes;

    laye_sema_analyse_node(sema, &constant_value, constant_value->type);

    layec_evaluated_constant eval_result = {0};
    laye_expr_evaluate(constant_value, &eval_result, true);
    constant_value = laye_create_constant_node(sema, constant_value, eval_result);
    assert(constant_value->kind == LAYE_NODE_EVALUATED_CONSTANT);

    arr_push(padding_type.node->type_container.length_values, constant_value);

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
    assert(sema->context != NULL);
    assert(node_ref != NULL);

    laye_node* node = *node_ref;
    assert(node != NULL);
    assert(node->type.node != NULL);

    if (node->kind == LAYE_NODE_CALL) {
        // TODO(local): check discardable nature of the callee
    }

    if (laye_type_is_void(node->type) || laye_type_is_noreturn(node->type)) {
        return;
    }

    laye_sema_insert_implicit_cast(sema, node_ref, LTY(sema->context->laye_types._void));
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

static int laye_sema_convert_impl(laye_sema* sema, laye_node** node_ref, laye_type to, bool perform_conversion) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node_ref != NULL);
    assert(to.node != NULL);
    assert(laye_node_is_type(to.node));
    layec_context* context = sema->context;

    laye_node* node = *node_ref;
    assert(node != NULL);

    laye_type from = (*node_ref)->type;
    assert(from.node != NULL);
    assert(laye_node_is_type(from.node));

    if (laye_type_is_poison(from) || laye_type_is_poison(to)) {
        return LAYE_CONVERT_NOOP;
    }

    if (from.node->sema_state == LAYEC_SEMA_ERRORED || to.node->sema_state == LAYEC_SEMA_ERRORED) {
        return LAYE_CONVERT_CONTAINS_ERRORS;
    }

    assert(from.node->sema_state == LAYEC_SEMA_OK);
    assert(to.node->sema_state == LAYEC_SEMA_OK);

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

        if (laye_type_equals(from.node->type_container.element_type, to.node->type_container.element_type, LAYE_MUT_CONVERTIBLE)) {
            if (from.is_modifiable == to.is_modifiable || !to.is_modifiable)
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

static bool laye_sema_convert(laye_sema* sema, laye_node** node, laye_type to) {
    assert(node != NULL);
    assert(*node != NULL);

    if ((*node)->sema_state == LAYEC_SEMA_ERRORED) {
        return true;
    }

    return laye_sema_convert_impl(sema, node, to, true) >= 0;
}

static void laye_sema_convert_or_error(laye_sema* sema, laye_node** node, laye_type to) {
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
            laye_type ffi_int_type = LTY(laye_node_create((*node)->module, LAYE_NODE_TYPE_INT, (*node)->location, LTY(sema->context->laye_types.type)));
            assert(ffi_int_type.node != NULL);
            ffi_int_type.node->type_primitive.is_signed = laye_type_is_signed_int((*node)->type);
            ffi_int_type.node->type_primitive.bit_width = sema->context->target->c.size_of_int;
            laye_sema_insert_implicit_cast(sema, node, ffi_int_type);
            laye_sema_analyse_node(sema, node, NOTY);
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
    (*node)->type = LTY(sema->context->laye_types.poison);
}

static bool laye_sema_convert_to_common_type(laye_sema* sema, laye_node** a, laye_node** b) {
    assert(a != NULL);
    assert(*a != NULL);
    assert(b != NULL);
    assert(*b != NULL);

    if (laye_sema_try_convert(sema, a, (*b)->type) >= 0) {
        return laye_sema_convert(sema, a, (*b)->type);
    }

    return laye_sema_convert(sema, b, (*a)->type);
}

static int laye_sema_try_convert(laye_sema* sema, laye_node** node, laye_type to) {
    return laye_sema_convert_impl(sema, node, to, false);
}

static void laye_sema_wrap_with_cast(laye_sema* sema, laye_node** node, laye_type type, laye_cast_kind cast_kind) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node != NULL);
    assert(*node != NULL);
    assert((*node)->module != NULL);
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));

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

    if (laye_type_is_pointer((*node)->type) || laye_type_is_buffer((*node)->type)) {
        laye_sema_wrap_with_cast(sema, node, LTY(sema->context->laye_types._int), LAYE_CAST_IMPLICIT);
    }
}

static void laye_sema_insert_implicit_cast(laye_sema* sema, laye_node** node, laye_type to) {
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
        laye_sema_wrap_with_cast(sema, node, (*node)->type.node->type_container.element_type, LAYE_CAST_REFERENCE_TO_LVALUE);
        laye_sema_lvalue_to_rvalue(sema, node, false);
    }
}

static bool laye_sema_implicit_dereference(laye_sema* sema, laye_node** node) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(node != NULL);
    assert(*node != NULL);
    assert((*node)->module != NULL);
    assert((*node)->type.node != NULL);

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
    assert(sema->context != NULL);
    assert(node != NULL);
    assert(*node != NULL);
    assert((*node)->module != NULL);
    assert((*node)->type.node != NULL);

    if (laye_type_is_reference((*node)->type)) {
        laye_sema_lvalue_to_rvalue(sema, node, false);
        laye_sema_wrap_with_cast(sema, node, (*node)->type.node->type_container.element_type, LAYE_CAST_REFERENCE_TO_LVALUE);
        // laye_expr_set_lvalue(*node, true);
    }

    return laye_expr_is_lvalue(*node);
}

static laye_type laye_sema_get_pointer_to_type(laye_sema* sema, laye_type element_type, bool is_modifiable) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(element_type.node != NULL);
    assert(element_type.node->module != NULL);
    assert(laye_node_is_type(element_type.node));
    laye_node* type = laye_node_create(element_type.node->module, LAYE_NODE_TYPE_POINTER, element_type.node->location, LTY(sema->context->laye_types.type));
    assert(type != NULL);
    type->type_container.element_type = element_type;
    return LTY(type);
}

static laye_type laye_sema_get_buffer_of_type(laye_sema* sema, laye_type element_type, bool is_modifiable) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(element_type.node != NULL);
    assert(element_type.node->module != NULL);
    assert(laye_node_is_type(element_type.node));
    laye_node* type = laye_node_create(element_type.node->module, LAYE_NODE_TYPE_BUFFER, element_type.node->location, LTY(sema->context->laye_types.type));
    assert(type != NULL);
    type->type_container.element_type = element_type;
    return LTY(type);
}

static laye_type laye_sema_get_reference_to_type(laye_sema* sema, laye_type element_type, bool is_modifiable) {
    assert(sema != NULL);
    assert(sema->context != NULL);
    assert(element_type.node != NULL);
    assert(element_type.node->module != NULL);
    assert(laye_node_is_type(element_type.node));
    laye_node* type = laye_node_create(element_type.node->module, LAYE_NODE_TYPE_REFERENCE, element_type.node->location, LTY(sema->context->laye_types.type));
    assert(type != NULL);
    type->type_container.element_type = element_type;
    return LTY(type);
}
