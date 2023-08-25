#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "layec/context.h"
#include "layec/source.h"
#include "layec/string.h"
#include "layec/vector.h"

#include "layec/c/lexer.h"

#include "layec/laye/lexer.h"

const char* usage_text = "Usage: layec [options] file...\n";

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        printf("%s", usage_text);
        return 0;
    }

    const char* file_path = argv[1];
    layec_string_view file_path_view = layec_string_view_create(file_path, (long long)strlen(file_path));

    layec_context* context = layec_context_create();
    assert(context > 0);

    int source_id = layec_context_get_or_add_source_buffer_from_file(context, file_path);
    if (source_id <= 0)
    {
        layec_context_destroy(context);
        return 1;
    }

    layec_source_buffer source_buffer = layec_context_get_source_buffer(context, source_id);
    assert(source_buffer.name == file_path);
    assert(source_buffer.text);
    
    if (layec_string_view_ends_with_cstring(file_path_view, ".c"))
    {
        layec_c_token_buffer token_buffer = layec_c_get_tokens(context, source_id);

        for (long long i = 0; i < vector_count(token_buffer.semantic_tokens); i++)
        {
            layec_c_token token = token_buffer.semantic_tokens[i];
            layec_location_print(context, token.location);
            printf(": ");
            layec_c_token_print(context, token);
            printf("\n");
        }

        layec_c_token_buffer_destroy(&token_buffer);
    }
    else if (layec_string_view_ends_with_cstring(file_path_view, ".laye"))
    {
        layec_laye_token_buffer token_buffer = layec_laye_get_tokens(context, source_id);

        for (long long i = 0; i < vector_count(token_buffer.tokens); i++)
        {
            layec_laye_token token = token_buffer.tokens[i];
            layec_location_print(context, token.location);
            printf(": ");
            layec_laye_token_print(context, token);
            printf("\n");
        }

        layec_laye_token_buffer_destroy(&token_buffer);
    }

    layec_context_destroy(context);
    return 0;
}
