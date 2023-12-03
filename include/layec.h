#ifndef LAYEC_H
#define LAYEC_H

#include <stdbool.h>
#include <stdint.h>

#include "lcads.h"
#include "lcamem.h"
#include "lcastr.h"

#define LAYEC_VERSION "0.1.0"

typedef int64_t sourceid;

typedef struct layec_source {
    string name;
    string text;
} layec_source;

typedef struct layec_context {
    lca_allocator allocator;
    dynarr(layec_source) sources;
    dynarr(string) include_directories;
} layec_context;

typedef struct layec_module {
    layec_context* context;
    sourceid sourceid;

    lca_arena* arena;
} layec_module;

typedef struct layec_location {
    sourceid sourceid;
    int64_t offset;
    int64_t length;
} layec_location;

// Laye

#define LAYE_TOKEN_KINDS(X) \
    X(EOF)                  \
    X(IDENT)                \
    X(LITINT)               \
    X(LITFLOAT)             \
    X(LITSTRING)            \
    X(LITRUNE)              \
    X(LESSLESS)             \
    X(GREATERGREATER)       \
    X(EQUALEQUAL)           \
    X(BANGEQUAL)            \
    X(PLUSEQUAL)            \
    X(MINUSEQUAL)           \
    X(SLASHEQUAL)           \
    X(STAREQUAL)            \
    X(PERCENTEQUAL)         \
    X(LESSEQUAL)            \
    X(GREATEREQUAL)         \
    X(AMPERSANDEQUAL)       \
    X(PIPEEQUAL)            \
    X(TILDEEQUAL)           \
    X(LESSLESSEQUAL)        \
    X(GREATERGREATEREQUAL)  \
    X(EQUALGREATER)         \
    X(COLONCOLON)           \
    X(BOOL)                 \
    X(BOOLSIZED)            \
    X(INT)                  \
    X(INTSIZED)             \
    X(UINT)                 \
    X(UINTSIZED)            \
    X(FLOAT)                \
    X(FLOATSIZED)           \
    X(TRUE)                 \
    X(FALSE)                \
    X(NIL)                  \
    X(GLOBAL)               \
    X(IF)                   \
    X(ELSE)                 \
    X(FOR)                  \
    X(DO)                   \
    X(SWITCH)               \
    X(CASE)                 \
    X(DEFAULT)              \
    X(RETURN)               \
    X(BREAK)                \
    X(CONTINUE)             \
    X(DEFER)                \
    X(GOTO)                 \
    X(XYZZY)                \
    X(STRUCT)               \
    X(VARIANT)              \
    X(ENUM)                 \
    X(ALIAS)                \
    X(TEST)                 \
    X(IMPORT)               \
    X(EXPORT)               \
    X(FROM)                 \
    X(AS)                   \
    X(OPERATOR)             \
    X(MUT)                  \
    X(NEW)                  \
    X(DELETE)               \
    X(CAST)                 \
    X(TRY)                  \
    X(CATCH)                \
    X(SIZEOF)               \
    X(ALIGNOF)              \
    X(OFFSETOF)             \
    X(NOT)                  \
    X(AND)                  \
    X(OR)                   \
    X(XOR)                  \
    X(VARARGS)              \
    X(CONST)                \
    X(FOREIGN)              \
    X(INLINE)               \
    X(CALLCONV)             \
    X(IMPURE)               \
    X(NODISCARD)            \
    X(VOID)                 \
    X(VAR)                  \
    X(NORETURN)

typedef enum laye_token_kind {
    Invalid = 0,

    TILDE = '~',
    BANG = '!',
    PERCENT = '%',
    AMPERSAND = '&',
    STAR = '*',
    OPENPAREN = '(',
    CLOSEPAREN = ')',
    MINUS = '-',
    EQUAL = '=',
    PLUS = '+',
    OPENBRACKET = '[',
    CLOSEBRACKET = ']',
    OPENBRACE = '{',
    CLOSEBRACE = '}',
    PIPE = '|',
    SEMICOLON = ';',
    COLON = ':',
    COMMA = ',',
    LESS = '<',
    GREATER = '>',
    DOT = '.',
    SLASH = '/',
    QUESTION = '?',

    __LAYE_TOKEN_MULTIBYTE_START__ = 256,
#define X(N) LAYE_TOKEN_ ## N,
LAYE_TOKEN_KINDS(X)
#undef X
} laye_token_kind;

typedef struct laye_token {
    laye_token_kind kind;
    int64_t int_value;
    double float_value;
    string string_value;
} laye_token;

// ========== Context ==========

layec_context* layec_context_create(lca_allocator allocator);
void layec_context_destroy(layec_context* context);

sourceid layec_context_get_or_add_source_from_file(layec_context* context, string_view file_path);
layec_source layec_context_get_source(layec_context* context, sourceid sourceid);

// ========== Module ==========

layec_module* layec_module_create(layec_context* context, sourceid sourceid);
void layec_module_destroy(layec_module* module);

layec_source layec_module_get_source(layec_module* module);

// ========== Laye ==========

const char* laye_token_kind_to_cstring(laye_token_kind kind);

#endif // LAYEC_H
