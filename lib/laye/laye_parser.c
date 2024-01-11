#include <assert.h>

#include "layec.h"
#include "laye.h"

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
} laye_parser;

typedef struct laye_parse_result {
    bool success;
    dynarr(layec_diag) diags;
    laye_node* node;
} laye_parse_result;

static void laye_parse_result_write_diags(layec_context* context, laye_parse_result result) {
    for (int64_t i = 0, count = arr_count(result.diags); i < count; i++) {
        layec_write_diag(context, result.diags[i]);
    }
}

static void laye_parse_result_destroy(laye_parse_result result) {
    arr_free(result.diags);
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

laye_module* laye_parse(layec_context* context, layec_sourceid sourceid) {
    assert(context != NULL);
    assert(sourceid >= 0);

    laye_module* module = lca_allocate(context->allocator, sizeof *module);
    assert(module);
    module->context = context;
    module->sourceid = sourceid;
    module->arena = lca_arena_create(context->allocator, 1024 * 1024);
    assert(module->arena);

    laye_scope* module_scope = laye_scope_create(module, NULL);
    assert(module_scope != NULL);

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

    return module;
}

// ========== Parser ==========

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

    laye_node* invalid_node = laye_node_create(p->module, LAYE_NODE_INVALID, p->token.location, p->context->laye_types._void);
    laye_next_token(p);
    return invalid_node;
}

static laye_node* laye_parser_create_invalid_node_from_child(laye_parser* p, laye_node* node) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(node != NULL);

    laye_node* invalid_node = laye_node_create(p->module, LAYE_NODE_INVALID, node->location, p->context->laye_types._void);
    return invalid_node;
}

static dynarr(laye_node*) laye_parse_attributes(laye_parser* p);
static bool laye_can_parse_type(laye_parser* p);
static laye_parse_result laye_parse_type(laye_parser* p);
static laye_node* laye_parse_declaration(laye_parser* p, bool can_be_expression);

static laye_node* laye_parse_top_level_node(laye_parser* p) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);

    laye_node* top_level_declaration = laye_parse_declaration(p, false);
    assert(top_level_declaration != NULL);

    return top_level_declaration;
}

