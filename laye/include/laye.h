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

#ifndef LAYEC_LAYE_H
#define LAYEC_LAYE_H

#include "lyir.h"

//

typedef struct laye_scope laye_scope;
typedef struct laye_token laye_token;

typedef struct laye_attributes {
    // if a declaration is marked as `foreign` *and* a name was specified,
    // this field represents that name. `foreign` also controls name mangling.
    lca_string_view foreign_name;
    // the linkage for this declaration.
    lyir_linkage linkage;
    // the name mangling strategy to use for this declaration, if any.
    // can be controled by the `foreign` Laye attribute.
    lyir_mangling mangling;
    // the calling convention for this declaration, if any.
    // used only on functions.
    lyir_calling_convention calling_convention;
    // by default, a function return value cannot be implicitly discarded.
    // when a function is marked `discardable`, it can be implicitly discarded.
    // a return value may be *explicitly* discarded in either case.
    bool is_discardable;
    // true if this declaration should be inlined, false otherwise.
    // used only on functions.
    bool is_inline;
} laye_attributes;

typedef enum laye_varargs_style {
    // No variadic arguments, a standard function.
    LAYE_VARARGS_NONE,
    // C-style variadic arguments, additional arguments are not type-checked (but
    // are converted as necessary to varargs-accepted types automatically) and are
    // passed on the stack.
    LAYE_VARARGS_C,
    // Laye-style variadic arguments, where the last argument must be a Laye collection
    // and the additional arguments must be convertible to the element type of that collection.
    // the collection must be constructible by the compiler, and is passed as the single final
    // argument following regular Laye calling convention otherwise.
    LAYE_VARARGS_LAYE,
} laye_varargs_style;

typedef enum laye_cast_kind {
    LAYE_CAST_SOFT,
    LAYE_CAST_HARD,
    LAYE_CAST_STRUCT_BITCAST,
    LAYE_CAST_IMPLICIT,
    LAYE_CAST_LVALUE_TO_RVALUE,
    LAYE_CAST_LVALUE_TO_REFERENCE,
    LAYE_CAST_REFERENCE_TO_LVALUE,
} laye_cast_kind;

typedef struct laye_module_import {
    bool is_exported;
    lca_string_view name;
    struct laye_module* referenced_module;
} laye_module_import;

typedef struct laye_aliased_node {
    lca_string_view name;
    laye_node* node;
} laye_aliased_node;

typedef enum laye_symbol_kind {
    LAYE_SYMBOL_ENTITY,
    LAYE_SYMBOL_NAMESPACE,
} laye_symbol_kind;

typedef struct laye_symbol {
    laye_symbol_kind kind;
    lca_string_view name;
    union {
        lca_da(laye_node*) nodes;
        lca_da(struct laye_symbol*) symbols;
    };
} laye_symbol;

typedef struct laye_context laye_context;

typedef struct laye_module {
    laye_context* context;
    lyir_sourceid sourceid;

    bool has_handled_imports;
    bool dependencies_generated;

    struct lyir_module* ir_module;

    laye_scope* scope;
    lca_arena* arena;

    lca_da(laye_node*) top_level_nodes;

    // namespaces to reference when traversing imports.
    // *only* for imports which generate namespaces.
    //lca_da(laye_module_import) imports;

    laye_symbol* exports;
    laye_symbol* imports;

    lca_da(laye_token) _all_tokens;
    lca_da(laye_node*) _all_nodes;
    lca_da(laye_scope*) _all_scopes;
    lca_da(laye_symbol*) _all_symbols;
} laye_module;

struct laye_scope {
    // the module this scope is defined in.
    laye_module* module;
    // this scope's parent.
    laye_scope* parent;
    // the name of this scope, if any.
    // used mostly for debugging.
    lca_string_view name;
    // true if this is a function scope (outermost scope containing a function body).
    bool is_function_scope;
    // "value"s declared in this scope.
    // here, "value" refers to non-type declarations like variables or functions.
    lca_da(laye_aliased_node) value_declarations;
    // types declared in this scope.
    lca_da(laye_aliased_node) type_declarations;
};

typedef struct laye_context {
    lyir_context* lyir_context;
    lca_allocator allocator;

    bool use_color;
    bool has_reported_errors;
    bool use_byte_positions_in_diagnostics;

    lca_da(lca_string_view) include_directories;
    lca_da(lca_string_view) library_directories;
    lca_da(lca_string_view) link_libraries;

    lca_da(struct laye_module*) laye_modules;

    // types for use in Laye semantic analysis.
    // should not be stored within syntax nodes that have explicit
    // type syntax in the source code, since source location information
    // should be preserved whenever possible. These types are more
    // useful for known type conversions, like type checking the condition
    // of an if statement or for loop to be convertible to type `bool`, or
    // when converting array indices into a platform integer type.
    struct {
        laye_node* poison;
        laye_node* unknown;
        laye_node* var;
        laye_node* type;
        laye_node* _void;
        laye_node* noreturn;
        laye_node* _bool;
        laye_node* i8;
        laye_node* _int;
        laye_node* _uint;
        laye_node* _float;
        laye_node* i8_buffer;
    } laye_types;

    lyir_dependency_graph* laye_dependencies;

    lca_da(struct cached_struct_type { laye_node* node; lyir_type* type; }) _all_struct_types;
} laye_context;

typedef enum laye_mut_compare {
    LAYE_MUT_EQUAL,
    LAYE_MUT_IGNORE,
    LAYE_MUT_CONVERTIBLE,
} laye_mut_compare;

#define LAYE_TRIVIA_KINDS(X) \
    X(HASH_COMMENT)          \
    X(LINE_COMMENT)          \
    X(DELIMITED_COMMENT)

