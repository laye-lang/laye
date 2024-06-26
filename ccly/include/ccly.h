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

#ifndef LAYEC_C_H
#define LAYEC_C_H

#include "lyir.h"

#define ccly_return_defer(value) do { result = (value); goto defer; } while (0)

typedef struct c_context {
    lyir_context* lyir_context;
    lca_allocator allocator;

    bool use_color;

    lca_da(lca_string_view) include_directories;
    lca_da(lca_string_view) library_directories;
    lca_da(lca_string_view) link_libraries;

    lca_da(struct c_translation_unit*) c_translation_units;
} c_context;

#define C_TOKEN_KINDS(X)     \
    X(EOF)                   \
    X(LIT_INT)               \
    X(LIT_CHAR)              \
    X(LIT_STRING)            \
    X(IDENT)                 \
    X(AUTO)                  \
    X(BREAK)                 \
    X(CASE)                  \
    X(CHAR)                  \
    X(CONST)                 \
    X(CONTINUE)              \
    X(DEFAULT)               \
    X(DO)                    \
    X(DOUBLE)                \
    X(ELSE)                  \
    X(ENUM)                  \
    X(EXTERN)                \
    X(FLOAT)                 \
    X(FOR)                   \
    X(GOTO)                  \
    X(IF)                    \
    X(INT)                   \
    X(LONG)                  \
    X(REGISTER)              \
    X(RETURN)                \
    X(SHORT)                 \
    X(SIGNED)                \
    X(SIZEOF)                \
    X(STATIC)                \
    X(STRUCT)                \
    X(SWITCH)                \
    X(TYPEDEF)               \
    X(UNION)                 \
    X(UNSIGNED)              \
    X(VOID)                  \
    X(VOLATILE)              \
    X(WHILE)                 \
    X(TRIPLE_DOT)            \
    X(PLUS_PLUS)             \
    X(PLUS_EQUAL)            \
    X(MINUS_MINUS)           \
    X(MINUS_EQUAL)           \
    X(STAR_EQUAL)            \
    X(SLASH_EQUAL)           \
    X(PERCENT_EQUAL)         \
    X(AMPERSAND_AMPERSAND)   \
    X(AMPERSAND_EQUAL)       \
    X(PIPE_PIPE)             \
    X(PIPE_EQUAL)            \
    X(CARET_EQUAL)           \
    X(LESS_LESS_EQUAL)       \
    X(LESS_LESS)             \
    X(LESS_EQUAL)            \
    X(GREATER_GREATER_EQUAL) \
    X(GREATER_GREATER)       \
    X(GREATER_EQUAL)         \
    X(EQUAL_EQUAL)           \
    X(BANG_EQUAL)            \
    X(DUMMY)

typedef enum c_token_kind {
    C_TOKEN_INVALID = 0,

    C_TOKEN_TILDE = '~',
    C_TOKEN_BANG = '!',
    C_TOKEN_PERCENT = '%',
    C_TOKEN_AMPERSAND = '&',
    C_TOKEN_STAR = '*',
    C_TOKEN_OPENPAREN = '(',
    C_TOKEN_CLOSEPAREN = ')',
    C_TOKEN_MINUS = '-',
    C_TOKEN_EQUAL = '=',
    C_TOKEN_PLUS = '+',
    C_TOKEN_OPENBRACKET = '[',
    C_TOKEN_CLOSEBRACKET = ']',
    C_TOKEN_OPENBRACE = '{',
    C_TOKEN_CLOSEBRACE = '}',
    C_TOKEN_PIPE = '|',
    C_TOKEN_SEMICOLON = ';',
    C_TOKEN_COLON = ':',
    C_TOKEN_COMMA = ',',
    C_TOKEN_LESS = '<',
    C_TOKEN_GREATER = '>',
    C_TOKEN_DOT = '.',
    C_TOKEN_SLASH = '/',
    C_TOKEN_QUESTION = '?',

#define X(N) C_TOKEN_##N,
    C_TOKEN_KINDS(X)
#undef X
} c_token_kind;

typedef struct c_token {
    c_token_kind kind;
    lyir_location location;

    bool is_macro_param;
    int macro_param_index;
    bool is_angle_string;

    union {
        int64_t int_value;
        double float_value;
        lca_string_view string_value;
    };
} c_token;

typedef struct c_token_buffer {
    lca_da(c_token) semantic_tokens;
} c_token_buffer;

typedef struct c_macro_def {
    lca_string_view name;
    bool has_params;
    lca_da(lca_string_view) params;
    lca_da(c_token) body;
} c_macro_def;

typedef struct c_translation_unit {
    c_context* context;
    lyir_sourceid sourceid;

    lca_arena* arena;

    lca_da(c_macro_def*) macro_defs;

    //lca_da(c_token) _all_tokens;
    c_token_buffer token_buffer;
} c_translation_unit;

//

typedef enum ccly_args_parse_result {
    CCLY_ARGS_OK,
    CCLY_ARGS_NO_ARGS,
    CCLY_ARGS_ERR_UNKNOWN,
} ccly_args_parse_result;

typedef struct ccly_args {
    const char* program_name;
} ccly_args;

typedef void (*ccly_args_parse_logger)(char* message);
void ccly_args_parse_logger_default(char* message);

ccly_args_parse_result ccly_args_parse(ccly_args* args, int argc, char** argv, ccly_args_parse_logger logger);

int ccly_main(ccly_args args);

//

c_context* c_context_create(lyir_context* lyir_context);
void c_context_destroy(c_context* c_context);

lca_string c_translation_unit_debug_print(c_translation_unit* tu);
c_translation_unit* ccly_parse(c_context* context, lyir_sourceid sourceid);
void c_translation_unit_destroy(c_translation_unit* tu);

//

const char* c_token_kind_to_cstring(c_token_kind kind);

#endif // !LAYEC_C_H
