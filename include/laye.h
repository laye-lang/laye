#ifndef LAYEC_LAYE_H
#define LAYEC_LAYE_H

#include "layec.h"

//

typedef struct laye_scope laye_scope;
typedef struct laye_token laye_token;

typedef struct laye_attributes {
    // if a declaration is marked as `foreign` *and* a name was specified,
    // this field represents that name. `foreign` also controls name mangling.
    string foreign_name;
    // the linkage for this declaration.
    layec_linkage linkage;
    // the name mangling strategy to use for this declaration, if any.
    // can be controled by the `foreign` Laye attribute.
    layec_mangling mangling;
    // the calling convention for this declaration, if any.
    // used only on functions.
    layec_calling_convention calling_convention;
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

typedef struct laye_module {
    layec_context* context;
    layec_sourceid sourceid;

    bool dependencies_generated;

    lca_arena* arena;

    dynarr(laye_node*) top_level_nodes;

    dynarr(laye_token) _all_tokens;
    dynarr(laye_node*) _all_nodes;
    dynarr(laye_scope*) _all_scopes;
} laye_module;

struct laye_scope {
    // the module this scope is defined in.
    laye_module* module;
    // this scope's parent.
    laye_scope* parent;
    // the name of this scope, if any.
    // used mostly for debugging.
    string name;
    // true if this is a function scope (outermost scope containing a function body).
    bool is_function_scope;
    // "value"s declared in this scope.
    // here, "value" refers to non-type declarations like variables or functions.
    dynarr(laye_node*) value_declarations;
    // types declared in this scope.
    dynarr(laye_node*) type_declarations;
};

typedef enum laye_mut_compare {
    LAYE_MUT_EQUAL,
    LAYE_MUT_IGNORE,
    LAYE_MUT_CONVERTIBLE,
} laye_mut_compare;

#define LAYE_TRIVIA_KINDS(X) \
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
    layec_location location;
    string text;
} laye_trivia;

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
// clang-format on

struct laye_token {
    laye_token_kind kind;
    layec_location location;
    dynarr(laye_trivia) leading_trivia;
    dynarr(laye_trivia) trailing_trivia;
    union {
        int64_t int_value;
        double float_value;
        string string_value;
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
    X(LABEL)                   \
    X(EMPTY)                   \
    X(COMPOUND)                \
    X(ASSIGNMENT)              \
    X(DELETE)                  \
    X(IF)                      \
    X(FOR)                     \
    X(FOREACH)                 \
    X(DOFOR)                   \
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

typedef struct laye_nameref {
    laye_nameref_kind kind;
    // the scope this name is used from.
    // used for name resolution during semantic analysis.
    laye_scope* scope;
    // list of identifiers which are part of the name path.
    // a single identifier is still a one-element list, to ease name resolution
    // implementations.
    // every token in this list is assumed to be an identifier.
    dynarr(laye_token) pieces;
    // template arguments provided to this name for instantiation.
    dynarr(laye_node*) template_arguments;

    // the declaration this name references, once it has been resolved.
    laye_node* referenced_declaration;
    // the type this name references, once it has been resolved.
    laye_node* referenced_type;
} laye_nameref;

typedef struct laye_struct_type_field {
    laye_node* type;
    string name;
    layec_evaluated_constant initial_value;
} laye_struct_type_field;

typedef struct laye_struct_type_variant {
    laye_node* type;
    string name;
} laye_struct_type_variant;

typedef struct laye_enum_type_variant {
    string name;
    int64_t value;
} laye_enum_type_variant;

struct laye_node {
    laye_node_kind kind;
    layec_context* context;
    laye_module* module;

    // where in the source text the "primary" information for this syntax node is.
    // this does not have to span the very start to the very end of all nodes contained
    // within this node.
    // For example, a function declaration is probably a very sizeable chunk of source code,
    // but this location can (and probably should) only cover the function name identifier token.
    // child nodes may provide additional location information for better, more specific
    // error reporting if needed.
    layec_location location;

    // should be set to true if this node was generated by the compiler without
    // a direct source text analog. For example, implicit cast nodes are inserted
    // around many expression but aren't present in the source text, or an arrow function
    // body implicitly being a return statement.
    // this is mostly for debug or user-reporting aid and is not mission critical if not handled
    // 100% correctly, but should be maintained wherever possible and at least marked for
    // correction if an error ints setting is caught.
    bool compiler_generated;

