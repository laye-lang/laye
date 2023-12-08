#include "layec.h"

#include <assert.h>

typedef struct laye_parser {
    layec_context* context;
    laye_module* module;
    layec_sourceid sourceid;

    layec_source source;
    int64_t lexer_position;
    int current_char;

    laye_token token;
} laye_parser;

const char* laye_trivia_kind_to_cstring(laye_trivia_kind kind) {
    switch (kind) {
        default: return "<invalid laye trivia kind>";

#define X(N) \
    case LAYE_TRIVIA_##N: return #N;
            LAYE_TRIVIA_KINDS(X)
#undef X
    }
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

bool laye_node_kind_is_decl(laye_node_kind kind) {
    return kind > __LAYE_NODE_DECL_START__ && kind < __LAYE_NODE_DECL_END__;
}

bool laye_node_kind_is_stmt(laye_node_kind kind) {
    return kind > __LAYE_NODE_STMT_START__ && kind < __LAYE_NODE_STMT_END__;
}

bool laye_node_kind_is_expr(laye_node_kind kind) {
    return kind > __LAYE_NODE_EXPR_START__ && kind < __LAYE_NODE_EXPR_END__;
}

bool laye_node_kind_is_type(laye_node_kind kind) {
    return kind > __LAYE_NODE_TYPE_START__ && kind < __LAYE_NODE_TYPE_END__;
}

bool laye_node_is_decl(laye_node* node) {
    assert(node != NULL);
    return laye_node_kind_is_decl(node->kind);
}

bool laye_node_is_stmt(laye_node* node) {
    assert(node != NULL);
    return laye_node_kind_is_stmt(node->kind);
}

bool laye_node_is_expr(laye_node* node) {
    assert(node != NULL);
    return laye_node_kind_is_expr(node->kind);
}

bool laye_node_is_type(laye_node* node) {
    assert(node != NULL);
    return laye_node_kind_is_type(node->kind);
}

bool laye_node_is_lvalue(laye_node* node) {
    assert(node != NULL);
    return laye_node_is_expr(node) && node->expr.value_category == LAYEC_LVALUE;
}

bool laye_node_is_rvalue(laye_node* node) {
    assert(node != NULL);
    return laye_node_is_expr(node) && node->expr.value_category == LAYEC_RVALUE;
}

bool laye_node_is_modifiable_lvalue(laye_node* node) {
    assert(node != NULL);
    return laye_node_is_lvalue(node) && laye_type_is_modifiable(node->expr.type);
}

bool laye_type_is_modifiable(laye_node* node) {
    assert(node != NULL);
    return laye_node_is_type(node) && node->type.is_modifiable;
}

static void laye_next_token(laye_parser* p);
static laye_node* laye_parse_top_level_node(laye_parser* p);

laye_module* laye_parse(layec_context* context, layec_sourceid sourceid) {
    assert(context != NULL);
    assert(sourceid >= 0);

    laye_module* module = lca_allocate(context->allocator, sizeof *module);
    assert(module);
    module->context = context;
    module->sourceid = sourceid;
    module->arena = lca_arena_create(context->allocator, 1024 * 1024);
    assert(module->arena);

    layec_source source = layec_context_get_source(context, sourceid);

    laye_parser p = {
        .context = context,
        .module = module,
        .sourceid = sourceid,
        .source = source,
    };

    if (source.text.count > 0) {
        p.current_char = source.text.data[0];
    }

    // prime the first token before we begin parsing
    laye_next_token(&p);

    while (p.token.kind != LAYE_TOKEN_EOF) {
        layec_write_note(context, p.token.location, "%s", laye_token_kind_to_cstring(p.token.kind));
        laye_next_token(&p);
    }

    return module;
}

// ========== Parser ==========

static bool laye_parser_is_eof(laye_parser* p) {
    assert(p != NULL);
    return p->token.kind == LAYE_TOKEN_EOF;
}

static laye_node* laye_parse_top_level_node(laye_parser* p) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);

    assert(false);
    return NULL;
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

static dynarr(laye_trivia) laye_read_trivia(laye_parser* p, bool leading) {
    dynarr(laye_trivia) trivia = NULL;

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
                    line_trivia.text = string_view_to_string(p->context->allocator, line_comment_text);

                    arr_push(trivia, line_trivia);

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
                    block_trivia.text = string_view_to_string(p->context->allocator, block_comment_text);

                    if (nesting_count > 0) {
                        layec_write_error(p->context, block_trivia.location, "Unterminated delimimted comment");
                    }

                    arr_push(trivia, block_trivia);

                    if (!leading && newline_encountered) goto exit_loop;
                } else {
                    goto exit_loop;
                }
            } break;
        }
    }

exit_loop:;
    return trivia;
}

static void laye_next_token(laye_parser* p) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);

    laye_token token = {
        .kind = LAYE_TOKEN_INVALID,
        .location.sourceid = p->sourceid,
        .location.offset = p->lexer_position,
    };

    token.leading_trivia = laye_read_trivia(p, true);

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

        default: {
            laye_char_advance(p);

            token.location.length = p->lexer_position - token.location.offset;
            layec_write_error(p->context, token.location, "Invalid character in Laye source file");

            laye_next_token(p);
            return;
        }
    }

    assert(token.kind != LAYE_TOKEN_INVALID && "tokenization routines failed to update the kind of the token");

    token.location.length = p->lexer_position - token.location.offset;
    assert(token.location.length > 0 && "returning a zero-length token means probably broken tokenizer, oops");

    token.trailing_trivia = laye_read_trivia(p, false);
    p->token = token;
}
