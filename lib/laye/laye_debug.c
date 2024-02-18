#include "laye.h"
#include "layec.h"

#include <assert.h>

#define COL_COMMENT BRIGHT_BLACK
#define COL_DELIM   WHITE
#define COL_TREE    RED
#define COL_NODE    RED
#define COL_ADDR    BLUE
#define COL_OFFS    MAGENTA
#define COL_NAME    GREEN
#define COL_ERROR   RED
#define COL_CONST   BLUE

typedef struct laye_print_context {
    layec_context* context;
    laye_module* module;
    bool use_color;
    string* indents;
    string* output;
} laye_print_context;

static void laye_node_debug_print(laye_print_context* print_context, laye_node* node);

string laye_module_debug_print(laye_module* module) {
    assert(module != NULL);
    assert(module->context != NULL);

    int64_t indents_string_capacity = 256;
    char* indents_string_data = lca_allocate(module->context->allocator, indents_string_capacity);
    assert(indents_string_data != NULL);
    string indents_string = string_from_data(module->context->allocator, indents_string_data, 0, indents_string_capacity);

    string output_string = string_create(module->context->allocator);

    laye_print_context print_context = {
        .context = module->context,
        .module = module,
        .use_color = module->context->use_color,
        .indents = &indents_string,
        .output = &output_string,
    };

    bool use_color = print_context.use_color;
    string_append_format(print_context.output, "%s; %.*s%s\n", COL(COL_COMMENT), STR_EXPAND(layec_context_get_source(module->context, module->sourceid).name), COL(RESET));
    string_append_format(print_context.output, "%s; %016llX%s\n", COL(COL_COMMENT), (size_t)module, COL(RESET));

    for (int64_t i = 0, count = arr_count(module->top_level_nodes); i < count; i++) {
        laye_node* top_level_node = module->top_level_nodes[i];
        assert(top_level_node != NULL);

        laye_node_debug_print(&print_context, top_level_node);
    }

    string_destroy(&indents_string);

    return output_string;
}

static void laye_node_debug_print_children(laye_print_context* print_context, dynarr(laye_node*) children) {
    bool use_color = print_context->use_color;

    for (int64_t i = 0, count = arr_count(children); i < count; i++) {
        bool is_last = i == count - 1;
        laye_node* child = children[i];
        string indents = *print_context->indents;

        const char* next_leader = is_last ? "└─" : "├─";
        string_append_format(print_context->output, "%s%.*s%s", COL(COL_TREE), STR_EXPAND(indents), next_leader);

        int64_t old_indents_count = print_context->indents->count;
        string_append_format(print_context->indents, "%s", is_last ? "  " : "│ ");

        laye_node_debug_print(print_context, child);

        print_context->indents->count = old_indents_count;
    }
}

void laye_constant_print_to_string(layec_evaluated_constant constant, string* s, bool use_color);

