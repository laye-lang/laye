#include <assert.h>

#include "layec/string.h"
#include "layec/laye/token.h"

const char* layec_laye_token_kind_to_string(layec_laye_token_kind kind)
{
    switch (kind)
    {
        default:
        {
            if (kind < LAYEC_LTK_MULTI_BYTE_START)
            {
                static char single_byte_tokens[256 * 2] = {0};
                if (single_byte_tokens[kind * 2] == 0) single_byte_tokens[kind * 2] = (char)kind;
                return &single_byte_tokens[kind * 2];
            }

            return "<unknown Laye token>";
        }

        case LAYEC_LTK_INVALID: return "<invalid Laye token>";
        case LAYEC_LTK_EOF: return "<eof>";

#define LTK(N, ...) case LAYEC_LTK_ ## N: return #N;
#include "layec/laye/tokens.def"
#undef LTK
    }
}

void layec_laye_token_print(layec_context* context, layec_laye_token token)
{
    assert(context);
    assert(token.location.source_id);

    const char* kind_name = layec_laye_token_kind_to_string(token.kind);
    if (token.kind == LAYEC_LTK_INVALID)
        printf("%s", kind_name);
    else
    {
        layec_string_view token_image = layec_location_get_source_image(context, token.location);
        printf("%s  ::  `%.*s`", kind_name, (int)token_image.length, token_image.data);
    }
}

void layec_laye_token_buffer_destroy(layec_laye_token_buffer* token_buffer)
{
    vector_free(token_buffer->tokens);
}
