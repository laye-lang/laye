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

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

#include "lyir.h"
#include "laye.h"

laye_context* laye_context_create(lyir_context* lyir_context) {
    assert(lyir_context != NULL);

    lca_allocator allocator = lyir_context->allocator;

    laye_context* context = lca_allocate(allocator, sizeof *context);
    assert(context != NULL);
    context->lyir_context = lyir_context;
    context->allocator = allocator;

    context->laye_types.type = laye_node_create_in_context(context, LAYE_NODE_TYPE_TYPE, (laye_type){0});
    assert(context->laye_types.type != NULL);
    context->laye_types.type->type = LTY(context->laye_types.type);
    context->laye_types.type->sema_state = LYIR_SEMA_OK;

    context->laye_types.poison = laye_node_create_in_context(context, LAYE_NODE_TYPE_POISON, LTY(context->laye_types.type));
    assert(context->laye_types.poison != NULL);
    context->laye_types.poison->sema_state = LYIR_SEMA_OK;

    context->laye_types.unknown = laye_node_create_in_context(context, LAYE_NODE_TYPE_UNKNOWN, LTY(context->laye_types.type));
    assert(context->laye_types.unknown != NULL);
    context->laye_types.unknown->sema_state = LYIR_SEMA_OK;

    context->laye_types.var = laye_node_create_in_context(context, LAYE_NODE_TYPE_VAR, LTY(context->laye_types.type));
    assert(context->laye_types.var != NULL);
    context->laye_types.var->sema_state = LYIR_SEMA_OK;

    context->laye_types._void = laye_node_create_in_context(context, LAYE_NODE_TYPE_VOID, LTY(context->laye_types.type));
    assert(context->laye_types._void != NULL);
    context->laye_types._void->sema_state = LYIR_SEMA_OK;

    context->laye_types.noreturn = laye_node_create_in_context(context, LAYE_NODE_TYPE_NORETURN, LTY(context->laye_types.type));
    assert(context->laye_types.noreturn != NULL);
    context->laye_types.noreturn->sema_state = LYIR_SEMA_OK;

    context->laye_types._bool = laye_node_create_in_context(context, LAYE_NODE_TYPE_BOOL, LTY(context->laye_types.type));
    assert(context->laye_types._bool != NULL);
    context->laye_types._bool->type_primitive.bit_width = 8;
    context->laye_types._bool->sema_state = LYIR_SEMA_OK;

    context->laye_types.i8 = laye_node_create_in_context(context, LAYE_NODE_TYPE_INT, LTY(context->laye_types.type));
    assert(context->laye_types.i8 != NULL);
    context->laye_types.i8->sema_state = LYIR_SEMA_OK;
    context->laye_types.i8->type_primitive.bit_width = 8;
    context->laye_types.i8->type_primitive.is_signed = true;

    context->laye_types._int = laye_node_create_in_context(context, LAYE_NODE_TYPE_INT, LTY(context->laye_types.type));
    assert(context->laye_types._int != NULL);
    context->laye_types._int->sema_state = LYIR_SEMA_OK;
    context->laye_types._int->type_primitive.is_platform_specified = true;
    context->laye_types._int->type_primitive.bit_width = context->lyir_context->target->size_of_pointer;
    context->laye_types._int->type_primitive.is_signed = true;

    context->laye_types._uint = laye_node_create_in_context(context, LAYE_NODE_TYPE_INT, LTY(context->laye_types.type));
    assert(context->laye_types._uint != NULL);
    context->laye_types._uint->sema_state = LYIR_SEMA_OK;
    context->laye_types._uint->type_primitive.is_platform_specified = true;
    context->laye_types._uint->type_primitive.bit_width = context->lyir_context->target->size_of_pointer;
    context->laye_types._uint->type_primitive.is_signed = false;

    // TODO(local): remove the generic `float` type from Laye
    context->laye_types._float = laye_node_create_in_context(context, LAYE_NODE_TYPE_FLOAT, LTY(context->laye_types.type));
    assert(context->laye_types._float != NULL);
    context->laye_types._float->type_primitive.is_platform_specified = true;
    context->laye_types._float->type_primitive.bit_width = 64;
    context->laye_types._float->sema_state = LYIR_SEMA_OK;
    // TODO(local): remove the generic `float` type from Laye

    context->laye_types.i8_buffer = laye_node_create_in_context(context, LAYE_NODE_TYPE_BUFFER, LTY(context->laye_types.type));
    assert(context->laye_types.i8_buffer != NULL);
    context->laye_types.i8_buffer->type_container.element_type = LTY(context->laye_types.i8);
    context->laye_types.i8_buffer->sema_state = LYIR_SEMA_OK;

    context->laye_dependencies = lyir_dependency_graph_create_in_context(lyir_context);
    assert(context->laye_dependencies != NULL);

    return context;
}

void laye_context_destroy(laye_context* context) {
    if (context == NULL) return;

    lca_allocator allocator = context->allocator;
    
    lca_da_free(context->include_directories);
    lca_da_free(context->library_directories);
    lca_da_free(context->link_libraries);

    for (int64_t i = 0; i < lca_da_count(context->laye_modules); i++) {
        laye_module_destroy(context->laye_modules[i]);
        context->laye_modules[i] = NULL;
    }

    lca_da_free(context->laye_modules);

    lca_deallocate(allocator, context->laye_types.poison);
    lca_deallocate(allocator, context->laye_types.unknown);
    lca_deallocate(allocator, context->laye_types.var);
    lca_deallocate(allocator, context->laye_types.type);
    lca_deallocate(allocator, context->laye_types._void);
    lca_deallocate(allocator, context->laye_types.noreturn);
    lca_deallocate(allocator, context->laye_types._bool);
    lca_deallocate(allocator, context->laye_types.i8);
    lca_deallocate(allocator, context->laye_types._int);
    lca_deallocate(allocator, context->laye_types._uint);
    lca_deallocate(allocator, context->laye_types._float);
    lca_deallocate(allocator, context->laye_types.i8_buffer);
}
