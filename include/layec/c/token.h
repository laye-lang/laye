#ifndef LAYEC_C_TOKEN_H
#define LAYEC_C_TOKEN_H

#include "layec/context.h"
#include "layec/source.h"
#include "layec/util.h"

typedef struct layec_c_token layec_c_token;

struct layec_c_token
{
    int kind;
    layec_location location;

    layec_string_view string_value;
    long long int_value;
    double real_value;
};

/// Return a view into the original source text at the location of this token.
/// This is the literal text of the token as it appears in the source, not a processed version.
layec_string_view layec_c_token_get_source_image(layec_context* context, layec_c_token token);

#endif // LAYEC_C_TOKEN_H
