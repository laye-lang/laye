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

#include "laye.h"

#include <assert.h>

typedef struct break_continue_target {
    string_view name;
    laye_node* target;
} break_continue_target;

typedef struct laye_parser {
    layec_context* context;
    laye_module* module;
    layec_sourceid sourceid;

    layec_source source;
    int64_t lexer_position;
    int current_char;

    laye_token token;
    laye_token next_token;

    laye_scope* scope;

    dynarr(break_continue_target) break_continue_stack;
} laye_parser;

typedef struct laye_parse_result {
    bool success;
    dynarr(layec_diag) diags;
    // even if success is false, this node/type should be populated with data
    // representing the portion of the source where parsing was attempted.
    // basically, if any tokens are consumed, something should be here
    // representing that.
    union {
        laye_node* node;
        laye_type type;
    };
} laye_parse_result;

// tentative parse macros to ease parse result handling
#define PARSE_UNWRAP(N, P, ...)                                \
    laye_node* N = NULL;                                       \
    do {                                                       \
        laye_parse_result result = P(__VA_ARGS__);             \
        N = result.node;                                       \
        if (!result.success) {                                 \
            laye_parse_result_write_diags(p->context, result); \
        }                                                      \
        laye_parse_result_destroy(result);                     \
    } while (0)

static laye_parse_result laye_parse_result_success(laye_node* node) {
    return (laye_parse_result){
        .success = true,
        .node = node,
    };
}

static laye_parse_result laye_parse_result_success_type(laye_type type) {
    return (laye_parse_result){
        .success = true,
        .type = type,
    };
}

static laye_parse_result laye_parse_result_failure(laye_node* node, layec_diag diag) {
    laye_parse_result result = (laye_parse_result){
        .success = false,
        .node = node,
    };
    arr_push(result.diags, diag);
    return result;
}

static laye_parse_result laye_parse_result_failure_type(laye_type type, layec_diag diag) {
    laye_parse_result result = (laye_parse_result){
        .success = false,
        .type = type,
    };
    arr_push(result.diags, diag);
    return result;
}

static void laye_parse_result_copy_diags(laye_parse_result* target, laye_parse_result from) {
    if (!from.success) {
        target->success = false;
    }

    for (int64_t i = 0, count = arr_count(from.diags); i < count; i++) {
        arr_push(target->diags, from.diags[i]);
    }
}

static void laye_parse_result_write_diags(layec_context* context, laye_parse_result result) {
    for (int64_t i = 0, count = arr_count(result.diags); i < count; i++) {
        layec_write_diag(context, result.diags[i]);
    }
}

static void laye_parse_result_destroy(laye_parse_result result) {
    arr_free(result.diags);
}

static laye_parse_result laye_parse_result_combine(laye_parse_result a, laye_parse_result b) {
    laye_parse_result_copy_diags(&a, b);

    a.type = (laye_type){0};
    a.node = b.node;

    laye_parse_result_destroy(b);
    return a;
}

static laye_parse_result laye_parse_result_combine_type(laye_parse_result a, laye_parse_result b) {
    laye_parse_result_copy_diags(&a, b);

    a.node = NULL;
    a.type = b.type;

    laye_parse_result_destroy(b);
    return a;
}

const char* laye_trivia_kind_to_cstring(laye_trivia_kind kind) {
    switch (kind) {
        default: return "<invalid laye trivia kind>";

#define X(N) \
    case LAYE_TRIVIA_##N: return #N;
            LAYE_TRIVIA_KINDS(X)
#undef X
    }
}

static void laye_parser_push_break_continue_target(laye_parser* p, string_view name, laye_node* target) {
    break_continue_target t = {
        .name = name,
        .target = target,
    };
    arr_push(p->break_continue_stack, t);
}

static void laye_parser_pop_break_continue_target(laye_parser* p) {
    assert(arr_count(p->break_continue_stack) != 0);
    arr_pop(p->break_continue_stack);
}

static break_continue_target laye_parser_peek_break_continue_target(laye_parser* p) {
    assert(arr_count(p->break_continue_stack) != 0);
    return p->break_continue_stack[arr_count(p->break_continue_stack) - 1];
}

const char* laye_token_kind_to_cstring(laye_token_kind kind) {
    static bool single_chars_initialized = false;
    static char single_chars[256 * 2];

    switch (kind) {
        case LAYE_TOKEN_INVALID: return "<invalid laye token kind>";

#define X(N) \
    case LAYE_TOKEN_##N: return #N;
            LAYE_TOKEN_KINDS(X)
#undef X

        default: {
            if (kind < 256) {
                if (!single_chars_initialized) {
                    for (int i = 0; i < 256; i++)
                        single_chars[i * 2] = (char)i;
                }

                return &single_chars[kind * 2];
            }

            return "<unknown laye token kind>";
        }
    }
}

const char* laye_node_kind_to_cstring(laye_node_kind kind) {
    switch (kind) {
        case LAYE_NODE_INVALID: return "<invalid laye token kind>";

#define X(N) \
    case LAYE_NODE_##N: return #N;
            LAYE_NODE_KINDS(X)
#undef X

        default: return "<unknown laye node kind>";
    }
}

static void laye_next_token(laye_parser* p);
static laye_node* laye_parse_top_level_node(laye_parser* p);
static laye_nameref laye_parse_nameref(laye_parser* p, laye_parse_result* result, layec_location* location, bool allocate);

laye_module* laye_parse(layec_context* context, layec_sourceid sourceid) {
    assert(context != NULL);
    assert(sourceid >= 0);

    laye_module* module = lca_allocate(context->allocator, sizeof *module);
    assert(module);
    module->context = context;
    module->sourceid = sourceid;
    module->arena = lca_arena_create(context->allocator, 1024 * 1024);
    assert(module->arena);
    arr_push(context->laye_modules, module);

    laye_scope* module_scope = laye_scope_create(module, NULL);
    assert(module_scope != NULL);
    module->scope = module_scope;

    layec_source source = layec_context_get_source(context, sourceid);

    laye_parser p = {
        .context = context,
        .module = module,
        .sourceid = sourceid,
        .source = source,
        .scope = module_scope,
    };

    if (source.text.count > 0) {
        p.current_char = source.text.data[0];
    }

    // prime the first token before we begin parsing
    laye_next_token(&p);

    while (p.token.kind != LAYE_TOKEN_EOF) {
        layec_location node_start_location = p.token.location;

        laye_node* top_level_node = laye_parse_top_level_node(&p);
        assert(top_level_node != NULL);
        assert(p.token.location.offset != node_start_location.offset);
        assert(p.scope == module_scope);

        arr_push(module->top_level_nodes, top_level_node);
    }

    arr_free(p.break_continue_stack);

    return module;
}

// ========== Parser ==========

typedef struct operator_info {
    laye_token_kind operator_kind;
    int precedence;
} operator_info;

static operator_info operator_precedences[] = {
    {LAYE_TOKEN_OR, 5},
    {LAYE_TOKEN_XOR, 5},
    {LAYE_TOKEN_AND, 6},

    {LAYE_TOKEN_EQUALEQUAL, 10},
    {LAYE_TOKEN_BANGEQUAL, 10},

    {LAYE_TOKEN_LESS, 20},
    {LAYE_TOKEN_LESSEQUAL, 20},
    {LAYE_TOKEN_GREATER, 20},
    {LAYE_TOKEN_GREATEREQUAL, 20},

    {LAYE_TOKEN_AMPERSAND, 30},
    {LAYE_TOKEN_PIPE, 30},
    {LAYE_TOKEN_TILDE, 30},
    {LAYE_TOKEN_LESSLESS, 30},
    {LAYE_TOKEN_GREATERGREATER, 30},

    {LAYE_TOKEN_PLUS, 40},
    {LAYE_TOKEN_MINUS, 40},

    {LAYE_TOKEN_SLASH, 50},
    {LAYE_TOKEN_STAR, 50},
    {LAYE_TOKEN_PERCENT, 50},

    {0, 0}
};

struct laye_parser_mark {
    laye_token token;
    laye_token next_token;
    int64_t lexer_position;
};

static struct laye_parser_mark laye_parser_mark(laye_parser* p) {
    assert(p != NULL);
    return (struct laye_parser_mark){
        .token = p->token,
        .next_token = p->next_token,
        .lexer_position = p->lexer_position,
    };
}

static void laye_parser_reset_to_mark(laye_parser* p, struct laye_parser_mark mark) {
    assert(p != NULL);
    p->token = mark.token;
    p->next_token = mark.next_token;
    p->lexer_position = mark.lexer_position;
    assert(mark.lexer_position >= 0 && mark.lexer_position <= p->source.text.count);
    p->current_char = p->source.text.data[mark.lexer_position];
}

static void laye_parser_push_scope(laye_parser* p) {
    assert(p != NULL);
    assert(p->module != NULL);

    laye_scope* parent_scope = p->scope;
    laye_scope* scope = laye_scope_create(p->module, parent_scope);
    p->scope = scope;
}

static void laye_parser_pop_scope(laye_parser* p) {
    assert(p != NULL);
    assert(p->scope != NULL);

    laye_scope* scope = p->scope;
    p->scope = scope->parent;
}

static bool laye_parser_at(laye_parser* p, laye_token_kind kind) {
    assert(p != NULL);
    return p->token.kind == kind;
}

static bool laye_parser_at2(laye_parser* p, laye_token_kind kind1, laye_token_kind kind2) {
    assert(p != NULL);
    return p->token.kind == kind1 || p->token.kind == kind2;
}

static bool laye_parser_is_eof(laye_parser* p) {
    assert(p != NULL);
    return laye_parser_at(p, LAYE_TOKEN_EOF);
}

static void laye_parser_peek(laye_parser* p) {
    assert(p != NULL);

    if (p->next_token.kind != LAYE_TOKEN_INVALID)
        return;

    laye_token current_token = p->token;
    laye_next_token(p);

    p->next_token = p->token;
    p->token = current_token;
}

static bool laye_parser_peek_at(laye_parser* p, laye_token_kind kind) {
    assert(p != NULL);
    laye_parser_peek(p); // ensure peek token avail
    return p->next_token.kind == kind;
}

static bool laye_parser_consume(laye_parser* p, laye_token_kind kind, laye_token* out_token) {
    assert(p != NULL);

    if (laye_parser_at(p, kind)) {
        if (out_token != NULL) *out_token = p->token;
        laye_next_token(p);
        return true;
    }

    return false;
}

static void laye_parser_expect(laye_parser* p, laye_token_kind kind, laye_parse_result* result) {
    assert(p != NULL);
    assert(result != NULL);

    if (!laye_parser_consume(p, kind, NULL)) {
        assert((kind > __LAYE_PRINTABLE_TOKEN_START__ && kind <= __LAYE_PRINTABLE_TOKEN_END__) && "support certain non-printable kinds");
        arr_push(result->diags, layec_error(p->context, p->token.location, "Expected '%c'.", (int)kind));
    }
}

static bool laye_parser_consume_assignment(laye_parser* p, laye_token* out_token) {
    assert(p != NULL);

    switch (p->token.kind) {
        default: return false;

        case '=':
        case LAYE_TOKEN_LESSMINUS: {
            if (out_token != NULL) *out_token = p->token;
            laye_next_token(p);
            return true;
        }
    }
}

static void laye_parser_try_synchronize(laye_parser* p, laye_token_kind kind) {
    assert(p != NULL);
    while (!laye_parser_at2(p, LAYE_TOKEN_EOF, kind)) {
        laye_next_token(p);
    }
}

static void laye_parser_try_synchronize_to_end_of_node(laye_parser* p) {
    assert(p != NULL);
    while (!laye_parser_at(p, LAYE_TOKEN_EOF) && !laye_parser_at2(p, '}', ';')) {
        laye_next_token(p);
    }
}

static laye_node* laye_parser_create_invalid_node_from_token(laye_parser* p) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);

    laye_node* invalid_node = laye_node_create(p->module, LAYE_NODE_INVALID, p->token.location, LTY(p->context->laye_types._void));
    laye_next_token(p);
    return invalid_node;
}

static laye_node* laye_parser_create_invalid_node_from_child(laye_parser* p, laye_node* node) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(node != NULL);

    laye_node* invalid_node = laye_node_create(p->module, LAYE_NODE_INVALID, node->location, LTY(p->context->laye_types._void));
    return invalid_node;
}

static dynarr(laye_node*) laye_parse_attributes(laye_parser* p, laye_parse_result* result);
static bool laye_can_parse_type(laye_parser* p);
static laye_parse_result laye_parse_type(laye_parser* p);
static laye_parse_result laye_parse_declaration(laye_parser* p, bool can_be_expression, bool consume_semi);

static laye_parse_result laye_parse_statement(laye_parser* p, bool consume_semi);
static laye_parse_result laye_parse_expression(laye_parser* p);

static laye_node* laye_parse_top_level_node(laye_parser* p) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);

    PARSE_UNWRAP(top_level_declaration, laye_parse_declaration, p, false, true);
    assert(top_level_declaration != NULL);

    return top_level_declaration;
}

static bool laye_parse_type_modifiable_modifiers(laye_parser* p, laye_parse_result* result, bool allocate) {
    bool type_is_modifiable = false;

    {
        laye_token mut_token = {0};
        while (laye_parser_consume(p, LAYE_TOKEN_MUT, &mut_token)) {
            assert(mut_token.kind == LAYE_TOKEN_MUT);

            if (type_is_modifiable && result->success) {
                result->success = false;
                if (allocate) {
                    arr_push(result->diags, layec_error(p->context, mut_token.location, "Duplicate type modifier 'mut'."));
                }
            }

            type_is_modifiable = true;
        }
    }

    return type_is_modifiable;
}

