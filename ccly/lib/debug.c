#include "ccly.h"

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

typedef struct c_print_context {
    c_context* context;
    c_translation_unit* tu;
    bool use_color;
    lca_string* indents;
    lca_string* output;
} c_print_context;

lca_string c_translation_unit_debug_print(c_translation_unit* tu) {
    assert(tu != NULL);
    assert(tu->context != NULL);

    lca_string output_string = lca_string_create(tu->context->allocator);

    int64_t indents_string_capacity = 256;
    char* indents_string_data = lca_allocate(tu->context->allocator, indents_string_capacity);
    assert(indents_string_data != NULL);
    lca_string indents_string = lca_string_from_data(tu->context->allocator, indents_string_data, 0, indents_string_capacity);

    c_print_context print_context = {
        .context = tu->context,
        .tu = tu,
        .use_color = tu->context->use_color,
        .indents = &indents_string,
        .output = &output_string,
    };

    bool use_color = print_context.use_color;
    lca_string_append_format(print_context.output, "%s; %.*s%s\n", COL(COL_COMMENT), LCA_STR_EXPAND(lyir_context_get_source(tu->context->lyir_context, tu->sourceid).name), COL(RESET));

    for (int64_t i = 0; i < lca_da_count(tu->token_buffer.semantic_tokens); i++) {
        c_token token = tu->token_buffer.semantic_tokens[i];
        lyir_source source = lyir_context_get_source(tu->context->lyir_context, token.location.sourceid);
        lca_string_append_format(
            print_context.output,
            "%s :: %.*s\n",
            c_token_kind_to_cstring(token.kind),
            (int)token.location.length,
            source.text.data + token.location.offset
        );
    }

    lca_string_destroy(&indents_string);

    return output_string;
}
