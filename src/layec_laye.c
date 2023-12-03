#include "layec.h"

#include <assert.h>

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