static laye_parse_result laye_try_parse_type_continue(laye_parser* p, laye_type type, bool allocate) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);

    struct laye_parser_mark start_mark = laye_parser_mark(p);
    // NOTE(local): `laye_type` is a value type, so this copy here can safely be modified without affecting the contents of the parameter
    laye_parse_result result = laye_parse_result_success_type(type);

    switch (p->token.kind) {
        default: return result;

        case '[': {
            laye_next_token(p);
            if (laye_parser_at(p, '*') && laye_parser_peek_at(p, ']')) {
                laye_next_token(p);
                if (allocate) {
                    result.type.node = laye_node_create(p->module, LAYE_NODE_TYPE_BUFFER, type.node->location, LTY(p->context->laye_types.type));
                    assert(result.type.node != NULL);
                    result.type.node->type_container.element_type = type;
                }
            } else if (laye_parser_at(p, ']')) {
                if (allocate) {
                    result.type.node = laye_node_create(p->module, LAYE_NODE_TYPE_SLICE, type.node->location, LTY(p->context->laye_types.type));
                    assert(result.type.node != NULL);
                    result.type.node->type_container.element_type = type;
                }
            } else {
                dynarr(laye_node*) length_values = NULL;

                laye_parse_result expr_result = laye_parse_expression(p);
                assert(expr_result.node != NULL);
                laye_parse_result_copy_diags(&result, expr_result);
                laye_parse_result_destroy(expr_result);
                if (allocate) {
                    arr_push(length_values, expr_result.type.node);
                }

                // laye_parse_result
                //  we'll error when we don't see ']', so nothing special to do here other than allocate
                if (allocate) {
                    result.type.node = laye_node_create(p->module, LAYE_NODE_TYPE_ARRAY, type.node->location, LTY(p->context->laye_types.type));
                    assert(result.type.node != NULL);
                    assert(arr_count(length_values) != 0);
                    result.type.node->type_container.length_values = length_values;
                    result.type.node->type_container.element_type = type;
                }
            }

            laye_token closing_token = {0};
            if (!laye_parser_consume(p, ']', &closing_token)) {
                if (allocate) {
                    result.success = false;
                    arr_push(result.diags, layec_error(p->context, p->token.location, "Expected ']'."));
                }
            }

            if (allocate) {
                assert(result.type.node != NULL);
                result.type.node->location.length = closing_token.location.offset + closing_token.location.length - result.type.node->location.offset;
            }
        } break;

        case '*': {
            laye_token star_token = p->token;
            laye_next_token(p);

            if (allocate) {
                result.type.node = laye_node_create(p->module, LAYE_NODE_TYPE_POINTER, type.node->location, LTY(p->context->laye_types.type));
                assert(result.type.node != NULL);
                result.type.node->type_container.element_type = type;
                result.type.node->location.length = star_token.location.offset + star_token.location.length - result.type.node->location.offset;
            }
        } break;

        case '&': {
            laye_token amp_token = p->token;
            laye_next_token(p);

            if (allocate) {
                result.type.node = laye_node_create(p->module, LAYE_NODE_TYPE_REFERENCE, type.node->location, LTY(p->context->laye_types.type));
                assert(result.type.node != NULL);
                result.type.node->type_container.element_type = type;
                result.type.node->location.length = amp_token.location.offset + amp_token.location.length - result.type.node->location.offset;
            }
        } break;
    }

    bool type_is_modifiable = laye_parse_type_modifiable_modifiers(p, &result, allocate);
    if (allocate) {
        assert(result.type.node != NULL);
        result.type.is_modifiable = type_is_modifiable;
    }

    result = laye_parse_result_combine_type(result, laye_try_parse_type_continue(p, result.type, allocate));
    if (!allocate) {
        assert(result.type.node == NULL);
        laye_parser_reset_to_mark(p, start_mark);
        assert(p->lexer_position == start_mark.lexer_position);
        assert(p->token.kind == start_mark.token.kind);
        assert(p->token.location.offset == start_mark.token.location.offset);
        assert(p->next_token.kind == start_mark.next_token.kind);
        assert(p->next_token.location.offset == start_mark.next_token.location.offset);
    }

    return result;
}

static laye_parse_result laye_try_parse_type_impl(laye_parser* p, bool allocate, bool allow_functions) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);

    struct laye_parser_mark start_mark = laye_parser_mark(p);
    laye_parse_result result = {
        .success = true,
    };

    bool type_is_modifiable = false;
    // NOTE(local): if we want to disable "west-mut", remove this line.
    type_is_modifiable |= laye_parse_type_modifiable_modifiers(p, &result, allocate);

    switch (p->token.kind) {
        default: {
            result.success = false;
            if (allocate) {
                laye_node* poison = laye_node_create(p->module, LAYE_NODE_TYPE_POISON, p->token.location, LTY(p->context->laye_types.type));
                result = laye_parse_result_combine_type(
                    result,
                    laye_parse_result_failure_type(laye_type_qualify(poison, false), layec_error(p->context, p->token.location, "Unexpected token when a type was expected."))
                );
                laye_next_token(p);
            }
        } break;

        case LAYE_TOKEN_IDENT: {
            layec_location nameref_location = p->token.location;
            assert(p->scope != NULL);

            laye_nameref type_nameref = laye_parse_nameref(p, &result, &nameref_location, allocate);
            assert(type_nameref.scope != NULL);

            if (allocate) {
                assert(arr_count(type_nameref.pieces) > 0);
                result.type.node = laye_node_create(p->module, LAYE_NODE_TYPE_NAMEREF, p->token.location, LTY(p->context->laye_types.type));
                assert(result.type.node != NULL);
                result.type.node->nameref = type_nameref;
            } else {
                assert(type_nameref.pieces == NULL);
                assert(type_nameref.template_arguments == NULL);
            }
        } break;

        case LAYE_TOKEN_VOID: {
            if (allocate) {
                result.type.node = laye_node_create(p->module, LAYE_NODE_TYPE_VOID, p->token.location, LTY(p->context->laye_types.type));
                assert(result.type.node != NULL);
            }
            laye_next_token(p);
        } break;

        case LAYE_TOKEN_NORETURN: {
            if (allocate) {
                result.type.node = laye_node_create(p->module, LAYE_NODE_TYPE_NORETURN, p->token.location, LTY(p->context->laye_types.type));
                assert(result.type.node != NULL);
            }
            laye_next_token(p);
        } break;

        case LAYE_TOKEN_BOOL: {
            if (allocate) {
                result.type.node = laye_node_create(p->module, LAYE_NODE_TYPE_BOOL, p->token.location, LTY(p->context->laye_types.type));
                assert(result.type.node != NULL);
                result.type.node->type_primitive.bit_width = p->context->target->laye.size_of_bool;
                result.type.node->type_primitive.is_platform_specified = true;
            }
            laye_next_token(p);
        } break;

        case LAYE_TOKEN_BOOLSIZED: {
            if (allocate) {
                result.type.node = laye_node_create(p->module, LAYE_NODE_TYPE_BOOL, p->token.location, LTY(p->context->laye_types.type));
                assert(result.type.node != NULL);
                assert(p->token.int_value > 0 && p->token.int_value < 65536);
                result.type.node->type_primitive.bit_width = (int)p->token.int_value;
            }
            laye_next_token(p);
        } break;

        case LAYE_TOKEN_INT: {
            if (allocate) {
                result.type.node = laye_node_create(p->module, LAYE_NODE_TYPE_INT, p->token.location, LTY(p->context->laye_types.type));
                assert(result.type.node != NULL);
                result.type.node->type_primitive.bit_width = p->context->target->laye.size_of_int;
                result.type.node->type_primitive.is_signed = true;
                result.type.node->type_primitive.is_platform_specified = true;
            }
            laye_next_token(p);
        } break;

        case LAYE_TOKEN_INTSIZED: {
            if (allocate) {
                result.type.node = laye_node_create(p->module, LAYE_NODE_TYPE_INT, p->token.location, LTY(p->context->laye_types.type));
                assert(result.type.node != NULL);
                assert(p->token.int_value > 0 && p->token.int_value < 65536);
                result.type.node->type_primitive.bit_width = (int)p->token.int_value;
                result.type.node->type_primitive.is_signed = true;
            }
            laye_next_token(p);
        } break;

        case LAYE_TOKEN_UINT: {
            if (allocate) {
                result.type.node = laye_node_create(p->module, LAYE_NODE_TYPE_INT, p->token.location, LTY(p->context->laye_types.type));
                assert(result.type.node != NULL);
                result.type.node->type_primitive.bit_width = p->context->target->laye.size_of_int;
                result.type.node->type_primitive.is_signed = false;
                result.type.node->type_primitive.is_platform_specified = true;
            }
            laye_next_token(p);
        } break;

        case LAYE_TOKEN_UINTSIZED: {
            if (allocate) {
                result.type.node = laye_node_create(p->module, LAYE_NODE_TYPE_INT, p->token.location, LTY(p->context->laye_types.type));
                assert(result.type.node != NULL);
                assert(p->token.int_value > 0 && p->token.int_value < 65536);
                result.type.node->type_primitive.bit_width = (int)p->token.int_value;
                result.type.node->type_primitive.is_signed = false;
            }
            laye_next_token(p);
        } break;

        case LAYE_TOKEN_FLOATSIZED: {
            if (allocate) {
                result.type.node = laye_node_create(p->module, LAYE_NODE_TYPE_FLOAT, p->token.location, LTY(p->context->laye_types.type));
                assert(result.type.node != NULL);
                assert(p->token.int_value > 0 && p->token.int_value <= 128);
                result.type.node->type_primitive.bit_width = (int)p->token.int_value;
            }
            laye_next_token(p);
        } break;
    }

    type_is_modifiable |= laye_parse_type_modifiable_modifiers(p, &result, allocate);
    if (allocate) {
        assert(result.type.node != NULL);
        result.type.is_modifiable = type_is_modifiable;
    }

    result = laye_parse_result_combine_type(result, laye_try_parse_type_continue(p, result.type, allocate));
    if (!allocate) {
        assert(result.type.node == NULL);
        laye_parser_reset_to_mark(p, start_mark);
        assert(p->lexer_position == start_mark.lexer_position);
        assert(p->token.kind == start_mark.token.kind);
        assert(p->token.location.offset == start_mark.token.location.offset);
        assert(p->next_token.kind == start_mark.next_token.kind);
        assert(p->next_token.location.offset == start_mark.next_token.location.offset);
    }

    return result;
}

static bool laye_can_parse_type(laye_parser* p) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);

    laye_parse_result result = laye_try_parse_type_impl(p, false, true);
    laye_parse_result_destroy(result);
    return result.success;
}

static laye_parse_result laye_parse_type(laye_parser* p) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);

    return laye_try_parse_type_impl(p, true, true);
}

static laye_type laye_parse_type_or_error(laye_parser* p) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);

    laye_parse_result result = laye_try_parse_type_impl(p, true, true);
    if (result.success) {
        assert(arr_count(result.diags) == 0);
        assert(result.node != NULL);
        return result.type;
    }

    laye_parse_result_write_diags(p->context, result);
    laye_parse_result_destroy(result);
    return result.type;
}

static void laye_apply_attributes(laye_node* node, dynarr(laye_node*) attributes) {
    assert(node != NULL);
    assert(laye_node_is_decl(node));

    node->attribute_nodes = attributes;

    for (int64_t i = 0, count = arr_count(attributes); i < count; i++) {
        laye_node* attribute = attributes[i];
        assert(attribute != NULL);

        switch (attribute->meta_attribute.kind) {
            default: assert(false && "unreachable"); break;

            case LAYE_TOKEN_CALLCONV: {
                node->attributes.calling_convention = attribute->meta_attribute.calling_convention;
            } break;

            case LAYE_TOKEN_DISCARDABLE: {
                node->attributes.is_discardable = true;
            } break;

            case LAYE_TOKEN_EXPORT: {
                node->attributes.linkage = LAYEC_LINK_EXPORTED;
            } break;

            case LAYE_TOKEN_FOREIGN: {
                node->attributes.mangling = attribute->meta_attribute.mangling;
                node->attributes.foreign_name = attribute->meta_attribute.foreign_name;
            } break;

            case LAYE_TOKEN_INLINE: {
                node->attributes.is_inline = true;
            } break;
        }
    }
}

static void laye_expect_semi(laye_parser* p, laye_parse_result* result) {
    assert(p != NULL);
    assert(p->context != NULL);

    if (!laye_parser_consume(p, ';', NULL)) {
        if (result) {
            arr_push(result->diags, layec_error(p->context, p->token.location, "Expected ';'."));
        } else {
            layec_write_error(p->context, p->token.location, "Expected ';'.");
        }
    }
}

static dynarr(laye_node*) laye_parse_attributes(laye_parser* p, laye_parse_result* result) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);

    dynarr(laye_node*) attributes = NULL;

    int64_t last_iteration_token_position = p->token.location.offset;
    while (p->token.kind != LAYE_TOKEN_EOF) {
        last_iteration_token_position = p->token.location.offset;
        switch (p->token.kind) {
            default: goto done_parsing_attributes;

            case LAYE_TOKEN_EXPORT:
            case LAYE_TOKEN_DISCARDABLE:
            case LAYE_TOKEN_INLINE: {
                laye_node* simple_attribute_node = laye_node_create(p->module, LAYE_NODE_META_ATTRIBUTE, p->token.location, LTY(p->context->laye_types._void));
                assert(simple_attribute_node != NULL);
                simple_attribute_node->meta_attribute.kind = p->token.kind;
                simple_attribute_node->meta_attribute.keyword_token = p->token;

                laye_next_token(p);
                arr_push(attributes, simple_attribute_node);
            } break;

            case LAYE_TOKEN_FOREIGN: {
                laye_node* foreign_node = laye_node_create(p->module, LAYE_NODE_META_ATTRIBUTE, p->token.location, LTY(p->context->laye_types._void));
                assert(foreign_node != NULL);
                foreign_node->meta_attribute.kind = p->token.kind;
                foreign_node->meta_attribute.keyword_token = p->token;
                foreign_node->meta_attribute.mangling = LAYEC_MANGLE_NONE;

                laye_next_token(p);
                arr_push(attributes, foreign_node);

                if (laye_parser_consume(p, '(', NULL)) {
                    if (p->token.kind == LAYE_TOKEN_IDENT) {
                        string_view mangling_kind_name = p->token.string_value;
                        if (string_view_equals(mangling_kind_name, SV_CONSTANT("none"))) {
                            foreign_node->meta_attribute.mangling = LAYEC_MANGLE_NONE;
                        } else if (string_view_equals(mangling_kind_name, SV_CONSTANT("laye"))) {
                            foreign_node->meta_attribute.mangling = LAYEC_MANGLE_LAYE;
                        } else {
                            layec_write_error(
                                p->context,
                                p->token.location,
                                "Unknown name mangling kind '%.*s'. Expected one of 'none' or 'laye'.",
                                STR_EXPAND(mangling_kind_name)
                            );
                        }

                        laye_next_token(p);
                    } else {
                        layec_write_error(
                            p->context,
                            p->token.location,
                            "Expected an identifier as the foreign name mangling kind. Expected one of 'none' or 'laye'."
                        );

                        laye_parser_try_synchronize(p, ')');
                    }

                    if (!laye_parser_consume(p, ')', NULL)) {
                        layec_write_error(p->context, p->token.location, "Expected ')' to close foreign name mangling kind parameter.");
                    }
                }

                laye_token foreign_name_token = {0};
                if (laye_parser_consume(p, LAYE_TOKEN_LITSTRING, &foreign_name_token)) {
                    foreign_node->meta_attribute.foreign_name = foreign_name_token.string_value;
                }
            } break;

            case LAYE_TOKEN_CALLCONV: {
                laye_node* callconv_node = laye_node_create(p->module, LAYE_NODE_META_ATTRIBUTE, p->token.location, LTY(p->context->laye_types._void));
                assert(callconv_node != NULL);
                callconv_node->meta_attribute.kind = p->token.kind;
                callconv_node->meta_attribute.keyword_token = p->token;

                laye_next_token(p);
                arr_push(attributes, callconv_node);

                if (laye_parser_consume(p, '(', NULL)) {
                    if (p->token.kind == LAYE_TOKEN_IDENT) {
                        string_view callconv_kind_name = p->token.string_value;
                        if (string_view_equals(callconv_kind_name, SV_CONSTANT("cdecl"))) {
                            callconv_node->meta_attribute.calling_convention = LAYEC_CCC;
                        } else if (string_view_equals(callconv_kind_name, SV_CONSTANT("laye"))) {
                            callconv_node->meta_attribute.calling_convention = LAYEC_LAYECC;
                        } else {
                            layec_write_error(
                                p->context,
                                p->token.location,
                                "Unknown calling convention kind '%.*s'. Expected one of 'cdecl' or 'laye'.",
                                STR_EXPAND(callconv_kind_name)
                            );
                        }

                        laye_next_token(p);
                    } else {
                        layec_write_error(
                            p->context,
                            p->token.location,
                            "Expected an identifier as the calling convention kind. Expected one of 'cdecl' or 'laye'."
                        );

                        laye_parser_try_synchronize(p, ')');
                    }

                    if (!laye_parser_consume(p, ')', NULL)) {
                        layec_write_error(p->context, p->token.location, "Expected ')' to close calling convention kind parameter.");
                    }
                }
            } break;
        }

        assert(p->token.location.offset != last_iteration_token_position);
    }

