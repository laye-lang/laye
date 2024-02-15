#ifndef LAYEC_C_H
#define LAYEC_C_H

#include "layec.h"

#define C_TOKEN_KINDS(X) \
    X(EOF)               \
    X(IDENT)

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
    layec_location location;

    bool is_macro_param;
    int macro_param_index;

    union {
        int64_t int_value;
        double float_value;
        string_view string_value;
    };
} c_token;

typedef struct c_translation_unit {
    dynarr(c_token) _all_tokens;
} c_translation_unit;

#endif // !LAYEC_C_H