static void laye_parse_result_copy_diags(laye_parse_result* target, laye_parse_result from) {
    for (int64_t i = 0, count = arr_count(from.diags); i < count; i++) {
        arr_push(target->diags, from.diags[i]);
    }
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

static laye_parse_result laye_try_parse_type_continue(laye_parser* p, laye_node* type, bool allocate) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);

    struct laye_parser_mark start_mark = laye_parser_mark(p);
    laye_parse_result result = {
        .node = type,
        .success = true,
    };

    switch (p->token.kind) {
        default: return result;

        case '[': {
            laye_next_token(p);
            if (laye_parser_at(p, '*') && laye_parser_peek_at(p, ']')) {
                laye_next_token(p);
                if (allocate) {
                    result.node = laye_node_create(p->module, LAYE_NODE_TYPE_BUFFER, type->location, p->context->laye_types.type);
                    assert(result.node != NULL);
                    result.node->type_container.element_type = type;
                }
            } else if (laye_parser_at(p, ']')) {
                if (allocate) {
                    result.node = laye_node_create(p->module, LAYE_NODE_TYPE_SLICE, type->location, p->context->laye_types.type);
                    assert(result.node != NULL);
                    result.node->type_container.element_type = type;
                }
            } else {
                // we'll error when we don't see ']', so nothing special to do here other than allocate
                if (allocate) {
                    result.node = laye_node_create(p->module, LAYE_NODE_TYPE_POISON, type->location, p->context->laye_types.type);
                    assert(result.node != NULL);
                }
            }

            laye_token closing_token = {0};
            if (!laye_parser_consume(p, ']', &closing_token)) {
                if (allocate) {
                    arr_push(result.diags, layec_error(p->context, p->token.location, "Expected ']'."));
                }
            }

            if (allocate) {
                assert(result.node != NULL);
                result.node->location.length = closing_token.location.offset + closing_token.location.length - result.node->location.offset;
            }
        } break;

        case '*': {
            laye_token star_token = p->token;
            laye_next_token(p);

            if (allocate) {
                result.node = laye_node_create(p->module, LAYE_NODE_TYPE_POINTER, type->location, p->context->laye_types.type);
                assert(result.node != NULL);
                result.node->type_container.element_type = type;
                result.node->location.length = star_token.location.offset + star_token.location.length - result.node->location.offset;
            }
        } break;
    }

    bool type_is_modifiable = laye_parse_type_modifiable_modifiers(p, &result, allocate);
    if (allocate) {
        assert(result.node != NULL);
        result.node->type_is_modifiable = type_is_modifiable;
    }

    laye_parse_result continue_result = laye_try_parse_type_continue(p, result.node, allocate);
    laye_parse_result_copy_diags(&result, continue_result);
    result.node = continue_result.node;
    laye_parse_result_destroy(continue_result);

    if (!allocate) {
        assert(result.node == NULL);
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

    switch (p->token.kind) {
        default: {
            result.success = false;
            if (allocate) {
                arr_push(result.diags, layec_error(p->context, p->token.location, "Unexpected token when a type was expected."));
                result.node = laye_node_create(p->module, LAYE_NODE_TYPE_POISON, p->token.location, p->context->laye_types.type);
                assert(result.node != NULL);
                laye_next_token(p);
            }
        } break;

        case LAYE_TOKEN_VOID: {
            if (allocate) {
                result.node = laye_node_create(p->module, LAYE_NODE_TYPE_VOID, p->token.location, p->context->laye_types.type);
                assert(result.node != NULL);
            }
            laye_next_token(p);
        } break;

        case LAYE_TOKEN_NORETURN: {
            if (allocate) {
                result.node = laye_node_create(p->module, LAYE_NODE_TYPE_NORETURN, p->token.location, p->context->laye_types.type);
                assert(result.node != NULL);
            }
            laye_next_token(p);
        } break;

        case LAYE_TOKEN_BOOL: {
            if (allocate) {
                result.node = laye_node_create(p->module, LAYE_NODE_TYPE_BOOL, p->token.location, p->context->laye_types.type);
                assert(result.node != NULL);
                result.node->type_primitive.bit_width = p->context->target->laye.size_of_bool;
                result.node->type_primitive.is_platform_specified = true;
            }
            laye_next_token(p);
        } break;

        case LAYE_TOKEN_BOOLSIZED: {
            if (allocate) {
                result.node = laye_node_create(p->module, LAYE_NODE_TYPE_BOOL, p->token.location, p->context->laye_types.type);
                assert(result.node != NULL);
                assert(p->token.int_value > 0 && p->token.int_value < 65536);
                result.node->type_primitive.bit_width = (int)p->token.int_value;
            }
            laye_next_token(p);
        } break;

        case LAYE_TOKEN_INT: {
            if (allocate) {
                result.node = laye_node_create(p->module, LAYE_NODE_TYPE_INT, p->token.location, p->context->laye_types.type);
                assert(result.node != NULL);
                result.node->type_primitive.bit_width = p->context->target->laye.size_of_int;
                result.node->type_primitive.is_signed = true;
                result.node->type_primitive.is_platform_specified = true;
            }
            laye_next_token(p);
        } break;

        case LAYE_TOKEN_INTSIZED: {
            if (allocate) {
                result.node = laye_node_create(p->module, LAYE_NODE_TYPE_INT, p->token.location, p->context->laye_types.type);
                assert(result.node != NULL);
                assert(p->token.int_value > 0 && p->token.int_value < 65536);
                result.node->type_primitive.bit_width = (int)p->token.int_value;
                result.node->type_primitive.is_signed = true;
            }
            laye_next_token(p);
        } break;

        case LAYE_TOKEN_UINT: {
            if (allocate) {
                result.node = laye_node_create(p->module, LAYE_NODE_TYPE_INT, p->token.location, p->context->laye_types.type);
                assert(result.node != NULL);
                result.node->type_primitive.bit_width = p->context->target->laye.size_of_int;
                result.node->type_primitive.is_signed = false;
                result.node->type_primitive.is_platform_specified = true;
            }
            laye_next_token(p);
        } break;

        case LAYE_TOKEN_UINTSIZED: {
            if (allocate) {
                result.node = laye_node_create(p->module, LAYE_NODE_TYPE_INT, p->token.location, p->context->laye_types.type);
                assert(result.node != NULL);
                assert(p->token.int_value > 0 && p->token.int_value < 65536);
                result.node->type_primitive.bit_width = (int)p->token.int_value;
                result.node->type_primitive.is_signed = false;
            }
            laye_next_token(p);
        } break;
    }

    bool type_is_modifiable = laye_parse_type_modifiable_modifiers(p, &result, allocate);
    if (allocate) {
        assert(result.node != NULL);
        result.node->type_is_modifiable = type_is_modifiable;
    }

    laye_parse_result continue_result = laye_try_parse_type_continue(p, result.node, allocate);
    laye_parse_result_copy_diags(&result, continue_result);
    result.success = 0 == arr_count(result.diags);
    result.node = continue_result.node;
    laye_parse_result_destroy(continue_result);

    if (!allocate) {
        assert(result.node == NULL);
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

static laye_node* laye_parse_type_or_error(laye_parser* p) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);

    laye_parse_result result = laye_try_parse_type_impl(p, true, true);
    if (result.success) {
        assert(arr_count(result.diags) == 0);
        assert(result.node != NULL);
        return result.node;
    }

    laye_parse_result_write_diags(p->context, result);
    laye_parse_result_destroy(result);
    return result.node;
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

static dynarr(laye_node*) laye_parse_attributes(laye_parser* p) {
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
                laye_node* simple_attribute_node = laye_node_create(p->module, LAYE_NODE_META_ATTRIBUTE, p->token.location, p->context->laye_types._void);
                assert(simple_attribute_node != NULL);
                simple_attribute_node->meta_attribute.kind = p->token.kind;
                simple_attribute_node->meta_attribute.keyword_token = p->token;

                laye_next_token(p);
                arr_push(attributes, simple_attribute_node);
            } break;

            case LAYE_TOKEN_FOREIGN: {
                laye_node* foreign_node = laye_node_create(p->module, LAYE_NODE_META_ATTRIBUTE, p->token.location, p->context->laye_types._void);
                assert(foreign_node != NULL);
                foreign_node->meta_attribute.kind = p->token.kind;
                foreign_node->meta_attribute.keyword_token = p->token;
                foreign_node->meta_attribute.mangling = LAYEC_MANGLE_NONE;

                laye_next_token(p);
                arr_push(attributes, foreign_node);

                if (laye_parser_consume(p, '(', NULL)) {
                    if (p->token.kind == LAYE_TOKEN_IDENT) {
                        string_view mangling_kind_name = string_as_view(p->token.string_value);
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

                    if (laye_parser_at(p, LAYE_TOKEN_LITSTRING)) {
                        string foreign_name_value = p->token.string_value;
                        foreign_node->meta_attribute.foreign_name = foreign_name_value;
                        laye_next_token(p);
                    }
                }

                laye_token foreign_name_token = {0};
                if (laye_parser_consume(p, LAYE_TOKEN_LITSTRING, &foreign_name_token)) {
                    foreign_node->meta_attribute.foreign_name = foreign_name_token.string_value;
                }
            } break;

            case LAYE_TOKEN_CALLCONV: {
                laye_node* callconv_node = laye_node_create(p->module, LAYE_NODE_META_ATTRIBUTE, p->token.location, p->context->laye_types._void);
                assert(callconv_node != NULL);
                callconv_node->meta_attribute.kind = p->token.kind;
                callconv_node->meta_attribute.keyword_token = p->token;

                laye_next_token(p);
                arr_push(attributes, callconv_node);

                if (laye_parser_consume(p, '(', NULL)) {
                    if (p->token.kind == LAYE_TOKEN_IDENT) {
                        string_view callconv_kind_name = string_as_view(p->token.string_value);
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

static laye_node* laye_parse_statement(laye_parser* p);
static laye_node* laye_parse_expression(laye_parser* p);

static laye_node* laye_parse_compound_expression(laye_parser* p) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind == '{');

    layec_location start_location = p->token.location;
    laye_next_token(p);

    dynarr(laye_node*) children = NULL;

    laye_parser_push_scope(p);

    while (!laye_parser_at2(p, LAYE_TOKEN_EOF, '}')) {
        laye_node* child_expr = laye_parse_declaration(p, true);
        assert(child_expr != NULL);

        arr_push(children, child_expr);
    }

    layec_location end_location = {0};
    laye_token closing_token = {0};

    if (laye_parser_consume(p, '}', &closing_token)) {
        assert(closing_token.kind == '}');
        end_location = closing_token.location;
    } else {
        if (arr_count(children) == 0) {
            end_location = start_location;
        } else {
            end_location = (*arr_back(children))->location;
        }

        layec_write_error(p->context, p->token.location, "Expected '}'.");
    }

    laye_parser_pop_scope(p);

    layec_location total_location = start_location;
    assert(end_location.offset >= start_location.offset);
    total_location.length = (end_location.offset + end_location.length) - start_location.offset;
    assert(total_location.length >= start_location.length);

    // for now, assume void; see if we can't deduce the need for a type + what it would be, so far, syntactically.
    laye_node* compound_type = p->context->laye_types._void;
    laye_node* compound_expression = laye_node_create(p->module, LAYE_NODE_COMPOUND, total_location, compound_type);
    compound_expression->compound.children = children;

    return compound_expression;
}

static laye_node* laye_parse_declaration_continue(laye_parser* p, dynarr(laye_node*) attributes, laye_node* declared_type, laye_token name_token) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);

    if (laye_parser_consume(p, '(', NULL)) {
        dynarr(laye_node*) parameter_types = NULL;
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

            laye_node* parameter_type = laye_parse_type_or_error(p);
            assert(parameter_type != NULL);
            arr_push(parameter_types, parameter_type);

            laye_token name_token = p->token;
            if (!laye_parser_consume(p, LAYE_TOKEN_IDENT, NULL)) {
                layec_write_error(p->context, p->token.location, "Expected an identifier.");
                name_token.kind = LAYE_TOKEN_INVALID;
                name_token.location.length = 0;
            }

            layec_location parameter_location = name_token.location.length != 0 ? name_token.location : parameter_type->location;
            laye_node* parameter_node = laye_node_create(p->module, LAYE_NODE_DECL_FUNCTION_PARAMETER, parameter_location, parameter_type);
            assert(parameter_node != NULL);
            parameter_node->declared_type = parameter_type;
            parameter_node->declared_name = name_token.string_value;

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

        laye_node* function_type = laye_node_create(p->module, LAYE_NODE_TYPE_FUNCTION, name_token.location, p->context->laye_types.type);
        assert(function_type != NULL);
        function_type->type_function.return_type = declared_type;
        function_type->type_function.parameter_types = parameter_types;
        function_type->type_function.varargs_style = varargs_style;

        laye_node* function_node = laye_node_create(p->module, LAYE_NODE_DECL_FUNCTION, name_token.location, p->context->laye_types._void);
        assert(function_node != NULL);
        laye_apply_attributes(function_node, attributes);
        function_node->declared_name = name_token.string_value;
        function_node->declared_type = function_type;
        function_node->decl_function.return_type = declared_type;
        function_node->decl_function.parameter_declarations = parameters;
        assert(p->scope != NULL);
        laye_scope_declare(p->scope, function_node);

        function_type->type_function.calling_convention = function_node->attributes.calling_convention;

        laye_parser_push_scope(p);
        p->scope->name = name_token.string_value;
        p->scope->is_function_scope = true;

        laye_node* function_body = NULL;
        if (!laye_parser_consume(p, ';', NULL)) {
            if (laye_parser_consume(p, LAYE_TOKEN_EQUALGREATER, NULL)) {
                laye_node* function_body_expr = laye_parse_expression(p);
                assert(function_body_expr != NULL);

                if (!laye_parser_consume(p, ';', NULL)) {
                    layec_write_error(p->context, p->token.location, "Expected ';'.");
                }

                laye_node* implicit_return_node = laye_node_create(p->module, LAYE_NODE_RETURN, function_body_expr->location, p->context->laye_types._void);
                assert(implicit_return_node != NULL);
                implicit_return_node->compiler_generated = true;
                implicit_return_node->_return.value = function_body_expr;

                function_body = laye_node_create(p->module, LAYE_NODE_COMPOUND, function_body_expr->location, p->context->laye_types._void);
                assert(function_body != NULL);
                function_body->compiler_generated = true;
                function_body->compound.scope_name = name_token.string_value;
                arr_push(function_body->compound.children, implicit_return_node);
                assert(1 == arr_count(function_body->compound.children));
            } else {
                if (!laye_parser_at(p, '{')) {
                    layec_write_error(p->context, p->token.location, "Expected '{'.");
                    function_body = laye_node_create(p->module, LAYE_NODE_INVALID, p->token.location, p->context->laye_types.poison);
                    assert(function_body != NULL);
                } else {
                    function_body = laye_parse_compound_expression(p);
                    assert(function_body != NULL);
                }
            }

            assert(function_body != NULL);
        }

        laye_parser_pop_scope(p);

        function_node->decl_function.body = function_body;
        return function_node;
    }

    laye_node* binding_node = laye_node_create(p->module, LAYE_NODE_DECL_BINDING, name_token.location, p->context->laye_types._void);
    assert(binding_node != NULL);
    laye_apply_attributes(binding_node, attributes);
    binding_node->declared_type = declared_type;
    binding_node->declared_name = name_token.string_value;
    assert(p->scope != NULL);
    laye_scope_declare(p->scope, binding_node);

    if (laye_parser_consume(p, '=', NULL)) {
        laye_node* initial_value = laye_parse_expression(p);
        assert(initial_value != NULL);
        binding_node->decl_binding.initializer = initial_value;
    }

    if (!laye_parser_consume(p, ';', NULL)) {
        layec_write_error(p->context, p->token.location, "Expected ';'.");
    }

    return binding_node;
}

static laye_node* laye_parse_declaration(laye_parser* p, bool can_be_expression) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);

    struct laye_parser_mark start_mark = laye_parser_mark(p);

    dynarr(laye_node*) attributes = laye_parse_attributes(p);
    if (arr_count(attributes) != 0) {
        goto try_decl;
    }

    switch (p->token.kind) {
        case LAYE_TOKEN_INVALID: assert(false && "unreachable"); return NULL;

        case LAYE_TOKEN_IF:
        case LAYE_TOKEN_FOR:
        case LAYE_TOKEN_RETURN:
        case LAYE_TOKEN_BREAK:
        case LAYE_TOKEN_CONTINUE:
        case LAYE_TOKEN_YIELD:
        case LAYE_TOKEN_XYZZY: {
            return laye_parse_statement(p);
        }

        default: {
        try_decl:;
            laye_parse_result declared_type_result = laye_parse_type(p);
            assert(declared_type_result.node != NULL);

            if (!declared_type_result.success) {
                laye_parse_result_destroy(declared_type_result);
                arr_free(attributes);

                if (can_be_expression) {
                    laye_parser_reset_to_mark(p, start_mark);
                    return laye_parse_statement(p);
                }

                laye_node* invalid_node = laye_parser_create_invalid_node_from_child(p, declared_type_result.node);
                invalid_node->attribute_nodes = attributes;
                layec_write_error(p->context, invalid_node->location, "Expected 'import', 'struct', 'enum', or a function declaration.");
                laye_parser_try_synchronize_to_end_of_node(p);
                return invalid_node;
            }

            laye_token name_token = {0};
            if (!laye_parser_consume(p, LAYE_TOKEN_IDENT, &name_token)) {
                laye_parse_result_destroy(declared_type_result);
                arr_free(attributes);

                if (can_be_expression) {
                    laye_parser_reset_to_mark(p, start_mark);
                    return laye_parse_statement(p);
                }

                laye_node* invalid_node = laye_parser_create_invalid_node_from_token(p);
                invalid_node->attribute_nodes = attributes;
                layec_write_error(p->context, invalid_node->location, "Expected an identifier.");
                return invalid_node;
            }

            laye_node* declared_type = declared_type_result.node;
            laye_parse_result_destroy(declared_type_result);

            return laye_parse_declaration_continue(p, attributes, declared_type, name_token);
        }
    }

    assert(false && "unreachable");
    return NULL;
}