done_parsing_attributes:;
    return attributes;
}

static laye_parse_result laye_parse_compound_expression(laye_parser* p) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind == '{');

    layec_location start_location = p->token.location;
    laye_next_token(p);

    laye_node* compound_expression = laye_node_create(p->module, LAYE_NODE_COMPOUND, p->token.location, LTY(p->context->laye_types._void));
    laye_parse_result result = laye_parse_result_success(compound_expression);

    laye_parser_push_scope(p);

    while (!laye_parser_at2(p, LAYE_TOKEN_EOF, '}')) {
        result = laye_parse_result_combine(result, laye_parse_declaration(p, true, true));
        assert(result.node != NULL);
        arr_push(compound_expression->compound.children, result.node);
    }

    result.node = compound_expression;

    layec_location end_location = {0};
    laye_token closing_token = {0};

    if (laye_parser_consume(p, '}', &closing_token)) {
        assert(closing_token.kind == '}');
        end_location = closing_token.location;
    } else {
        if (arr_count(compound_expression->compound.children) == 0) {
            end_location = start_location;
        } else {
            end_location = (*arr_back(compound_expression->compound.children))->location;
        }

        result = laye_parse_result_combine(
            result,
            laye_parse_result_failure(compound_expression, layec_error(p->context, p->token.location, "Expected '}'."))
        );
    }

    laye_parser_pop_scope(p);

    layec_location total_location = start_location;
    assert(end_location.offset >= start_location.offset);
    total_location.length = (end_location.offset + end_location.length) - start_location.offset;
    assert(total_location.length >= start_location.length);
    compound_expression->location = total_location;

    return result;
}

static laye_parse_result laye_parse_import_declaration(laye_parser* p, dynarr(laye_node*) attributes) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind == LAYE_TOKEN_IMPORT);

    laye_node* import_decl = laye_node_create(p->module, LAYE_NODE_DECL_IMPORT, p->token.location, LTY(p->context->laye_types._void));
    assert(import_decl != NULL);
    laye_apply_attributes(import_decl, attributes);

    laye_parse_result result = laye_parse_result_success(import_decl);
    laye_next_token(p);

    if (laye_parser_at2(p, LAYE_TOKEN_LITSTRING, LAYE_TOKEN_IDENT) && (laye_parser_peek_at(p, ';')/* || laye_parser_peek_at(p, LAYE_TOKEN_AS)*/)) {
        import_decl->decl_import.module_name = p->token;
        laye_next_token(p);
        goto parse_end;
    }

    laye_token temp_token = {0};
    do {
        if (laye_parser_consume(p, '*', &temp_token)) {
            if (import_decl->decl_import.is_wildcard) {
                arr_push(result.diags, layec_error(p->context, temp_token.location, "Duplicate wildcard specifier in import declaration."));
            }

            import_decl->decl_import.is_wildcard = true;

            laye_node* query_wildcard = laye_node_create(p->module, LAYE_NODE_IMPORT_QUERY, temp_token.location, LTY(p->context->laye_types._void));
            query_wildcard->import_query.is_wildcard = true;

            arr_push(import_decl->decl_import.import_queries, query_wildcard);
            continue;
        }

        dynarr(laye_token) pieces = NULL;
        laye_token alias = {0};

        laye_token identifier_token = {0};
        if (!laye_parser_consume(p, LAYE_TOKEN_IDENT, &identifier_token)) {
            arr_push(result.diags, layec_error(p->context, p->token.location, "Expected identifier as import query."));
            continue;
        } else {
            arr_push(pieces, identifier_token);
        }

        while (laye_parser_consume(p, LAYE_TOKEN_COLONCOLON, NULL)) {
            if (!laye_parser_consume(p, LAYE_TOKEN_IDENT, &identifier_token)) {
                arr_push(result.diags, layec_error(p->context, p->token.location, "Expected identifier to continue import query."));
                break;
            } else {
                arr_push(pieces, identifier_token);
            }
        }

        if (laye_parser_consume(p, LAYE_TOKEN_AS, NULL)) {
            if (!laye_parser_consume(p, LAYE_TOKEN_IDENT, &alias)) {
                arr_push(result.diags, layec_error(p->context, p->token.location, "Expected identifier as import query alias."));
            }
        }

        if (laye_parser_at(p, ';') && !import_decl->decl_import.is_wildcard && arr_count(import_decl->decl_import.import_queries) == 0 && arr_count(pieces) == 1) {
            import_decl->decl_import.module_name = pieces[0];
            import_decl->decl_import.import_alias = alias;
            arr_free(pieces);
            goto parse_end;
        }

        assert(arr_count(pieces) > 0);

        layec_location query_location = pieces[0].location;
        if (alias.kind != 0) {
            query_location = alias.location;
        } else if (arr_count(pieces) > 1) {
            query_location = pieces[arr_count(pieces) - 1].location;
        }

        laye_node* query = laye_node_create(p->module, LAYE_NODE_IMPORT_QUERY, query_location, LTY(p->context->laye_types._void));
        query->import_query.pieces = pieces;
        query->import_query.alias = alias;

        arr_push(import_decl->decl_import.import_queries, query);
    } while (laye_parser_consume(p, ',', NULL));

    if (!laye_parser_consume(p, LAYE_TOKEN_FROM, NULL)) {
        arr_push(result.diags, layec_error(p->context, p->token.location, "Expected 'from'."));
    }

    if (laye_parser_at2(p, LAYE_TOKEN_LITSTRING, LAYE_TOKEN_IDENT) && (laye_parser_peek_at(p, ';') || laye_parser_peek_at(p, LAYE_TOKEN_AS))) {
        import_decl->decl_import.module_name = p->token;
        laye_next_token(p);
    } else {
        arr_push(result.diags, layec_error(p->context, p->token.location, "Expected identifier or string as module name."));
    }

parse_alias_or_end:;

    if (laye_parser_consume(p, LAYE_TOKEN_AS, NULL)) {
        if (!laye_parser_consume(p, LAYE_TOKEN_IDENT, &import_decl->decl_import.import_alias)) {
            arr_push(result.diags, layec_error(p->context, p->token.location, "Expected an identifier as import declaration alias."));
        }
    }

parse_end:;
    laye_expect_semi(p, &result);

    return result;
}

static laye_parse_result laye_parse_struct_declaration(laye_parser* p, dynarr(laye_node*) attributes) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind == LAYE_TOKEN_STRUCT || p->token.kind == LAYE_TOKEN_VARIANT);

    laye_node* struct_decl = laye_node_create(p->module, LAYE_NODE_DECL_STRUCT, p->token.location, LTY(p->context->laye_types._void));
    assert(struct_decl != NULL);
    laye_apply_attributes(struct_decl, attributes);

    laye_parse_result result = laye_parse_result_success(struct_decl);

    bool is_variant = p->token.kind == LAYE_TOKEN_VAR;
    laye_next_token(p);

    laye_token ident_token = {0};
    if (!laye_parser_consume(p, LAYE_TOKEN_IDENT, &ident_token)) {
        result = laye_parse_result_combine(
            result,
            laye_parse_result_failure(struct_decl, layec_error(p->context, p->token.location, "Expected an identifier to name this %s.", is_variant ? "variant" : "struct"))
        );

        if (!laye_parser_at(p, '{')) {
            return result;
        }
    } else {
        struct_decl->declared_name = ident_token.string_value;

        assert(p->scope != NULL);
        laye_scope_declare(p->scope, struct_decl);
    }

    if (!laye_parser_consume(p, '{', NULL)) {
        return laye_parse_result_combine(
            result,
            laye_parse_result_failure(struct_decl, layec_error(p->context, p->token.location, "Expected '{' to begin this %s.", is_variant ? "variant" : "struct"))
        );
    }

    while (!laye_parser_at2(p, LAYE_TOKEN_EOF, '}')) {
        if (laye_parser_at2(p, LAYE_TOKEN_STRUCT, LAYE_TOKEN_VARIANT)) {
            bool is_variant = p->token.kind == LAYE_TOKEN_VARIANT;
            if (!is_variant) {
                result = laye_parse_result_combine(
                    result,
                    laye_parse_result_failure(struct_decl, layec_error(p->context, p->token.location, "Struct variants must be declared with the `variant` keyword."))
                );

                laye_parse_result variant_result = laye_parse_struct_declaration(p, NULL);
                laye_node* variant_node = variant_result.node;
                assert(variant_node != NULL);
                result = laye_parse_result_combine(result, variant_result);
                result.node = struct_decl;

                arr_push(struct_decl->decl_struct.variant_declarations, variant_node);
            }

            continue;
        }

        laye_type field_type = laye_parse_type_or_error(p);
        assert(field_type.node != NULL);

        laye_token field_name_token = { .location = p->token.location };
        if (!laye_parser_consume(p, LAYE_TOKEN_IDENT, &field_name_token)) {
            result = laye_parse_result_combine(
                result,
                laye_parse_result_failure(struct_decl, layec_error(p->context, p->token.location, "Expected an identifier to name this field."))
            );
        }

        laye_expect_semi(p, &result);

        laye_node* field_node = laye_node_create(p->module, LAYE_NODE_DECL_STRUCT_FIELD, field_name_token.location, LTY(p->context->laye_types._void));
        assert(field_node != NULL);
        field_node->declared_type = field_type;
        field_node->declared_name = field_name_token.string_value;

        arr_push(struct_decl->decl_struct.field_declarations, field_node);
    }

    if (!laye_parser_consume(p, '}', NULL)) {
        result = laye_parse_result_combine(
            result,
            laye_parse_result_failure(struct_decl, layec_error(p->context, p->token.location, "Expected '}' to end this %s.", is_variant ? "variant" : "struct"))
        );
    }

    result.node = struct_decl;
    return result;
}

static laye_parse_result laye_parse_test_declaration(laye_parser* p) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind == LAYE_TOKEN_TEST);

    laye_parse_result result = {
        .success = true,
    };

    laye_node* test_node = laye_node_create(p->module, LAYE_NODE_DECL_TEST, p->token.location, LTY(p->context->laye_types._void));
    assert(test_node != NULL);
    laye_next_token(p);

    // the name is optional, and can be one of these two things.
    if (!laye_parser_consume(p, LAYE_TOKEN_LITSTRING, &test_node->decl_test.description)) {
        if (laye_parser_at(p, LAYE_TOKEN_IDENT)) {
            test_node->decl_test.is_named = true;
            test_node->decl_test.nameref = laye_parse_nameref(p, &result, NULL, true);
        } else {
            arr_push(result.diags, layec_error(p->context, p->token.location, "Test declaration must either reference a declaration by name or have a string description."));
        }
    }

    if (!laye_parser_at(p, '{')) {
        arr_push(result.diags, layec_error(p->context, p->token.location, "Expected a test body, starting with a '{'."));
        goto return_test_decl;
    }

    result = laye_parse_result_combine(result, laye_parse_compound_expression(p));
    assert(result.node != NULL);
    test_node->decl_test.body = result.node;

return_test_decl:;
    result.node = test_node;
    return result;
}

