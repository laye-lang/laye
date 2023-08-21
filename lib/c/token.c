#include <assert.h>

#include "layec/c/token.h"

layec_string_view layec_c_token_get_source_image(layec_context* context, layec_c_token token)
{
    assert(context);
    assert(token.location.source_id);

    layec_source_buffer source_buffer = layec_context_get_source_buffer(context, token.location.source_id);
    assert(source_buffer.text);

    return layec_string_view_create(source_buffer.text + token.location.offset, token.location.length);
}