static laye_node* laye_parse_primary_expression_continue(laye_parser* p, laye_node* primary_expr) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);
    assert(primary_expr != NULL);

    switch (p->token.kind) {
        default: {
            return primary_expr;
        }

        case '(': {
            laye_next_token(p);

            dynarr(laye_node*) arguments = NULL;

            if (!laye_parser_at(p, ')')) {
                do {
                    laye_node* argument_expr = laye_parse_expression(p);
                    assert(argument_expr != NULL);
                    arr_push(arguments, argument_expr);
                } while (laye_parser_consume(p, ',', NULL));
            }

            if (!laye_parser_consume(p, ')', NULL)) {
                layec_write_error(p->context, p->token.location, "Expected ')'.");
            }

            laye_node* call_expr = laye_node_create(p->module, LAYE_NODE_CALL, primary_expr->location, p->context->laye_types.unknown);
            assert(call_expr != NULL);
            call_expr->call.callee = primary_expr;
            call_expr->call.arguments = arguments;

            return laye_parse_primary_expression_continue(p, call_expr);
        }
    }

    assert(false && "unreachable");
    return NULL;
}

static void laye_parse_if_only(laye_parser* p, bool expr_context, laye_node** condition, laye_node** body) {
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

    laye_node* if_condition = laye_parse_expression(p);
    assert(if_condition != NULL);

    if (!laye_parser_consume(p, ')', NULL)) {
        layec_write_error(p->context, p->token.location, "Expected ')' to close `if` condition.");
    }

    laye_node* if_body = NULL;
    // we're doing this check to generate errors earlier, it's not technically necessary
    if (laye_parser_at(p, '{')) {
        if_body = laye_parse_compound_expression(p);
    } else {
        if (expr_context) {
            if_body = laye_parse_expression(p);
        } else {
            layec_write_error(p->context, p->token.location, "Expected '{' to open `if` body. (Compound expressions are currently required, but may not be in future versions.)");
            if_body = laye_parse_statement(p);
        }
    }

    assert(if_body != NULL);

    *condition = if_condition;
    *body = if_body;
}