#define LAYE_TOKEN_KINDS(X) \
    X(UNKNOWN)              \
    X(EOF)                  \
    X(IDENT)                \
    X(LITINT)               \
    X(LITFLOAT)             \
    X(LITSTRING)            \
    X(LITRUNE)              \
    X(PLUSPLUS)             \
    X(MINUSMINUS)           \
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
    X(WHILE)                \
    X(DO)                   \
    X(SWITCH)               \
    X(CASE)                 \
    X(DEFAULT)              \
    X(RETURN)               \
    X(BREAK)                \
    X(CONTINUE)             \
    X(FALLTHROUGH)          \
    X(YIELD)                \
    X(UNREACHABLE)          \
    X(DEFER)                \
    X(DISCARD)              \
    X(GOTO)                 \
    X(XYZZY)                \
    X(ASSERT)               \
    X(STRUCT)               \
    X(VARIANT)              \
    X(ENUM)                 \
    X(STRICT)               \
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
    X(IS)                   \
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
    X(DISCARDABLE)          \
    X(VOID)                 \
    X(VAR)                  \
    X(NORETURN)

// clang-format off
typedef enum laye_trivia_kind {
    LAYE_TRIVIA_NONE,

#define X(N) LAYE_TRIVIA_ ## N,
LAYE_TRIVIA_KINDS(X)
#undef X
} laye_trivia_kind;
// clang-format on

typedef struct laye_trivia {
    laye_trivia_kind kind;
    lyir_location location;
    lca_string_view text;
} laye_trivia;

// clang-format off
typedef enum laye_token_kind {
    LAYE_TOKEN_INVALID = 0,

    __LAYE_PRINTABLE_TOKEN_START__ = 32,

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

    __LAYE_PRINTABLE_TOKEN_END__ = 128,

    __LAYE_TOKEN_MULTIBYTE_START__ = 256,
#define X(N) LAYE_TOKEN_##N,
    LAYE_TOKEN_KINDS(X)
#undef X
} laye_token_kind;
// clang-format on

struct laye_token {
    laye_token_kind kind;
    lyir_location location;
    lca_da(laye_trivia) leading_trivia;
    lca_da(laye_trivia) trailing_trivia;
    union {
        int64_t int_value;
        double float_value;
        lca_string_view string_value;
    };
};

#define LAYE_NODE_KINDS(X)     \
    X(DECL_IMPORT)             \
    X(DECL_OVERLOADS)          \
    X(DECL_FUNCTION)           \
    X(DECL_FUNCTION_PARAMETER) \
    X(DECL_BINDING)            \
    X(DECL_STRUCT)             \
    X(DECL_STRUCT_FIELD)       \
    X(DECL_ENUM)               \
    X(DECL_ENUM_VARIANT)       \
    X(DECL_ALIAS)              \
    X(DECL_TEMPLATE_TYPE)      \
    X(DECL_TEMPLATE_VALUE)     \
    X(DECL_TEST)               \
    X(IMPORT_QUERY)            \
    X(LABEL)                   \
    X(EMPTY)                   \
    X(COMPOUND)                \
    X(ASSIGNMENT)              \
    X(DELETE)                  \
    X(IF)                      \
    X(FOR)                     \
    X(FOREACH)                 \
    X(WHILE)                   \
    X(DOWHILE)                 \
    X(SWITCH)                  \
    X(CASE)                    \
    X(RETURN)                  \
    X(BREAK)                   \
    X(CONTINUE)                \
    X(FALLTHROUGH)             \
    X(YIELD)                   \
    X(UNREACHABLE)             \
    X(DEFER)                   \
    X(DISCARD)                 \
    X(GOTO)                    \
    X(XYZZY)                   \
    X(ASSERT)                  \
    X(EVALUATED_CONSTANT)      \
    X(TEMPLATE_PARAMETER)      \
    X(SIZEOF)                  \
    X(OFFSETOF)                \
    X(ALIGNOF)                 \
    X(NAMEREF)                 \
    X(MEMBER)                  \
    X(INDEX)                   \
    X(SLICE)                   \
    X(CALL)                    \
    X(CTOR)                    \
    X(NEW)                     \
    X(MEMBER_INITIALIZER)      \
    X(UNARY)                   \
    X(BINARY)                  \
    X(CAST)                    \
    X(PATTERN_MATCH)           \
    X(UNWRAP_NILABLE)          \
    X(TRY)                     \
    X(CATCH)                   \
    X(LITNIL)                  \
    X(LITBOOL)                 \
    X(LITINT)                  \
    X(LITFLOAT)                \
    X(LITSTRING)               \
    X(LITRUNE)                 \
    X(TYPE_POISON)             \
    X(TYPE_UNKNOWN)            \
    X(TYPE_VAR)                \
    X(TYPE_TYPE)               \
    X(TYPE_VOID)               \
    X(TYPE_NORETURN)           \
    X(TYPE_BOOL)               \
    X(TYPE_INT)                \
    X(TYPE_FLOAT)              \
    X(TYPE_TEMPLATE_PARAMETER) \
    X(TYPE_ERROR_PAIR)         \
    X(TYPE_NAMEREF)            \
    X(TYPE_OVERLOADS)          \
    X(TYPE_NILABLE)            \
    X(TYPE_ARRAY)              \
    X(TYPE_SLICE)              \
    X(TYPE_REFERENCE)          \
    X(TYPE_POINTER)            \
    X(TYPE_BUFFER)             \
    X(TYPE_FUNCTION)           \
    X(TYPE_STRUCT)             \
    X(TYPE_VARIANT)            \
    X(TYPE_ENUM)               \
    X(TYPE_ALIAS)              \
    X(TYPE_STRICT_ALIAS)       \
    X(META_ATTRIBUTE)          \
    X(META_PATTERN)

