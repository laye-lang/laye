#include "c.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

typedef struct c_lexer c_lexer;
typedef struct c_macro_expansion c_macro_expansion;
typedef struct keyword_info keyword_info;

struct c_lexer {
    lyir_context* context;
    c_translation_unit* tu;
    int sourceid;
    lyir_source source_buffer;

    const char* cur;
    const char* end;

    const char* current_char_location;
    int current_char;
    bool at_start_of_line;
    bool is_in_preprocessor;
    bool is_in_include;

    dynarr(c_macro_expansion) macro_expansions;
};

struct c_macro_expansion {
    c_macro_def* def;
    long long body_position;
    dynarr(dynarr(c_token)) args;
    long long arg_index; // set to -1 when not expanding an argument
    long long arg_position;
};

struct keyword_info {
    const char* name;
    c_token_kind kind;
};

static bool is_space(int c) {
    return c == ' ' || c == '\t' || c == '\v' || c == '\r' || c == '\n';
}

static bool is_alpha_numeric(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (c >= '0' && c <= '9');
}

static bool is_digit(int c) {
    return c >= '0' && c <= '9';
}

static bool is_hex_digit(int c) {
    return (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || (c >= '0' && c <= '9');
}

struct keyword_info c89_keywords[] = {
    {"auto", C_TOKEN_AUTO},
    {"break", C_TOKEN_BREAK},
    {"case", C_TOKEN_CASE},
    {"char", C_TOKEN_CHAR},
    {"const", C_TOKEN_CONST},
    {"continue", C_TOKEN_CONTINUE},
    {"default", C_TOKEN_DEFAULT},
    {"do", C_TOKEN_DO},
    {"double", C_TOKEN_DOUBLE},
    {"else", C_TOKEN_ELSE},
    {"enum", C_TOKEN_ENUM},
    {"extern", C_TOKEN_EXTERN},
    {"float", C_TOKEN_FLOAT},
    {"for", C_TOKEN_FOR},
    {"goto", C_TOKEN_GOTO},
    {"if", C_TOKEN_IF},
    {"int", C_TOKEN_INT},
    {"long", C_TOKEN_LONG},
    {"register", C_TOKEN_REGISTER},
    {"return", C_TOKEN_RETURN},
    {"short", C_TOKEN_SHORT},
    {"signed", C_TOKEN_SIGNED},
    {"sizeof", C_TOKEN_SIZEOF},
    {"static", C_TOKEN_STATIC},
    {"struct", C_TOKEN_STRUCT},
    {"switch", C_TOKEN_SWITCH},
    {"typedef", C_TOKEN_TYPEDEF},
    {"union", C_TOKEN_UNION},
    {"unsigned", C_TOKEN_UNSIGNED},
    {"void", C_TOKEN_VOID},
    {"volatile", C_TOKEN_VOLATILE},
    {"while", C_TOKEN_WHILE},
    {0},
};

static lyir_location c_lexer_get_location(c_lexer* lexer) {
    return (lyir_location){
        .sourceid = lexer->sourceid,
        .offset = lexer->current_char_location - lexer->source_buffer.text.data,
        .length = 1,
    };
}

static bool c_lexer_at_eof(c_lexer* lexer);
static void c_lexer_advance(c_lexer* lexer, bool allow_comments);
static int c_lexer_peek_no_process(c_lexer* lexer, int ahead);
static void c_lexer_read_token(c_lexer* lexer, c_token* out_token);

c_token_buffer c_get_tokens(lyir_context* context, c_translation_unit* tu, lyir_sourceid sourceid) {
    assert(context);

    c_lexer lexer = {
        .context = context,
        .tu = tu,
        .sourceid = sourceid,
        .at_start_of_line = true,
    };

    lexer.source_buffer = lyir_context_get_source(context, sourceid);
    lexer.cur = lexer.source_buffer.text.data;
    lexer.end = lexer.cur + lexer.source_buffer.text.count;

    c_lexer_advance(&lexer, true);

    c_token_buffer token_buffer = {0};
    for (;;) {
        c_token token = {0};
        c_lexer_read_token(&lexer, &token);
        if (token.kind == C_TOKEN_EOF) break;
        arr_push(token_buffer.semantic_tokens, token);
    }

    return token_buffer;
}

static void c_lexer_eat_white_space(c_lexer* lexer) {
    while (is_space(lexer->current_char)) {
        if (lexer->is_in_preprocessor && lexer->current_char == '\n')
            break;
        c_lexer_advance(lexer, true);
    }
}

static int c_lexer_read_escape_sequence(c_lexer* lexer, bool allow_comments) {
    assert(lexer->current_char == '\\' && c_lexer_peek_no_process(lexer, 1) != '\n' && c_lexer_peek_no_process(lexer, 1) != '\r');
    c_lexer_advance(lexer, allow_comments);
    if (c_lexer_at_eof(lexer)) {
        lyir_write_error(lexer->context, c_lexer_get_location(lexer), "End of file reached when lexing escape sequence");
        return 0;
    }

    switch (lexer->current_char) {
        default: {
            lyir_write_error(lexer->context, c_lexer_get_location(lexer), "Unrecognized escape sequence");
            c_lexer_advance(lexer, allow_comments);
            return 0;
        }

        // TODO(local): `\0` is actually a sub-case of octal escapes
        case '0': return '\0';
        case 'a': return '\a';
        case 'b': return '\b';
        case 'f': return '\f';
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        case 'v':
            return '\v';
            // TODO(local): other C escape sequences
    }
}

static void c_lexer_read_token_no_preprocess(c_lexer* lexer, c_token* out_token) {
    c_lexer_eat_white_space(lexer);
    if (c_lexer_at_eof(lexer)) {
        out_token->kind = C_TOKEN_EOF;
        return;
    }

    lyir_location start_location = c_lexer_get_location(lexer);
    out_token->location = start_location;

    int cur = lexer->current_char;
    switch (cur) {
        case '\n': {
            assert(lexer->is_in_preprocessor);
            out_token->kind = (c_token_kind)'\n';
            c_lexer_advance(lexer, true);
            if (lexer->current_char != 0 && lexer->cur < lexer->end) assert(lexer->at_start_of_line);
        } break;

        case '~':
        case '?':
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '}':
        case ';':
        case ':':
        case ',': {
            out_token->kind = (c_token_kind)cur;
            c_lexer_advance(lexer, true);
        } break;

        case '.': {
            c_lexer_advance(lexer, true);
            if (lexer->current_char == '.' && c_lexer_peek_no_process(lexer, 1) == '.') {
                c_lexer_advance(lexer, true);
                c_lexer_advance(lexer, true);
                out_token->kind = C_TOKEN_TRIPLE_DOT;
            } else out_token->kind = '.';
        } break;

        case '+': {
            c_lexer_advance(lexer, true);
            if (lexer->current_char == '+') {
                c_lexer_advance(lexer, true);
                out_token->kind = C_TOKEN_PLUS_PLUS;
            } else if (lexer->current_char == '=') {
                c_lexer_advance(lexer, true);
                out_token->kind = C_TOKEN_PLUS_EQUAL;
            } else out_token->kind = '+';
        } break;

        case '-': {
            c_lexer_advance(lexer, true);
            if (lexer->current_char == '-') {
                c_lexer_advance(lexer, true);
                out_token->kind = C_TOKEN_MINUS_MINUS;
            } else if (lexer->current_char == '=') {
                c_lexer_advance(lexer, true);
                out_token->kind = C_TOKEN_MINUS_EQUAL;
            } else out_token->kind = '-';
        } break;

        case '*': {
            c_lexer_advance(lexer, true);
            if (lexer->current_char == '=') {
                c_lexer_advance(lexer, true);
                out_token->kind = C_TOKEN_STAR_EQUAL;
            } else out_token->kind = '*';
        } break;

        case '/': {
            c_lexer_advance(lexer, true);
            if (lexer->current_char == '=') {
                c_lexer_advance(lexer, true);
                out_token->kind = C_TOKEN_SLASH_EQUAL;
            } else out_token->kind = '/';
        } break;

        case '%': {
            c_lexer_advance(lexer, true);
            if (lexer->current_char == '=') {
                c_lexer_advance(lexer, true);
                out_token->kind = C_TOKEN_PERCENT_EQUAL;
            } else out_token->kind = '%';
        } break;

        case '&': {
            c_lexer_advance(lexer, true);
            if (lexer->current_char == '&') {
                c_lexer_advance(lexer, true);
                out_token->kind = C_TOKEN_AMPERSAND_AMPERSAND;
            } else if (lexer->current_char == '=') {
                c_lexer_advance(lexer, true);
                out_token->kind = C_TOKEN_AMPERSAND_EQUAL;
            } else out_token->kind = '&';
        } break;

        case '|': {
            c_lexer_advance(lexer, true);
            if (lexer->current_char == '|') {
                c_lexer_advance(lexer, true);
                out_token->kind = C_TOKEN_PIPE_PIPE;
            } else if (lexer->current_char == '=') {
                c_lexer_advance(lexer, true);
                out_token->kind = C_TOKEN_PIPE_EQUAL;
            } else out_token->kind = '|';
        } break;

        case '^': {
            c_lexer_advance(lexer, true);
            if (lexer->current_char == '=') {
                c_lexer_advance(lexer, true);
                out_token->kind = C_TOKEN_CARET_EQUAL;
            } else out_token->kind = '^';
        } break;

        case '<': {
            if (lexer->is_in_include)
                goto parse_string_literal;

            c_lexer_advance(lexer, true);
            if (lexer->current_char == '<') {
                c_lexer_advance(lexer, true);
                if (lexer->current_char == '=') {
                    c_lexer_advance(lexer, true);
                    out_token->kind = C_TOKEN_LESS_LESS_EQUAL;
                } else out_token->kind = C_TOKEN_LESS_LESS;
            } else if (lexer->current_char == '=') {
                c_lexer_advance(lexer, true);
                out_token->kind = C_TOKEN_LESS_EQUAL;
            } else out_token->kind = '<';
        } break;

        case '>': {
            c_lexer_advance(lexer, true);
            if (lexer->current_char == '>') {
                c_lexer_advance(lexer, true);
                if (lexer->current_char == '=') {
                    c_lexer_advance(lexer, true);
                    out_token->kind = C_TOKEN_GREATER_GREATER_EQUAL;
                } else out_token->kind = C_TOKEN_GREATER_GREATER;
            } else if (lexer->current_char == '=') {
                c_lexer_advance(lexer, true);
                out_token->kind = C_TOKEN_GREATER_EQUAL;
            } else out_token->kind = '>';
        } break;

        case '=': {
            c_lexer_advance(lexer, true);
            if (lexer->current_char == '=') {
                c_lexer_advance(lexer, true);
                out_token->kind = C_TOKEN_EQUAL_EQUAL;
            } else out_token->kind = '=';
        } break;

        case '!': {
            c_lexer_advance(lexer, true);
            if (lexer->current_char == '=') {
                c_lexer_advance(lexer, true);
                out_token->kind = C_TOKEN_BANG_EQUAL;
            } else out_token->kind = '!';
        } break;

        case '\'': {
            c_lexer_advance(lexer, false);
            out_token->kind = C_TOKEN_LIT_CHAR;
            if (lexer->current_char == '\'') {
                lyir_write_error(lexer->context, start_location, "Quoted character should contain at least one character.");
                c_lexer_advance(lexer, false);
            } else {
                if (lexer->current_char == '\\')
                    out_token->int_value = c_lexer_read_escape_sequence(lexer, false);
                else {
                    out_token->int_value = lexer->current_char;
                    c_lexer_advance(lexer, false);
                }
            }

            if (lexer->current_char != '\'') {
                lyir_write_error(lexer->context, start_location, "Missing close quote.");
                c_lexer_advance(lexer, true);
            } else c_lexer_advance(lexer, false);
        } break;

        case '"': {
        parse_string_literal:;
            char end_delim = lexer->current_char == '"' ? '"' : '>';
            string builder = string_create(lexer->context->allocator);

            c_lexer_advance(lexer, false);
            out_token->kind = C_TOKEN_LIT_STRING;

            while (lexer->current_char != end_delim) {
                if (c_lexer_at_eof(lexer)) {
                    lyir_write_error(lexer->context, start_location, "Unfinished string.");
                    c_lexer_advance(lexer, false);
                    goto finish_token;
                } else if (lexer->current_char == '\\' && !lexer->is_in_include)
                    string_append_rune(&builder, c_lexer_read_escape_sequence(lexer, false));
                else {
                    string_append_rune(&builder, lexer->current_char);
                    c_lexer_advance(lexer, false);
                }
            }

            if (lexer->current_char != end_delim) {
                lyir_write_error(lexer->context, start_location, "Missing close quote.");
                c_lexer_advance(lexer, true);
            } else c_lexer_advance(lexer, false);

            out_token->string_value = lyir_context_intern_string_view(lexer->context, string_as_view(builder));
            string_destroy(&builder);
        } break;

        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9': {
            int radix = 10;
            if (lexer->current_char == '0') {
                c_lexer_advance(lexer, true);
                if (lexer->current_char == 'x' || lexer->current_char == 'X') {
                    radix = 16;
                    c_lexer_advance(lexer, true);
                } else if (lexer->current_char == 'b' || lexer->current_char == 'B') {
                    radix = 2;
                    c_lexer_advance(lexer, true);
                } else radix = 8;
            }

            while (radix == 16 ? is_hex_digit(lexer->current_char) : is_digit(lexer->current_char))
                c_lexer_advance(lexer, true);

            // TODO(local): integer suffixes
            // u l ll ull llu lu f ld

            string_view suffix_view = {0};
            if (is_alpha_numeric(lexer->current_char)) {
                lyir_location suffix_location = c_lexer_get_location(lexer);
                do c_lexer_advance(lexer, true);
                while (is_alpha_numeric(lexer->current_char));
                lyir_location suffix_end_location = c_lexer_get_location(lexer);
                out_token->string_value = string_view_slice(string_as_view(lexer->source_buffer.text), suffix_location.offset, suffix_end_location.offset - suffix_location.offset);
            }

            if (suffix_view.count != 0) {
                lyir_write_error(lexer->context, start_location, "Integer literal suffixes are not yet supported.");
                c_lexer_advance(lexer, true);
            }

            out_token->kind = C_TOKEN_LIT_INT;
        } break;

        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
        case 'g':
        case 'h':
        case 'i':
        case 'j':
        case 'k':
        case 'l':
        case 'm':
        case 'n':
        case 'o':
        case 'p':
        case 'q':
        case 'r':
        case 's':
        case 't':
        case 'u':
        case 'v':
        case 'w':
        case 'x':
        case 'y':
        case 'z':

        case 'A':
        case 'B':
        case 'C':
        case 'D':
        case 'E':
        case 'F':
        case 'G':
        case 'H':
        case 'I':
        case 'J':
        case 'K':
        case 'L':
        case 'M':
        case 'N':
        case 'O':
        case 'P':
        case 'Q':
        case 'R':
        case 'S':
        case 'T':
        case 'U':
        case 'V':
        case 'W':
        case 'X':
        case 'Y':
        case 'Z':

        case '_': {
            out_token->kind = C_TOKEN_IDENT;

            while (is_alpha_numeric(lexer->current_char) || lexer->current_char == '_')
                c_lexer_advance(lexer, true);

            lyir_location ident_end_location = c_lexer_get_location(lexer);
            out_token->string_value = string_view_slice(string_as_view(lexer->source_buffer.text), start_location.offset, ident_end_location.offset - start_location.offset);
        } break;

        default: {
            lyir_write_error(lexer->context, start_location, "Invalid character in source text.");
            c_lexer_advance(lexer, true);
        } break;
    }

finish_token:;
    lyir_location end_location = c_lexer_get_location(lexer);
    out_token->location.length = end_location.offset - start_location.offset;
}