static laye_parse_result laye_parse_declaration_continue(laye_parser* p, dynarr(laye_node*) attributes, laye_type declared_type, laye_token name_token, bool consume_semi) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);

    if (laye_parser_consume(p, '(', NULL)) {
        dynarr(laye_type) parameter_types = NULL;
        dynarr(laye_node*) parameters = NULL;

        laye_varargs_style varargs_style = LAYE_VARARGS_NONE;
        bool has_errored_for_additional_params = false;

        while (!laye_parser_at2(p, LAYE_TOKEN_EOF, ')')) {
            if (varargs_style != LAYE_VARARGS_NONE && !has_errored_for_additional_params) {
                has_errored_for_additional_params = true;
                layec_write_error(p->context, p->token.location, "Additional parameters are not allowed after `varargs`.");
            }

            if (laye_parser_consume(p, LAYE_TOKEN_VARARGS, NULL)) {
                varargs_style = LAYE_VARARGS_C;
                if (laye_parser_at2(p, LAYE_TOKEN_EOF, ')') || laye_parser_consume(p, ',', NULL)) {
                    continue;
                } else {
                    varargs_style = LAYE_VARARGS_LAYE;
                }
            }

            laye_type parameter_type = laye_parse_type_or_error(p);
            assert(parameter_type.node != NULL);
            arr_push(parameter_types, parameter_type);

            laye_token name_token = p->token;
            if (!laye_parser_consume(p, LAYE_TOKEN_IDENT, NULL)) {
                layec_write_error(p->context, p->token.location, "Expected an identifier.");
                name_token.kind = LAYE_TOKEN_INVALID;
                name_token.location.length = 0;
                name_token.string_value = SV_CONSTANT("<invalid>");
            }

            layec_location parameter_location = name_token.location.length != 0 ? name_token.location : parameter_type.node->location;
            laye_node* parameter_node = laye_node_create(p->module, LAYE_NODE_DECL_FUNCTION_PARAMETER, parameter_location, parameter_type);
            assert(parameter_node != NULL);
            parameter_node->declared_type = parameter_type;
            parameter_node->declared_name = name_token.string_value;
            assert(parameter_node->declared_name.count > 0);

            arr_push(parameters, parameter_node);

            if (laye_parser_consume(p, ',', NULL)) {
                if (laye_parser_at2(p, LAYE_TOKEN_EOF, ')')) {
                    layec_write_error(p->context, p->token.location, "Expected a type.");
                    break;
                }
            } else {
                break;
            }
        }

        if (!laye_parser_consume(p, ')', NULL)) {
            layec_write_error(p->context, p->token.location, "Expected ')' to close function parameter list.");
            laye_parser_try_synchronize(p, ')');
        }

        laye_node* function_type = laye_node_create(p->module, LAYE_NODE_TYPE_FUNCTION, name_token.location, LTY(p->context->laye_types.type));
        assert(function_type != NULL);
        function_type->type_function.return_type = declared_type;
        function_type->type_function.parameter_types = parameter_types;
        function_type->type_function.varargs_style = varargs_style;

        laye_node* function_node = laye_node_create(p->module, LAYE_NODE_DECL_FUNCTION, name_token.location, LTY(p->context->laye_types._void));
        assert(function_node != NULL);
        laye_apply_attributes(function_node, attributes);
        function_node->declared_name = name_token.string_value;
        function_node->declared_type = LTY(function_type);
        function_node->decl_function.return_type = declared_type;
        function_node->decl_function.parameter_declarations = parameters;
        assert(p->scope != NULL);
        laye_scope_declare(p->scope, function_node);

        function_type->type_function.calling_convention = function_node->attributes.calling_convention;

        laye_parser_push_scope(p);
        p->scope->name = name_token.string_value;
        p->scope->is_function_scope = true;

        for (int64_t i = 0, count = arr_count(parameters); i < count; i++) {
            laye_scope_declare(p->scope, parameters[i]);
        }

        laye_node* function_body = NULL;
        if (!laye_parser_consume(p, ';', NULL)) {
            if (laye_parser_consume(p, LAYE_TOKEN_EQUALGREATER, NULL)) {
                PARSE_UNWRAP(function_body_expr, laye_parse_expression, p);
                assert(function_body_expr != NULL);

                if (!laye_parser_consume(p, ';', NULL)) {
                    layec_write_error(p->context, p->token.location, "Expected ';'.");
                }

                laye_node* implicit_return_node = laye_node_create(p->module, LAYE_NODE_RETURN, function_body_expr->location, LTY(p->context->laye_types.noreturn));
                assert(implicit_return_node != NULL);
                implicit_return_node->compiler_generated = true;
                implicit_return_node->_return.value = function_body_expr;

                function_body = laye_node_create(p->module, LAYE_NODE_COMPOUND, function_body_expr->location, LTY(p->context->laye_types.noreturn));
                assert(function_body != NULL);
                function_body->compiler_generated = true;
                //function_body->compound.scope_name = name_token.string_value;
                arr_push(function_body->compound.children, implicit_return_node);
                assert(1 == arr_count(function_body->compound.children));
            } else {
                if (!laye_parser_at(p, '{')) {
                    layec_write_error(p->context, p->token.location, "Expected '{'.");
                    function_body = laye_node_create(p->module, LAYE_NODE_INVALID, p->token.location, LTY(p->context->laye_types.poison));
                    assert(function_body != NULL);
                } else {
                    laye_parse_result function_body_result = laye_parse_compound_expression(p);
                    assert(function_body_result.node != NULL);
                    function_body = function_body_result.node;
                    laye_parse_result_write_diags(p->context, function_body_result);
                    laye_parse_result_destroy(function_body_result);
                }
            }

            assert(function_body != NULL);
        }

        laye_parser_pop_scope(p);

        function_node->decl_function.body = function_body;
        return laye_parse_result_success(function_node);
    }

    laye_node* binding_node = laye_node_create(p->module, LAYE_NODE_DECL_BINDING, name_token.location, LTY(p->context->laye_types._void));
    assert(binding_node != NULL);
    laye_apply_attributes(binding_node, attributes);
    binding_node->declared_type = declared_type;
    binding_node->declared_name = name_token.string_value;
    assert(p->scope != NULL);
    laye_scope_declare(p->scope, binding_node);

    if (laye_parser_consume(p, '=', NULL)) {
        PARSE_UNWRAP(initial_value, laye_parse_expression, p);
        assert(initial_value != NULL);
        binding_node->decl_binding.initializer = initial_value;
    }

    laye_parse_result result = laye_parse_result_success(binding_node);
    if (consume_semi) laye_expect_semi(p, &result);

    return result;
}

static laye_parse_result laye_parse_declaration(laye_parser* p, bool can_be_expression, bool consume_semi) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);

    struct laye_parser_mark start_mark = laye_parser_mark(p);

    laye_parse_result result = {
        .success = true,
    };

    dynarr(laye_node*) attributes = laye_parse_attributes(p, &result);

    switch (p->token.kind) {
        case LAYE_TOKEN_INVALID: assert(false && "unreachable"); return (laye_parse_result){0};

        case LAYE_TOKEN_ASSERT:
        case LAYE_TOKEN_IF:
        case LAYE_TOKEN_FOR:
        case LAYE_TOKEN_WHILE:
        case LAYE_TOKEN_RETURN:
        case LAYE_TOKEN_BREAK:
        case LAYE_TOKEN_CONTINUE:
        case LAYE_TOKEN_YIELD:
        case LAYE_TOKEN_XYZZY: {
            if (arr_count(attributes) != 0) {
                result.success = false;
                arr_push(result.diags, layec_error(p->context, p->token.location, "Cannot apply attributes to statements."));
                arr_free(attributes);
            }

            return laye_parse_result_combine(result, laye_parse_statement(p, consume_semi));
        }

        case LAYE_TOKEN_IMPORT: {
            return laye_parse_result_combine(result, laye_parse_import_declaration(p, attributes));
        }

        case LAYE_TOKEN_STRUCT: {
            return laye_parse_result_combine(result, laye_parse_struct_declaration(p, attributes));
        }

        case LAYE_TOKEN_TEST: {
            if (arr_count(attributes) != 0) {
                result.success = false;
                arr_push(result.diags, layec_error(p->context, p->token.location, "Cannot apply attributes to test declarations."));
                arr_free(attributes);
            }

            return laye_parse_result_combine(result, laye_parse_test_declaration(p));
        }

        default: {
            laye_parse_result declared_type_result = laye_parse_type(p);
            assert(declared_type_result.node != NULL);

            if (!declared_type_result.success) {
                laye_parse_result_destroy(declared_type_result);
                arr_free(attributes);

                if (can_be_expression) {
                    laye_parser_reset_to_mark(p, start_mark);
                    return laye_parse_statement(p, consume_semi);
                }

                laye_node* invalid_node = laye_parser_create_invalid_node_from_child(p, declared_type_result.node);
                invalid_node->attribute_nodes = attributes;
                laye_parser_try_synchronize_to_end_of_node(p);
                return laye_parse_result_failure(invalid_node, layec_error(p->context, invalid_node->location, "Expected 'import', 'struct', 'enum', or a function declaration."));
            }

            laye_token name_token = {0};
            if (!laye_parser_consume(p, LAYE_TOKEN_IDENT, &name_token)) {
                if (can_be_expression) {
                    laye_parse_result_destroy(declared_type_result);
                    arr_free(attributes);
                    laye_parser_reset_to_mark(p, start_mark);
                    return laye_parse_statement(p, consume_semi);
                }

                laye_node* invalid_node = laye_parser_create_invalid_node_from_token(p);
                invalid_node->attribute_nodes = attributes;
                return laye_parse_result_combine(
                    declared_type_result,
                    laye_parse_result_failure(invalid_node, layec_error(p->context, invalid_node->location, "Expected an identifier."))
                );
            }

            laye_type declared_type = declared_type_result.type;
            laye_parse_result_destroy(declared_type_result);

            return laye_parse_declaration_continue(p, attributes, declared_type, name_token, consume_semi);
        }
    }

    assert(false && "unreachable");
    return (laye_parse_result){0};
}

static laye_parse_result laye_parse_primary_expression_continue(laye_parser* p, laye_node* primary_expr) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);
    assert(primary_expr != NULL);

    laye_parse_result result = laye_parse_result_success(primary_expr);

    switch (p->token.kind) {
        default: {
            return result;
        }

        case '.': {
            laye_token field_token = { .location = p->token.location };
            laye_next_token(p);

            if (!laye_parser_consume(p, LAYE_TOKEN_IDENT, &field_token)) {
                arr_push(result.diags, layec_error(p->context, field_token.location, "Expected an identifier as the member name."));
            }
            
            laye_node* member_expr = laye_node_create(p->module, LAYE_NODE_MEMBER, field_token.location, LTY(p->context->laye_types.unknown));
            assert(member_expr != NULL);
            member_expr->member.value = primary_expr;
            member_expr->member.field_name = field_token;

            return laye_parse_result_combine(result, laye_parse_primary_expression_continue(p, member_expr));
        }

        case '(': {
            laye_next_token(p);

            dynarr(laye_node*) arguments = NULL;
            if (!laye_parser_at(p, ')')) {
                do {
                    result = laye_parse_result_combine(result, laye_parse_expression(p));
                    assert(result.node != NULL);
                    arr_push(arguments, result.node);
                } while (laye_parser_consume(p, ',', NULL));
            }

            if (!laye_parser_consume(p, ')', NULL)) {
                arr_push(result.diags, layec_error(p->context, p->token.location, "Expected ')'."));
            }

            laye_node* call_expr = laye_node_create(p->module, LAYE_NODE_CALL, primary_expr->location, LTY(p->context->laye_types.unknown));
            assert(call_expr != NULL);
            call_expr->call.callee = primary_expr;
            call_expr->call.arguments = arguments;

            return laye_parse_result_combine(result, laye_parse_primary_expression_continue(p, call_expr));
        }

        case '[': {
            laye_next_token(p);

            dynarr(laye_node*) indices = NULL;
            if (!laye_parser_at(p, ']')) {
                do {
                    result = laye_parse_result_combine(result, laye_parse_expression(p));
                    assert(result.node != NULL);
                    arr_push(indices, result.node);
                } while (laye_parser_consume(p, ',', NULL));
            }

            if (!laye_parser_consume(p, ']', NULL)) {
                arr_push(result.diags, layec_error(p->context, p->token.location, "Expected ']'."));
            }

            laye_node* index_expr = laye_node_create(p->module, LAYE_NODE_INDEX, primary_expr->location, LTY(p->context->laye_types.unknown));
            assert(index_expr != NULL);
            index_expr->index.value = primary_expr;
            index_expr->index.indices = indices;

            return laye_parse_result_combine(result, laye_parse_primary_expression_continue(p, index_expr));
        }
    }

    assert(false && "unreachable");
    return (laye_parse_result){0};
}

static void laye_parse_if_only(laye_parser* p, bool expr_context, laye_parse_result* result, laye_node** condition, laye_node** body) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind == LAYE_TOKEN_IF);
    assert(condition != NULL);
    assert(body != NULL);

    laye_next_token(p);

    if (!laye_parser_consume(p, '(', NULL)) {
        layec_write_error(p->context, p->token.location, "Expected '(' to open `if` condition.");
    }

    *result = laye_parse_result_combine(*result, laye_parse_expression(p));
    laye_node* if_condition = result->node;
    assert(if_condition != NULL);

    if (!laye_parser_consume(p, ')', NULL)) {
        layec_write_error(p->context, p->token.location, "Expected ')' to close `if` condition.");
    }

    laye_node* if_body = NULL;
    // we're doing this check to generate errors earlier, it's not technically necessary
    if (laye_parser_at(p, '{')) {
        *result = laye_parse_result_combine(*result, laye_parse_compound_expression(p));
        if_body = result->node;
    } else {
        if (expr_context) {
            *result = laye_parse_result_combine(*result, laye_parse_expression(p));
            if_body = result->node;
        } else {
            arr_push(result->diags, layec_error(p->context, p->token.location, "Expected '{' to open `if` body. (Compound expressions are currently required, but may not be in future versions.)"));
            *result = laye_parse_result_combine(*result, laye_parse_statement(p, true));
            if_body = result->node;
        }
    }

    assert(if_body != NULL);

    *condition = if_condition;
    *body = if_body;
}

static laye_parse_result laye_parse_if(laye_parser* p, bool expr_context) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);

    layec_location if_location = p->token.location;

    laye_node* if_condition = NULL;
    laye_node* if_body = NULL;

    layec_location total_location = p->token.location;
    laye_parse_result result = {
        .success = true,
    };

    laye_parse_if_only(p, expr_context, &result, &if_condition, &if_body);

    assert(if_condition != NULL);
    assert(if_body != NULL);

    total_location = layec_location_combine(total_location, if_body->location);
    laye_node* if_result = laye_node_create(p->module, LAYE_NODE_IF, total_location, LTY(p->context->laye_types._void));
    assert(if_result != NULL);

    arr_push(if_result->_if.conditions, if_condition);
    arr_push(if_result->_if.passes, if_body);

    while (laye_parser_at(p, LAYE_TOKEN_ELSE)) {
        laye_next_token(p);

        if (laye_parser_at(p, LAYE_TOKEN_IF)) {
            laye_node* elseif_condition = NULL;
            laye_node* elseif_body = NULL;

            laye_parse_if_only(p, expr_context, &result, &elseif_condition, &elseif_body);

            assert(elseif_condition != NULL);
            assert(elseif_body != NULL);

            arr_push(if_result->_if.conditions, elseif_condition);
            arr_push(if_result->_if.passes, elseif_body);

            total_location = layec_location_combine(total_location, elseif_body->location);
        } else {
            laye_node* else_body = NULL;
            // we're doing this check to generate errors earlier, it's not technically necessary
            if (laye_parser_at(p, '{')) {
                result = laye_parse_result_combine(result, laye_parse_compound_expression(p));
                else_body = result.node;
            } else {
                if (expr_context) {
                    result = laye_parse_result_combine(result, laye_parse_expression(p));
                    else_body = result.node;
                } else {
                    arr_push(result.diags, layec_error(p->context, p->token.location, "Expected '{' to open `else` body. (Compound expressions are currently required, but may not be in future versions.)"));
                    result = laye_parse_result_combine(result, laye_parse_statement(p, true));
                    else_body = result.node;
                }
            }

            if_result->_if.fail = else_body;
            total_location = layec_location_combine(total_location, else_body->location);
        }
    }

    if_result->location = total_location;
    result.node = if_result;

    return result;
}