    // the state of semantic analysis for this node.
    layec_sema_state sema_state;

    // the value category of this expression. i.e., is this an lvalue or rvalue expression?
    layec_value_category value_category;
    // the type of this expression.
    // will be void if this type has no expression.
    laye_node* type;

    // the declared name of this declaration.
    // not all declarations have names, but enough of them do that this
    // shared field is useful.
    // if this node is an import declaration, this field is set to the
    // namespace alias (after the optional `as` keyword). Since the alias must be
    // a valid Laye identifier, an empty string indicates it was not provided.
    // if either `import.is_wildcard` is set to true *or* `import.imported_names`
    // contains values, then this is assumed to be empty except for to report a syntax
    // error when used imporperly.
    string declared_name;
    // attributes for this declaration that aren't covered by other standard cases.
    laye_attributes attributes;
    // template parameters for this declaration, if there are any.
    // will contain template type and value declarations.
    dynarr(laye_node*) template_parameters;
    // the declared type of this declaration, if one is required for this node.
    // this is used for the type of a binding, the combined type of a function,
    // and the types declared by struct, enum or alias declarations.
    // this is not needed for import declarations, for example.
    laye_node* declared_type;

    // should not contain any unique information when compared to the shared fields above,
    // but for syntactic preservation these nodes are stored with every declaration anyway.
    dynarr(laye_node*) attribute_nodes;

    // when a expression of this type is an lvalue, can it be written to?
    // when true, this means the `mut` keyword was used with the type.
    // for example, `mut int` is a platform integer type that can be
    // assigned to. `int mut[]` is a slice who's value can change, but its
    // elements cannot. `mut int mut[]` is a slice who's elements and value
    // can both change.
    bool type_is_modifiable;

    // populated during IRgen, makes it easier to keep track of
    // since we don't have usable hash tables woot.
    layec_value* ir_value;

    union {
        // node describing an import declaration.
        // note that `export import` reuses the `linkage` field shared by all declarations.
        struct {
            // true if this import specifies the wildcard character `*`, false otherwise.
            bool is_wildcard;
            // the list of names this import declaration specifies, if any.
            // if there are no names specified or the `is_wildcard` field is set to
            // true, then this list is assumed empty except to report a syntax error.
            dynarr(string) imported_names;
            // the name of the module to import. This can be either a string literal
            // or a Laye identifier. They are allowed to have different semantics, but
            // their representation in this string does not have to be unique. In cases
            // where the semantics are different, the `is_module_name_identifier` flag
            // is set to true for identifier names and false for string literal names.
            string module_name;
            // true if the module name was specified as a Laye identifer, false if it's
            // a string literal.
            bool is_module_name_identifier;
            laye_module* referenced_module;
        } decl_import;

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
            dynarr(laye_node*) declarations;
        } decl_overloads;

        struct {
            // the return type of this function.
            laye_node* return_type;
            // the parameter declarations of this function (of type `FUNCTION_PARAMETER`).
            dynarr(laye_node*) parameter_declarations;
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
            dynarr(laye_node*) field_declarations;
            // child variant declarations within this struct type.
            // yes, variants can contain other variants.
            dynarr(laye_node*) variant_declarations;
        } decl_struct;

        struct {
            // the initializer expression, if one was provided, else null.
            // a semantically valid initial value must be constructible at compile time.
            laye_node* initializer;
        } decl_struct_field;

        struct {
            // the underlying type of this enum, or null if the default should be used.
            laye_node* underlying_type;
            // the enum variants (of type `ENUM_VARIANT`).
            // an enum variant is a name with an optional assigned constant integral value.
            dynarr(laye_node*) variants;
        } decl_enum;

        // used exclusively within enum declarations.
        struct {
            // the name of this enum variant.
            string name;
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
            // currently, there is nothing unique to a template type declaration,
            // but there might be in the future?
            int dummy;
        } decl_template_type;

        struct {
            // currently, there is nothing unique to a template value declaration,
            // but there might be in the future?
            int dummy;
        } decl_template_value;

