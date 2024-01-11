#include <assert.h>

#include "layec.h"
#include "laye.h"

#define COL_DELIM WHITE
#define COL_TREE RED
#define COL_NODE RED
#define COL_ADDR BLUE
#define COL_OFFS MAGENTA
#define COL_NAME GREEN
#define COL_ERROR RED
#define COL_CONST BLUE

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
            lca_string_append_format(print_context->output, " FOREIGN \"%s\"", STR_EXPAND(node->attributes.foreign_name));
        }
    }

    if (node->declared_type != NULL) {
        lca_string_append_format(print_context->output, " ");
        laye_type_print_to_string(node->declared_type, print_context->output, use_color);
    } else if (node->type != NULL) {
        lca_string_append_format(print_context->output, " ");
        laye_type_print_to_string(node->type, print_context->output, use_color);
    }

    dynarr(laye_node*) children = NULL;

    switch (node->kind) {
        default: break;

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

        case LAYE_NODE_RETURN: {
            if (node->_return.value != NULL) {
                arr_push(children, node->_return.value);
            }
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

                    laye_node* template_argument = node->nameref.template_arguments[i];
                    assert(template_argument != NULL);

                    if (laye_node_is_type(template_argument)) {
                        laye_type_print_to_string(template_argument, print_context->output, use_color);
                    } else if (template_argument->kind == LAYE_NODE_EVALUATED_CONSTANT) {
                        laye_constant_print_to_string(template_argument->evaluated_constant.result, print_context->output, use_color);
                    } else {
                        string_append_format(print_context->output, "%s{?}", COL(COL_ERROR));
                    }
                }

                string_append_format(print_context->output, "%s>", COL(COL_DELIM));
            }

            if (node->nameref.referenced_declaration != NULL) {
                string_append_format(print_context->output, " %s%016x", COL(COL_ADDR), (size_t)node->nameref.referenced_declaration);
            }
        } break;

        case LAYE_NODE_LITINT: {
            string_append_format(print_context->output, " %s%lld", COL(COL_CONST), node->litint.value);
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

#define COL_KEYWORD MAGENTA
#define COL_TEMPLATE_PARAM YELLOW
#define COL_UNREAL RED

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

void laye_type_print_to_string(laye_node* type, string* s, bool use_color) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    assert(s != NULL);

    switch (type->kind) {
        default: {
            fprintf(stderr, "unimplemented type kind %s\n", laye_node_kind_to_cstring(type->kind));
            assert(false && "unreachable");
        } break;

        case LAYE_NODE_TYPE_POISON: {
            string_append_format(s, "%spoison", COL(COL_UNREAL));
        } break;

        case LAYE_NODE_TYPE_UNKNOWN: {
            string_append_format(s, "%sunknown", COL(COL_UNREAL));
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
            if (type->type_primitive.is_platform_specified) {
                string_append_format(s, "%s%sint", COL(COL_KEYWORD), (type->type_primitive.is_signed ? "" : "u"));
            } else { 
                string_append_format(s, "%s%s%d", COL(COL_KEYWORD), (type->type_primitive.is_signed ? "i" : "u"), type->type_primitive.bit_width);
            }
        } break;

        case LAYE_NODE_TYPE_FLOAT: {
            if (type->type_primitive.is_platform_specified) {
                string_append_format(s, "%sfloat", COL(COL_KEYWORD));
            } else {
                string_append_format(s, "%sf%d", COL(COL_KEYWORD), type->type_primitive.bit_width);
            }
        } break;

        case LAYE_NODE_TYPE_TEMPLATE_PARAMETER: {
            assert(type->type_template_parameter.declaration != NULL);
            string_append_format(s, "%s%.*s", COL(COL_TEMPLATE_PARAM), STR_EXPAND(type->type_template_parameter.declaration->declared_name));
        } break;

        case LAYE_NODE_TYPE_ERROR_PAIR: {
            assert(type->type_error_pair.error_type != NULL);
            assert(type->type_error_pair.value_type != NULL);
            laye_type_print_to_string(type->type_error_pair.error_type, s, use_color);
            string_append_format(s, "%s!", COL(COL_DELIM));
            laye_type_print_to_string(type->type_error_pair.value_type, s, use_color);
        } break;

        case LAYE_NODE_TYPE_NAMEREF: {
            if (type->nameref.kind == LAYE_NAMEREF_HEADLESS) {
                string_append_format(s, "%s::", COL(COL_DELIM));
            } else if (type->nameref.kind == LAYE_NAMEREF_GLOBAL) {
                string_append_format(s, "%sglobal%s::", COL(COL_KEYWORD), COL(COL_DELIM));
            }

            for (int64_t i = 0, count = arr_count(type->nameref.pieces); i < count; i++) {
                if (i > 0) {
                    string_append_format(s, "%s::", COL(COL_DELIM));
                }

                string_append_format(s, "%s%.*s", COL(COL_NAME), STR_EXPAND(type->nameref.pieces[i].string_value));
            }

            if (0 != arr_count(type->nameref.template_arguments)) {
                string_append_format(s, "%s<", COL(COL_DELIM));
                for (int64_t i = 0, count = arr_count(type->nameref.template_arguments); i < count; i++) {
                    if (i > 0) {
                        string_append_format(s, "%s, ", COL(COL_DELIM));
                    }

                    laye_node* template_argument = type->nameref.template_arguments[i];
                    assert(template_argument != NULL);

                    if (laye_node_is_type(template_argument)) {
                        laye_type_print_to_string(template_argument, s, use_color);
                    } else if (template_argument->kind == LAYE_NODE_EVALUATED_CONSTANT) {
                        laye_constant_print_to_string(template_argument->evaluated_constant.result, s, use_color);
                    } else {
                        string_append_format(s, "%s{?}", COL(COL_ERROR));
                    }
                }

                string_append_format(s, "%s>", COL(COL_DELIM));
            }

            if (type->nameref.referenced_type != NULL) {
                string_append_format(s, " %s%016x", COL(COL_ADDR), (size_t)type->nameref.referenced_type);
            }
        } break;

        case LAYE_NODE_TYPE_FUNCTION: {
            laye_type_print_to_string(type->type_function.return_type, s, use_color);

            if (type->type_is_modifiable) {
                string_append_format(s, " %smut", COL(COL_KEYWORD));
            }

            string_append_format(s, "%s(", COL(COL_DELIM));
            for (int64_t i = 0, count = arr_count(type->type_function.parameter_types); i < count; i++) {
                if (i > 0) {
                    string_append_format(s, "%s, ", COL(COL_DELIM));
                }

                laye_type_print_to_string(type->type_function.parameter_types[i], s, use_color);
            }

            string_append_format(s, "%s)", COL(COL_DELIM));
        } break;

        case LAYE_NODE_TYPE_REFERENCE: {
            laye_type_print_to_string(type->type_container.element_type, s, use_color);
            string_append_format(s, "%s&", COL(COL_DELIM));
        } break;

        case LAYE_NODE_TYPE_POINTER: {
            laye_type_print_to_string(type->type_container.element_type, s, use_color);
            string_append_format(s, "%s*", COL(COL_DELIM));
        } break;

        case LAYE_NODE_TYPE_BUFFER: {
            laye_type_print_to_string(type->type_container.element_type, s, use_color);
            string_append_format(s, "%s[*]", COL(COL_DELIM));
        } break;

        case LAYE_NODE_TYPE_SLICE: {
            laye_type_print_to_string(type->type_container.element_type, s, use_color);
            string_append_format(s, "%s[]", COL(COL_DELIM));
        } break;
    }

    if (type->type_is_modifiable) {
        string_append_format(s, " %smut", COL(COL_KEYWORD));
    }

    string_append_format(s, "%s", COL(RESET));
}