static laye_nameref laye_parse_nameref(laye_parser* p, laye_parse_result* result, layec_location* location, bool allocate) {
    assert(p != NULL);
    assert(p->scope != NULL);
    assert(p->scope->module != NULL);

    laye_nameref nameref = {
        .scope = p->scope
    };

    layec_location last_name_location = p->token.location;
    if (location != NULL) {
        if (location->offset == 0 && location->length == 0) {
            *location = last_name_location;
        } else {
            *location = layec_location_combine(*location, last_name_location);
        }
    }

    if (allocate) {
        arr_push(nameref.pieces, p->token);
    }
    laye_next_token(p);

    while (laye_parser_at(p, LAYE_TOKEN_COLONCOLON)) {
        laye_next_token(p);

        laye_token next_piece = {0};
        if (!laye_parser_consume(p, LAYE_TOKEN_IDENT, &next_piece)) {
            result->success = false;
            arr_push(result->diags, layec_error(p->context, p->token.location, "Expected identifier."));
            break;
        }

        last_name_location = next_piece.location;
        if (location != NULL) {
            *location = layec_location_combine(*location, last_name_location);
        }

        if (allocate) {
            arr_push(nameref.pieces, next_piece);
        }
    }

    return nameref;
}

static laye_parse_result laye_parse_primary_expression(laye_parser* p) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);

    switch (p->token.kind) {
        default: {
            layec_location token_location = p->token.location;
            if (!laye_parser_at(p, ';')) {
                laye_next_token(p);
            }

            laye_node* invalid_expr = laye_node_create(p->module, LAYE_NODE_INVALID, token_location, LTY(p->context->laye_types.poison));
            assert(invalid_expr != NULL);

            return laye_parse_result_failure(invalid_expr, layec_error(p->context, token_location, "Unexpected token. Expected an expression."));
        }

        case LAYE_TOKEN_CAST: {
            laye_node* cast_node = laye_node_create(p->module, LAYE_NODE_CAST, p->token.location, LTY(p->context->laye_types.unknown));
            assert(cast_node != NULL);
            cast_node->cast.kind = LAYE_CAST_HARD;

            laye_next_token(p);

            laye_parse_result result = laye_parse_result_success(cast_node);

            laye_parser_expect(p, '(', &result);
            result = laye_parse_result_combine_type(result, laye_parse_type(p));
            cast_node->type = result.type;
            laye_parser_expect(p, ')', &result);

            result = laye_parse_result_combine(result, laye_parse_primary_expression(p));
            cast_node->cast.operand = result.node;

            result.node = cast_node;
            cast_node->location = layec_location_combine(cast_node->location, cast_node->cast.operand->location);

            return result;
        }

        case '(': {
            layec_location start_location = p->token.location;
            laye_next_token(p);

            laye_parse_result expr_result = laye_parse_expression(p);
            assert(expr_result.node != NULL);

            laye_token close_token = {0};
            if (laye_parser_consume(p, ')', &close_token)) {
                start_location.length = close_token.location.offset + close_token.location.length - start_location.offset;
                expr_result.node->location = start_location;
            } else {
                expr_result = laye_parse_result_combine(
                    expr_result,
                    laye_parse_result_failure(expr_result.node, layec_error(p->context, expr_result.node->location, "Expected ')'."))
                );
            }

            return laye_parse_result_combine(expr_result, laye_parse_primary_expression_continue(p, expr_result.node));
        } break;

        case '{': {
            laye_parse_result expr_result = laye_parse_compound_expression(p);
            assert(expr_result.node != NULL);
            expr_result.node->compound.is_expr = true;
            return laye_parse_result_combine(expr_result, laye_parse_primary_expression_continue(p, expr_result.node));
        } break;

        case '-':
        case '+':
        case '~': {
            laye_token operator_token = p->token;
            laye_next_token(p);

            laye_parse_result operand_result = laye_parse_primary_expression(p);
            assert(operand_result.node != NULL);
            assert(operand_result.node->type.node != NULL);

            laye_node* expr = laye_node_create(p->module, LAYE_NODE_UNARY, operand_result.node->location, operand_result.node->type);
            assert(expr != NULL);
            expr->unary.operand = operand_result.node;
            expr->unary.operator = operator_token;
            expr->location = layec_location_combine(operator_token.location, expr->location);

            return laye_parse_result_combine(operand_result, laye_parse_result_success(expr));
        } break;

        case '&': {
            laye_token operator_token = p->token;
            laye_next_token(p);

            laye_parse_result operand_result = laye_parse_primary_expression(p);
            assert(operand_result.node != NULL);
            assert(operand_result.node->type.node != NULL);

            laye_node* reftype = laye_node_create(p->module, LAYE_NODE_TYPE_POINTER, operand_result.node->location, LTY(p->context->laye_types.type));
            assert(reftype != NULL);
            reftype->type_container.element_type = operand_result.node->type;

            laye_node* expr = laye_node_create(p->module, LAYE_NODE_UNARY, operand_result.node->location, LTY(reftype));
            assert(expr != NULL);
            expr->unary.operand = operand_result.node;
            expr->unary.operator = operator_token;
            expr->location = layec_location_combine(operator_token.location, expr->location);

            return laye_parse_result_combine(operand_result, laye_parse_result_success(expr));
        } break;

        case '*': {
            laye_token operator_token = p->token;
            laye_next_token(p);

            laye_parse_result operand_result = laye_parse_primary_expression(p);
            assert(operand_result.node != NULL);
            assert(operand_result.node->type.node != NULL);

            laye_type elemtype = {0};
            if (operand_result.node->type.node->kind == LAYEC_TYPE_POINTER) {
                elemtype = operand_result.node->type.node->type_container.element_type;
            } else {
                elemtype = LTY(p->context->laye_types.unknown);
            }

            assert(elemtype.node != NULL);

            laye_node* expr = laye_node_create(p->module, LAYE_NODE_UNARY, operand_result.node->location, elemtype);
            assert(expr != NULL);
            laye_expr_set_lvalue(expr, true);
            expr->unary.operand = operand_result.node;
            expr->unary.operator = operator_token;
            expr->location = layec_location_combine(operator_token.location, expr->location);

            return laye_parse_result_combine(operand_result, laye_parse_result_success(expr));
        } break;

        case LAYE_TOKEN_NOT: {
            laye_token operator_token = p->token;
            laye_next_token(p);

            laye_parse_result operand_result = laye_parse_primary_expression(p);
            assert(operand_result.node != NULL);
            assert(operand_result.node->type.node != NULL);

            laye_node* expr = laye_node_create(p->module, LAYE_NODE_UNARY, operand_result.node->location, LTY(p->context->laye_types._bool));
            assert(expr != NULL);
            expr->unary.operand = operand_result.node;
            expr->unary.operator = operator_token;
            expr->location = layec_location_combine(operator_token.location, expr->location);

            return laye_parse_result_combine(operand_result, laye_parse_result_success(expr));
        } break;

        case LAYE_TOKEN_IF: {
            laye_parse_result if_result = laye_parse_if(p, true);
            assert(if_result.node != NULL);
            if_result.node->_if.is_expr = true;
            return if_result;
        } break;

        case LAYE_TOKEN_IDENT: {
            laye_node* nameref_expr = laye_node_create(p->module, LAYE_NODE_NAMEREF, p->token.location, LTY(p->context->laye_types.unknown));
            assert(nameref_expr != NULL);

            laye_parse_result nameref_result = laye_parse_result_success(nameref_expr);
            nameref_expr->nameref = laye_parse_nameref(p, &nameref_result, &nameref_expr->location, true);

            return laye_parse_result_combine(nameref_result, laye_parse_primary_expression_continue(p, nameref_expr));
        }

        case LAYE_TOKEN_TRUE:
        case LAYE_TOKEN_FALSE: {
            laye_node* litbool_expr = laye_node_create(p->module, LAYE_NODE_LITBOOL, p->token.location, LTY(p->context->laye_types.unknown));
            assert(litbool_expr != NULL);
            litbool_expr->litbool.value = p->token.kind == LAYE_TOKEN_TRUE;
            litbool_expr->type = LTY(p->context->laye_types._bool);
            laye_next_token(p);
            return laye_parse_primary_expression_continue(p, litbool_expr);
        }

        case LAYE_TOKEN_LITINT: {
            laye_node* litint_expr = laye_node_create(p->module, LAYE_NODE_LITINT, p->token.location, LTY(p->context->laye_types.unknown));
            assert(litint_expr != NULL);
            litint_expr->litint.value = p->token.int_value;
            litint_expr->type = LTY(p->context->laye_types._int);
            laye_next_token(p);
            return laye_parse_primary_expression_continue(p, litint_expr);
        }

        case LAYE_TOKEN_LITSTRING: {
            laye_node* litstr_expr = laye_node_create(p->module, LAYE_NODE_LITSTRING, p->token.location, LTY(p->context->laye_types.unknown));
            assert(litstr_expr != NULL);
            litstr_expr->litstring.value = p->token.string_value;
            litstr_expr->type = LTY(p->context->laye_types.i8_buffer);
            laye_next_token(p);
            return laye_parse_primary_expression_continue(p, litstr_expr);
        }
    }

    assert(false && "unreachable");
    return (laye_parse_result){0};
}

static laye_parse_result laye_parse_foreach_from_names(laye_parser* p, laye_parse_result foreach_result) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);

    assert(laye_parser_at(p, LAYE_TOKEN_ENUM) || (laye_parser_at(p, LAYE_TOKEN_IDENT) && laye_parser_peek_at(p, ':')));

    laye_node* foreach_node = foreach_result.node;
    assert(foreach_node != NULL);
    assert(foreach_node->kind == LAYE_NODE_FOREACH);
    
    if (laye_parser_consume(p, LAYE_TOKEN_ENUM, NULL)) {
        laye_token index_name_token = {0};
        if (laye_parser_consume(p, LAYE_TOKEN_IDENT, &index_name_token)) {
            foreach_node->foreach.index_binding = laye_node_create(p->module, LAYE_NODE_DECL_BINDING, index_name_token.location, LTY(p->context->laye_types._void));
            assert(foreach_node->foreach.index_binding != NULL);
            foreach_node->foreach.index_binding->declared_name = index_name_token.string_value;
            foreach_node->foreach.index_binding->declared_type = LTY(p->context->laye_types.var);
            assert(p->scope != NULL);
            laye_scope_declare(p->scope, foreach_node->foreach.index_binding);
        } else {
            arr_push(foreach_result.diags, layec_error(p->context, p->token.location, "Expected identifer as iterator index binding name."));
        }

        if (!laye_parser_consume(p, ',', NULL)) {
            arr_push(foreach_result.diags, layec_error(p->context, p->token.location, "Expected ','."));
        }
    }

    laye_token element_name_token = {0};
    if (laye_parser_consume(p, LAYE_TOKEN_IDENT, &element_name_token)) {
        foreach_node->foreach.element_binding = laye_node_create(p->module, LAYE_NODE_DECL_BINDING, element_name_token.location, LTY(p->context->laye_types._void));
        assert(foreach_node->foreach.element_binding != NULL);
        foreach_node->foreach.element_binding->declared_name = element_name_token.string_value;
        foreach_node->foreach.element_binding->declared_type = LTY(p->context->laye_types.var);
        assert(p->scope != NULL);
        laye_scope_declare(p->scope, foreach_node->foreach.element_binding);
    } else {
        arr_push(foreach_result.diags, layec_error(p->context, p->token.location, "Expected identifer as iterator element binding name."));
    }

    assert(foreach_node->foreach.element_binding != NULL);

    if (!laye_parser_consume(p, ':', NULL)) {
        arr_push(foreach_result.diags, layec_error(p->context, p->token.location, "Expected ':'."));
    }

    foreach_result = laye_parse_result_combine(foreach_result, laye_parse_expression(p));
    assert(foreach_result.node != NULL);
    foreach_node->foreach.iterable = foreach_result.node;

    if (!laye_parser_consume(p, ')', NULL)) {
        arr_push(foreach_result.diags, layec_error(p->context, p->token.location, "Expected ')'."));
    }

    if (laye_parser_at(p, '{')) {
        foreach_result = laye_parse_result_combine(foreach_result, laye_parse_compound_expression(p));
        foreach_node->foreach.pass = foreach_result.node;
    } else {
        arr_push(foreach_result.diags, layec_error(p->context, p->token.location, "Expected '{' to open `for` body. (Compound expressions are currently required, but may not be in future versions.)"));
        foreach_result = laye_parse_result_combine(foreach_result, laye_parse_statement(p, true));
        foreach_node->foreach.pass = foreach_result.node;
    }

    foreach_result.node = foreach_node;
    return foreach_result;
}

static laye_parse_result laye_parse_for(laye_parser* p) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind == LAYE_TOKEN_FOR);

    layec_location for_location = p->token.location;
    laye_next_token(p);

    laye_node* for_node = laye_node_create(p->module, LAYE_NODE_FOR, for_location, LTY(p->context->laye_types._void));
    assert(for_node != NULL);
    laye_parser_push_break_continue_target(p, SV_EMPTY, for_node);

    laye_parse_result for_result = laye_parse_result_success(for_node);

    if (!laye_parser_consume(p, '(', NULL)) {
        arr_push(for_result.diags, layec_error(p->context, p->token.location, "Expected '('."));
    }

    if (laye_parser_at(p, LAYE_TOKEN_ENUM) || (laye_parser_at(p, LAYE_TOKEN_IDENT) && laye_parser_peek_at(p, ':'))) {
        for_node->kind = LAYE_NODE_FOREACH;
        for_result.node = for_node;
        return laye_parse_foreach_from_names(p, for_result);
    }

    laye_node* first_node = NULL;
    if (laye_parser_at(p, ';')) {
        first_node = laye_node_create(p->module, LAYE_NODE_XYZZY, p->token.location, LTY(p->context->laye_types._void));
        assert(first_node != NULL);
        first_node->compiler_generated = true;
    } else {
        for_result = laye_parse_result_combine(for_result, laye_parse_declaration(p, true, false));
        first_node = for_result.node;
    }

    assert(first_node != NULL);

    for_node->_for.initializer = first_node;
    laye_expect_semi(p, &for_result);

    if (!laye_parser_at(p, ';')) {
        for_result = laye_parse_result_combine(for_result, laye_parse_expression(p));
        for_node->_for.condition = for_result.node;
    } else {
        for_node->_for.condition = laye_node_create(p->module, LAYE_NODE_LITBOOL, p->token.location, LTY(p->context->laye_types._bool));
        assert(for_node->_for.condition != NULL);
        for_node->_for.condition->compiler_generated = true;
        for_node->_for.condition->litbool.value = true;
    }

    assert(for_node->_for.condition != NULL);
    laye_expect_semi(p, &for_result);

    if (!laye_parser_at(p, ')')) {
        for_result = laye_parse_result_combine(for_result, laye_parse_statement(p, false));
        for_node->_for.increment = for_result.node;
    } else {
        for_node->_for.increment = laye_node_create(p->module, LAYE_NODE_XYZZY, p->token.location, LTY(p->context->laye_types._void));
        assert(for_node->_for.increment != NULL);
        for_node->_for.increment->compiler_generated = true;
    }

    assert(for_node->_for.increment != NULL);

    if (!laye_parser_consume(p, ')', NULL)) {
        arr_push(for_result.diags, layec_error(p->context, p->token.location, "Expected ')'."));
    }

    if (laye_parser_at(p, '{')) {
        for_result = laye_parse_result_combine(for_result, laye_parse_compound_expression(p));
        for_node->_for.pass = for_result.node;
    } else {
        arr_push(for_result.diags, layec_error(p->context, p->token.location, "Expected '{' to open `for` body. (Compound expressions are currently required, but may not be in future versions.)"));
        for_result = laye_parse_result_combine(for_result, laye_parse_statement(p, true));
        for_node->_for.pass = for_result.node;
    }

    if (laye_parser_consume(p, LAYE_TOKEN_ELSE, NULL)) {
        if (laye_parser_at(p, '{')) {
            for_result = laye_parse_result_combine(for_result, laye_parse_compound_expression(p));
            for_node->_for.fail = for_result.node;
        } else {
            arr_push(for_result.diags, layec_error(p->context, p->token.location, "Expected '{' to open `for` `else` body. (Compound expressions are currently required, but may not be in future versions.)"));
            for_result = laye_parse_result_combine(for_result, laye_parse_statement(p, true));
            for_node->_for.fail = for_result.node;
        }
    }
    
    for_result.node = for_node;
    return for_result;
}