static void laye_node_debug_print(laye_print_context* print_context, laye_node* node) {
    assert(print_context != NULL);
    assert(print_context->context != NULL);
    assert(print_context->module != NULL);
    assert(print_context->indents != NULL);
    assert(print_context->output != NULL);
    assert(node != NULL);

    bool use_color = print_context->use_color;

    string_append_format(
        print_context->output,
        "%s%s%s%s %s%016llX %s<%lld>",
        COL(COL_NODE),
        laye_node_kind_to_cstring(node->kind),
        (node->compiler_generated ? "*" : ""),
        (laye_expr_is_lvalue(node) ? "&" : ""),
        COL(COL_ADDR),
        (size_t)node,
        COL(COL_OFFS),
        node->location.offset
    );

    if (laye_node_is_decl(node)) {
        string_append_format(print_context->output, "%s", COL(COL_NODE));
        switch (node->attributes.linkage) {
            case LAYEC_LINK_LOCAL: lca_string_append_format(print_context->output, " LOCAL"); break;
            case LAYEC_LINK_INTERNAL: lca_string_append_format(print_context->output, " INTERNAL"); break;
            case LAYEC_LINK_IMPORTED: lca_string_append_format(print_context->output, " IMPORTED"); break;
            case LAYEC_LINK_EXPORTED: lca_string_append_format(print_context->output, " EXPORTED"); break;
            case LAYEC_LINK_REEXPORTED: lca_string_append_format(print_context->output, " REEXPORTED"); break;
        }

        switch (node->attributes.calling_convention) {
            case LAYEC_DEFAULTCC: break;
            case LAYEC_CCC: lca_string_append_format(print_context->output, " CCC"); break;
            case LAYEC_LAYECC: lca_string_append_format(print_context->output, " LAYECC"); break;
        }

        switch (node->attributes.mangling) {
            case LAYEC_MANGLE_DEFAULT: break;
            case LAYEC_MANGLE_NONE: lca_string_append_format(print_context->output, " NO_MANGLE"); break;
            case LAYEC_MANGLE_LAYE: lca_string_append_format(print_context->output, " LAYE_MANGLE"); break;
        }

        if (node->attributes.is_discardable) {
            lca_string_append_format(print_context->output, " DISCARDABLE");
        }

        if (node->attributes.is_inline) {
            lca_string_append_format(print_context->output, " INLINE");
        }

        if (node->attributes.foreign_name.count != 0) {
            lca_string_append_format(print_context->output, " FOREIGN \"%.*s\"", STR_EXPAND(node->attributes.foreign_name));
        }
    }

    if (node->declared_type.node != NULL) {
        lca_string_append_format(print_context->output, " ");
        laye_type_print_to_string(node->declared_type, print_context->output, use_color);
    } else if (node->type.node != NULL) {
        lca_string_append_format(print_context->output, " ");
        laye_type_print_to_string(node->type, print_context->output, use_color);
    }

    dynarr(laye_node*) children = NULL;

    switch (node->kind) {
        default: break;

        case LAYE_NODE_DECL_IMPORT: {
            layec_source source = layec_context_get_source(print_context->context, node->location.sourceid);
            string_append_format(
                print_context->output,
                " %s%.*s",
                COL(COL_NAME),
                (int)node->decl_import.module_name.location.length,
                source.text.data + node->decl_import.module_name.location.offset
            );

            if (node->decl_import.import_alias.kind != 0) {
                string_append_format(print_context->output, " %sas %s%.*s", COL(COL_TREE), COL(COL_NAME), STR_EXPAND(node->decl_import.import_alias.string_value));
            }

            if (node->decl_import.referenced_module != NULL) {
                string_append_format(print_context->output, " %s%016llX", COL(COL_ADDR), (size_t)node->decl_import.referenced_module);
            }

            for (int64_t i = 0, count = arr_count(node->decl_import.import_queries); i < count; i++) {
                arr_push(children, node->decl_import.import_queries[i]);
            }
        } break;

        case LAYE_NODE_IMPORT_QUERY: {
            lca_string_append_format(print_context->output, " ");

            if (node->import_query.is_wildcard) {
                string_append_format(print_context->output, "%s*", COL(COL_NAME));
            } else {
                for (int64_t i = 0, count = arr_count(node->import_query.pieces); i < count; i++) {
                    laye_token piece = node->import_query.pieces[i];
                    if (i > 0) {
                        string_append_format(print_context->output, "%s::", COL(RESET));
                    }

                    string_append_format(print_context->output, "%s%.*s", COL(COL_NAME), STR_EXPAND(piece.string_value));
                }

                if (node->import_query.alias.kind != 0) {
                    string_append_format(print_context->output, " %sas %s%.*s", COL(COL_TREE), COL(COL_NAME), STR_EXPAND(node->import_query.alias.string_value));
                }
            }
        } break;

        case LAYE_NODE_DECL_FUNCTION: {
            string_append_format(print_context->output, " %s%.*s", COL(COL_NAME), STR_EXPAND(node->declared_name));

            if (node->decl_function.body != NULL)
                arr_push(children, node->decl_function.body);
        } break;

        case LAYE_NODE_DECL_BINDING: {
            string_append_format(print_context->output, " %s%.*s", COL(COL_NAME), STR_EXPAND(node->declared_name));

            if (node->decl_binding.initializer != NULL)
                arr_push(children, node->decl_binding.initializer);
        } break;

        case LAYE_NODE_DECL_STRUCT: {
            string_append_format(print_context->output, " %s%.*s", COL(COL_NAME), STR_EXPAND(node->declared_name));

            for (int64_t i = 0, count = arr_count(node->decl_struct.field_declarations); i < count; i++)
                arr_push(children, node->decl_struct.field_declarations[i]);

            for (int64_t i = 0, count = arr_count(node->decl_struct.variant_declarations); i < count; i++)
                arr_push(children, node->decl_struct.variant_declarations[i]);
        } break;

        case LAYE_NODE_DECL_STRUCT_FIELD: {
            string_append_format(print_context->output, " %s%.*s", COL(COL_NAME), STR_EXPAND(node->declared_name));

            if (node->decl_binding.initializer != NULL)
                arr_push(children, node->decl_struct_field.initializer);
        } break;

        case LAYE_NODE_IF: {
            assert(arr_count(node->_if.conditions) == arr_count(node->_if.passes));
            for (int64_t i = 0, count = arr_count(node->_if.conditions); i < count; i++) {
                arr_push(children, node->_if.conditions[i]);
                arr_push(children, node->_if.passes[i]);
            }

            if (node->_if.fail != NULL) {
                arr_push(children, node->_if.fail);
            }
        } break;

        case LAYE_NODE_FOR: {
            if (node->_for.has_breaks) {
                string_append_format(print_context->output, " %sHAS_BREAKS", COL(COL_TREE));
            }

            if (node->_for.has_continues) {
                string_append_format(print_context->output, " %sHAS_CONTINUES", COL(COL_TREE));
            }

            if (node->_for.initializer != NULL) {
                arr_push(children, node->_for.initializer);
            }

            if (node->_for.condition != NULL) {
                arr_push(children, node->_for.condition);
            }

            if (node->_for.increment != NULL) {
                arr_push(children, node->_for.increment);
            }

            if (node->_for.pass != NULL) {
                arr_push(children, node->_for.pass);
            }

            if (node->_for.fail != NULL) {
                arr_push(children, node->_for.fail);
            }
        } break;

        case LAYE_NODE_FOREACH: {
            if (node->foreach.has_breaks) {
                string_append_format(print_context->output, " %sHAS_BREAKS", COL(COL_TREE));
            }

            if (node->foreach.has_continues) {
                string_append_format(print_context->output, " %sHAS_CONTINUES", COL(COL_TREE));
            }

            if (node->foreach.index_binding != NULL) {
                arr_push(children, node->foreach.index_binding);
            }

            arr_push(children, node->foreach.element_binding);

            if (node->foreach.iterable != NULL) {
                arr_push(children, node->foreach.iterable);
            }

            if (node->foreach.pass != NULL) {
                arr_push(children, node->foreach.pass);
            }
        } break;

        case LAYE_NODE_WHILE: {
            if (node->_while.has_breaks) {
                string_append_format(print_context->output, " %sHAS_BREAKS", COL(COL_TREE));
            }

            if (node->_while.has_continues) {
                string_append_format(print_context->output, " %sHAS_CONTINUES", COL(COL_TREE));
            }

            if (node->_while.condition != NULL) {
                arr_push(children, node->_while.condition);
            }

            if (node->_while.pass != NULL) {
                arr_push(children, node->_while.pass);
            }

            if (node->_while.fail != NULL) {
                arr_push(children, node->_while.fail);
            }
        } break;

        case LAYE_NODE_DOWHILE: {
            if (node->dowhile.has_breaks) {
                string_append_format(print_context->output, " %sHAS_BREAKS", COL(COL_TREE));
            }

            if (node->dowhile.has_continues) {
                string_append_format(print_context->output, " %sHAS_CONTINUES", COL(COL_TREE));
            }

            assert(node->dowhile.condition != NULL);
            arr_push(children, node->dowhile.condition);

            if (node->dowhile.pass != NULL) {
                arr_push(children, node->dowhile.pass);
            }
        } break;

        case LAYE_NODE_LABEL: {
            string_append_format(print_context->output, " %s%.*s", COL(COL_NAME), STR_EXPAND(node->declared_name));
        } break;

        case LAYE_NODE_DEFER: {
            arr_push(children, node->defer.body);
        } break;

        case LAYE_NODE_RETURN: {
            if (node->_return.value != NULL) {
                arr_push(children, node->_return.value);
            }
        } break;

        case LAYE_NODE_BREAK: {
            if (node->_break.target.count != 0) {
                string_append_format(print_context->output, " %s%.*s", COL(COL_NAME), STR_EXPAND(node->_break.target));
            }

            if (node->_break.target_node != NULL) {
                string_append_format(
                    print_context->output,
                    " %s%s %s%016llX",
                    COL(COL_NODE),
                    laye_node_kind_to_cstring(node->_break.target_node->kind),
                    COL(COL_ADDR),
                    (size_t)node->_break.target_node
                );
            }
        } break;

        case LAYE_NODE_CONTINUE: {
            if (node->_continue.target.count != 0) {
                string_append_format(print_context->output, " %s%.*s", COL(COL_NAME), STR_EXPAND(node->_continue.target));
            }

            if (node->_break.target_node != NULL) {
                string_append_format(
                    print_context->output,
                    " %s%s %s%016llX",
                    COL(COL_NODE),
                    laye_node_kind_to_cstring(node->_continue.target_node->kind),
                    COL(COL_ADDR),
                    (size_t)node->_continue.target_node
                );
            }
        } break;

        case LAYE_NODE_GOTO: {
            string_append_format(print_context->output, " %s%.*s", COL(COL_NAME), STR_EXPAND(node->_goto.label));
        } break;

        case LAYE_NODE_YIELD: {
            arr_push(children, node->yield.value);
        } break;

        case LAYE_NODE_COMPOUND: {
            for (int64_t i = 0, count = arr_count(node->compound.children); i < count; i++) {
                arr_push(children, node->compound.children[i]);
            }
        } break;

        case LAYE_NODE_EVALUATED_CONSTANT: {
            assert(node->evaluated_constant.expr != NULL);
            arr_push(children, node->evaluated_constant.expr);

            if (node->evaluated_constant.result.kind == LAYEC_EVAL_INT) {
                string_append_format(print_context->output, " %s%lld", COL(COL_CONST), node->evaluated_constant.result.int_value);
            }
        } break;

        case LAYE_NODE_CAST: {
            assert(node->cast.operand != NULL);
            arr_push(children, node->cast.operand);

            string_append_format(print_context->output, " %s", COL(COL_NODE));
            switch (node->cast.kind) {
                case LAYE_CAST_SOFT: lca_string_append_format(print_context->output, "SOFT"); break;
                case LAYE_CAST_HARD: lca_string_append_format(print_context->output, "HARD"); break;
                case LAYE_CAST_STRUCT_BITCAST: lca_string_append_format(print_context->output, "STRUCT_BITCAST"); break;
                case LAYE_CAST_IMPLICIT: lca_string_append_format(print_context->output, "IMPLICIT"); break;
                case LAYE_CAST_LVALUE_TO_RVALUE: lca_string_append_format(print_context->output, "LVALUE_TO_RVALUE"); break;
                case LAYE_CAST_LVALUE_TO_REFERENCE: lca_string_append_format(print_context->output, "LVALUE_TO_REFERENCE"); break;
                case LAYE_CAST_REFERENCE_TO_LVALUE: lca_string_append_format(print_context->output, "REFERENCE_TO_LVALUE"); break;
            }
        } break;

        case LAYE_NODE_CALL: {
            assert(node->call.callee != NULL);
            arr_push(children, node->call.callee);

            for (int64_t i = 0, count = arr_count(node->call.arguments); i < count; i++) {
                arr_push(children, node->call.arguments[i]);
            }
        } break;

        case LAYE_NODE_INDEX: {
            assert(node->index.value != NULL);
            arr_push(children, node->index.value);

            for (int64_t i = 0, count = arr_count(node->index.indices); i < count; i++) {
                arr_push(children, node->index.indices[i]);
            }
        } break;

        case LAYE_NODE_MEMBER: {
            string_append_format(print_context->output, " %s%.*s", COL(COL_NAME), STR_EXPAND(node->member.field_name.string_value));

            assert(node->member.value != NULL);
            arr_push(children, node->member.value);
        } break;

        case LAYE_NODE_UNARY: {
            arr_push(children, node->unary.operand);

            layec_source source = layec_context_get_source(print_context->context, node->location.sourceid);
            string_append_format(print_context->output, " %s%.*s", COL(COL_NODE), (int)node->unary.operator.location.length, source.text.data + node->unary.operator.location.offset);
        } break;

        case LAYE_NODE_BINARY: {
            arr_push(children, node->binary.lhs);
            arr_push(children, node->binary.rhs);

            layec_source source = layec_context_get_source(print_context->context, node->location.sourceid);
            string_append_format(print_context->output, " %s%.*s", COL(COL_NODE), (int)node->binary.operator.location.length, source.text.data + node->binary.operator.location.offset);
        } break;

        case LAYE_NODE_ASSIGNMENT: {
            arr_push(children, node->assignment.lhs);
            arr_push(children, node->assignment.rhs);

            layec_source source = layec_context_get_source(print_context->context, node->location.sourceid);
            string_append_format(print_context->output, " %s%.*s", COL(COL_NODE), (int)node->location.length, source.text.data + node->location.offset);
        } break;

        case LAYE_NODE_NAMEREF: {
            lca_string_append_format(print_context->output, " ");

            if (node->nameref.kind == LAYE_NAMEREF_HEADLESS) {
                string_append_format(print_context->output, "%s::", COL(COL_DELIM));
            } else if (node->nameref.kind == LAYE_NAMEREF_GLOBAL) {
                string_append_format(print_context->output, "%sglobal%s::", COL(COL_TREE), COL(COL_DELIM));
            }

            for (int64_t i = 0, count = arr_count(node->nameref.pieces); i < count; i++) {
                if (i > 0) {
                    string_append_format(print_context->output, "%s::", COL(COL_DELIM));
                }

                string_append_format(print_context->output, "%s%.*s", COL(COL_NAME), STR_EXPAND(node->nameref.pieces[i].string_value));
            }

            if (0 != arr_count(node->nameref.template_arguments)) {
                string_append_format(print_context->output, "%s<", COL(COL_DELIM));
                for (int64_t i = 0, count = arr_count(node->nameref.template_arguments); i < count; i++) {
                    if (i > 0) {
                        string_append_format(print_context->output, "%s, ", COL(COL_DELIM));
                    }

                    laye_template_arg template_argument = node->nameref.template_arguments[i];
                    if (template_argument.is_type) {
                        laye_type_print_to_string(template_argument.type, print_context->output, use_color);
                    } else if (template_argument.node->kind == LAYE_NODE_EVALUATED_CONSTANT) {
                        laye_constant_print_to_string(template_argument.node->evaluated_constant.result, print_context->output, use_color);
                    } else {
                        string_append_format(print_context->output, "%s{?}", COL(COL_ERROR));
                    }
                }

                string_append_format(print_context->output, "%s>", COL(COL_DELIM));
            }

            if (node->nameref.referenced_declaration != NULL) {
                string_append_format(print_context->output, " %s%016llX", COL(COL_ADDR), (size_t)node->nameref.referenced_declaration);
            }
        } break;

        case LAYE_NODE_LITINT: {
            string_append_format(print_context->output, " %s%lld", COL(COL_CONST), node->litint.value);
        } break;

        case LAYE_NODE_LITBOOL: {
            string_append_format(print_context->output, " %s%s", COL(COL_CONST), node->litbool.value ? "true" : "false");
        } break;

        case LAYE_NODE_LITSTRING: {
            layec_source source = layec_context_get_source(print_context->context, node->location.sourceid);
            string_append_format(print_context->output, " %s%.*s", COL(COL_CONST), (int)node->location.length, source.text.data + node->location.offset);
        } break;
    }

    string_append_format(print_context->output, "%s\n", COL(RESET));

    if (children != NULL) {
        laye_node_debug_print_children(print_context, children);
        arr_free(children);
    }
}

