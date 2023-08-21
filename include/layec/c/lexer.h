#ifndef LAYEC_C_LEXER_H
#define LAYEC_C_LEXER_H

#include "layec/context.h"
#include "layec/c/token.h"

typedef struct layec_c_token_buffer layec_c_token_buffer;

struct layec_c_token_buffer
{
    layec_c_token* tokens;
    long long capacity;
    long long count;
};

/// Return a buffer of tokens as lexed from the input file.
layec_c_token_buffer layec_c_get_tokens(layec_context* context, int source_id);

#endif // LAYEC_C_LEXER_H
