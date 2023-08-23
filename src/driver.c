#include <assert.h>
#include <stdio.h>

#include "layec/context.h"
#include "layec/source.h"
#include "layec/util.h"

#include "layec/c/lexer.h"

const char* usage_text = "Usage: layec [options] file...\n";

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        printf("%s", usage_text);
        return 0;
    }

    const char* file_path = argv[1];

    layec_context* context = layec_context_create();
    assert(context > 0);

    int source_id = layec_context_get_or_add_source_buffer_from_file(context, file_path);
    if (source_id <= 0)
        return 1;
        
    layec_c_token_buffer token_buffer = layec_c_get_tokens(context, source_id);

    layec_source_buffer source_buffer = layec_context_get_source_buffer(context, source_id);
    assert(source_buffer.name == file_path);
    assert(source_buffer.text);

    for (long long i = 0; i < vector_count(token_buffer.tokens); i++)
    {
        layec_c_token token = token_buffer.tokens[i];
        layec_location_print(context, token.location);
        printf(": ");
        layec_c_token_print(context, token);
        printf("\n");
    }

    return 0;
}
