#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "layec/string.h"
#include "layec/laye/lexer.h"

typedef struct layec_laye_lexer layec_laye_lexer;

struct layec_laye_lexer
{
    layec_context* context;
    int source_id;
    layec_source_buffer source_buffer;
};

layec_laye_token_buffer layec_laye_get_tokens(layec_context* context, int source_id)
{
    layec_laye_lexer lexer =
    {
        .context = context,
        .source_id = source_id,
    };

    lexer.source_buffer = layec_context_get_source_buffer(context, source_id);

    layec_laye_token_buffer token_buffer = {0};

    return token_buffer;
}