static c_macro_def* c_lexer_lookup_macro_def(c_lexer* lexer, string_view macro_name) {
    for (long long i = 0; i < arr_count(lexer->tu->macro_defs); i++) {
        c_macro_def* def = lexer->tu->macro_defs[i];
        if (string_view_equals(macro_name, def->name))
            return def;
    }

    return NULL;
}

static void c_lexer_handle_preprocessor_directive(c_lexer* lexer);

static void c_lexer_read_token(c_lexer* lexer, c_token* out_token) {
    assert(lexer);
    if (arr_count(lexer->macro_expansions) > 0) {
        c_macro_expansion* macro_expansion = arr_back(lexer->macro_expansions);
        assert(macro_expansion);

        if (macro_expansion->arg_index >= 0) {
            long long arg_position = macro_expansion->arg_position;
            *out_token = macro_expansion->args[macro_expansion->arg_index][arg_position];
            macro_expansion->arg_position++;

            if (macro_expansion->arg_position >= arr_count(macro_expansion->args[macro_expansion->arg_index])) {
                macro_expansion->arg_index = -1;
                macro_expansion->arg_position = 0;

                if (macro_expansion->body_position >= arr_count(macro_expansion->def->body))
                    arr_pop(lexer->macro_expansions);
            }
        } else {
            assert(macro_expansion->def);
            dynarr(c_token) body = macro_expansion->def->body;
            if (macro_expansion->body_position >= arr_count(body)) {
                arr_pop(lexer->macro_expansions);
                goto regular_lex_token;
            }

            long long body_position = macro_expansion->body_position;
            assert(body_position >= 0);
            assert(macro_expansion->def->body);
            assert(body_position < arr_count(macro_expansion->def->body));
            *out_token = macro_expansion->def->body[body_position];
            macro_expansion->body_position++;

            if (out_token->is_macro_param) {
                macro_expansion->arg_index = out_token->macro_param_index;
                macro_expansion->arg_position = 0;

                *out_token = (c_token){0};
                c_lexer_read_token(lexer, out_token);
                return;
            }

            if (macro_expansion->body_position >= arr_count(macro_expansion->def->body))
                arr_pop(lexer->macro_expansions);
        }

        return;
    }

regular_lex_token:;
    c_lexer_eat_white_space(lexer);
    while (lexer->at_start_of_line && lexer->current_char == '#') {
        c_lexer_handle_preprocessor_directive(lexer);
        assert(!lexer->is_in_preprocessor);
        c_lexer_eat_white_space(lexer);
    }

    c_lexer_read_token_no_preprocess(lexer, out_token);

    if (out_token->kind == C_TOKEN_IDENT) {
        c_macro_def* macro_def = c_lexer_lookup_macro_def(lexer, out_token->string_value);
        if (macro_def != NULL) {
            if (macro_def->has_params) {
                c_lexer lexer_cache = *lexer;

                c_token arg_token = {0};
                c_lexer_read_token_no_preprocess(lexer, &arg_token);
                if (arg_token.kind != '(') {
                    *lexer = lexer_cache;
                    goto not_a_macro;
                }

                dynarr(dynarr(c_token)) args = NULL;
                dynarr(c_token) current_arg = NULL;
                for (;;) {
                    if (c_lexer_at_eof(lexer)) {
                        lyir_write_error(lexer->context, c_lexer_get_location(lexer), "Expected ')' in macro argument list.");
                        c_lexer_advance(lexer, true);
                        out_token->kind = C_TOKEN_EOF;
                        break;
                    }

                    c_lexer_read_token_no_preprocess(lexer, &arg_token);
                    if (arg_token.kind == ')') {
                        arr_push(args, current_arg);
                        current_arg = NULL;
                        break;
                    } else if (arg_token.kind == ',') {
                        arr_push(args, current_arg);
                        current_arg = NULL;
                        continue;
                    }

                    arr_push(current_arg, arg_token);
                }

                c_macro_expansion macro_expansion = {
                    .def = macro_def,
                    .args = args,
                    .arg_index = -1,
                };

                arr_push(lexer->macro_expansions, macro_expansion);
            } else {
                c_macro_expansion macro_expansion = {
                    .def = macro_def,
                    .arg_index = -1,
                };

                arr_push(lexer->macro_expansions, macro_expansion);
            }

            *out_token = (c_token){0};
            c_lexer_read_token(lexer, out_token);
            return;
        }

    not_a_macro:;
        for (int i = 0; c89_keywords[i].name != NULL; i++) {
            if (string_view_equals_cstring(out_token->string_value, c89_keywords[i].name)) {
                out_token->kind = c89_keywords[i].kind;
                break;
            }
        }
    }
}