static laye_parse_result laye_parse_while(laye_parser* p) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind == LAYE_TOKEN_WHILE);

    layec_location while_location = p->token.location;
    laye_next_token(p);

    laye_node* while_node = laye_node_create(p->module, LAYE_NODE_WHILE, while_location, LTY(p->context->laye_types._void));
    assert(while_node != NULL);
    laye_parser_push_break_continue_target(p, SV_EMPTY, while_node);

    laye_parse_result while_result = laye_parse_result_success(while_node);

    if (laye_parser_at(p, '{')) {
        while_result = laye_parse_result_combine(while_result, laye_parse_compound_expression(p));
        assert(while_result.node != NULL);
        while_node->_while.pass = while_result.node;

        while_result.node = while_node;
        return while_result;
    }

    if (!laye_parser_consume(p, '(', NULL)) {
        arr_push(while_result.diags, layec_error(p->context, p->token.location, "Expected '('."));
    }

    while_result = laye_parse_result_combine(while_result, laye_parse_expression(p));
    while_node->_while.condition = while_result.node;
    assert(while_node->_while.condition != NULL);

    if (!laye_parser_consume(p, ')', NULL)) {
        arr_push(while_result.diags, layec_error(p->context, p->token.location, "Expected ')'."));
    }

    if (laye_parser_at(p, '{')) {
        while_result = laye_parse_result_combine(while_result, laye_parse_compound_expression(p));
        while_node->_while.pass = while_result.node;
    } else {
        arr_push(while_result.diags, layec_error(p->context, p->token.location, "Expected '{' to open `while` body. (Compound expressions are currently required, but may not be in future versions.)"));
        while_result = laye_parse_result_combine(while_result, laye_parse_statement(p, true));
        while_node->_while.pass = while_result.node;
    }

    if (laye_parser_consume(p, LAYE_TOKEN_ELSE, NULL)) {
        if (laye_parser_at(p, '{')) {
            while_result = laye_parse_result_combine(while_result, laye_parse_compound_expression(p));
            while_node->_while.fail = while_result.node;
        } else {
            arr_push(while_result.diags, layec_error(p->context, p->token.location, "Expected '{' to open `while` `else` body. (Compound expressions are currently required, but may not be in future versions.)"));
            while_result = laye_parse_result_combine(while_result, laye_parse_statement(p, true));
            while_node->_while.fail = while_result.node;
        }
    }
    
    while_result.node = while_node;
    return while_result;
}

static laye_parse_result laye_maybe_parse_assignment(laye_parser* p, laye_node* lhs, bool consume_semi) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);
    assert(lhs != NULL);

    // TODO(local): assignment+operator
    laye_token assign_op = {0};
    if (!laye_parser_consume_assignment(p, &assign_op)) {
        return laye_parse_result_success(lhs);
    }

    assert(assign_op.kind != LAYE_TOKEN_INVALID);

    laye_parse_result rhs_result = laye_parse_expression(p);
    assert(rhs_result.node != NULL);

    laye_node* assign = laye_node_create(p->module, LAYE_NODE_ASSIGNMENT, assign_op.location, LTY(p->context->laye_types._void));
    assert(assign != NULL);
    assign->assignment.lhs = lhs;
    assign->assignment.reference_reassign = assign_op.kind == LAYE_TOKEN_LESSMINUS;
    assign->assignment.rhs = rhs_result.node;

    if (consume_semi) laye_expect_semi(p, &rhs_result);
    return laye_parse_result_combine(rhs_result, laye_parse_result_success(assign));
}

static laye_parse_result laye_parse_binary_expression(laye_parser* p, laye_node* lhs, int precedence);

static laye_parse_result laye_parse_statement(laye_parser* p, bool consume_semi) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);

    laye_parse_result result = {
        .success = true,
    };

    switch (p->token.kind) {
        case '{': {
            result = laye_parse_compound_expression(p);
            assert(result.node != NULL);

            laye_node* compound_only = result.node;

            result = laye_parse_result_combine(result, laye_maybe_parse_assignment(p, result.node, consume_semi));
            assert(result.node != NULL);

            if (result.node != compound_only) {
                compound_only->compound.is_expr = true;
            }
        } break;

        case LAYE_TOKEN_ASSERT: {
            laye_node* assert_node = laye_node_create(p->module, LAYE_NODE_ASSERT, p->token.location, LTY(p->context->laye_types._void));
            assert(assert_node != NULL);

            result = laye_parse_result_combine(result, laye_parse_result_success(assert_node));
            laye_next_token(p);

            result = laye_parse_result_combine(result, laye_parse_expression(p));
            assert(result.node != NULL);
            assert_node->_assert.condition = result.node;

            if (laye_parser_consume(p, ',', NULL)) {
                if (!laye_parser_consume(p, LAYE_TOKEN_LITSTRING, &assert_node->_assert.message)) {
                    arr_push(result.diags, layec_error(p->context, p->token.location, "Expected string literal for assert message."));
                }
            }

            result.node = assert_node;
            if (consume_semi) laye_expect_semi(p, &result);
        } break;

        case LAYE_TOKEN_IF: {
            result = laye_parse_if(p, false);
            assert(result.node != NULL);
        } break;

        case LAYE_TOKEN_FOR: {
            laye_parser_push_scope(p);
            int64_t initial_break_continue_count = arr_count(p->break_continue_stack);
            result = laye_parse_for(p);
            assert(initial_break_continue_count + 1 == arr_count(p->break_continue_stack));
            laye_parser_pop_break_continue_target(p);
            laye_parser_pop_scope(p);
            assert(result.node != NULL);
        } break;

        case LAYE_TOKEN_WHILE: {
            laye_parser_push_scope(p);
            int64_t initial_break_continue_count = arr_count(p->break_continue_stack);
            result = laye_parse_while(p);
            assert(initial_break_continue_count + 1 == arr_count(p->break_continue_stack));
            laye_parser_pop_break_continue_target(p);
            laye_parser_pop_scope(p);
            assert(result.node != NULL);
        } break;

        case LAYE_TOKEN_DEFER: {
            result = laye_parse_result_success(laye_node_create(p->module, LAYE_NODE_DEFER, p->token.location, LTY(p->context->laye_types._void)));
            assert(result.node != NULL);
            laye_next_token(p);
            
            laye_parse_result body_result = laye_parse_statement(p, false);
            assert(body_result.node != NULL);
            result.node->defer.body = body_result.node;
            laye_parse_result_copy_diags(&result, body_result);
            laye_parse_result_destroy(body_result);

            if (consume_semi) laye_expect_semi(p, &result);
        } break;

        case LAYE_TOKEN_RETURN: {
            result = laye_parse_result_success(laye_node_create(p->module, LAYE_NODE_RETURN, p->token.location, LTY(p->context->laye_types.noreturn)));
            assert(result.node != NULL);
            laye_next_token(p);

            if (!laye_parser_at(p, ';')) {
                laye_parse_result value_result = laye_parse_expression(p);
                result.node->_return.value = value_result.node;
                assert(result.node->_return.value != NULL);
                laye_parse_result_copy_diags(&result, value_result);
                laye_parse_result_destroy(value_result);
            }

            if (consume_semi) laye_expect_semi(p, &result);
        } break;

        case LAYE_TOKEN_BREAK: {
            result = laye_parse_result_success(laye_node_create(p->module, LAYE_NODE_BREAK, p->token.location, LTY(p->context->laye_types._void)));
            assert(result.node != NULL);
            laye_next_token(p);
            laye_token target_label = {0};
            if (laye_parser_consume(p, LAYE_TOKEN_IDENT, &target_label)) {
                result.node->_break.target = target_label.string_value;
            }
            if (consume_semi) laye_expect_semi(p, &result);

            if (arr_count(p->break_continue_stack) == 0) {
                layec_write_error(p->context, result.node->location, "`break` statement can only occur within a `for` loop.");
            } else {
                if (result.node->_break.target.count != 0) {
                    layec_write_error(p->context, result.node->location, "`break` currently does not support label targets.");
                } else {
                    break_continue_target bc_targ = laye_parser_peek_break_continue_target(p);
                    assert(bc_targ.target != NULL);
                    result.node->_break.target_node = bc_targ.target;
                    switch (bc_targ.target->kind) {
                        default: assert(false && "unreachable"); break;
                        case LAYE_NODE_FOR: {
                            bc_targ.target->_for.has_breaks = true;
                        } break;
                        case LAYE_NODE_FOREACH: {
                            bc_targ.target->foreach.has_breaks = true;
                        } break;
                        case LAYE_NODE_WHILE: {
                            bc_targ.target->_while.has_breaks = true;
                        } break;
                        case LAYE_NODE_DOWHILE: {
                            bc_targ.target->dowhile.has_breaks = true;
                        } break;
                    }
                }
            }
        } break;

        case LAYE_TOKEN_CONTINUE: {
            result = laye_parse_result_success(laye_node_create(p->module, LAYE_NODE_CONTINUE, p->token.location, LTY(p->context->laye_types._void)));
            assert(result.node != NULL);
            laye_next_token(p);
            laye_token target_label = {0};
            if (laye_parser_consume(p, LAYE_TOKEN_IDENT, &target_label)) {
                result.node->_continue.target = target_label.string_value;
            }
            if (consume_semi) laye_expect_semi(p, &result);

            if (arr_count(p->break_continue_stack) == 0) {
                layec_write_error(p->context, result.node->location, "`continue` statement can only occur within a `for` loop.");
            } else {
                if (result.node->_continue.target.count != 0) {
                    layec_write_error(p->context, result.node->location, "`continue` currently does not support label targets.");
                } else {
                    break_continue_target bc_targ = laye_parser_peek_break_continue_target(p);
                    assert(bc_targ.target != NULL);
                    result.node->_continue.target_node = bc_targ.target;
                    switch (bc_targ.target->kind) {
                        default: assert(false && "unreachable"); break;
                        case LAYE_NODE_FOR: {
                            bc_targ.target->_for.has_continues = true;
                        } break;
                        case LAYE_NODE_FOREACH: {
                            bc_targ.target->foreach.has_continues = true;
                        } break;
                        case LAYE_NODE_WHILE: {
                            bc_targ.target->_while.has_continues = true;
                        } break;
                        case LAYE_NODE_DOWHILE: {
                            bc_targ.target->dowhile.has_continues = true;
                        } break;
                    }
                }
            }
        } break;

        case LAYE_TOKEN_GOTO: {
            result = laye_parse_result_success(laye_node_create(p->module, LAYE_NODE_GOTO, p->token.location, LTY(p->context->laye_types._void)));
            assert(result.node != NULL);
            laye_next_token(p);
            laye_token target_label = {0};
            if (laye_parser_consume(p, LAYE_TOKEN_IDENT, &target_label)) {
                result.node->_goto.label = target_label.string_value;
            } else {
                arr_push(result.diags, layec_error(p->context, p->token.location, "Expected an identifier as `goto` target label name."));
            }
            if (consume_semi) laye_expect_semi(p, &result);
        } break;

        case LAYE_TOKEN_YIELD: {
            result = laye_parse_result_success(laye_node_create(p->module, LAYE_NODE_YIELD, p->token.location, LTY(p->context->laye_types._void)));
            assert(result.node != NULL);
            laye_next_token(p);

            laye_parse_result value_result = laye_parse_expression(p);
            result.node->yield.value = value_result.node;
            assert(result.node->yield.value != NULL);
            laye_parse_result_copy_diags(&result, value_result);
            laye_parse_result_destroy(value_result);

            if (consume_semi) laye_expect_semi(p, &result);
        } break;

        case LAYE_TOKEN_XYZZY: {
            result = laye_parse_result_success(laye_node_create(p->module, LAYE_NODE_XYZZY, p->token.location, LTY(p->context->laye_types._void)));
            assert(result.node != NULL);
            laye_next_token(p);
            if (consume_semi) laye_expect_semi(p, &result);
        } break;

        default: {
            if (laye_parser_at(p, LAYE_TOKEN_IDENT) && laye_parser_peek_at(p, ':')) {
                result = laye_parse_result_success(laye_node_create(p->module, LAYE_NODE_LABEL, p->token.location, LTY(p->context->laye_types._void)));
                assert(result.node != NULL);
                result.node->declared_name = p->token.string_value;
                laye_next_token(p);
                laye_next_token(p);
                break;
            }

            result = laye_parse_primary_expression(p);
            assert(result.node != NULL);

            laye_node* expr_only = result.node;

            result = laye_parse_result_combine(result, laye_maybe_parse_assignment(p, result.node, consume_semi));
            assert(result.node != NULL);

            if (result.node == expr_only) {
                result = laye_parse_result_combine(result, laye_parse_binary_expression(p, result.node, 0));
                assert(result.node != NULL);

                if (consume_semi) laye_expect_semi(p, &result);
            }
        } break;
    }

    assert(result.node != NULL);
    if (result.node->type.node == NULL) {
        result.node->type = LTY(p->context->laye_types._void);
    }

    return result;
}