// clang-format off
typedef enum laye_node_kind {
    LAYE_NODE_INVALID = 0,

#define X(N) LAYE_NODE_##N,
    LAYE_NODE_KINDS(X)
#undef X
} laye_node_kind;
// clang-format on

typedef enum laye_nameref_kind {
    LAYE_NAMEREF_DEFAULT,
    LAYE_NAMEREF_GLOBAL,
    LAYE_NAMEREF_HEADLESS,
} laye_nameref_kind;

typedef struct laye_type {
    // the syntax node of the fully resolved, unqualified type.
    laye_node* node;
    // the original syntax node which resolved to this type.
    // for example, `vec2i` could resolve to a struct type, the type of `struct vec2i { ... }`
    // but we would still want to be able to report diagnostics as though the
    // original `vec2i` lookup type were present.
    // mostly for debug purposes.
    laye_node* source_node;
    // when a expression of this type is an lvalue, can it be written to?
    // when true, this means the `mut` keyword was used with the type.
    // for example, `int mut` is a platform integer type that can be
    // assigned to. `int[] mut` is a slice who's value can change, but its
    // elements cannot. `int mut[] mut` is a slice who's elements and value
    // can both change.
    bool is_modifiable;
} laye_type;

typedef struct laye_template_arg {
    bool is_type;
    laye_type type;
    laye_node* node;
} laye_template_arg;

typedef struct laye_nameref {
    laye_nameref_kind kind;
    // the scope this name is used from.
    // used for name resolution during semantic analysis.
    laye_scope* scope;
    // list of identifiers which are part of the name path.
    // a single identifier is still a one-element list, to ease name resolution
    // implementations.
    // every token in this list is assumed to be an identifier.
    lca_da(laye_token) pieces;
    // template arguments provided to this name for instantiation.
    lca_da(laye_template_arg) template_arguments;

    // the declaration this name references, once it has been resolved.
    laye_node* referenced_declaration;
    // the type this name references, once it has been resolved.
    laye_node* referenced_type;
} laye_nameref;

typedef struct laye_struct_type_field {
    laye_type type;
    lca_string_view name;
    lyir_evaluated_constant initial_value;
} laye_struct_type_field;

typedef struct laye_struct_type_variant {
    laye_type type;
    lca_string_view name;
} laye_struct_type_variant;

typedef struct laye_enum_type_variant {
    lca_string_view name;
    int64_t value;
} laye_enum_type_variant;

typedef enum laye_member_init_kind {
    LAYE_MEMBER_INIT_NONE,
    LAYE_MEMBER_INIT_NAMED,
    LAYE_MEMBER_INIT_INDEXED,
} laye_member_init_kind;

/// Bitmask that indicates whether a node is dependent.
///
/// A node is *type-dependent* if its type depends on a template
/// parameter. For example, in the program
///
///   T add<var T>(T a, T b) { return a + b; }
///
/// the expression `a` is type-dependent because it *is* a template
/// parameter, and the expression `a + b` is type-dependent because
/// the type of a `+` expression is the common type of its operands,
/// which in this case are both type-dependent.
///
/// A node is *value-dependent* if its *value* depends on a template
/// parameter. Value dependence is usually only relevant in contexts
/// that require constant evaluation. For example, in the program
///
///   void foo<int N>() { int[N] a; }
///
/// in the array type `int[N]`, the expression `N` is value-dependent
/// because its value won’t be known until instantiation time; this
/// means we also can’t check if the array is valid until instantiation
/// time. As a result, the type of the array is dependent.
///
/// Finally, a type is *instantiation-dependent* if it contains a
/// template parameter. Type-dependence and value-dependence both
/// imply instantiation-dependence.
///
/// Note that value-dependence does not imply type-dependence or vice
/// versa. Furthermore, an expression that contains a value-dependent
/// expression or a type-dependent type need not be value- or type-
/// dependent itself, but it *is* instantiation-dependent.
///
/// For example, if `a` is a type template parameter, then `a[4]` is
/// type-dependent, but `sizeof(a[4])` is not type-dependent since the
/// type of `sizeof(...)` is always `int`. However, it *is* value-dependent
/// since the size of a dependent type is unknown. Furthermore, the
/// expression `sizeof(sizeof(a[4]))` is neither type- nor value-dependent:
/// `sizeof(...)` is never type-dependent, and only value-dependent
/// if its operand is type-dependent, so `sizeof(sizeof(a[4])) is neither.
///
/// However, it *is* still instantiation-dependent since we need to instantiate
/// it at instantiation time, not to know its value, but still to check that
/// the inner `sizeof` is actually valid. For example, although the value of
/// `sizeof(sizeof(a[-1]))` is logically always `sizeof(int)`, this expression
/// is still invalid because it contains an invalid type.
///
/// Finally, error handling in sema is very similar to handling dependence:
/// if an expression is type-dependent, we can’t perform any checks that rely
/// on its type, and the same applies to value-dependence. The same is true
/// for expressions that contain an error, so we simply model errors as another
/// form of dependence, though error-dependence, unlike type- or value-dependence
/// is never resolved. Error-dependent expressions are simply skipped at instantiation
/// time.
typedef enum laye_dependence {
    // not dependent.
    LAYE_DEPENDENCE_NONE = 0,

    // node contains a dependent node.
    LAYE_DEPENDENCE_INSTANTIATION = 1 << 0,

    // the node's type depends on a template parameter
    LAYE_DEPENDENCE_TYPE = 1 << 1,
    LAYE_DEPENDENCE_TYPE_DEPENDENT = LAYE_DEPENDENCE_INSTANTIATION | LAYE_DEPENDENCE_TYPE,

    // the node's value depends on a template parameter
    LAYE_DEPENDENCE_VALUE = 1 << 2,
    LAYE_DEPENDENCE_VALUE_DEPENDENT = LAYE_DEPENDENCE_INSTANTIATION | LAYE_DEPENDENCE_VALUE,

    // the node contains an error.
    LAYE_DEPENDENCE_ERROR = 1 << 3,
    LAYE_DEPENDENCE_ERROR_DEPENDENT = LAYE_DEPENDENCE_INSTANTIATION | LAYE_DEPENDENCE_ERROR,
} laye_dependence;

