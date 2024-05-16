/*
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2023 Local Atticus
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "ccly.h"

#include <assert.h>

#define COL_COMMENT BRIGHT_BLACK
#define COL_DELIM   WHITE
#define COL_TREE    RED
#define COL_NODE    RED
#define COL_ADDR    BLUE
#define COL_OFFS    MAGENTA
#define COL_NAME    GREEN
#define COL_ERROR   RED
#define COL_CONST   BLUE

typedef struct c_print_context {
    c_context* context;
    c_translation_unit* tu;
    bool use_color;
    lca_string* indents;
    lca_string* output;
} c_print_context;

lca_string c_translation_unit_debug_print(c_translation_unit* tu) {
    assert(tu != NULL);
    assert(tu->context != NULL);

    lca_string output_string = lca_string_create(tu->context->allocator);

    int64_t indents_string_capacity = 256;
    char* indents_string_data = lca_allocate(tu->context->allocator, indents_string_capacity);
    assert(indents_string_data != NULL);
    lca_string indents_string = lca_string_from_data(tu->context->allocator, indents_string_data, 0, indents_string_capacity);

    c_print_context print_context = {
        .context = tu->context,
        .tu = tu,
        .use_color = tu->context->use_color,
        .indents = &indents_string,
        .output = &output_string,
    };

    bool use_color = print_context.use_color;
    lca_string_append_format(print_context.output, "%s; %.*s%s\n", COL(COL_COMMENT), LCA_STR_EXPAND(lyir_context_get_source(tu->context->lyir_context, tu->sourceid).name), COL(RESET));

    for (int64_t i = 0; i < lca_da_count(tu->token_buffer.semantic_tokens); i++) {
        c_token token = tu->token_buffer.semantic_tokens[i];
        lyir_source source = lyir_context_get_source(tu->context->lyir_context, token.location.sourceid);
        lca_string_append_format(
            print_context.output,
            "%s :: %.*s\n",
            c_token_kind_to_cstring(token.kind),
            (int)token.location.length,
            source.text.data + token.location.offset
        );
    }

    lca_string_destroy(&indents_string);

    return output_string;
}
