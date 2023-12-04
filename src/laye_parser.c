#include <assert.h>

#include "layec.h"

typedef struct laye_parser {
    layec_context* context;
    laye_module* module;
    layec_sourceid sourceid;

    layec_source source;
    int64_t lexer_position;
    int current_char;

    laye_token token;
} laye_parser;

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

static laye_token laye_next_token(laye_parser* p);
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
        .source = source,
    };

    // prime the first token before we begin parsing
    laye_next_token(&p);

    while (p.token.kind != LAYE_TOKEN_EOF) {
        layec_write_note(context, p.token.location, "%s", laye_token_kind_to_cstring(p.token.kind));
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

static laye_token laye_next_token(laye_parser* p) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);

    laye_token token = {
        .kind = LAYE_TOKEN_INVALID,
        .location = (layec_location) {
            .sourceid = p->sourceid,
            .offset = p->lexer_position,
        }
    };

    if (p->lexer_position >= p->source.text.count) {
        token.kind = LAYE_TOKEN_EOF;
        return token;
    }

    assert(token.kind != LAYE_TOKEN_INVALID && "tokenization routines failed to update the kind of the token");

    token.location.length = p->lexer_position - token.location.offset;
    assert(token.location.length > 0 && "returning a zero-length token means probably broken tokenizer, oops");
    
    return token;
}