static laye_node* laye_parse_if(laye_parser* p, bool expr_context) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);

    layec_location if_location = p->token.location;

    laye_node* if_condition = NULL;
    laye_node* if_body = NULL;

    laye_parse_if_only(p, expr_context, &if_condition, &if_body);

    assert(if_condition != NULL);
    assert(if_body != NULL);

    laye_node* result = laye_node_create(p->module, LAYE_NODE_IF, if_location, p->context->laye_types._void);
    assert(result != NULL);

    arr_push(result->_if.conditions, if_condition);
    arr_push(result->_if.passes, if_body);

    while (laye_parser_at(p, LAYE_TOKEN_ELSE)) {
        laye_next_token(p);

        if (laye_parser_at(p, LAYE_TOKEN_IF)) {
            laye_node* elseif_condition = NULL;
            laye_node* elseif_body = NULL;

            laye_parse_if_only(p, expr_context, &elseif_condition, &elseif_body);

            assert(elseif_condition != NULL);
            assert(elseif_body != NULL);

            arr_push(result->_if.conditions, elseif_condition);
            arr_push(result->_if.passes, elseif_body);
        } else {
            laye_node* else_body = NULL;
            // we're doing this check to generate errors earlier, it's not technically necessary
            if (laye_parser_at(p, '{')) {
                else_body = laye_parse_compound_expression(p);
            } else {
                if (expr_context) {
                    else_body = laye_parse_expression(p);
                } else {
                    layec_write_error(p->context, p->token.location, "Expected '{' to open `else` body. (Compound expressions are currently required, but may not be in future versions.)");
                    else_body = laye_parse_statement(p);
                }
            }

            result->_if.fail = else_body;
        }
    }

    return result;
}