static void c_lexer_skip_to_end_of_directive(c_lexer* lexer, c_token token) {
    assert(lexer);
    assert(lexer->is_in_preprocessor);
    while (token.kind != C_TOKEN_EOF && token.kind != '\n') {
        c_lexer_read_token_no_preprocess(lexer, &token);
    }

    if (lexer->current_char != 0 && lexer->cur < lexer->end) assert(lexer->at_start_of_line);
    lexer->is_in_preprocessor = false;
}

#define LEXER_PAST_EOF(L) ((L)->cur >= (L)->end)

static void c_lexer_handle_define_directive(c_lexer* lexer, c_token token) {
    assert(lexer);
    assert(lexer->is_in_preprocessor);
    assert(token.kind == C_TOKEN_IDENT);
    assert(string_view_equals_cstring(token.string_value, "define"));

    lyir_location start_location = token.location;
    c_lexer_read_token_no_preprocess(lexer, &token);

    if (token.kind == C_TOKEN_EOF) {
        lyir_write_error(lexer->context, token.location, "Macro name missing.");
        return;
    }

    if (token.kind != C_TOKEN_IDENT) {
        lyir_write_error(lexer->context, token.location, "Macro name must be an identifier.");
        c_lexer_skip_to_end_of_directive(lexer, token);
        return;
    }

    assert(token.kind == C_TOKEN_IDENT);
    string_view macro_name = token.string_value;

    bool macro_has_params = false;
    dynarr(string_view) macro_params = NULL;
    dynarr(c_token) macro_body = NULL;

    if (LEXER_PAST_EOF(lexer))
        goto store_macro_in_translation_unit;
    assert(lexer->cur < lexer->end);

    if (lexer->current_char == '(' &&
        (lexer->current_char_location - lexer->source_buffer.text.data) == token.location.offset + token.location.length) {
        macro_has_params = true;
        c_lexer_read_token_no_preprocess(lexer, &token);

        for (;;) {
            c_lexer_read_token_no_preprocess(lexer, &token);
            if (token.kind == C_TOKEN_INVALID) {
                c_lexer_skip_to_end_of_directive(lexer, token);
                return;
            }

            if (token.kind == C_TOKEN_EOF || token.kind == '\n') {
                lyir_write_error(lexer->context, token.location, "Expected ')' in macro parameter list.");
                c_lexer_advance(lexer, true);
                c_lexer_skip_to_end_of_directive(lexer, token);
                return;
            }

            if (token.kind != C_TOKEN_IDENT) {
                lyir_write_error(lexer->context, token.location, "Invalid token in macro parameter list (expected identifier.)");
                c_lexer_advance(lexer, true);
                c_lexer_skip_to_end_of_directive(lexer, token);
                return;
            }

            assert(token.kind == C_TOKEN_IDENT);
            arr_push(macro_params, token.string_value);

            c_lexer_read_token_no_preprocess(lexer, &token);
            if (token.kind == ')')
                break;
            else if (token.kind != ',') {
                lyir_write_error(lexer->context, token.location, "Expected comma in macro parameter list.");
                c_lexer_skip_to_end_of_directive(lexer, token);
                return;
            }
        }

        // c_lexer_read_token_no_preprocess(lexer, &token);
        if (token.kind != ')') {
            lyir_write_error(lexer->context, token.location, "Expected ')' in macro parameter list.");
            c_lexer_advance(lexer, true);
            c_lexer_skip_to_end_of_directive(lexer, token);
            return;
        }
    }

    for (;;) {
        token = (c_token){0};
        c_lexer_read_token_no_preprocess(lexer, &token);
        if (token.kind == C_TOKEN_INVALID) {
            c_lexer_skip_to_end_of_directive(lexer, token);
            return;
        }

        if (token.kind == C_TOKEN_EOF || token.kind == '\n')
            break;

        if (token.kind == C_TOKEN_IDENT && macro_has_params) {
            for (long long i = 0; i < arr_count(macro_params); i++) {
                if (string_view_equals(token.string_value, macro_params[i])) {
                    token.is_macro_param = true;
                    token.macro_param_index = i;
                    break;
                }
            }
        }

        arr_push(macro_body, token);
    }

store_macro_in_translation_unit:;
    c_macro_def* def = calloc(1, sizeof *def);
    def->name = macro_name;
    def->has_params = macro_has_params;
    def->params = macro_params;
    def->body = macro_body;

    arr_push(lexer->tu->macro_defs, def);

    lexer->is_in_preprocessor = false;
    lexer->is_in_include = false;
}

