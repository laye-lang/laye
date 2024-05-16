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
#include "lexer.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

typedef struct ccly_parser {
    c_context* context;
    c_translation_unit* tu;

    int64_t token_position;
} ccly_parser;

c_token ccly_parser_peek(ccly_parser* p, int64_t offset) {
    lca_da(c_token) tokens = p->tu->token_buffer.semantic_tokens;
    int64_t token_index = p->token_position + offset;
    return tokens[token_index];
}

c_token ccly_parser_current(ccly_parser* p) {
    return ccly_parser_peek(p, 0);
}

c_translation_unit* ccly_parse(c_context* context, lyir_sourceid sourceid) {
    assert(context != NULL);
    assert(sourceid >= 0);

    c_translation_unit* tu = lca_allocate(context->allocator, sizeof *tu);
    assert(tu != NULL);
    tu->context = context;
    tu->sourceid = sourceid;
    tu->arena = lca_arena_create(context->allocator, 1024 * 1024);

    tu->token_buffer = c_get_tokens(context, tu, sourceid);

    ccly_parser p = {
        .context = context,
        .tu = tu,
    };

    return tu;
}