static laye_node* laye_parse_primary_expression(laye_parser* p) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);

    switch (p->token.kind) {
        default: {
            laye_node* invalid_expr = laye_node_create(p->module, LAYE_NODE_INVALID, p->token.location, p->context->laye_types.poison);
            assert(invalid_expr != NULL);
            layec_write_error(p->context, p->token.location, "Unexpected token. Expected an expression.");
            laye_next_token(p);
            return invalid_expr;
        }

        case '{': {
            laye_node* expr = laye_parse_compound_expression(p);
            assert(expr != NULL);
            expr->compound.is_expr = true;
            return laye_parse_primary_expression_continue(p, expr);
        } break;

        case '&': {
            laye_token operator_token = p->token;
            laye_next_token(p);

            laye_node* operand = laye_parse_primary_expression(p);
            assert(operand != NULL);
            assert(operand->type != NULL);

            laye_node* reftype = laye_node_create(p->module, LAYE_NODE_TYPE_POINTER, operand->location, p->context->laye_types.type);
            assert(reftype != NULL);
            reftype->type_container.element_type = operand->type;

            laye_node* expr = laye_node_create(p->module, LAYE_NODE_UNARY, operand->location, reftype);
            assert(expr != NULL);
            expr->unary.operand = operand;
            expr->unary.operator = operator_token;

            return expr;
        } break;

        case '*': {
            laye_token operator_token = p->token;
            laye_next_token(p);

            laye_node* operand = laye_parse_primary_expression(p);
            assert(operand != NULL);
            assert(operand->type != NULL);

            laye_node* elemtype = NULL;
            if (operand->type == LAYEC_TYPE_POINTER) {
                elemtype = operand->type->type_container.element_type;
            } else {
                elemtype = p->context->laye_types.unknown;
            }

            assert(elemtype != NULL);

            laye_node* expr = laye_node_create(p->module, LAYE_NODE_UNARY, operand->location, elemtype);
            assert(expr != NULL);
            laye_expr_set_lvalue(expr, true);
            expr->unary.operand = operand;
            expr->unary.operator = operator_token;

            return expr;
        } break;

        case LAYE_TOKEN_IF: {
            laye_node* expr = laye_parse_if(p, true);
            assert(expr != NULL);
            expr->_if.is_expr = true;
            return expr;
        } break;

        case LAYE_TOKEN_IDENT: {
            laye_node* nameref_expr = laye_node_create(p->module, LAYE_NODE_NAMEREF, p->token.location, p->context->laye_types.unknown);
            assert(nameref_expr != NULL);
            arr_push(nameref_expr->nameref.pieces, p->token);
            nameref_expr->nameref.scope = p->scope;
            assert(nameref_expr->nameref.scope != NULL);
            assert(nameref_expr->nameref.scope->module != NULL);
            laye_next_token(p);
            return laye_parse_primary_expression_continue(p, nameref_expr);
        }

        case LAYE_TOKEN_TRUE:
        case LAYE_TOKEN_FALSE: {
            laye_node* litbool_expr = laye_node_create(p->module, LAYE_NODE_LITBOOL, p->token.location, p->context->laye_types.unknown);
            assert(litbool_expr != NULL);
            litbool_expr->litbool.value = p->token.kind == LAYE_TOKEN_TRUE;
            litbool_expr->type = p->context->laye_types._bool;
            laye_next_token(p);
            return laye_parse_primary_expression_continue(p, litbool_expr);
        }

        case LAYE_TOKEN_LITINT: {
            laye_node* litint_expr = laye_node_create(p->module, LAYE_NODE_LITINT, p->token.location, p->context->laye_types.unknown);
            assert(litint_expr != NULL);
            litint_expr->litint.value = p->token.int_value;
            litint_expr->type = p->context->laye_types._int;
            laye_next_token(p);
            return laye_parse_primary_expression_continue(p, litint_expr);
        }

        case LAYE_TOKEN_LITSTRING: {
            laye_node* litstr_expr = laye_node_create(p->module, LAYE_NODE_LITSTRING, p->token.location, p->context->laye_types.unknown);
            assert(litstr_expr != NULL);
            litstr_expr->litstring.value = p->token.string_value;
            litstr_expr->type = p->context->laye_types.i8_buffer;
            laye_next_token(p);
            return laye_parse_primary_expression_continue(p, litstr_expr);
        }
    }

    assert(false && "unreachable");
    return NULL;
}

