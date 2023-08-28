#ifndef LAYEC_LAYE_TOKEN_H
#define LAYEC_LAYE_TOKEN_H

#include "layec/context.h"
#include "layec/string.h"
#include "layec/vector.h"

typedef struct layec_laye_token layec_laye_token;
typedef struct layec_laye_token_buffer layec_laye_token_buffer;

typedef enum layec_laye_token_kind
{
    LAYEC_LTK_INVALID,
    LAYEC_LTK_EOF,

    LAYEC_LTK_MULTI_BYTE_START = 255,

#define LTK(N, ...) LAYEC_LTK_ ## N,
#include "layec/laye/tokens.def"
#undef LTK

    LAYEC_LTK_COUNT,
} layec_laye_token_kind;

struct layec_laye_token
{
    layec_laye_token_kind kind;
    layec_location location;
    
    layec_string_view string_value;
    long long int_value;
    double real_value;
};

struct layec_laye_token_buffer
{
    vector(layec_laye_token) tokens;
};

const char* layec_laye_token_kind_to_string(layec_laye_token_kind kind);
void layec_laye_token_print(layec_context* context, layec_laye_token token);

void layec_laye_token_buffer_destroy(layec_laye_token_buffer* token_buffer);

#endif // LAYEC_LAYE_TOKEN_H
