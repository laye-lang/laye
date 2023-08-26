#ifndef LAYEC_C_LEXER_H
#define LAYEC_C_LEXER_H

#include "layec/context.h"
#include "layec/c/token.h"
#include "layec/c/translation_unit.h"

/// Return a buffer of tokens as lexed from the input file.
layec_c_token_buffer layec_c_get_tokens(layec_context* context, layec_c_translation_unit* tu, int source_id);

#endif // LAYEC_C_LEXER_H