struct laye_node {
    laye_node_kind kind;
    laye_context* context;
    laye_module* module;

    // where in the source text the "primary" information for this syntax node is.
    // this does not have to span the very start to the very end of all nodes contained
    // within this node.
    // For example, a function declaration is probably a very sizeable chunk of source code,
    // but this location can (and probably should) only cover the function name identifier token.
    // child nodes may provide additional location information for better, more specific
    // error reporting if needed.
    lyir_location location;

    // should be set to true if this node was generated by the compiler without
    // a direct source text analog. For example, implicit cast nodes are inserted
    // around many expression but aren't present in the source text, or an arrow function
    // body implicitly being a return statement.
    // this is mostly for debug or user-reporting aid and is not mission critical if not handled
    // 100% correctly, but should be maintained wherever possible and at least marked for
    // correction if an error ints setting is caught.
    bool compiler_generated;

    // the state of semantic analysis for this node.
    lyir_sema_state sema_state;
    laye_dependence dependence;

    // the value category of this expression. i.e., is this an lvalue or rvalue expression?
    lyir_value_category value_category;
    // the type of this expression.
    // will be void if this expression has no type.
    laye_type type;

    // the declared name of this declaration.
    // not all declarations have names, but enough of them do that this
    // shared field is useful.
    // if this node is an import declaration, this field is set to the
    // namespace alias (after the optional `as` keyword). Since the alias must be
    // a valid Laye identifier, an empty string indicates it was not provided.
    // if either `import.is_wildcard` is set to true *or* `import.imported_names`
    // contains values, then this is assumed to be empty except for to report a syntax
    // error when used imporperly.
    lca_string_view declared_name;
    // attributes for this declaration that aren't covered by other standard cases.
    laye_attributes attributes;
    // template parameters for this declaration, if there are any.
    // will contain template type and value declarations.
    lca_da(laye_node*) template_parameters;
    // the declared type of this declaration, if one is required for this node.
    // this is used for the type of a binding, the combined type of a function,
    // and the types declared by struct, enum or alias declarations.
    // this is not needed for import declarations, for example.
    laye_type declared_type;
    // for declarations which declare scopes, this is that.
    laye_scope* declared_scope;

    // should not contain any unique information when compared to the shared fields above,
    // but for syntactic preservation these nodes are stored with every declaration anyway.
    lca_da(laye_node*) attribute_nodes;

    // populated during IRgen, makes it easier to keep track of
    // since we don't have usable hash tables woot.
    //layec_value* ir_value;

    union {
        // node describing an import declaration.
        // note that `export import` reuses the `linkage` field shared by all declarations.
        struct {
            // true if this import specifies the wildcard character `*`, false otherwise.
            bool is_wildcard;
            // the list of names this import declaration specifies, if any.
            // if there are no names specified or the `is_wildcard` field is set to
            // true, then this list is assumed empty except to report a syntax error.
            lca_da(laye_node*) import_queries;
            // the name of the module to import. This can be either a string literal
            // or a Laye identifier. They are allowed to have different semantics, but
            // their representation in this string does not have to be unique. In cases
            // where the semantics are different, the `is_module_name_identifier` flag
            // is set to true for identifier names and false for string literal names.
            laye_token module_name;
            // the alias to use as the name of the namespace generated from this import,
            // if one is allowed (i.e. only if no import names/wildcard are specified).
            laye_token import_alias;

            laye_module* referenced_module;
        } decl_import;

        struct {
            bool is_wildcard;
            lca_da(laye_token) pieces;
            laye_token alias;
        } import_query;

        // not likely to ever be representable in Laye syntax, the `overloads`
        // node wraps the concept of overloading into a declaration node for
        // as close to type safety as we can get. when a named reference could refer
        // to many different declarations in a scope that share a name, they are
        // packaged into an overload declaration implicitly and returned as the
        // referenced declaration. Consumers of the declaration must explicitly handle
        // the possibility of an overload resolution needing to occur and report errors
        // if the overload resolution fails or is not semantically valid in that context.
        struct {
            // the list of declarations participating in this overload.
            lca_da(laye_node*) declarations;
        } decl_overloads;

        struct {
            // the return type of this function.
            laye_type return_type;
            // the parameter declarations of this function (of type `FUNCTION_PARAMETER`).
            lca_da(laye_node*) parameter_declarations;
            // the body of the function.
            // by the time sema is complete for this node,
            // this should always be a compound statement node.
            // at all times, this should be at least a statement node.
            laye_node* body;
        } decl_function;

        struct {
            // the default value for this parameter, if one is provided, else null.
            // a semantically valid default value must be constructible at compile time.
            laye_node* default_value;
        } decl_function_parameter;

        // note that this is used not only for global and local "variable" declarations,
        // but also for function parameter declarations.
        struct {
            // the initializer expression, if one was provided, else null.
            laye_node* initializer;
        } decl_binding;

