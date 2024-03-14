#include "c.h"

#include <assert.h>

typedef struct c_macro_def {
    string_view name;
    bool has_params;
    dynarr(string_view) params;
    dynarr(c_token) body;
} c_macro_def;

typedef struct c_macro_expansion {
    c_macro_def* def;
    int64_t body_position;
    dynarr(dynarr(c_token)) args;
    int arg_index; // set to -1 when not expanding an argument
    int arg_position;
} c_macro_expansion;

typedef struct c_parser {
    layec_context* context;
    c_translation_unit* tu;
    layec_sourceid sourceid;

    layec_source source;
    int64_t lexer_position;
    int current_char;

    c_token token;
    c_token next_token;

    bool at_start_of_line;
    bool is_in_preprocessor;
    bool is_in_include;

    dynarr(c_macro_def) macro_defs;
    dynarr(c_macro_expansion) macro_expansions;
} c_parser;

static bool c_is_space(int c) {
    return c == ' ' || c == '\t' || c == '\v' || c == '\r' || c == '\n';
}

static bool c_skip_backslash_newline(c_parser* p) {

}

static int c_read_next_char(c_parser* p, bool allow_comments) {
    if (p->lexer_position >= p->source.text.count) {
        return 0;
    }

    if (p->current_char == '\n') {
        p->at_start_of_line = true;
    } else if (!c_is_space(p->current_char) && p->current_char != 0) {
        p->at_start_of_line = false;
    }

    while (c_skip_backslash_newline(p)) {
    }

    if (p->lexer_position >= p->source.text.count) {
        return 0;
    }
}

static void c_char_advance(c_parser* p, bool allow_comments) {
    p->lexer_position++;

    if (p->lexer_position >= p->source.text.count) {
        p->lexer_position = p->source.text.count;
        p->current_char = 0;
        return;
    }

    p->current_char = p->source.text.data[p->lexer_position];
}

static void c_skip_whitespace(c_parser* p) {
    while (c_is_space(p->current_char)) {
        if (p->is_in_preprocessor && p->current_char == '\n')
            break;
        
        c_char_advance(p, true);
    }
}

static void c_read_token_no_preprocess(c_parser* p, c_token* out_token) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->tu != NULL);
    assert(out_token != NULL);

    out_token->location.offset = p->lexer_position;

token_finished:;
    assert(out_token->kind != C_TOKEN_INVALID && "tokenization routines failed to update the kind of the token");

    out_token->location.length = p->lexer_position - out_token->location.offset;
    assert(out_token->location.length > 0 && "returning a zero-length token means probably broken tokenizer, oops");
}

static c_macro_def* c_lookup_macro_def(c_parser* p, string_view macro_name) {
    for (int64_t i = 0, count = arr_count(p->macro_defs); i < count; i++) {
        c_macro_def* def = &p->macro_defs[i];
        if (string_view_equals(macro_name, def->name)) {
            return def;
        }
    }

    return NULL;
}

static void c_handle_pp_directive(c_parser* p) {
}

static void c_read_token(c_parser* p, c_token* out_token) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->tu != NULL);
    assert(out_token != NULL);

    if (arr_count(p->macro_expansions) > 0) {
        c_macro_expansion* macro_expansion = arr_back(p->macro_expansions);
        if (macro_expansion->arg_index != -1) {
            assert(false && "TODO: C macro argument expansion");

            int64_t body_position = macro_expansion->body_position;
            *out_token = macro_expansion->def->body[body_position];
            macro_expansion->body_position++;
        } else {
            int64_t body_position = macro_expansion->body_position;
            *out_token = macro_expansion->def->body[body_position];
            macro_expansion->body_position++;

            if (out_token->is_macro_param) {
                macro_expansion->arg_index = out_token->macro_param_index;
                macro_expansion->arg_position = 0;

                *out_token = (c_token){0};
                c_read_token(p, out_token);
                return;
            }

            if (macro_expansion->body_position >= arr_count(macro_expansion->def->body)) {
                arr_pop(p->macro_expansions);
            }
        }
    }

    c_skip_whitespace(p);
    while (p->at_start_of_line && p->current_char == '#') {
        c_handle_pp_directive(p);
        assert(!p->is_in_preprocessor);
        c_skip_whitespace(p);
    }

    c_read_token_no_preprocess(p, out_token);

    if (out_token->kind == C_TOKEN_IDENT) {
        c_macro_def* def = c_lookup_macro_def(p, out_token->string_value);
        if (def != NULL) {
            if (def->has_params) {
                c_parser parser_cache = *p;

                c_token arg_token = {0};
                c_read_token_no_preprocess(p, &arg_token);

                if (arg_token.kind != '(') {
                    *p = parser_cache;
                    goto not_a_macro;
                }

                dynarr(dynarr(c_token)) args = NULL;
                dynarr(c_token) current_arg = NULL;
                // TODO(local): nested parens shield commas
                while (true) {
                    if (p->lexer_position >= p->source.text.count) {
                        layec_location eof_location = (layec_location){.sourceid = p->sourceid, .offset = p->lexer_position, .length = 0};
                        layec_write_error(p->context, eof_location, "Expected ')' in macro argument list");
                        out_token->kind = C_TOKEN_EOF;
                        break;
                    }

                    c_read_token_no_preprocess(p, &arg_token);
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

                c_macro_expansion mexp = {
                    .def = def,
                    .args = args,
                    .arg_index = -1,
                };
                arr_push(p->macro_expansions, mexp);
            } else {
                c_macro_expansion mexp = {
                    .def = def,
                    .arg_index = -1,
                };
                arr_push(p->macro_expansions, mexp);
            }

            c_read_token(p, out_token);
            return;
        }

    not_a_macro:;

        // TODO(local): keywords
    }
}

c_translation_unit* c_parse(layec_context* context, layec_sourceid sourceid) {
    assert(context != NULL);
    assert(sourceid >= 0);

    c_translation_unit* tu = lca_allocate(context->allocator, sizeof *tu);
    assert(tu != NULL);
    tu->context = context;
    tu->sourceid = sourceid;
    tu->arena = lca_arena_create(context->allocator, 1024 * 1024);
    assert(tu->arena);

    layec_source source = layec_context_get_source(context, sourceid);

    c_parser p = {
        .context = context,
        .tu = tu,
        .sourceid = sourceid,
        .source = source,
    };

    return tu;
}
