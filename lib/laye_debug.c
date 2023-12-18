#include "layec.h"

#include <assert.h>

#define COL_TREE RED
#define COL_NODE RED
#define COL_ADDR BLUE
#define COL_OFFS MAGENTA

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

static void laye_node_debug_print(laye_print_context* print_context, laye_node* node) {
    assert(print_context != NULL);
    assert(print_context->context != NULL);
    assert(print_context->module != NULL);
    assert(print_context->indents != NULL);
    assert(print_context->output != NULL);
    assert(node != NULL);

    bool use_color = print_context->use_color;

    lca_string_append_format(
        print_context->output,
        "%s%s %s%016llX %s<%lld>",
        COL(COL_NODE),
        laye_node_kind_to_cstring(node->kind),
        COL(COL_ADDR),
        (size_t)node,
        COL(COL_OFFS),
        node->location.offset
    );

    if (node->type != NULL) {
        lca_string_append_format(print_context->output, " ");
        laye_type_print_to_string(node->type, print_context->output, use_color);
    }

    lca_string_append_format(print_context->output, "%s\n", COL(RESET));
}

#define COL_DELIM WHITE
#define COL_KEYWORD MAGENTA

void laye_type_print_to_string(laye_node* type, string* s, bool use_color) {
    assert(type != NULL);
    assert(laye_node_is_type(type));
    assert(s != NULL);

    switch (type->kind) {
        default: {
            assert(false && "unreachable");
        } break;

        case LAYE_NODE_TYPE_NORETURN: {
            string_append_format(s, "%snoreturn", COL(COL_KEYWORD));
        } break;

        case LAYE_NODE_TYPE_INT: {
            if (type->type_primitive.is_platform_specified) {
                string_append_format(s, "%s%sint", COL(COL_KEYWORD), (type->type_primitive.is_signed ? "" : "u"));
            } else {
                string_append_format(s, "%s%s%d", COL(COL_KEYWORD), (type->type_primitive.is_signed ? "i" : "u"), type->type_primitive.bit_width);
            }
        } break;

        case LAYE_NODE_TYPE_FUNCTION: {
            laye_type_print_to_string(type->type_function.return_type, s, use_color);

            string_append_format(s, "%s(", COL(COL_DELIM));
            for (int i = 0, count = arr_count(type->type_function.parameter_types); i < count; i++) {
                if (i > 0) {
                    string_append_format(s, "%s, ", COL(COL_DELIM));
                }

                laye_type_print_to_string(type->type_function.parameter_types[i], s, use_color);
            }

            string_append_format(s, "%s)", COL(COL_DELIM));
        } break;
    }

    string_append_format(s, "%s", COL(RESET));
}
