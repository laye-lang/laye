#ifndef LAYEC_H
#define LAYEC_H

#include "ansi.h"
#include "lcads.h"
#include "lcamem.h"
#include "lcastr.h"

#include <stdbool.h>
#include <stdint.h>

#define LAYEC_VERSION "0.1.0"

#define COL(X) (use_color ? ANSI_COLOR_##X : "")

typedef int64_t sourceid;

typedef struct layec_source {
    string name;
    string text;
} layec_source;

typedef struct layec_context {
    lca_allocator allocator;

    bool use_color;

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

typedef enum layec_status {
    LAYEC_NO_STATUS,
    LAYEC_INFO,
    LAYEC_NOTE,
    LAYEC_WARN,
    LAYEC_ERROR,
    LAYEC_FATAL,
    LAYEC_ICE,
} layec_status;

typedef struct layec_diag {
    layec_status status;
    layec_location location;
    string message;
} layec_diag;

typedef enum layec_value_category {
    LAYEC_RVALUE,
    LAYEC_LVALUE,
} layec_value_category;

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
    X(LESSMINUS)            \
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

// clang-format off
typedef enum laye_token_kind {
    LAYE_TOKEN_INVALID = 0,

    LAYE_TOKEN_TILDE = '~',
    LAYE_TOKEN_BANG = '!',
    LAYE_TOKEN_PERCENT = '%',
    LAYE_TOKEN_AMPERSAND = '&',
    LAYE_TOKEN_STAR = '*',
    LAYE_TOKEN_OPENPAREN = '(',
    LAYE_TOKEN_CLOSEPAREN = ')',
    LAYE_TOKEN_MINUS = '-',
    LAYE_TOKEN_EQUAL = '=',
    LAYE_TOKEN_PLUS = '+',
    LAYE_TOKEN_OPENBRACKET = '[',
    LAYE_TOKEN_CLOSEBRACKET = ']',
    LAYE_TOKEN_OPENBRACE = '{',
    LAYE_TOKEN_CLOSEBRACE = '}',
    LAYE_TOKEN_PIPE = '|',
    LAYE_TOKEN_SEMICOLON = ';',
    LAYE_TOKEN_COLON = ':',
    LAYE_TOKEN_COMMA = ',',
    LAYE_TOKEN_LESS = '<',
    LAYE_TOKEN_GREATER = '>',
    LAYE_TOKEN_DOT = '.',
    LAYE_TOKEN_SLASH = '/',
    LAYE_TOKEN_QUESTION = '?',

    __LAYE_TOKEN_MULTIBYTE_START__ = 256,
#define X(N) LAYE_TOKEN_##N,
    LAYE_TOKEN_KINDS(X)
#undef X
} laye_token_kind;

typedef struct laye_token {
    laye_token_kind kind;
    layec_location location;
    int64_t int_value;
    double float_value;
    string string_value;
} laye_token;
// clang-format on

#define LAYE_NODE_DECL_KINDS(X) \
    X(IMPORT)                   \
    X(OVERLOADS)                \
    X(FUNCTION)                 \
    X(BINDING)                  \
    X(STRUCT)                   \
    X(ENUM)                     \
    X(ALIAS)                    \
    X(TEMPLATE_TYPE)            \
    X(TEMPLATE_VALUE)           \
    X(TEST)

#define LAYE_NODE_STMT_KINDS(X) \
    X(EMPTY)                    \
    X(COMPOUND)                 \
    X(ASSIGNMENT)               \
    X(DELETE)                   \
    X(IF)                       \
    X(FOR)                      \
    X(FOREACH)                  \
    X(DOFOR)                    \
    X(SWITCH)                   \
    X(RETURN)                   \
    X(BREAK)                    \
    X(CONTINUE)                 \
    X(FALLTHROUGH)              \
    X(DEFER)                    \
    X(GOTO)                     \
    X(XYZZY)                    \
    X(EXPR)

#define LAYE_NODE_EXPR_KINDS(X) \
    X(EVALUATED_CONSTANT)       \
    X(TEMPLATE_PARAMETER)       \
    X(SIZEOF)                   \
    X(OFFSETOF)                 \
    X(ALIGNOF)                  \
    X(NAME)                     \
    X(MEMBER)                   \
    X(INDEX)                    \
    X(SLICE)                    \
    X(CALL)                     \
    X(CTOR)                     \
    X(UNARY)                    \
    X(BINARY)                   \
    X(AND)                      \
    X(OR)                       \
    X(XOR)                      \
    X(CAST)                     \
    X(UNWRAP_NILABLE)           \
    X(NEW)                      \
    X(TRY)                      \
    X(CATCH)                    \
    X(LITNIL)                   \
    X(LITBOOL)                  \
    X(LITINT)                   \
    X(LITFLOAT)                 \
    X(LITSTRING)                \
    X(LITRUNE)

#define LAYE_NODE_TYPE_KINDS(X) \
    X(TYPE_POISON)              \
    X(TYPE_VOID)                \
    X(TYPE_NORETURN)            \
    X(TYPE_BOOL)                \
    X(TYPE_INT)                 \
    X(TYPE_FLOAT)               \
    X(TYPE_TEMPLATE_PARAMETER)  \
    X(TYPE_ERROR_PAIR)          \
    X(TYPE_NAME)                \
    X(TYPE_OVERLOADS)           \
    X(TYPE_NILABLE)             \
    X(TYPE_ARRAY)               \
    X(TYPE_SLICE)               \
    X(TYPE_REFERENCE)           \
    X(TYPE_POINTER)             \
    X(TYPE_BUFFER)              \
    X(TYPE_FUNCTION)            \
    X(TYPE_STRUCT)              \
    X(TYPE_VARIANT)             \
    X(TYPE_ENUM)                \
    X(TYPE_STRICT_ALIAS)

#define LAYE_NODE_KINDS(X) LAYE_NODE_DECL_KINDS(X) LAYE_NODE_STMT_KINDS(X) LAYE_NODE_EXPR_KINDS(X) LAYE_NODE_TYPE_KINDS(X)

// clang-format off
typedef enum laye_node_kind {
    LAYE_NODE_INVALID = 0,

#define X(N) LAYE_NODE_##N,
    __LAYE_NODE_DECL_START__,
    LAYE_NODE_DECL_KINDS(X)
    __LAYE_NODE_DECL_END__,

    __LAYE_NODE_STMT_START__,
    LAYE_NODE_STMT_KINDS(X)
    __LAYE_NODE_STMT_END__,

    __LAYE_NODE_EXPR_START__,
    LAYE_NODE_EXPR_KINDS(X)
    __LAYE_NODE_EXPR_END__,

    __LAYE_NODE_TYPE_START__,
    LAYE_NODE_TYPE_KINDS(X)
    __LAYE_NODE_TYPE_END__,
#undef X
} laye_node_kind;
// clang-format on

typedef struct laye_node laye_node;

struct laye_node {
    laye_node_kind kind;
    layec_location location;

    union {
        struct {
        } decl;

        struct {
            struct {
                // `<-` instead of `=`, reassigns a reference rather than assigning to its underlying value
                bool reference_reassign;
                // the target of assignment. must be able to evaluate to an lvalue expression.
                laye_node* lhs;
                // the value to assign. must be able to evaluate to an rvalue expression.
                laye_node* rhs;
            } assign;
        } stmt;

        struct {
            layec_value_category value_category;
            laye_node* type;
        } expr;

        struct {
            // when a expression of this type is an lvalue, can it be written to?
            bool is_modifiable;
        } type;
    };
};

// ========== Context ==========

layec_context* layec_context_create(lca_allocator allocator);
void layec_context_destroy(layec_context* context);

sourceid layec_context_get_or_add_source_from_file(layec_context* context, string_view file_path);
layec_source layec_context_get_source(layec_context* context, sourceid sourceid);

bool layec_context_get_location_info(layec_context* context, layec_location location, string_view* out_name, int64_t* out_line, int64_t* out_column);
void layec_context_print_location_info(layec_context* context, layec_location location, layec_status status, FILE* stream, bool use_color);

layec_diag layec_info(layec_context* context, layec_location location, const char* format, ...);
layec_diag layec_note(layec_context* context, layec_location location, const char* format, ...);
layec_diag layec_warn(layec_context* context, layec_location location, const char* format, ...);
layec_diag layec_error(layec_context* context, layec_location location, const char* format, ...);

void layec_write_diag(layec_context* context, layec_diag diag);
void layec_write_info(layec_context* context, layec_location location, const char* format, ...);
void layec_write_note(layec_context* context, layec_location location, const char* format, ...);
void layec_write_warn(layec_context* context, layec_location location, const char* format, ...);
void layec_write_error(layec_context* context, layec_location location, const char* format, ...);

// ========== Module ==========

layec_module* layec_module_create(layec_context* context, sourceid sourceid);
void layec_module_destroy(layec_module* module);

layec_source layec_module_get_source(layec_module* module);

// ========== Shared Data ==========

const char* layec_value_category_to_cstring(layec_value_category category);

// ========== Laye ==========

const char* laye_token_kind_to_cstring(laye_token_kind kind);
const char* laye_node_kind_to_cstring(laye_node_kind kind);

bool laye_node_kind_is_decl(laye_node_kind kind);
bool laye_node_kind_is_stmt(laye_node_kind kind);
bool laye_node_kind_is_expr(laye_node_kind kind);
bool laye_node_kind_is_type(laye_node_kind kind);

bool laye_node_is_decl(laye_node* node);
bool laye_node_is_stmt(laye_node* node);
bool laye_node_is_expr(laye_node* node);
bool laye_node_is_type(laye_node* node);

bool laye_node_is_lvalue(laye_node* node);
bool laye_node_is_rvalue(laye_node* node);
bool laye_node_is_modifiable_lvalue(laye_node* node);

bool laye_type_is_modifiable(laye_node* node);

#endif // LAYEC_H
