#ifndef LAYEC_LAYE_LEXER_H
#define LAYEC_LAYE_LEXER_H

#include "layec/context.h"
#include "layec/laye/token.h"

layec_laye_token_buffer layec_laye_get_tokens(layec_context* context, int source_id);

#endif // LAYEC_LAYE_LEXER_H
