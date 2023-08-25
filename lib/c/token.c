#include <assert.h>

#include "layec/c/token.h"

const char* layec_c_token_kind_to_string(layec_c_token_kind kind)
{
    switch (kind)
    {
        default:
        {
            if (kind < LAYEC_CTK_MULTI_BYTE_START)
            {
                static char single_byte_tokens[256 * 2] = {0};
                if (single_byte_tokens[kind * 2] == 0) single_byte_tokens[kind * 2] = (char)kind;
                return &single_byte_tokens[kind * 2];
            }

            return "<unknown C token>";
        }

        case LAYEC_CTK_INVALID: return "<invalid C token>";
        case LAYEC_CTK_EOF: return "<eof>";

#define CTK(N, ...) case LAYEC_CTK_ ## N: return #N;
#include "layec/c/tokens.def"
#undef CTK
    }
}

void layec_c_token_print(layec_context* context, layec_c_token token)
{
    assert(context);
    assert(token.location.source_id);

    const char* kind_name = layec_c_token_kind_to_string(token.kind);
    if (token.kind == LAYEC_CTK_INVALID)
        printf("%s", kind_name);
    else
    {
        layec_string_view token_image = layec_location_get_source_image(context, token.location);
        printf("%s  ::  `%.*s`", kind_name, (int)token_image.length, token_image.data);
    }
}

void layec_c_token_buffer_destroy(layec_c_token_buffer* token_buffer)
{
    vector_free(token_buffer->semantic_tokens);
}