static void c_lexer_handle_include_directive(c_lexer* lexer, c_token token) {
    assert(lexer);
    assert(lexer->is_in_preprocessor);
    assert(token.kind == C_TOKEN_IDENT);
    assert(string_view_equals_cstring(token.string_value, "include"));

    lyir_location include_location = token.location;

    lexer->is_in_include = true;
    c_lexer_read_token_no_preprocess(lexer, &token);

    bool is_angle_string = false;
    string_view include_path = {0};

    if (token.kind == '\n') {
        lyir_write_error(lexer->context, token.location, "Expected a file name.");
        c_lexer_advance(lexer, true);
        c_lexer_skip_to_end_of_directive(lexer, token);
        return;
    }

    if (token.kind == C_TOKEN_LIT_STRING) {
        is_angle_string = token.is_angle_string;
        include_path = token.string_value;
    } else {
        // TODO(local): computed #include
        lyir_write_error(lexer->context, token.location, "Expected a file name.");
        c_lexer_advance(lexer, true);
    }

    c_lexer_skip_to_end_of_directive(lexer, token);

    lexer->is_in_preprocessor = false;
    lexer->is_in_include = false;

    // TODO(local): process the include
    int include_sourceid = lyir_context_get_or_add_source_from_file(lexer->context, include_path);
    if (include_sourceid <= 0) {
        // TODO(local): only do this for quote strings, not angle strings
        if (!is_angle_string) {
            string include_path2 = string_create(lexer->context->allocator);
            string_append_format(&include_path2, "%.*s", STR_EXPAND(lexer->source_buffer.name));
            for (int64_t i = include_path2.count - 1; i >= 0 && include_path2.data[i] != '/' && include_path2.data[i] != '\\'; i--) {
                include_path2.count = i;
            }
            string_path_append_view(&include_path2, include_path);

            // string_view parent_dir = string_as_view(lexer->source_buffer.name);
            // string_view include_path2 = string_view_path_concat(parent_dir, include_path);

            include_sourceid = lyir_context_get_or_add_source_from_file(lexer->context, string_as_view(include_path2));
            string_destroy(&include_path2);

            if (include_sourceid > 0) {
                goto good_include_gogogo;
            }

            // free((void*)include_path2.data);
        }

        for (long long i = 0; i < arr_count(lexer->context->include_directories); i++) {
            string_view include_dir = lexer->context->include_directories[i];

            string include_path2 = string_create(lexer->context->allocator);
            string_append_format(&include_path2, "%.*s", STR_EXPAND(include_dir));
            string_path_append_view(&include_path2, include_path);
            // string_view include_path2 = string_view_path_concat(include_dir, include_path);

            include_sourceid = lyir_context_get_or_add_source_from_file(lexer->context, string_as_view(include_path2));
            string_destroy(&include_path2);

            if (include_sourceid > 0) {
                goto good_include_gogogo;
            }

            free((void*)include_path2.data);
        }

        goto handle_include_error;
    }

good_include_gogogo:;
    c_token_buffer include_token_buffer = c_get_tokens(lexer->context, lexer->tu, include_sourceid);

    c_macro_def* include_macro_def = calloc(1, sizeof *include_macro_def);
    include_macro_def->name = SV_CONSTANT("< include >");
    include_macro_def->body = include_token_buffer.semantic_tokens;

    arr_push(lexer->tu->macro_defs, include_macro_def);
    c_macro_def* include_macro_def_ptr = *arr_back(lexer->tu->macro_defs);

    c_macro_expansion macro_expansion = {
        .def = include_macro_def_ptr,
        .arg_index = -1,
    };

    arr_push(lexer->macro_expansions, macro_expansion);
    return;

handle_include_error:;
    lyir_write_error(lexer->context, include_location, "Could not read included file '%.*s'.", STR_EXPAND(include_path));
    c_lexer_advance(lexer, true);
}