        struct {
            // the brief description of this test, if one was provided.
            string description;
            // the body of this test. the body of a test is always
            // a compound statement, even during parse, and is a syntax error otherwise.
            // it will still be provided even if of the wrong type for debug purposes.
            laye_node* body;
        } decl_test;

        struct {
            bool is_expr;
            // the scope name for this compound block, if one was provided.
            // e.g.:
            //   init: { x = 10; }
            // or
            //   for (x < 10) outer: {
            //     for (y < 10) { if (foo()) break outer; }
            //   }
            // the scope name of a block is also the name of a label declared by the
            // identifier + colon construct. This is an additional special-case of the
            // syntax and is not a distinct part of the compound statement.
            string scope_name;
            // the children of this compound node.
            dynarr(laye_node*) children;
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
            dynarr(laye_node*) conditions;
            // if a label was specified on the pass body, it is stored here and
            // should be handled accordingly. since the pass or fail statements
            // must only be a single node, the case of labeling the node is handled
            // like this, explicitly.
            // laye_node* pass_label;
            // statements to be executed if the coresponding condition evaluates to true.
            dynarr(laye_node*) passes;
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
        } _for;

        struct {
            // the name of the "index" variable storing the current iteration index.
            string index_name;
            // the name of the "element" variable storing the current iteration result.
            // this is an lvalue to the iteration result.
            string element_name;
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
        } foreach;

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
        } dofor;

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
            dynarr(laye_node*) cases;
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
            string target;
        } _break;

        struct {
            // optionally, the labeled statement to continue from.
            // applies to loops and switch which have a label associated with
            // their primary "body".
            string target;
        } _continue;

        // note that yields basically only apply within compound expressions
        // that aren't non-expression function bodies.
        struct {
            // optionally, the labeled statement to yield from.
            // applies to loops and switch which have a label associated with
            // their primary "body".
            string target;
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
            string label;
        } _goto;

        struct {
            // the condition to assert against.
            laye_node* condition;
        } _assert;

        struct {
            // the expression that was evaluated to get this result.
            laye_node* expr;
            // the constant result of the evaluation.
            layec_evaluated_constant result;
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
        } member;

        struct {
            // the value to access the index of.
            laye_node* value;
            // the list of indices.
            // multi-dimensional arrays are supported, and index operator overloading
            // is planned to be supported eventually, so yes there can be multiple indices.
            dynarr(laye_node*) indices;
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
            dynarr(laye_node*) arguments;
        } call;

        // note that the type to be constructed is stored in the `expr.type` field.
        // note also that this is not the case for the `new` expression, since it
        // returns a pointer (or an overloaded return) to the type instead.
        struct {
            dynarr(laye_node*) initializers;
        } ctor;

        struct {
            // the type argument to the new operator.
            laye_node* type;
            // additional arguments to the new operator.
            dynarr(laye_node*) arguments;
            // member initializers.
            dynarr(laye_node*) initializers;
        } new;

        struct {
            // TODO(local): array index member initializer
            // the name of the field to initialize.
            laye_token field_name;
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
            string value;
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
            laye_node* value_type;
            // the type of the error case for this node, if any
            laye_node* error_type;
        } type_error_pair;

        struct {
            laye_node* element_type;
            dynarr(laye_node*) length_values;
        } type_container;

        struct {
            // what style of variadic arguments this function type uses, if any.
            laye_varargs_style varargs_style;
            // the calling convention used by this function type.
            layec_calling_convention calling_convention;
            // the return type of this function.
            laye_node* return_type;
            // the parameter types for this function, without parameter name information.
            dynarr(laye_node*) parameter_types;
        } type_function;

        // note that while struct and variant *types* are distinct,
        // they still have identical type data and therefore will share this struct.
        struct {
            string name;
            dynarr(laye_struct_type_field) fields;
            dynarr(laye_struct_type_variant) variants;
            laye_node* parent_struct_type;
        } type_struct;

        struct {
            string name;
            laye_node* underlying_type;
            dynarr(laye_enum_type_variant) variants;
        } type_enum;

        struct {
            string name;
            laye_node* underlying_type;
        } type_alias;

