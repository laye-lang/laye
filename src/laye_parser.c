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
    return kind >= LAYE_NODE_DECL_IMPORT && kind <= LAYE_NODE_DECL_TEMPLATE_VALUE;
}

bool laye_node_kind_is_type(laye_node_kind kind) {
    return kind > LAYE_NODE_TYPE_POISON && kind <= LAYE_NODE_TYPE_STRICT_ALIAS;
}

bool laye_node_is_decl(laye_node* node) {
    assert(node != NULL);
    return laye_node_kind_is_decl(node->kind);
}

bool laye_node_is_type(laye_node* node) {
    assert(node != NULL);
    return laye_node_kind_is_type(node->kind);
}

bool laye_node_is_lvalue(laye_node* node) {
    assert(node != NULL);
    return node->value_category == LAYEC_LVALUE;
}

bool laye_node_is_rvalue(laye_node* node) {
    assert(node != NULL);
    return node->value_category == LAYEC_RVALUE;
}

bool laye_node_is_modifiable_lvalue(laye_node* node) {
    assert(node != NULL);
    return laye_node_is_lvalue(node) && laye_type_is_modifiable(node->type);
}

bool laye_type_is_modifiable(laye_node* node) {
    assert(node != NULL);
    return laye_node_is_type(node) && node->type_is_modifiable;
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
        string_view token_text = string_slice(p.source.text, p.token.location.offset, p.token.location.length);
        layec_write_note(
            context,
            p.token.location,
            "%s %.*s",
            laye_token_kind_to_cstring(p.token.kind),
            STR_EXPAND(token_text)
        );
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

static layec_location laye_char_location(laye_parser* p) {
    return (layec_location){
        .sourceid = p->sourceid,
        .offset = p->lexer_position,
        .length = 1,
    };
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

struct keyword_info {
    string_view text;
    laye_token_kind kind;
};

static struct keyword_info laye_keywords[] = {
    {LCA_SV_CONSTANT("bool"), LAYE_TOKEN_BOOL},
    {LCA_SV_CONSTANT("int"), LAYE_TOKEN_INT},
    {LCA_SV_CONSTANT("uint"), LAYE_TOKEN_UINT},
    {LCA_SV_CONSTANT("float"), LAYE_TOKEN_FLOAT},
    {LCA_SV_CONSTANT("true"), LAYE_TOKEN_TRUE},
    {LCA_SV_CONSTANT("false"), LAYE_TOKEN_FALSE},
    {LCA_SV_CONSTANT("nil"), LAYE_TOKEN_NIL},
    {LCA_SV_CONSTANT("global"), LAYE_TOKEN_GLOBAL},
    {LCA_SV_CONSTANT("if"), LAYE_TOKEN_IF},
    {LCA_SV_CONSTANT("else"), LAYE_TOKEN_ELSE},
    {LCA_SV_CONSTANT("for"), LAYE_TOKEN_FOR},
    {LCA_SV_CONSTANT("do"), LAYE_TOKEN_DO},
    {LCA_SV_CONSTANT("switch"), LAYE_TOKEN_SWITCH},
    {LCA_SV_CONSTANT("case"), LAYE_TOKEN_CASE},
    {LCA_SV_CONSTANT("default"), LAYE_TOKEN_DEFAULT},
    {LCA_SV_CONSTANT("return"), LAYE_TOKEN_RETURN},
    {LCA_SV_CONSTANT("break"), LAYE_TOKEN_BREAK},
    {LCA_SV_CONSTANT("continue"), LAYE_TOKEN_CONTINUE},
    {LCA_SV_CONSTANT("fallthrough"), LAYE_TOKEN_FALLTHROUGH},
    {LCA_SV_CONSTANT("yield"), LAYE_TOKEN_YIELD},
    {LCA_SV_CONSTANT("unreachable"), LAYE_TOKEN_UNREACHABLE},
    {LCA_SV_CONSTANT("defer"), LAYE_TOKEN_DEFER},
    {LCA_SV_CONSTANT("goto"), LAYE_TOKEN_GOTO},
    {LCA_SV_CONSTANT("xyzzy"), LAYE_TOKEN_XYZZY},
    {LCA_SV_CONSTANT("assert"), LAYE_TOKEN_ASSERT},
    {LCA_SV_CONSTANT("struct"), LAYE_TOKEN_STRUCT},
    {LCA_SV_CONSTANT("variant"), LAYE_TOKEN_VARIANT},
    {LCA_SV_CONSTANT("enum"), LAYE_TOKEN_ENUM},
    {LCA_SV_CONSTANT("strict"), LAYE_TOKEN_STRICT},
    {LCA_SV_CONSTANT("alias"), LAYE_TOKEN_ALIAS},
    {LCA_SV_CONSTANT("test"), LAYE_TOKEN_TEST},
    {LCA_SV_CONSTANT("import"), LAYE_TOKEN_IMPORT},
    {LCA_SV_CONSTANT("export"), LAYE_TOKEN_EXPORT},
    {LCA_SV_CONSTANT("from"), LAYE_TOKEN_FROM},
    {LCA_SV_CONSTANT("as"), LAYE_TOKEN_AS},
    {LCA_SV_CONSTANT("operator"), LAYE_TOKEN_OPERATOR},
    {LCA_SV_CONSTANT("mut"), LAYE_TOKEN_MUT},
    {LCA_SV_CONSTANT("new"), LAYE_TOKEN_NEW},
    {LCA_SV_CONSTANT("delete"), LAYE_TOKEN_DELETE},
    {LCA_SV_CONSTANT("cast"), LAYE_TOKEN_CAST},
    {LCA_SV_CONSTANT("is"), LAYE_TOKEN_IS},
    {LCA_SV_CONSTANT("try"), LAYE_TOKEN_TRY},
    {LCA_SV_CONSTANT("catch"), LAYE_TOKEN_CATCH},
    {LCA_SV_CONSTANT("sizeof"), LAYE_TOKEN_SIZEOF},
    {LCA_SV_CONSTANT("alignof"), LAYE_TOKEN_ALIGNOF},
    {LCA_SV_CONSTANT("offsetof"), LAYE_TOKEN_OFFSETOF},
    {LCA_SV_CONSTANT("not"), LAYE_TOKEN_NOT},
    {LCA_SV_CONSTANT("and"), LAYE_TOKEN_AND},
    {LCA_SV_CONSTANT("or"), LAYE_TOKEN_OR},
    {LCA_SV_CONSTANT("xor"), LAYE_TOKEN_XOR},
    {LCA_SV_CONSTANT("varargs"), LAYE_TOKEN_VARARGS},
    {LCA_SV_CONSTANT("const"), LAYE_TOKEN_CONST},
    {LCA_SV_CONSTANT("foreign"), LAYE_TOKEN_FOREIGN},
    {LCA_SV_CONSTANT("inline"), LAYE_TOKEN_INLINE},
    {LCA_SV_CONSTANT("callconv"), LAYE_TOKEN_CALLCONV},
    {LCA_SV_CONSTANT("impure"), LAYE_TOKEN_IMPURE},
    {LCA_SV_CONSTANT("nodiscard"), LAYE_TOKEN_NODISCARD},
    {LCA_SV_CONSTANT("void"), LAYE_TOKEN_VOID},
    {LCA_SV_CONSTANT("var"), LAYE_TOKEN_VAR},
    {LCA_SV_CONSTANT("noreturn"), LAYE_TOKEN_NORETURN},
    {0}
};

static bool is_identifier_char(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c >= 256;
}

static int64_t digit_value_in_any_radix(int c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'a' && c <= 'z')
        return c - 'a';
    else if (c >= 'A' && c <= 'Z')
        return c - 'A';
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
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);

    laye_token token = {
        .kind = LAYE_TOKEN_INVALID,
        .location.sourceid = p->sourceid,
    };

    token.leading_trivia = laye_read_trivia(p, true);
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
                            }, "Invalid character in escape string sequence");
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
                    }
                } else {
                    arr_push(string_data, c);
                    laye_char_advance(p);
                }
            }

            arr_push(string_data, 0);
            arr_set_count(string_data, arr_count(string_data) - 1);

            token.string_value = string_from_data(p->context->allocator, string_data, arr_count(string_data), arr_capacity(string_data));

            arr_free(string_data);

            if (p->current_char != terminator) {
                token.location.length = p->lexer_position - token.location.offset;
                layec_write_error(p->context, token.location, "Unterminated %s literal", (is_char ? "rune" : "string"));
            } else {
                laye_char_advance(p);
            }

            if (error_char) {
                token.location.length = p->lexer_position - token.location.offset;
                layec_write_error(p->context, token.location, "Too many characters in rune literal");
            } else if (is_char && token.string_value.count == 0) {
                token.location.length = p->lexer_position - token.location.offset;
                layec_write_error(p->context, token.location, "Not enough characters in rune literal");
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
                    layec_write_error(p->context, radix_location, "Integer base must be between 2 and 36 inclusive");
                    if (integer_value < 2) {
                        radix = 2;
                    } else radix = 36;
                } else radix = (int)integer_value;

                laye_char_advance(p);
                if (!is_digit_char_in_any_radix(p->current_char) && p->current_char != '_') {
                    layec_write_error(p->context, laye_char_location(p), "Expected a digit value in base %d", radix);
                    goto end_literal_integer_radix;
                }

                if (p->current_char == '_') {
                    layec_write_error(p->context, laye_char_location(p), "Integer literals cannot begin wtih an underscore");
                }

                integer_value = 0;
                while (is_digit_char_in_any_radix(p->current_char) || p->current_char == '_') {
                    if (p->current_char != '_') {
                        int64_t digit_value = digit_value_in_any_radix(p->current_char);
                        if (!is_digit_char(p->current_char, radix)) {
                            digit_value = radix - 1;
                            layec_write_error(p->context, laye_char_location(p), "'%c' is not a digit value in base %d", p->current_char, radix);
                        }

                        assert(digit_value >= 0 && digit_value < radix);
                        // TODO(local): overflow check on integer parse
                        integer_value = digit_value + integer_value * radix;
                    } else {
                        // a number literal that starts or ends with an underscore is not actually a number literal
                        // in this case, we can't fall back to the identifier parser, so we do actually error it.
                        if (!is_digit_char_in_any_radix(laye_char_peek(p))) {
                            laye_char_advance(p);
                            layec_write_error(p->context, laye_char_location(p), "Integer literals cannot end in an underscore");
                            continue;
                        }
                    }

                    laye_char_advance(p);
                }

                if (p->current_char == '.') {
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
                    layec_write_error(p->context, laye_char_location(p), "Expected a digit value in base %d", radix);
                    goto end_literal_float;
                }

                if (p->current_char == '_') {
                    layec_write_error(p->context, laye_char_location(p), "The fractional part of a float literal cannot begin with an underscore");
                }

                while (is_digit_char_in_any_radix(p->current_char) || p->current_char == '_') {
                    if (p->current_char != '_') {
                        int64_t digit_value = digit_value_in_any_radix(p->current_char);
                        if (!is_digit_char(p->current_char, radix)) {
                            digit_value = radix - 1;
                            layec_write_error(p->context, laye_char_location(p), "'%c' is not a digit value in base %d", p->current_char, radix);
                        }

                        assert(digit_value >= 0 && digit_value < radix);
                        // TODO(local): overflow/underflow check on float parse
                        fractional_value = (digit_value + fractional_value) / radix;
                    } else {
                        // a number literal that starts or ends with an underscore is not actually a number literal
                        // in this case, we can't fall back to the identifier parser, so we do actually error it.
                        if (!is_digit_char_in_any_radix(laye_char_peek(p))) {
                            laye_char_advance(p);
                            layec_write_error(p->context, laye_char_location(p), "Float literals cannot end in an underscore");
                            continue;
                        }
                    }

                    laye_char_advance(p);
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
            string_view identifier_source_view = string_slice(p->source.text, token.location.offset, token.location.length);

            for (int64_t i = 0; laye_keywords[i].kind != 0; i++) {
                if (string_view_equals(laye_keywords[i].text, identifier_source_view)) {
                    token.kind = laye_keywords[i].kind;
                    goto token_finished;
                }
            }

            token.string_value = layec_context_intern_string_view(p->context, identifier_source_view);
            token.kind = LAYE_TOKEN_IDENT;
        } break;

        default: {
            laye_char_advance(p);

            token.location.length = p->lexer_position - token.location.offset;
            layec_write_error(p->context, token.location, "Invalid character in Laye source file");

            laye_next_token(p);
            return;
        }
    }

token_finished:;
    assert(token.kind != LAYE_TOKEN_INVALID && "tokenization routines failed to update the kind of the token");

    token.location.length = p->lexer_position - token.location.offset;
    assert(token.location.length > 0 && "returning a zero-length token means probably broken tokenizer, oops");

    token.trailing_trivia = laye_read_trivia(p, false);
    p->token = token;
}