static void c_lexer_handle_preprocessor_directive(c_lexer* lexer) {
    assert(lexer);
    assert(!lexer->is_in_preprocessor);
    assert(lexer->at_start_of_line && lexer->current_char == '#');

    lexer->is_in_preprocessor = true;
    c_lexer_advance(lexer, true);

    lyir_location start_location = c_lexer_get_location(lexer);

    c_token token = {0};
    c_lexer_read_token_no_preprocess(lexer, &token);

    switch (token.kind) {
        default: {
        invalid_preprocessing_directive:;
            lyir_write_error(lexer->context, token.location, "Invalid preprocessing directive.");
            c_lexer_advance(lexer, true);
            c_lexer_skip_to_end_of_directive(lexer, token);
            return;
        }

        case C_TOKEN_IDENT: {
            if (string_view_equals_cstring(token.string_value, "define"))
                c_lexer_handle_define_directive(lexer, token);
            else if (string_view_equals_cstring(token.string_value, "include"))
                c_lexer_handle_include_directive(lexer, token);
            else goto invalid_preprocessing_directive;
        } break;
    }

    assert(!lexer->is_in_preprocessor);
}

static bool c_lexer_skip_backslash_newline(c_lexer* lexer) {
    if (!c_lexer_at_eof(lexer) && *lexer->cur == '\\' &&
        (c_lexer_peek_no_process(lexer, 1) == '\n' || (c_lexer_peek_no_process(lexer, 1) == '\r' && c_lexer_peek_no_process(lexer, 2) == '\n'))) {
        lexer->cur++;
        assert(!c_lexer_at_eof(lexer));

        if (*lexer->cur == '\n') {
            lexer->cur++;
            if (!c_lexer_at_eof(lexer) && *lexer->cur == '\r')
                lexer->cur++;
        } else {
            assert(*lexer->cur == '\r' && c_lexer_peek_no_process(lexer, 1) == '\n');
            lexer->cur += 2;
        }

        return true;
    }

    return false;
}