        struct {
            // all of our declaration attributes are or start with a keyword.
            laye_token_kind kind;
            // if the attribute can affect the calling convention, what calling
            // convention it specifies. (usually just the `callconv` attribute).
            layec_calling_convention calling_convention;
            // if the attribute can affect the name of this declaration in
            // generated code (the foreign name), it will be populated here
            // if specified. (usually just the `foreign` attribute with a name).
            string foreign_name;
            // if the attribute can affect the name mangling scheme for this declaration,
            // it is specified here. (usually just the `foreign` attribute with a
            // mangling scheme argument).
            layec_mangling mangling;

            laye_token keyword_token;
        } meta_attribute;

        struct {
            // will have to figure out what valid patterns we want and how
            // to represent them.
            int dummy;
        } meta_pattern;
    };
};

//

string laye_module_debug_print(laye_module* module);
laye_module* laye_parse(layec_context* context, layec_sourceid sourceid);
void laye_analyse(laye_module* module);
layec_module* laye_irgen(laye_module* module);
void laye_module_destroy(laye_module* module);

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

bool laye_type_is_modifiable(laye_node* node);

layec_source laye_module_get_source(laye_module* module);

laye_scope* laye_scope_create(laye_module* module, laye_scope* parent);
void laye_scope_destroy(laye_scope* scope);
void laye_scope_declare(laye_scope* scope, laye_node* declaration);
laye_node* laye_scope_lookup_value(laye_scope* scope, string_view value_name);
laye_node* laye_scope_lookup_type(laye_scope* scope, string_view type_name);

laye_node* laye_node_create(laye_module* module, laye_node_kind kind, layec_location location, laye_node* type);
laye_node* laye_node_create_in_context(layec_context* context, laye_node_kind kind, laye_node* type);
void laye_node_destroy(laye_node* node);

void laye_node_set_sema_in_progress(laye_node* node);
void laye_node_set_sema_errored(laye_node* node);
void laye_node_set_sema_ok(laye_node* node);
bool laye_node_is_sema_in_progress(laye_node* node);
bool laye_node_is_sema_ok(laye_node* node);
bool laye_node_is_sema_ok_or_errored(laye_node* node);

bool laye_node_has_noreturn_semantics(laye_node* node);

bool laye_decl_is_exported(laye_node* decl);
bool laye_decl_is_template(laye_node* decl);

laye_node* laye_expr_type(laye_node* expr);
bool laye_expr_evaluate(laye_node* expr, layec_evaluated_constant* out_constant, bool is_required);
bool laye_expr_is_lvalue(laye_node* expr);
bool laye_expr_is_modifiable_lvalue(laye_node* expr);
void laye_expr_set_lvalue(laye_node* expr, bool is_lvalue);

int laye_type_size_in_bits(laye_node* type);
int laye_type_size_in_bytes(laye_node* type);
int laye_type_align_in_bytes(laye_node* type);

bool laye_type_is_poison(laye_node* type);
bool laye_type_is_void(laye_node* type);
bool laye_type_is_noreturn(laye_node* type);
bool laye_type_is_bool(laye_node* type);
bool laye_type_is_int(laye_node* type);
bool laye_type_is_signed_int(laye_node* type);
bool laye_type_is_unsigned_int(laye_node* type);
bool laye_type_is_float(laye_node* type);
bool laye_type_is_template_parameter(laye_node* type);
bool laye_type_is_error_pair(laye_node* type);
bool laye_type_is_nameref(laye_node* type);
bool laye_type_is_overload(laye_node* type);
bool laye_type_is_nilable(laye_node* type);
bool laye_type_is_array(laye_node* type);
bool laye_type_is_slice(laye_node* type);
bool laye_type_is_reference(laye_node* type);
bool laye_type_is_pointer(laye_node* type);
bool laye_type_is_buffer(laye_node* type);
bool laye_type_is_function(laye_node* type);
bool laye_type_is_struct(laye_node* type);
bool laye_type_is_variant(laye_node* type);
bool laye_type_is_enum(laye_node* type);
bool laye_type_is_alias(laye_node* type);
bool laye_type_is_strict_alias(laye_node* type);

laye_node* laye_type_strip_pointers_and_references(laye_node* type);
laye_node* laye_type_strip_references(laye_node* type);

bool laye_type_equals(laye_node* a, laye_node* b, laye_mut_compare mut_compare);

void laye_type_print_to_string(laye_node* type, string* s, bool use_color);

int laye_type_array_rank(laye_node* array_type);

#endif // !LAYEC_LAYE_H