        // the struct node is shared for the Laye `variant`, since they're almost
        // syntactically and semantically identical, though the *type* of each is
        // kept distinct. this means a `struct` node will have a `declared_type` of
        // `struct` and a `variant` node will have a `declared_type` of `variant`,
        // despite the shared declaration node data.
        struct {
            // the fields declared by this struct (of type `STRUCT_FIELD`).
            lca_da(laye_node*) field_declarations;
            // child variant declarations within this struct type.
            // yes, variants can contain other variants.
            lca_da(laye_node*) variant_declarations;
        } decl_struct;

        struct {
            // the initializer expression, if one was provided, else null.
            // a semantically valid initial value must be constructible at compile time.
            laye_node* initializer;
        } decl_struct_field;

        struct {
            // the underlying type of this enum, or null if the default should be used.
            // NOTE(local): the underlying type is heavily restricted and can never be qualified
            laye_node* underlying_type;
            // the enum variants (of type `ENUM_VARIANT`).
            // an enum variant is a name with an optional assigned constant integral value.
            lca_da(laye_node*) variants;
        } decl_enum;

        // used exclusively within enum declarations.
        struct {
            // the name of this enum variant.
            lca_string name;
            // the value of this enum variant, if provided.
            laye_node* value;
        } decl_enum_variant;

        struct {
            // set to true if this alias was declared `strict`.
            // the declared type of this alias will be of type `STRICT_ALIAS` if so.
            // a strict alias is an opaque type that is not implicitly convertible
            // to the declared type, but is *explicitly* convertible with a cast.
            bool is_strict;
        } decl_alias;

        struct {
            // true if this type parameter was prefixed with the `var` keyword,
            // causing usages of it to be unbound, unconstrained.
            // This enables duck typing in templates.
            bool is_duckable;
        } decl_template_type;

        struct {
            // currently, there is nothing unique to a template value declaration,
            // but there might be in the future?
            int dummy;
        } decl_template_value;

        struct {
            bool is_named;
            // the name of a declaration under test, if one was provided.
            laye_nameref nameref;
            // the brief description of this test, if one was provided.
            laye_token description;
            // the body of this test. the body of a test is always
            // a compound statement, even during parse, and is a syntax error otherwise.
            // it will still be provided even if of the wrong type for debug purposes.
            laye_node* body;

            laye_node* referenced_decl_node;
        } decl_test;

        struct {
            bool is_expr;
            // the scope name for this compound block, if one was provided.
            // e.g.:
            //   init: { x = 10; }
            // or
            //   outer: for (x < 10) {
            //     for (y < 10) { if (foo()) break outer; }
            //   }
            // the scope name of a block is also the name of a label declared by the
            // identifier + colon construct. This is an additional special-case of the
            // syntax and is not a distinct part of the compound statement.
            lca_string_view scope_name;
            // the children of this compound node.
            lca_da(laye_node*) children;
        } compound;

        struct {
            // `<-` instead of `=`, reassigns a reference rather than assigning to its underlying value
            bool reference_reassign;
            // the target of assignment. must be able to evaluate to an lvalue expression.
            laye_node* lhs;
            // the value to assign. must be able to evaluate to an rvalue expression.
            laye_node* rhs;
        } assignment;

        struct {
            // the expression to be deleted.
            // treated as a parameter to the delete operator when doing
            // operator overload resolution.
            laye_node* operand;
        } delete;

        struct {
            bool is_expr;
            // the conditions of this if(/else if) statement.
            lca_da(laye_node*) conditions;
            // if a label was specified on the pass body, it is stored here and
            // should be handled accordingly. since the pass or fail statements
            // must only be a single node, the case of labeling the node is handled
            // like this, explicitly.
            // laye_node* pass_label;
            // statements to be executed if the coresponding condition evaluates to true.
            lca_da(laye_node*) passes;
            // if a label was specified on the fail body, it is stored here and
            // should be handled accordingly. since the pass or fail statements
            // must only be a single node, the case of labeling the node is handled
            // like this, explicitly.
            // laye_node* fail_label;
            // statement to be executed if the conditions evaluate to false.
            laye_node* fail;
        } _if;

        struct {
            // the for loop initializer, if one is provided, else null.
            // must be a statement or declaration node.
            laye_node* initializer;
            // the condition of this for loop.
            // if not provided (read: is null), evaluates to `true`.
            laye_node* condition;
            // the for loop "increment" expression, if one is provided, else null.
            // may be a statement or expression, but not a declaration.
            laye_node* increment;
            // if a label was specified on the pass body, it is stored here and
            // should be handled accordingly. since the pass or fail statements
            // must only be a single node, the case of labeling the node is handled
            // like this, explicitly.
            laye_node* pass_label;
            // statement to be executed as long as the condition evaluates to true.
            laye_node* pass;
            // if a label was specified on the fail body, it is stored here and
            // should be handled accordingly. since the pass or fail statements
            // must only be a single node, the case of labeling the node is handled
            // like this, explicitly.
            laye_node* fail_label;
            // statement to be executed if the condition evaluated to false on the first check.
            // this statement is not hit if the first evaluation resulted in true, but subsequent
            // evaluations resulted in false; this is *only* executed if the very first
            // condition evaluation resulted in `false`.
            laye_node* fail;
            lyir_value* break_target_block;
            lyir_value* continue_target_block;
            // flags for easily determining if a loop contains specific control
            // flow constructs, to influence sema and IRgen.
            bool has_breaks    : 1;
            bool has_continues : 1;
        } _for;