static int c_lexer_read_next_char(c_lexer* lexer, bool allow_comments) {
    if (LEXER_PAST_EOF(lexer)) return 0;

    if (lexer->current_char == '\n')
        lexer->at_start_of_line = true;
    else if (!is_space(lexer->current_char) && lexer->current_char != 0)
        lexer->at_start_of_line = false;

    while (c_lexer_skip_backslash_newline(lexer)) {}
    if (LEXER_PAST_EOF(lexer)) return 0;

    if (allow_comments && *lexer->cur == '/') {
        if (c_lexer_peek_no_process(lexer, 2) == '/') {
            bool has_warned = false;

            lexer->cur += 2;
            while (!LEXER_PAST_EOF(lexer)) {
                if (*lexer->cur == '\\' && c_lexer_skip_backslash_newline(lexer)) {
                    if (!has_warned) {
                        // TODO(local): we could track the total length of the comment and report it after
                        lyir_location location = (lyir_location){
                            .sourceid = lexer->sourceid,
                            .offset = lexer->cur - lexer->source_buffer.text.data,
                            .length = 1,
                        };
                        lyir_write_warn(lexer->context, location, "Multiline // comment.");
                    }

                    has_warned = true;
                    continue;
                }

                if (*lexer->cur == '\n')
                    break;
                lexer->cur++;
            }

            return ' ';
        } else if (c_lexer_peek_no_process(lexer, 2) == '*') {
            lexer->cur += 2;

            int lastc = 0;
            for (;;) {
                if (!LEXER_PAST_EOF(lexer) && *lexer->cur == '\\')
                    c_lexer_skip_backslash_newline(lexer);

                if (LEXER_PAST_EOF(lexer)) {
                    // TODO(local): we could track the total length of the comment and report it after
                    lyir_location location = (lyir_location){
                        .sourceid = lexer->sourceid,
                        .offset = lexer->cur - lexer->source_buffer.text.data,
                        .length = 1,
                    };
                    lyir_write_warn(lexer->context, location, "Unfinished /* comment.");
                    break;
                }

                char curc = *lexer->cur;
                lexer->cur++;

                // NOTE(local): is this standard compliant behavior for start of line + preprocessor directive?
                if (curc == '\n')
                    lexer->at_start_of_line = true;

                if (curc == '/' && lastc == '*')
                    break;

                lastc = curc;
            }

            return ' ';
        }
    }

    assert(!LEXER_PAST_EOF(lexer));
    int result = *lexer->cur;
    lexer->cur++;
    return result;
}

#undef LEXER_PAST_EOF

static int c_lexer_peek_no_process(c_lexer* lexer, int ahead) {
    assert(ahead >= 1);
    const char* at = lexer->cur + ahead - 1;
    if (at >= lexer->end) return 0;
    return *at;
}

static bool c_lexer_at_eof(c_lexer* lexer) {
    return lexer->current_char == 0;
}

static void c_lexer_advance(c_lexer* lexer, bool allow_comments) {
    lexer->current_char_location = lexer->cur;
    lexer->current_char = c_lexer_read_next_char(lexer, allow_comments);
    if (lexer->current_char != 0) assert(lexer->current_char_location < lexer->cur);
}

c_translation_unit* c_parse(lyir_context* context, lyir_sourceid sourceid) {
    assert(context != NULL);
    assert(sourceid >= 0);

    c_translation_unit* tu = lca_allocate(context->allocator, sizeof *tu);
    assert(tu != NULL);
    tu->context = context;
    tu->sourceid = sourceid;
    tu->arena = lca_arena_create(context->allocator, 1024 * 1024);

    tu->token_buffer = c_get_tokens(context, tu, sourceid);

    return tu;
}
