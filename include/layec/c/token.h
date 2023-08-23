#ifndef LAYEC_C_TOKEN_H
#define LAYEC_C_TOKEN_H

#include <stdbool.h>

#include "layec/context.h"
#include "layec/source.h"
#include "layec/util.h"

typedef struct layec_c_token layec_c_token;

typedef enum layec_c_token_kind
{
    LAYEC_CTK_INVALID = 0,
    LAYEC_CTK_EOF,

    LAYEC_CTK_OPEN_PAREN = '(',
    LAYEC_CTK_CLOSE_PAREN = ')',

    LAYEC_CTK_OPEN_BRACE = '{',
    LAYEC_CTK_CLOSE_BRACE = '}',

    LAYEC_CTK_MULTI_BYTE_START = 255,

#define CTK(N, ...) LAYEC_CTK_ ## N,
#include "layec/c/tokens.def"
#undef CTK

    LAYEC_CTK_COUNT,
} layec_c_token_kind;

struct layec_c_token
{
    layec_c_token_kind kind;
    layec_location location;

    bool is_macro_param;
    long long macro_param_index;
    
    bool is_angle_string;

    layec_string_view string_value;
    long long int_value;
    double real_value;
};

const char* layec_c_token_kind_to_string(layec_c_token_kind kind);

void layec_c_token_print(layec_context* context, layec_c_token token);

/// Return a view into the original source text at the location of this token.
/// This is the literal text of the token as it appears in the source, not a processed version.
layec_string_view layec_c_token_get_source_image(layec_context* context, layec_c_token token);

#endif // LAYEC_C_TOKEN_H