        struct {
            // the declaration of the "index" variable storing the current iteration index.
            laye_node* index_binding;
            // the declaration of the "element" variable storing the current iteration result.
            // this is an lvalue to the iteration result.
            laye_node* element_binding;
            // the value to iterate over.
            // as long as only trivial iteration is supported, this must be a
            // built-in collection type with known bounds.
            // when non-trivial iteration is supported, the type of the iterable
            // must have resolvable functions that perform the iteration as defined
            // by Laye's iterator semantics.
            laye_node* iterable;
            // if a label was specified on the pass body, it is stored here and
            // should be handled accordingly. since the pass or fail statements
            // must only be a single node, the case of labeling the node is handled
            // like this, explicitly.
            laye_node* pass_label;
            // statement to be executed as long as the iterable provides element values.
            laye_node* pass;
            lyir_value* break_target_block;
            lyir_value* continue_target_block;
            // flags for easily determining if a loop contains specific control
            // flow constructs, to influence sema and IRgen.
            bool has_breaks    : 1;
            bool has_continues : 1;
        } foreach;

        struct {
            // the condition of this while loop.
            laye_node* condition;
            // if a label was specified on the pass body, it is stored here and
            // should be handled accordingly. since the pass or fail statements
            // must only be a single node, the case of labeling the node is handled
            // like this, explicitly.
            laye_node* pass_label;
            // statement to be executed as long as the condition evaluates to true.
            laye_node* pass;
            // if a label was specified on the fail body, it is stored here and
            // should be handled accordingly. since the pass or fail statements
            // must only be a single node, the case of labeling the node is handled
            // like this, explicitly.
            laye_node* fail_label;
            // statement to be executed if the condition evaluated to false on the first check.
            // this statement is not hit if the first evaluation resulted in true, but subsequent
            // evaluations resulted in false; this is *only* executed if the very first
            // condition evaluation resulted in `false`.
            laye_node* fail;
            lyir_value* break_target_block;
            lyir_value* continue_target_block;
            // flags for easily determining if a loop contains specific control
            // flow constructs, to influence sema and IRgen.
            bool has_breaks    : 1;
            bool has_continues : 1;
        } _while;

        struct {
            // if a label was specified on the pass body, it is stored here and
            // should be handled accordingly. since the pass or fail statements
            // must only be a single node, the case of labeling the node is handled
            // like this, explicitly.
            // this is not as necessary for a do-for loop, but it keeps the node
            // tree more consistent with the source representation.
            laye_node* pass_label;
            // statement to be executed as long as the condition evaluates to true.
            // is always executed once before the condition is checked.
            laye_node* pass;
            // the condition of this for loop.
            laye_node* condition;
            lyir_value* break_target_block;
            lyir_value* continue_target_block;
            // flags for easily determining if a loop contains specific control
            // flow constructs, to influence sema and IRgen.
            bool has_breaks    : 1;
            bool has_continues : 1;
        } dowhile;

        struct {
            // if a label was specified before the "body" of the switch, it is
            // stored here and should be handled accordingly.
            // jumping to this label is equivalent to jumping to the first case body.
            // targetted break can see this label, and it is the label of this switch
            // statement as a whole as well.
            laye_node* label;
            // the value being switched on.
            // until pattern switching is implemented, this must evaluate to an
            // integral value for the jump table.
            laye_node* value;
            // the list of cases in this switch statement, in the same order they're declared
            // in the source text.
            lca_da(laye_node*) cases;
        } _switch;

        struct {
            bool is_pattern;
            // the expression value or pattern of this case.
            laye_node* value;
            // the body of this case.
            // syntactically, a case can be followed by many declarations or statements,
            // but they're all wrapped up in an implicit compound block for easy storage here.
            // control flow that can leave this block like continue, break, fallthrough, etc.
            // is handled the same as any other section of code.
            laye_node* body;
        } _case;

        struct {
            // the value to return, or null if this is a void return.
            laye_node* value;
        } _return;

        struct {
            // optionally, the labeled statement to break out of.
            // applies to loops and switch which have a label associated with
            // their primary "body".
            lca_string_view target;
            // the syntax node to break out of. currently populated during sema,
            // but could be handled at parse time pretty easily instead.
            laye_node* target_node;
        } _break;

        struct {
            // optionally, the labeled statement to continue from.
            // applies to loops and switch which have a label associated with
            // their primary "body".
            lca_string_view target;
            // the syntax node to continue to. currently populated during sema,
            // but could be handled at parse time pretty easily instead.
            laye_node* target_node;
        } _continue;

        // note that yields basically only apply within compound expressions
        // that aren't non-expression function bodies.
        struct {
            // optionally, the labeled statement to yield from.
            // applies to loops and switch which have a label associated with
            // their primary "body".
            lca_string_view target;
            // the value to yield from this compound expression.
            laye_node* value;
        } yield;

        struct {
            // the statement to defer until scope exit.
            laye_node* body;
        } defer;

        struct {
            laye_node* value;
        } discard;

        struct {
            // the label to go to.
            lca_string_view label;
            // the syntax node to go to. this will be the label itself, not the node it
            // could be labeling.
            laye_node* target_node;
        } _goto;

        struct {
            // the condition to assert against.
            laye_node* condition;
            // string literal message to include when the assert fails.
            laye_token message;
        } _assert;

        struct {
            // the expression that was evaluated to get this result.
            laye_node* expr;
            // the constant result of the evaluation.
            lyir_evaluated_constant result;
        } evaluated_constant;

        struct {
            // the scope this parameter lookup is used from.
            laye_scope* scope;
            // the declaration this parameter lookup resolves to.
            laye_node* declaration;
        } template_parameter;

        struct {
            // the thing to check the size of.
            // if the query resolves to a type, then the size of that type is returned.
            // if the query resolves to an expression, then the size of the type of that
            // expression is returned.
            laye_node* query;
        } _sizeof;

        struct {
            // the thing to check the offset of.
            // if the query resolves to a field of a struct type, then the offset of
            // that field within the topmost struct is returned.
            // if the query resolves to a variant within a struct type, then the offset
            // of that variant within the topmost struct is returned.
            // if the query resolves to a member access expression, then the offset of
            // that field within the topmost struct type is returned.
            // if any other type or expression, an error is generated.
            laye_node* query;
        } _offsetof;