static bool laye_parser_at_binary_operator_with_precedence(laye_parser* p, int precedence, int* next_precedence) {
    assert(p != NULL);
    assert(next_precedence != NULL);

    for (int64_t i = 0; operator_precedences[i].operator_kind != 0; i++) {
        if (!laye_parser_at(p, operator_precedences[i].operator_kind)) {
            continue;
        }

        int p = operator_precedences[i].precedence;
        if (p >= precedence) {
            *next_precedence = p;
            return true;
        }
    }

    return false;
}

static laye_parse_result laye_parse_binary_expression(laye_parser* p, laye_node* lhs, int precedence) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);

    laye_parse_result result = laye_parse_result_success(lhs);

    int next_precedence = 0;
    while (laye_parser_at_binary_operator_with_precedence(p, precedence, &next_precedence)) {
        // TODO(local): template early out

        laye_token operator_token = p->token;
        laye_next_token(p);

        laye_parse_result rhs_result = laye_parse_primary_expression(p);
        assert(rhs_result.node != NULL);

        int rhs_precedence = next_precedence;
        while (laye_parser_at_binary_operator_with_precedence(p, rhs_precedence, &next_precedence)) {
            rhs_result = laye_parse_result_combine(rhs_result, laye_parse_binary_expression(p, rhs_result.node, rhs_precedence));
            assert(rhs_result.node != NULL);
        }

        laye_node* binary_expr = laye_node_create(p->module, LAYE_NODE_BINARY, layec_location_combine(lhs->location, rhs_result.node->location), LTY(p->context->laye_types.unknown));
        assert(binary_expr != NULL);
        binary_expr->binary.operator= operator_token;
        binary_expr->binary.lhs = result.node;
        binary_expr->binary.rhs = rhs_result.node;

        result = laye_parse_result_combine(result, laye_parse_result_success(binary_expr));
        laye_parse_result_copy_diags(&result, rhs_result);
        laye_parse_result_destroy(rhs_result);
    }

    return result;
}

static laye_parse_result laye_parse_expression(laye_parser* p) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);

    laye_parse_result primary_result = laye_parse_primary_expression(p);
    assert(primary_result.node != NULL);

    if (!primary_result.success) {
        return primary_result;
    }

    return laye_parse_result_combine(primary_result, laye_parse_binary_expression(p, primary_result.node, 0));
}

// ========== Lexer ==========

static void laye_char_advance(laye_parser* p) {
    p->lexer_position++;

    if (p->lexer_position >= p->source.text.count) {
        p->lexer_position = p->source.text.count;
        p->current_char = 0;
        return;
    }

    p->current_char = p->source.text.data[p->lexer_position];
}

static char laye_char_peek(laye_parser* p) {
    int64_t peek_position = p->lexer_position + 1;
    if (peek_position >= p->source.text.count) {
        return 0;
    }

    return p->source.text.data[peek_position];
}

static layec_location laye_char_location(laye_parser* p) {
    return (layec_location){
        .sourceid = p->sourceid,
        .offset = p->lexer_position,
        .length = 1,
    };
}

static /* dynarr(laye_trivia) */ void laye_read_trivia(laye_parser* p, bool leading) {
    // dynarr(laye_trivia) trivia = NULL;

try_again:;
    while (p->current_char != 0) {
        char c = p->current_char;
        switch (c) {
            default: goto exit_loop;

            case ' ':
            case '\n':
            case '\r':
            case '\t':
            case '\v': {
                laye_char_advance(p);
                goto try_again;
            }

            case '#': {
                laye_trivia line_trivia = {
                    .kind = LAYE_TRIVIA_HASH_COMMENT,
                    .location.sourceid = p->sourceid,
                    .location.offset = p->lexer_position,
                };

                laye_char_advance(p);

                int64_t text_start_position = p->lexer_position;
                while (p->current_char != 0 && p->current_char != '\n') {
                    laye_char_advance(p);
                }

                int64_t text_end_position = p->lexer_position;
                string_view line_comment_text = string_slice(p->source.text, text_start_position, text_end_position - text_start_position);

                line_trivia.location.length = line_comment_text.count - 2;
                line_trivia.text = layec_context_intern_string_view(p->context, line_comment_text);

                // arr_push(trivia, line_trivia);

                if (!leading) goto exit_loop;
            } break;

            case '/': {
                if (laye_char_peek(p) == '/') {
                    laye_trivia line_trivia = {
                        .kind = LAYE_TRIVIA_LINE_COMMENT,
                        .location.sourceid = p->sourceid,
                        .location.offset = p->lexer_position,
                    };

                    laye_char_advance(p);
                    laye_char_advance(p);

                    int64_t text_start_position = p->lexer_position;
                    while (p->current_char != 0 && p->current_char != '\n') {
                        laye_char_advance(p);
                    }

                    int64_t text_end_position = p->lexer_position;
                    string_view line_comment_text = string_slice(p->source.text, text_start_position, text_end_position - text_start_position);

                    line_trivia.location.length = line_comment_text.count - 2;
                    line_trivia.text = layec_context_intern_string_view(p->context, line_comment_text);

                    // arr_push(trivia, line_trivia);

                    if (!leading) goto exit_loop;
                } else if (laye_char_peek(p) == '*') {
                    laye_trivia block_trivia = {
                        .kind = LAYE_TRIVIA_DELIMITED_COMMENT,
                        .location.sourceid = p->sourceid,
                        .location.offset = p->lexer_position,
                    };

                    laye_char_advance(p);
                    laye_char_advance(p);

                    int64_t text_start_position = p->lexer_position;

                    int nesting_count = 1;
                    char last_char = 0;

                    bool newline_encountered = false;
                    while (p->current_char != 0 && nesting_count > 0) {
                        if (p->current_char == '/' && last_char == '*') {
                            last_char = 0;
                            nesting_count--;
                        } else if (p->current_char == '*' && last_char == '/') {
                            last_char = 0;
                            nesting_count++;
                        } else {
                            if (p->current_char == '\n')
                                newline_encountered = true;
                            last_char = p->current_char;
                        }

                        laye_char_advance(p);
                    }

                    int64_t text_end_position = p->lexer_position - (nesting_count == 0 ? 2 : 0);
                    string_view block_comment_text = string_slice(p->source.text, text_start_position, text_end_position - text_start_position);

                    block_trivia.location.length = p->lexer_position - block_trivia.location.offset;
                    block_trivia.text = layec_context_intern_string_view(p->context, block_comment_text);

                    if (nesting_count > 0) {
                        layec_write_error(p->context, block_trivia.location, "Unterminated delimimted comment.");
                    }

                    // arr_push(trivia, block_trivia);

                    if (!leading && newline_encountered) goto exit_loop;
                } else {
                    goto exit_loop;
                }
            } break;
        }
    }

exit_loop:;
    // return trivia;
}

struct keyword_info {
    const char* text;
    laye_token_kind kind;
};

static struct keyword_info laye_keywords[] = {
    {"bool", LAYE_TOKEN_BOOL},
    {"int", LAYE_TOKEN_INT},
    {"uint", LAYE_TOKEN_UINT},
    {"float", LAYE_TOKEN_FLOAT},
    {"true", LAYE_TOKEN_TRUE},
    {"false", LAYE_TOKEN_FALSE},
    {"nil", LAYE_TOKEN_NIL},
    {"global", LAYE_TOKEN_GLOBAL},
    {"if", LAYE_TOKEN_IF},
    {"else", LAYE_TOKEN_ELSE},
    {"for", LAYE_TOKEN_FOR},
    {"while", LAYE_TOKEN_WHILE},
    {"do", LAYE_TOKEN_DO},
    {"switch", LAYE_TOKEN_SWITCH},
    {"case", LAYE_TOKEN_CASE},
    {"default", LAYE_TOKEN_DEFAULT},
    {"return", LAYE_TOKEN_RETURN},
    {"break", LAYE_TOKEN_BREAK},
    {"continue", LAYE_TOKEN_CONTINUE},
    {"fallthrough", LAYE_TOKEN_FALLTHROUGH},
    {"yield", LAYE_TOKEN_YIELD},
    {"unreachable", LAYE_TOKEN_UNREACHABLE},
    {"defer", LAYE_TOKEN_DEFER},
    {"goto", LAYE_TOKEN_GOTO},
    {"xyzzy", LAYE_TOKEN_XYZZY},
    {"assert", LAYE_TOKEN_ASSERT},
    {"struct", LAYE_TOKEN_STRUCT},
    {"variant", LAYE_TOKEN_VARIANT},
    {"enum", LAYE_TOKEN_ENUM},
    {"strict", LAYE_TOKEN_STRICT},
    {"alias", LAYE_TOKEN_ALIAS},
    {"test", LAYE_TOKEN_TEST},
    {"import", LAYE_TOKEN_IMPORT},
    {"export", LAYE_TOKEN_EXPORT},
    {"from", LAYE_TOKEN_FROM},
    {"as", LAYE_TOKEN_AS},
    {"operator", LAYE_TOKEN_OPERATOR},
    {"mut", LAYE_TOKEN_MUT},
    {"new", LAYE_TOKEN_NEW},
    {"delete", LAYE_TOKEN_DELETE},
    {"cast", LAYE_TOKEN_CAST},
    {"is", LAYE_TOKEN_IS},
    {"try", LAYE_TOKEN_TRY},
    {"catch", LAYE_TOKEN_CATCH},
    {"sizeof", LAYE_TOKEN_SIZEOF},
    {"alignof", LAYE_TOKEN_ALIGNOF},
    {"offsetof", LAYE_TOKEN_OFFSETOF},
    {"not", LAYE_TOKEN_NOT},
    {"and", LAYE_TOKEN_AND},
    {"or", LAYE_TOKEN_OR},
    {"xor", LAYE_TOKEN_XOR},
    {"varargs", LAYE_TOKEN_VARARGS},
    {"const", LAYE_TOKEN_CONST},
    {"foreign", LAYE_TOKEN_FOREIGN},
    {"inline", LAYE_TOKEN_INLINE},
    {"callconv", LAYE_TOKEN_CALLCONV},
    {"impure", LAYE_TOKEN_IMPURE},
    {"discardable", LAYE_TOKEN_DISCARDABLE},
    {"void", LAYE_TOKEN_VOID},
    {"var", LAYE_TOKEN_VAR},
    {"noreturn", LAYE_TOKEN_NORETURN},
    {0}
};

static bool is_identifier_char(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c >= 256;
}

static int64_t digit_value_in_any_radix(int c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'a' && c <= 'z')
        return c - 'a' + 10;
    else if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;
    else return -1;
}

static bool is_digit_char_in_any_radix(int c) {
    return -1 != digit_value_in_any_radix(c);
}

static bool is_digit_char(int c, int radix) {
    int64_t digit_value = digit_value_in_any_radix(c);
    return radix > digit_value && digit_value != -1;
}