#define COL_KEYWORD        MAGENTA
#define COL_TEMPLATE_PARAM YELLOW
#define COL_UNREAL         RED

void laye_constant_print_to_string(layec_evaluated_constant constant, string* s, bool use_color) {
    assert(s != NULL);

    switch (constant.kind) {
        default: assert(false && "unreachable"); break;

        case LAYEC_EVAL_NULL: {
            string_append_format(s, "%snil", COL(COL_CONST));
        } break;

        case LAYEC_EVAL_VOID: {
            string_append_format(s, "%svoid", COL(COL_CONST));
        } break;

        case LAYEC_EVAL_BOOL: {
            if (constant.bool_value) {
                string_append_format(s, "%strue", COL(COL_CONST));
            } else {
                string_append_format(s, "%sfalse", COL(COL_CONST));
            }
        } break;

        case LAYEC_EVAL_INT: {
            string_append_format(s, "%s%lld", COL(COL_CONST), constant.int_value);
        } break;

        case LAYEC_EVAL_FLOAT: {
            string_append_format(s, "%s%f", COL(COL_CONST), constant.float_value);
        } break;

        case LAYEC_EVAL_STRING: {
            string_append_format(s, "%s\"%.*s\"", COL(COL_CONST), STR_EXPAND(constant.string_value));
        } break;
    }
}