        struct {
            // the thing to check the alignment of.
            // if the query resolves to a type, then the alignment of that type is returned.
            // if the query resolves to an expression, then the alignment of the type of that
            // expression is returned.
            laye_node* query;
        } _alignof;

        laye_nameref nameref;

        struct {
            // the value to access the member of.
            laye_node* value;
            // an identifier token representing the field to look up.
            laye_token field_name;
            // The index of the member in whatever type we look at in sema.
            int64_t member_offset;
        } member;

        struct {
            // the value to access the index of.
            laye_node* value;
            // the list of indices.
            // multi-dimensional arrays are supported, and index operator overloading
            // is planned to be supported eventually, so yes there can be multiple indices.
            lca_da(laye_node*) indices;
        } index;

        struct {
            // the value to slice.
            laye_node* value;
            // the value to offset by. if null, starts at the beginning.
            laye_node* offset_value;
            // the length to capture in the slice. if null, takes the remaining
            // number of elements after the offset.
            laye_node* length_value;
        } slice;

        struct {
            // the thing to call.
            // can be a static function, a function pointer, or anything
            // which overloads the call operator.
            laye_node* callee;
            // the arguments to this call.
            lca_da(laye_node*) arguments;
        } call;

        // note that the type to be constructed is stored in the `expr.type` field.
        // note also that this is not the case for the `new` expression, since it
        // returns a pointer (or an overloaded return) to the type instead.
        struct {
            lca_da(laye_node*) initializers;
            lca_da(int64_t) calculated_offsets;
        } ctor;

        struct {
            // the type argument to the new operator.
            laye_type type;
            // additional arguments to the new operator.
            lca_da(laye_node*) arguments;
            // member initializers.
            lca_da(laye_node*) initializers;
        } new;

        struct {
            // TODO(local): array index member initializer
            laye_member_init_kind kind;
            union {
                // the name of the field to initialize.
                laye_token name;
                //
                laye_node* index;
            };
            // the value to initialize the field with.
            laye_node* value;
        } member_initializer;

        struct {
            laye_token operator;
            laye_node* operand;
        } unary;

        struct {
            // includes the logical operators.
            laye_token operator;
            laye_node* lhs;
            laye_node* rhs;
        } binary;

        // note that the type to cast to is stored in the `expr.type` field.
        struct {
            laye_cast_kind kind;
            laye_node* operand;
        } cast;

        struct {
            laye_node* operand;
        } unwrap_nilable;

        struct {
            // the operand of the try expression.
            laye_node* operand;
        } try;

        struct {
            // the operand of the catch expression.
            laye_node* operand;
            // the body of the catch expression.
            // catch expression bodies are evaluated in an expression context.
            // this means they can return values, even from within a compound statement.
            // the `break` keyword has an additional function when used in an expression
            // context, allowing the "returning" of values from within a labeled block.
            laye_node* body;
        } catch;

        struct {
            bool value;
        } litbool;

        struct {
            int64_t value;
        } litint;

        struct {
            double value;
        } litfloat;

        struct {
            lca_string_view value;
        } litstring;

        struct {
            int value;
        } litrune;

        // shared structure for int and float types
        struct {
            int bit_width;
            bool is_signed;
            // true if this type has platform-specific attributes, like bit width,
            // false otherwise.
            bool is_platform_specified;
        } type_primitive;

        struct {
            laye_node* declaration;
        } type_template_parameter;

        struct {
            // the type of the value case for this node.
            laye_type value_type;
            // the type of the error case for this node, if any
            laye_type error_type;
        } type_error_pair;

        struct {
            laye_type element_type;
            lca_da(laye_node*) length_values;
        } type_container;

        struct {
            // what style of variadic arguments this function type uses, if any.
            laye_varargs_style varargs_style;
            // the calling convention used by this function type.
            lyir_calling_convention calling_convention;
            // the return type of this function.
            laye_type return_type;
            // the parameter types for this function, without parameter name information.
            lca_da(laye_type) parameter_types;
        } type_function;

        // note that while struct and variant *types* are distinct,
        // they still have identical type data and therefore will share this struct.
        struct {
            lca_string_view name;
            lca_da(laye_struct_type_field) fields;
            lca_da(laye_struct_type_variant) variants;
            // NOTE(local): The parent struct type need not be as `laye_type`,
            // as it will never be qualified or come from any source other than its declaration.
            laye_node* parent_struct_type;

            int cached_size;
            int cached_align;
        } type_struct;

        struct {
            lca_string_view name;
            // NOTE(local): the underlying type of an enum is heavily restricted, and cannot be qualified.
            laye_node* underlying_type;
            lca_da(laye_enum_type_variant) variants;
        } type_enum;

        struct {
            lca_string_view name;
            // NOTE(local): while I think you can obviously still apply type qualifiers to
            // aliased types, allowing them at the alias declaration site (at least for the
            // outermost type) might be semantically invalid in the future.
            // keep an eye on it, basically, but for now it'll be a qualified type
            laye_type underlying_type;
        } type_alias;

        struct {
            // all of our declaration attributes are or start with a keyword.
            laye_token_kind kind;
            // if the attribute can affect the calling convention, what calling
            // convention it specifies. (usually just the `callconv` attribute).
            lyir_calling_convention calling_convention;
            // if the attribute can affect the name of this declaration in
            // generated code (the foreign name), it will be populated here
            // if specified. (usually just the `foreign` attribute with a name).
            lca_string_view foreign_name;
            // if the attribute can affect the name mangling scheme for this declaration,
            // it is specified here. (usually just the `foreign` attribute with a
            // mangling scheme argument).
            lyir_mangling mangling;