static void laye_expect_semi(laye_parser* p) {
    assert(p != NULL);
    assert(p->context != NULL);

    if (!laye_parser_consume(p, ';', NULL)) {
        layec_write_error(p->context, p->token.location, "Expected ';'.");
    }
}

static laye_node* laye_maybe_parse_assignment(laye_parser* p, laye_node* lhs) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);
    assert(lhs != NULL);

    // TODO(local): assignment+operator
    laye_token assign_op = {0};
    if (!laye_parser_consume_assignment(p, &assign_op)) {
        return lhs;
    }

    assert(assign_op.kind != LAYE_TOKEN_INVALID);

    laye_node* rhs = laye_parse_expression(p);
    assert(rhs != NULL);

    laye_node* assign = laye_node_create(p->module, LAYE_NODE_ASSIGNMENT, assign_op.location, p->context->laye_types._void);
    assert(assign != NULL);
    assign->assignment.lhs = lhs;
    assign->assignment.reference_reassign = assign_op.kind == LAYE_TOKEN_LESSMINUS;
    assign->assignment.rhs = rhs;

    laye_expect_semi(p);
    return assign;
}

static laye_node* laye_parse_statement(laye_parser* p) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);

    laye_node* stmt = NULL;
    switch (p->token.kind) {
        case '{': {
            stmt = laye_parse_compound_expression(p);
            assert(stmt != NULL);

            laye_node* assignment = laye_maybe_parse_assignment(p, stmt);
            assert(assignment != NULL);

            if (assignment != stmt) {
                stmt->compound.is_expr = true;
                stmt = assignment;
            }
        } break;

        case LAYE_TOKEN_IF: {
            stmt = laye_parse_if(p, false);
            assert(stmt != NULL);
        } break;

        case LAYE_TOKEN_RETURN: {
            stmt = laye_node_create(p->module, LAYE_NODE_RETURN, p->token.location, p->context->laye_types.noreturn);
            assert(stmt != NULL);
            laye_next_token(p);

            if (!laye_parser_at(p, ';')) {
                stmt->_return.value = laye_parse_expression(p);
                assert(stmt->_return.value != NULL);
            }

            laye_expect_semi(p);
        } break;

        case LAYE_TOKEN_YIELD: {
            stmt = laye_node_create(p->module, LAYE_NODE_YIELD, p->token.location, p->context->laye_types._void);
            assert(stmt != NULL);
            laye_next_token(p);

            stmt->yield.value = laye_parse_expression(p);
            assert(stmt->yield.value != NULL);

            laye_expect_semi(p);
        } break;

        case LAYE_TOKEN_XYZZY: {
            stmt = laye_node_create(p->module, LAYE_NODE_XYZZY, p->token.location, p->context->laye_types._void);
            assert(stmt != NULL);
            laye_next_token(p);
            laye_expect_semi(p);
        } break;

        default: {
            // TODO(local): we could parse full expressions, but only primaries make honest sense...
            stmt = laye_parse_primary_expression(p);
            assert(stmt != NULL);

            laye_node* assign_stmt = laye_maybe_parse_assignment(p, stmt);
            if (assign_stmt != stmt) {
                stmt = assign_stmt;
            } else {
                laye_expect_semi(p);
            }
        } break;
    }

    assert(stmt != NULL);
    if (stmt->type == NULL) {
        stmt->type = p->context->laye_types._void;
    }

    return stmt;
}

static laye_node* laye_parse_expression(laye_parser* p) {
    assert(p != NULL);
    assert(p->context != NULL);
    assert(p->module != NULL);
    assert(p->token.kind != LAYE_TOKEN_INVALID);

    laye_node* result_expression = laye_parse_primary_expression(p);

    assert(result_expression != NULL);
    assert(result_expression->type != NULL);
    return result_expression;
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
    {LCA_SV_CONSTANT("discardable"), LAYE_TOKEN_DISCARDABLE},
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
                if (string_view_equals(laye_keywords[i].text, identifier_source_view)) {
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
            token.kind = LAYE_TOKEN_IDENT;
        } break;

        default: {
            laye_char_advance(p);

            token.kind = LAYE_TOKEN_UNKNOWN;
            token.location.length = p->lexer_position - token.location.offset;
            layec_write_error(p->context, token.location, "Invalid character in Laye source file.");

            arr_push(p->module->_all_tokens, token);

            laye_next_token(p);
            return;
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