void laye_type_print_to_string(laye_type type, string* s, bool use_color) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    assert(s != NULL);

    switch (type.node->kind) {
        default: {
            fprintf(stderr, "unimplemented type kind %s\n", laye_node_kind_to_cstring(type.node->kind));
            assert(false && "unreachable");
        } break;

        case LAYE_NODE_TYPE_POISON: {
            string_append_format(s, "%spoison", COL(COL_UNREAL));
        } break;

        case LAYE_NODE_TYPE_UNKNOWN: {
            string_append_format(s, "%sunknown", COL(COL_UNREAL));
        } break;

        case LAYE_NODE_TYPE_VAR: {
            string_append_format(s, "%svar", COL(COL_UNREAL));
        } break;

        case LAYE_NODE_TYPE_TYPE: {
            string_append_format(s, "%stype", COL(COL_KEYWORD));
        } break;

        case LAYE_NODE_TYPE_VOID: {
            string_append_format(s, "%svoid", COL(COL_KEYWORD));
        } break;

        case LAYE_NODE_TYPE_NORETURN: {
            string_append_format(s, "%snoreturn", COL(COL_KEYWORD));
        } break;

        case LAYE_NODE_TYPE_BOOL: {
            string_append_format(s, "%sbool", COL(COL_KEYWORD));
        } break;

        case LAYE_NODE_TYPE_INT: {
            if (type.node->type_primitive.is_platform_specified) {
                string_append_format(s, "%s%sint", COL(COL_KEYWORD), (type.node->type_primitive.is_signed ? "" : "u"));
            } else {
                string_append_format(s, "%s%s%d", COL(COL_KEYWORD), (type.node->type_primitive.is_signed ? "i" : "u"), type.node->type_primitive.bit_width);
            }
        } break;

        case LAYE_NODE_TYPE_FLOAT: {
            if (type.node->type_primitive.is_platform_specified) {
                string_append_format(s, "%sfloat", COL(COL_KEYWORD));
            } else {
                string_append_format(s, "%sf%d", COL(COL_KEYWORD), type.node->type_primitive.bit_width);
            }
        } break;

        case LAYE_NODE_TYPE_TEMPLATE_PARAMETER: {
            assert(type.node->type_template_parameter.declaration != NULL);
            string_append_format(s, "%s%.*s", COL(COL_TEMPLATE_PARAM), STR_EXPAND(type.node->type_template_parameter.declaration->declared_name));
        } break;

        case LAYE_NODE_TYPE_ERROR_PAIR: {
            assert(type.node->type_error_pair.error_type.node != NULL);
            assert(type.node->type_error_pair.value_type.node != NULL);
            laye_type_print_to_string(type.node->type_error_pair.error_type, s, use_color);
            string_append_format(s, "%s!", COL(COL_DELIM));
            laye_type_print_to_string(type.node->type_error_pair.value_type, s, use_color);
        } break;

        case LAYE_NODE_TYPE_NAMEREF: {
            if (type.node->nameref.kind == LAYE_NAMEREF_HEADLESS) {
                string_append_format(s, "%s::", COL(COL_DELIM));
            } else if (type.node->nameref.kind == LAYE_NAMEREF_GLOBAL) {
                string_append_format(s, "%sglobal%s::", COL(COL_KEYWORD), COL(COL_DELIM));
            }

            for (int64_t i = 0, count = arr_count(type.node->nameref.pieces); i < count; i++) {
                if (i > 0) {
                    string_append_format(s, "%s::", COL(COL_DELIM));
                }

                string_append_format(s, "%s%.*s", COL(COL_NAME), STR_EXPAND(type.node->nameref.pieces[i].string_value));
            }

            if (0 != arr_count(type.node->nameref.template_arguments)) {
                string_append_format(s, "%s<", COL(COL_DELIM));
                for (int64_t i = 0, count = arr_count(type.node->nameref.template_arguments); i < count; i++) {
                    if (i > 0) {
                        string_append_format(s, "%s, ", COL(COL_DELIM));
                    }

                    laye_template_arg template_argument = type.node->nameref.template_arguments[i];
                    if (template_argument.is_type) {
                        laye_type_print_to_string(template_argument.type, s, use_color);
                    } else if (template_argument.node->kind == LAYE_NODE_EVALUATED_CONSTANT) {
                        laye_constant_print_to_string(template_argument.node->evaluated_constant.result, s, use_color);
                    } else {
                        string_append_format(s, "%s{?}", COL(COL_ERROR));
                    }
                }

                string_append_format(s, "%s>", COL(COL_DELIM));
            }

            if (type.node->nameref.referenced_type != NULL) {
                string_append_format(s, " %s%016llX", COL(COL_ADDR), (size_t)type.node->nameref.referenced_type);
            }
        } break;

        case LAYE_NODE_TYPE_FUNCTION: {
            laye_type_print_to_string(type.node->type_function.return_type, s, use_color);

            string_append_format(s, "%s(", COL(COL_DELIM));
            for (int64_t i = 0, count = arr_count(type.node->type_function.parameter_types); i < count; i++) {
                if (i > 0) {
                    string_append_format(s, "%s, ", COL(COL_DELIM));
                }

                laye_type_print_to_string(type.node->type_function.parameter_types[i], s, use_color);
            }

            string_append_format(s, "%s)", COL(COL_DELIM));
        } break;

        case LAYE_NODE_TYPE_REFERENCE: {
            laye_type_print_to_string(type.node->type_container.element_type, s, use_color);
            string_append_format(s, "%s&", COL(COL_DELIM));
        } break;

        case LAYE_NODE_TYPE_POINTER: {
            laye_type_print_to_string(type.node->type_container.element_type, s, use_color);
            string_append_format(s, "%s*", COL(COL_DELIM));
        } break;

        case LAYE_NODE_TYPE_BUFFER: {
            laye_type_print_to_string(type.node->type_container.element_type, s, use_color);
            string_append_format(s, "%s[*]", COL(COL_DELIM));
        } break;

        case LAYE_NODE_TYPE_SLICE: {
            laye_type_print_to_string(type.node->type_container.element_type, s, use_color);
            string_append_format(s, "%s[]", COL(COL_DELIM));
        } break;

        case LAYE_NODE_TYPE_ARRAY: {
            laye_type_print_to_string(type.node->type_container.element_type, s, use_color);
            string_append_format(s, "%s[", COL(COL_DELIM));
            for (int i = 0, count = arr_count(type.node->type_container.length_values); i < count; i++) {
                laye_node* length_value = type.node->type_container.length_values[i];
                assert(length_value != NULL);

                if (length_value->kind == LAYE_NODE_EVALUATED_CONSTANT) {
                    layec_evaluated_constant constant = length_value->evaluated_constant.result;
                    switch (constant.kind) {
                        default: {
                            // fprintf(stderr, "unimplemented evaluated constant kind %s\n", laye_node_kind_to_cstring(type.node->kind));
                            assert(false && "unreachable");
                        } break;

                        case LAYEC_EVAL_INT: {
                            string_append_format(s, "%s%lld", COL(COL_CONST), constant.int_value);
                        } break;

                        case LAYEC_EVAL_FLOAT: {
                            string_append_format(s, "%s%f", COL(COL_CONST), constant.float_value);
                        } break;
                    }
                } else if (length_value->kind == LAYE_NODE_LITINT) {
                    string_append_format(s, "%s%lld", COL(COL_CONST), length_value->litint.value);
                } else {
                    string_append_format(s, "%s<expr>", COL(COL_CONST), length_value->litint.value);
                }
            }
            string_append_format(s, "%s]", COL(COL_DELIM));
        } break;

        case LAYE_NODE_TYPE_STRUCT: {
            string_append_format(s, "%s%.*s", COL(COL_NAME), STR_EXPAND(type.node->type_struct.name));
        } break;
    }

    if (type.is_modifiable) {
        string_append_format(s, " %smut", COL(COL_KEYWORD));
    }

    string_append_format(s, "%s", COL(RESET));
}