            laye_token keyword_token;
        } meta_attribute;

        struct {
            // will have to figure out what valid patterns we want and how
            // to represent them.
            int dummy;
        } meta_pattern;
    };
};

laye_context* laye_context_create(lyir_context* lyir_context);
void laye_context_destroy(laye_context* context);

//

lca_string laye_module_debug_print(laye_module* module);
laye_module* laye_parse(laye_context* context, lyir_sourceid sourceid);
void laye_analyse(laye_context* context);
void laye_generate_ir(laye_context* context);
void laye_module_destroy(laye_module* module);

//

laye_symbol* laye_symbol_create(laye_module* module, laye_symbol_kind kind, lca_string_view name);
void laye_symbol_destroy(laye_symbol* symbol);
laye_symbol* laye_symbol_lookup(laye_symbol* symbol_namespace, lca_string_view name);

//

const char* laye_trivia_kind_to_cstring(laye_trivia_kind kind);
const char* laye_token_kind_to_cstring(laye_token_kind kind);
const char* laye_node_kind_to_cstring(laye_node_kind kind);

bool laye_node_kind_is_decl(laye_node_kind kind);
bool laye_node_kind_is_type(laye_node_kind kind);

bool laye_node_is_decl(laye_node* node);
bool laye_node_is_type(laye_node* node);

bool laye_node_is_lvalue(laye_node* node);
bool laye_node_is_rvalue(laye_node* node);
bool laye_node_is_modifiable_lvalue(laye_node* node);

bool laye_type_is_modifiable(laye_type type);

lyir_source laye_module_get_source(laye_module* module);

laye_scope* laye_scope_create(laye_module* module, laye_scope* parent);
void laye_scope_destroy(laye_scope* scope);
void laye_scope_declare(laye_scope* scope, laye_node* declaration);
void laye_scope_declare_aliased(laye_scope* scope, laye_node* declaration, lca_string_view alias);
laye_node* laye_scope_lookup_value(laye_scope* scope, lca_string_view value_name);
laye_node* laye_scope_lookup_type(laye_scope* scope, lca_string_view type_name);

laye_node* laye_node_create(laye_module* module, laye_node_kind kind, lyir_location location, laye_type type);
laye_node* laye_node_create_in_context(laye_context* context, laye_node_kind kind, laye_type type);
void laye_node_destroy(laye_node* node);

void laye_node_set_sema_in_progress(laye_node* node);
void laye_node_set_sema_ok(laye_node* node);
bool laye_node_is_sema_in_progress(laye_node* node);
bool laye_node_is_sema_ok(laye_node* node);
bool laye_node_is_sema_ok_or_errored(laye_node* node);

bool laye_node_has_noreturn_semantics(laye_node* node);

bool laye_decl_is_exported(laye_node* decl);
bool laye_decl_is_template(laye_node* decl);

laye_type laye_expr_type(laye_node* expr);
bool laye_expr_evaluate(laye_node* expr, lyir_evaluated_constant* out_constant, bool is_required);
bool laye_expr_is_lvalue(laye_node* expr);
bool laye_expr_is_modifiable_lvalue(laye_node* expr);
void laye_expr_set_lvalue(laye_node* expr, bool is_lvalue);

int laye_type_size_in_bits(laye_type type);
int laye_type_size_in_bytes(laye_type type);
int laye_type_align_in_bytes(laye_type type);

bool laye_type_is_poison(laye_type type);
bool laye_type_is_void(laye_type type);
bool laye_type_is_noreturn(laye_type type);
bool laye_type_is_bool(laye_type type);
bool laye_type_is_int(laye_type type);
bool laye_type_is_signed_int(laye_type type);
bool laye_type_is_unsigned_int(laye_type type);
bool laye_type_is_float(laye_type type);
bool laye_type_is_numeric(laye_type type);
bool laye_type_is_template_parameter(laye_type type);
bool laye_type_is_error_pair(laye_type type);
bool laye_type_is_nameref(laye_type type);
bool laye_type_is_overload(laye_type type);
bool laye_type_is_nilable(laye_type type);
bool laye_type_is_array(laye_type type);
bool laye_type_is_slice(laye_type type);
bool laye_type_is_reference(laye_type type);
bool laye_type_is_pointer(laye_type type);
bool laye_type_is_buffer(laye_type type);
bool laye_type_is_function(laye_type type);
bool laye_type_is_struct(laye_type type);
bool laye_type_is_variant(laye_type type);
bool laye_type_is_enum(laye_type type);
bool laye_type_is_alias(laye_type type);
bool laye_type_is_strict_alias(laye_type type);

laye_type laye_type_qualify(laye_node* type_node, bool is_modifiable);
laye_type laye_type_add_qualifiers(laye_type type, bool is_modifiable);
laye_type laye_type_with_source(laye_node* type_node, laye_node* source_node, bool is_modifiable);

int laye_type_struct_field_offset_bits(laye_type struct_type, int64_t field_index);
int laye_type_struct_field_offset_bytes(laye_type struct_type, int64_t field_index);
int64_t laye_type_struct_field_index_by_name(laye_type struct_type, lca_string_view field_name);
int64_t laye_type_struct_variant_index_by_name(laye_type struct_type, lca_string_view variant_name);

laye_type laye_type_strip_pointers_and_references(laye_type type);
laye_type laye_type_strip_references(laye_type type);

bool laye_type_equals(laye_type a, laye_type b, laye_mut_compare mut_compare);

void laye_type_print_to_string(laye_type type, lca_string* s, bool use_color);

int laye_type_array_rank(laye_type array_type);

#define LTY(T) (laye_type_qualify(T, false))

#endif // !LAYEC_LAYE_H