static void laye_next_token(laye_parser* p) {
restart_token:;
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);

    laye_token token = {
        .kind = LAYE_TOKEN_INVALID,
        .location.sourceid = p->sourceid,
    };

    if (p->next_token.kind != LAYE_TOKEN_INVALID) {
        p->token = p->next_token;
        p->next_token = token;
        return;
    }

    /* token.leading_trivia = */ laye_read_trivia(p, true);
    token.location.offset = p->lexer_position;

    if (p->lexer_position >= p->source.text.count) {
        token.kind = LAYE_TOKEN_EOF;
        p->token = token;
        return;
    }

    char c = p->current_char;
    switch (c) {
        case '(':
        case ')':
        case '{':
        case '}':
        case '[':
        case ']':
        case ',':
        case ';':
        case '.': {
            laye_char_advance(p);
            token.kind = c;
        } break;

        case ':': {
            laye_char_advance(p);
            if (p->current_char == ':') {
                laye_char_advance(p);
                token.kind = LAYE_TOKEN_COLONCOLON;
            } else {
                token.kind = ':';
            }
        } break;

        case '~': {
            laye_char_advance(p);
            if (p->current_char == '=') {
                laye_char_advance(p);
                token.kind = LAYE_TOKEN_TILDEEQUAL;
            } else {
                token.kind = '~';
            }
        } break;

        case '!': {
            laye_char_advance(p);
            if (p->current_char == '=') {
                laye_char_advance(p);
                token.kind = LAYE_TOKEN_BANGEQUAL;
            } else {
                token.kind = '!';
            }
        } break;

        case '%': {
            laye_char_advance(p);
            if (p->current_char == '=') {
                laye_char_advance(p);
                token.kind = LAYE_TOKEN_PERCENTEQUAL;
            } else {
                token.kind = '%';
            }
        } break;

        case '&': {
            laye_char_advance(p);
            if (p->current_char == '=') {
                laye_char_advance(p);
                token.kind = LAYE_TOKEN_AMPERSANDEQUAL;
            } else {
                token.kind = '&';
            }
        } break;

        case '*': {
            laye_char_advance(p);
            if (p->current_char == '=') {
                laye_char_advance(p);
                token.kind = LAYE_TOKEN_STAREQUAL;
            } else {
                token.kind = '*';
            }
        } break;

        case '|': {
            laye_char_advance(p);
            if (p->current_char == '=') {
                laye_char_advance(p);
                token.kind = LAYE_TOKEN_PIPEEQUAL;
            } else {
                token.kind = '|';
            }
        } break;

        case '-': {
            laye_char_advance(p);
            if (p->current_char == '=') {
                laye_char_advance(p);
                token.kind = LAYE_TOKEN_MINUSEQUAL;
            } else {
                token.kind = '-';
            }
        } break;

        case '=': {
            laye_char_advance(p);
            if (p->current_char == '=') {
                laye_char_advance(p);
                token.kind = LAYE_TOKEN_EQUALEQUAL;
            } else if (p->current_char == '>') {
                laye_char_advance(p);
                token.kind = LAYE_TOKEN_EQUALGREATER;
            } else {
                token.kind = '=';
            }
        } break;

        case '+': {
            laye_char_advance(p);
            if (p->current_char == '=') {
                laye_char_advance(p);
                token.kind = LAYE_TOKEN_PLUSEQUAL;
            } else {
                token.kind = '+';
            }
        } break;

        case '/': {
            laye_char_advance(p);
            if (p->current_char == '=') {
                laye_char_advance(p);
                token.kind = LAYE_TOKEN_SLASHEQUAL;
            } else {
                token.kind = '/';
            }
        } break;

        case '<': {
            laye_char_advance(p);
            if (p->current_char == '=') {
                laye_char_advance(p);
                token.kind = LAYE_TOKEN_LESSEQUAL;
            } else if (p->current_char == '<') {
                laye_char_advance(p);
                if (p->current_char == '=') {
                    laye_char_advance(p);
                    token.kind = LAYE_TOKEN_LESSLESSEQUAL;
                } else {
                    token.kind = LAYE_TOKEN_LESSLESS;
                }
            } else if (p->current_char == '-') {
                laye_char_advance(p);
                token.kind = LAYE_TOKEN_LESSMINUS;
            } else {
                token.kind = '<';
            }
        } break;

        case '>': {
            laye_char_advance(p);
            if (p->current_char == '=') {
                laye_char_advance(p);
                token.kind = LAYE_TOKEN_GREATEREQUAL;
            } else if (p->current_char == '>') {
                laye_char_advance(p);
                if (p->current_char == '=') {
                    laye_char_advance(p);
                    token.kind = LAYE_TOKEN_GREATERGREATEREQUAL;
                } else {
                    token.kind = LAYE_TOKEN_GREATERGREATER;
                }
            } else {
                token.kind = '>';
            }
        } break;

        case '?': {
            laye_char_advance(p);
            token.kind = '?';
        } break;

        case '\'':
        case '"': {
            bool is_char = c == '\'';
            if (is_char) {
                token.kind = LAYE_TOKEN_LITRUNE;
            } else {
                token.kind = LAYE_TOKEN_LITSTRING;
            }
            char terminator = c;

            laye_char_advance(p);

            dynarr(char) string_data = NULL;

            bool error_char = false;
            while (p->current_char != 0 && p->current_char != terminator) {
                char c = p->current_char;
                assert(c != terminator);

                if (is_char && string_data != NULL) {
                    error_char = true;
                }

                if (c == '\\') {
                    laye_char_advance(p);
                    c = p->current_char;
                    switch (c) {
                        default: {
                            // clang-format off
                            layec_write_error(p->context, (layec_location) {
                                .sourceid = token.location.sourceid,
                                .offset = p->lexer_position,
                                .length = 1,
                            }, "Invalid character in escape string sequence.");
                            // clang-format on

                            arr_push(string_data, c);
                            laye_char_advance(p);
                        } break;

                        case '\\': {
                            arr_push(string_data, '\\');
                            laye_char_advance(p);
                        } break;

                        case '"': {
                            arr_push(string_data, '"');
                            laye_char_advance(p);
                        } break;

                        case '\'': {
                            arr_push(string_data, '\'');
                            laye_char_advance(p);
                        } break;

                        case 'a': {
                            arr_push(string_data, '\a');
                            laye_char_advance(p);
                        } break;

                        case 'b': {
                            arr_push(string_data, '\b');
                            laye_char_advance(p);
                        } break;

                        case 'f': {
                            arr_push(string_data, '\f');
                            laye_char_advance(p);
                        } break;

                        case 'n': {
                            arr_push(string_data, '\n');
                            laye_char_advance(p);
                        } break;

                        case 'r': {
                            arr_push(string_data, '\r');
                            laye_char_advance(p);
                        } break;

                        case 't': {
                            arr_push(string_data, '\t');
                            laye_char_advance(p);
                        } break;

                        case 'v': {
                            arr_push(string_data, '\v');
                            laye_char_advance(p);
                        } break;

                        case '0': {
                            arr_push(string_data, '\0');
                            laye_char_advance(p);
                        } break;

                        case 'x': {
                            laye_char_advance(p);

                            int value = 0;
                            for (int i = 0; i < 2; i++) {
                                if (p->lexer_position >= p->source.text.count || p->current_char == '"' || !is_digit_char(p->current_char, 16)) {
                                    layec_write_error(p->context, token.location, "The \\x escape sequence requires exactly two hexadecimal digits.");
                                    break;
                                }

                                int digit_value = (int)digit_value_in_any_radix(p->current_char);
                                value = (value << 4) | (digit_value & 0xF);
                                laye_char_advance(p);
                            }

                            arr_push(string_data, (char)(value & 0xFF));
                        } break;
                    }
                } else {
                    arr_push(string_data, c);
                    laye_char_advance(p);
                }
            }

            token.string_value = layec_context_intern_string_view(p->context, (string_view){.data = string_data, .count = arr_count(string_data)});
            arr_free(string_data);

            if (p->current_char != terminator) {
                token.location.length = p->lexer_position - token.location.offset;
                layec_write_error(p->context, token.location, "Unterminated %s literal.", (is_char ? "rune" : "string"));
            } else {
                laye_char_advance(p);
            }

            if (error_char) {
                token.location.length = p->lexer_position - token.location.offset;
                layec_write_error(p->context, token.location, "Too many characters in rune literal.");
            } else if (is_char && token.string_value.count == 0) {
                token.location.length = p->lexer_position - token.location.offset;
                layec_write_error(p->context, token.location, "Not enough characters in rune literal.");
            }
        } break;

            // clang-format off
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9': {
            // clang-format on
            int64_t integer_value = 0;
            while ((p->current_char >= '0' && p->current_char <= '9') || p->current_char == '_') {
                if (p->current_char != '_') {
                    int64_t digit_value = (int64_t)(p->current_char - '0');
                    assert(digit_value >= 0 && digit_value <= 9);
                    // TODO(local): overflow check on integer parse
                    integer_value = digit_value + integer_value * 10;
                } else {
                    // a number literal that starts or ends with an underscore is not actually a number literal
                    if (laye_char_peek(p) < '0' || laye_char_peek(p) > '9') {
                        goto change_int_to_ident;
                    }
                }

                laye_char_advance(p);
            }

            int radix = 10;
            bool has_explicit_radix = false;

            // TODO(local): radix literals, floats, identifiers
            if (p->current_char == '#') {
                has_explicit_radix = true;

                layec_location radix_location = token.location;
                radix_location.length = p->lexer_position - radix_location.offset;

                if (integer_value < 2 || integer_value > 36) {
                    layec_write_error(p->context, radix_location, "Integer base must be between 2 and 36 inclusive.");
                    if (integer_value < 2) {
                        radix = 2;
                    } else radix = 36;
                } else radix = (int)integer_value;

                laye_char_advance(p);
                if (!is_digit_char_in_any_radix(p->current_char) && p->current_char != '_') {
                    layec_write_error(p->context, laye_char_location(p), "Expected a digit value in base %d.", radix);
                    goto end_literal_integer_radix;
                }

                if (p->current_char == '_') {
                    layec_write_error(p->context, laye_char_location(p), "Integer literals cannot begin wtih an underscore.");
                }

                integer_value = 0;

                bool should_report_invalid_digits = false;
                int64_t integer_value_start_position = p->lexer_position;

                while (is_digit_char_in_any_radix(p->current_char) || p->current_char == '_') {
                    if (p->current_char != '_') {
                        int64_t digit_value = digit_value_in_any_radix(p->current_char);
                        if (!is_digit_char(p->current_char, radix)) {
                            digit_value = radix - 1;
                            // layec_write_error(p->context, laye_char_location(p), "'%c' is not a digit value in base %d.", p->current_char, radix);
                            should_report_invalid_digits = true;
                        }

                        assert(digit_value >= 0 && digit_value < radix);
                        // TODO(local): overflow check on integer parse
                        integer_value = digit_value + integer_value * radix;
                    } else {
                        // a number literal that starts or ends with an underscore is not actually a number literal
                        // in this case, we can't fall back to the identifier parser, so we do actually error it.
                        if (!is_digit_char_in_any_radix(laye_char_peek(p))) {
                            laye_char_advance(p);
                            layec_write_error(p->context, laye_char_location(p), "Integer literals cannot end in an underscore.");
                            continue;
                        }
                    }

                    laye_char_advance(p);
                }

                bool will_be_float = p->current_char == '.';
                if (should_report_invalid_digits) {
                    layec_location integer_value_location = (layec_location){
                        .sourceid = p->sourceid,
                        .offset = integer_value_start_position,
                        .length = p->lexer_position - integer_value_start_position,
                    };

                    if (will_be_float)
                        layec_write_error(p->context, integer_value_location, "Float value contains digits outside its specified base.");
                    else layec_write_error(p->context, integer_value_location, "Integer value contains digits outside its specified base.");
                }

                if (will_be_float) {
                    goto continue_float_literal;
                }

            end_literal_integer_radix:;
                token.int_value = integer_value;
                token.kind = LAYE_TOKEN_LITINT;
            } else if (p->current_char == '.') {
            continue_float_literal:;
                assert(radix >= 2 && radix <= 36);
                assert(p->current_char == '.');

                double fractional_value = 0;

                laye_char_advance(p);
                if (!is_digit_char_in_any_radix(p->current_char) && p->current_char != '_') {
                    layec_write_error(p->context, laye_char_location(p), "Expected a digit value in base %d.", radix);
                    goto end_literal_float;
                }

                if (p->current_char == '_') {
                    layec_write_error(p->context, laye_char_location(p), "The fractional part of a float literal cannot begin with an underscore.");
                }

                bool should_report_invalid_digits = false;
                int64_t fractional_value_start_position = p->lexer_position;

                while (is_digit_char_in_any_radix(p->current_char) || p->current_char == '_') {
                    if (p->current_char != '_') {
                        int64_t digit_value = digit_value_in_any_radix(p->current_char);
                        if (!is_digit_char(p->current_char, radix)) {
                            digit_value = radix - 1;
                            layec_write_error(p->context, laye_char_location(p), "'%c' is not a digit value in base %d.", p->current_char, radix);
                        }

                        assert(digit_value >= 0 && digit_value < radix);
                        // TODO(local): overflow/underflow check on float parse
                        fractional_value = (digit_value + fractional_value) / radix;
                    } else {
                        // a number literal that starts or ends with an underscore is not actually a number literal
                        // in this case, we can't fall back to the identifier parser, so we do actually error it.
                        if (!is_digit_char_in_any_radix(laye_char_peek(p))) {
                            laye_char_advance(p);
                            layec_write_error(p->context, laye_char_location(p), "Float literals cannot end in an underscore.");
                            continue;
                        }
                    }

                    laye_char_advance(p);
                }

                if (should_report_invalid_digits) {
                    layec_location integer_value_location = (layec_location){
                        .sourceid = p->sourceid,
                        .offset = fractional_value_start_position,
                        .length = p->lexer_position - fractional_value_start_position,
                    };
                    layec_write_error(p->context, integer_value_location, "Float value contains digits outside its specified base.");
                }

            end_literal_float:;
                token.float_value = integer_value + fractional_value;
                token.kind = LAYE_TOKEN_LITFLOAT;
            } else if (is_identifier_char(p->current_char)) {
            change_int_to_ident:;
                assert(token.location.offset >= 0 && token.location.offset < p->source.text.count);
                p->lexer_position = token.location.offset;
                p->current_char = p->source.text.data[p->lexer_position];
                goto identfier_lex;
            } else {
                token.int_value = integer_value;
                token.kind = LAYE_TOKEN_LITINT;
            }
        } break;

            // clang-format off
        case 'a': case 'b': case 'c': case 'd': case 'e':
        case 'f': case 'g': case 'h': case 'i': case 'j':
        case 'k': case 'l': case 'm': case 'n': case 'o':
        case 'p': case 'q': case 'r': case 's': case 't':
        case 'u': case 'v': case 'w': case 'x': case 'y':
        case 'z':
        case 'A': case 'B': case 'C': case 'D': case 'E':
        case 'F': case 'G': case 'H': case 'I': case 'J':
        case 'K': case 'L': case 'M': case 'N': case 'O':
        case 'P': case 'Q': case 'R': case 'S': case 'T':
        case 'U': case 'V': case 'W': case 'X': case 'Y':
        case 'Z':
        // clang-format on
        case '_': {
        identfier_lex:;
            while (is_identifier_char(p->current_char)) {
                laye_char_advance(p);
            }

            token.location.length = p->lexer_position - token.location.offset;
            assert(token.location.length > 0);
            string_view identifier_source_view = string_slice(p->source.text, token.location.offset, token.location.length);

            for (int64_t i = 0; laye_keywords[i].kind != 0; i++) {
                if (string_view_equals_cstring(identifier_source_view, laye_keywords[i].text)) {
                    token.kind = laye_keywords[i].kind;
                    goto token_finished;
                }
            }

            char first_char = identifier_source_view.data[0];
            if (first_char == 'i' || first_char == 'u' || first_char == 'f' || first_char == 'b') {
                bool are_remaining_characters_digits = true;
                int64_t integer_value = 0;
                for (int64_t i = 1; are_remaining_characters_digits && i < identifier_source_view.count; i++) {
                    char c = identifier_source_view.data[i];
                    if (c < '0' || c > '9') {
                        are_remaining_characters_digits = false;
                    } else {
                        integer_value = integer_value * 10 + (c - '0');
                    }
                }

                if (are_remaining_characters_digits) {
                    if (first_char == 'f') {
                        if (integer_value == 32 || integer_value == 64 || integer_value == 80 || integer_value == 128) {
                            token.kind = LAYE_TOKEN_FLOATSIZED;
                            token.int_value = integer_value;
                            goto token_finished;
                        }
                        // TODO(local): else error? or just allow it?
                    } else if (first_char == 'b') {
                        if (integer_value > 0 && integer_value < 65536) {
                            token.kind = LAYE_TOKEN_BOOLSIZED;
                            token.int_value = integer_value;
                            goto token_finished;
                        }
                        // TODO(local): else error? or just allow it?
                    } else {
                        if (integer_value > 0 && integer_value < 65536) {
                            token.kind = first_char == 'i' ? LAYE_TOKEN_INTSIZED : LAYE_TOKEN_UINTSIZED;
                            token.int_value = integer_value;
                            goto token_finished;
                        }
                        // TODO(local): else error? or just allow it?
                    }
                }
            }

            token.string_value = layec_context_intern_string_view(p->context, identifier_source_view);
            assert(token.string_value.count > 0);
            assert(token.string_value.data != NULL);
            token.kind = LAYE_TOKEN_IDENT;
        } break;

        default: {
            laye_char_advance(p);

            token.kind = LAYE_TOKEN_UNKNOWN;
            token.location.length = p->lexer_position - token.location.offset;
            layec_write_error(p->context, token.location, "Invalid character in Laye source file.");

            arr_push(p->module->_all_tokens, token);

            //laye_next_token(p);
            goto restart_token;
        }
    }

token_finished:;
    assert(token.kind != LAYE_TOKEN_INVALID && "tokenization routines failed to update the kind of the token");

    token.location.length = p->lexer_position - token.location.offset;
    assert(token.location.length > 0 && "returning a zero-length token means probably broken tokenizer, oops");

    /* token.trailing_trivia = */ laye_read_trivia(p, false);
    p->token = token;

    arr_push(p->module->_all_tokens, token);
}
