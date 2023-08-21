#include <assert.h>

#include "layec/c/lexer.h"

typedef struct layec_c_lexer layec_c_lexer;

struct layec_c_lexer
{
    layec_context* context;
    int source_id;
    layec_source_buffer source_buffer;
};

layec_c_token_buffer layec_c_get_tokens(layec_context* context, int source_id)
{
    assert(context);

    layec_c_lexer lexer = {
        .context = context,
        .source_id = source_id,
    };

    lexer.source_buffer = layec_context_get_source_buffer(context, source_id);
    assert(lexer.source_buffer.text);

    layec_c_token_buffer tokens = {0};

    return tokens;
}
