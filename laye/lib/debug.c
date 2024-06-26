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

#define COL_COMMENT BRIGHT_BLACK
#define COL_DELIM   WHITE
#define COL_TREE    RED
#define COL_NODE    RED
#define COL_ADDR    BLUE
#define COL_OFFS    MAGENTA
#define COL_NAME    GREEN
#define COL_ERROR   RED
#define COL_CONST   BLUE

typedef struct laye_print_context {
    laye_context* context;
    laye_module* module;
    bool use_color;
    lca_string* indents;
    lca_string* output;
} laye_print_context;

static void laye_node_debug_print(laye_print_context* print_context, laye_node* node);

void laye_symbol_print_to_string(laye_symbol* symbol, lca_string* s, int level) {
    assert(symbol != NULL);

    lca_string_append_format(s, "; ");
    for (int i = 0; i < level; i++) {
        lca_string_append_format(s, "  ");
    }

    lca_string_append_format(s, "SYM ");
    if (symbol->name.count > 0) {
        lca_string_append_format(s, "'%.*s' ", LCA_STR_EXPAND(symbol->name));
    }

    switch (symbol->kind) {
        default:
        case LAYE_SYMBOL_ENTITY: {
            lca_string_append_format(s, "ENTITY\n");
            for (int64_t i = 0, count = lca_da_count(symbol->nodes); i < count; i++) {
                lca_string_append_format(s, "; ");
                for (int i = 0; i < level + 1; i++) {
                    lca_string_append_format(s, "  ");
                }

                lca_string_append_format(s, "NODE %016llX\n", (size_t)(symbol->nodes[i]));
            }
        } break;

        case LAYE_SYMBOL_NAMESPACE: {
            lca_string_append_format(s, "NAMESPACE\n");
            for (int64_t i = 0, count = lca_da_count(symbol->symbols); i < count; i++) {
                laye_symbol_print_to_string(symbol->symbols[i], s, level + 1);
            }
        } break;
    }
}

lca_string laye_module_debug_print(laye_module* module) {
    assert(module != NULL);
    assert(module->context != NULL);

    int64_t indents_string_capacity = 256;
    char* indents_string_data = lca_allocate(module->context->allocator, indents_string_capacity);
    assert(indents_string_data != NULL);
    lca_string indents_string = lca_string_from_data(module->context->allocator, indents_string_data, 0, indents_string_capacity);

    lca_string output_string = lca_string_create(module->context->allocator);

    laye_print_context print_context = {
        .context = module->context,
        .module = module,
        .use_color = module->context->use_color,
        .indents = &indents_string,
        .output = &output_string,
    };

    bool use_color = print_context.use_color;
    lca_string_append_format(print_context.output, "%s; %.*s%s\n", COL(COL_COMMENT), LCA_STR_EXPAND(lyir_context_get_source(module->context->lyir_context, module->sourceid).name), COL(RESET));
    lca_string_append_format(print_context.output, "%s; %016llX%s\n", COL(COL_COMMENT), (size_t)module, COL(RESET));
    if (module->imports != NULL) {
        //string_append_format(print_context.output, "%s; Imports:\n", COL(COL_COMMENT));
        //laye_symbol_print_to_string(module->imports, print_context.output, 1);
    }
    if (module->exports != NULL) {
        //lca_string_append_format(print_context.output, "%s; Exports:\n", COL(COL_COMMENT));
        //laye_symbol_print_to_string(module->exports, print_context.output, 1);
    }
    lca_string_append_format(print_context.output, "%s", COL(RESET));

    for (int64_t i = 0, count = lca_da_count(module->top_level_nodes); i < count; i++) {
        laye_node* top_level_node = module->top_level_nodes[i];
        assert(top_level_node != NULL);

        laye_node_debug_print(&print_context, top_level_node);
    }

    lca_string_destroy(&indents_string);

    return output_string;
}

static void laye_node_debug_print_children(laye_print_context* print_context, lca_da(laye_node*) children) {
    bool use_color = print_context->use_color;

    for (int64_t i = 0, count = lca_da_count(children); i < count; i++) {
        bool is_last = i == count - 1;
        laye_node* child = children[i];
        lca_string indents = *print_context->indents;

        const char* next_leader = is_last ? "└─" : "├─";
        lca_string_append_format(print_context->output, "%s%.*s%s", COL(COL_TREE), LCA_STR_EXPAND(indents), next_leader);

        int64_t old_indents_count = print_context->indents->count;
        lca_string_append_format(print_context->indents, "%s", is_last ? "  " : "│ ");

        laye_node_debug_print(print_context, child);

        print_context->indents->count = old_indents_count;
    }
}

void laye_constant_print_to_string(lyir_evaluated_constant constant, lca_string* s, bool use_color);
void laye_nameref_print_to_string(laye_nameref nameref, lca_string* s, bool use_color);
void laye_template_parameters_print_to_string(lca_da(laye_node*) template_params, lca_string* s, bool use_color);

static void laye_node_debug_print(laye_print_context* print_context, laye_node* node) {
    assert(print_context != NULL);
    assert(print_context->context != NULL);
    assert(print_context->module != NULL);
    assert(print_context->indents != NULL);
    assert(print_context->output != NULL);
    assert(node != NULL);

    bool use_color = print_context->use_color;

    lca_string_append_format(
        print_context->output,
        "%s%s%s%s %s%016llX %s<%lld>",
        COL(COL_NODE),
        laye_node_kind_to_cstring(node->kind),
        (node->compiler_generated ? "*" : ""),
        (laye_expr_is_lvalue(node) ? "&" : ""),
        COL(COL_ADDR),
        (size_t)node,
        COL(COL_OFFS),
        node->location.offset
    );

    if (laye_node_is_decl(node)) {
        lca_string_append_format(print_context->output, "%s", COL(COL_NODE));
        switch (node->attributes.linkage) {
            case LYIR_LINK_LOCAL: lca_string_append_format(print_context->output, " LOCAL"); break;
            case LYIR_LINK_INTERNAL: lca_string_append_format(print_context->output, " INTERNAL"); break;
            case LYIR_LINK_IMPORTED: lca_string_append_format(print_context->output, " IMPORTED"); break;
            case LYIR_LINK_EXPORTED: lca_string_append_format(print_context->output, " EXPORTED"); break;
            case LYIR_LINK_REEXPORTED: lca_string_append_format(print_context->output, " REEXPORTED"); break;
        }

        switch (node->attributes.calling_convention) {
            case LYIR_DEFAULTCC: break;
            case LYIR_CCC: lca_string_append_format(print_context->output, " CCC"); break;
            case LYIR_LAYECC: lca_string_append_format(print_context->output, " LAYECC"); break;
        }

        switch (node->attributes.mangling) {
            case LYIR_MANGLE_DEFAULT: break;
            case LYIR_MANGLE_NONE: lca_string_append_format(print_context->output, " NO_MANGLE"); break;
            case LYIR_MANGLE_LAYE: lca_string_append_format(print_context->output, " LAYE_MANGLE"); break;
        }

        if (node->attributes.is_discardable) {
            lca_string_append_format(print_context->output, " DISCARDABLE");
        }

        if (node->attributes.is_inline) {
            lca_string_append_format(print_context->output, " INLINE");
        }

        if (node->attributes.foreign_name.count != 0) {
            lca_string_append_format(print_context->output, " FOREIGN \"%.*s\"", LCA_STR_EXPAND(node->attributes.foreign_name));
        }
    }

    if (laye_node_is_dependent(node)) {
        lca_string_append_format(print_context->output, " DEPENDENT");
    }

    if (node->declared_type.node != NULL) {
        lca_string_append_format(print_context->output, " ");
        laye_type_print_to_string(node->declared_type, print_context->output, use_color);
    } else if (node->type.node != NULL) {
        lca_string_append_format(print_context->output, " ");
        laye_type_print_to_string(node->type, print_context->output, use_color);
    }

    lca_da(laye_node*) children = NULL;

    switch (node->kind) {
        default: break;

        case LAYE_NODE_DECL_IMPORT: {
            lyir_source source = lyir_context_get_source(print_context->context->lyir_context, node->location.sourceid);
            lca_string_append_format(
                print_context->output,
                " %s%.*s",
                COL(COL_NAME),
                (int)node->decl_import.module_name.location.length,
                source.text.data + node->decl_import.module_name.location.offset
            );

            if (node->decl_import.import_alias.kind != 0) {
                lca_string_append_format(print_context->output, " %sas %s%.*s", COL(COL_TREE), COL(COL_NAME), LCA_STR_EXPAND(node->decl_import.import_alias.string_value));
            }

            if (node->decl_import.referenced_module != NULL) {
                lca_string_append_format(print_context->output, " %s%016llX", COL(COL_ADDR), (size_t)node->decl_import.referenced_module);
            }

            for (int64_t i = 0, count = lca_da_count(node->decl_import.import_queries); i < count; i++) {
                lca_da_push(children, node->decl_import.import_queries[i]);
            }
        } break;

        case LAYE_NODE_IMPORT_QUERY: {
            lca_string_append_format(print_context->output, " ");

            if (node->import_query.is_wildcard) {
                lca_string_append_format(print_context->output, "%s*", COL(COL_NAME));
            } else {
                for (int64_t i = 0, count = lca_da_count(node->import_query.pieces); i < count; i++) {
                    laye_token piece = node->import_query.pieces[i];
                    if (i > 0) {
                        lca_string_append_format(print_context->output, "%s::", COL(RESET));
                    }

                    lca_string_append_format(print_context->output, "%s%.*s", COL(COL_NAME), LCA_STR_EXPAND(piece.string_value));
                }

                if (node->import_query.alias.kind != 0) {
                    lca_string_append_format(print_context->output, " %sas %s%.*s", COL(COL_TREE), COL(COL_NAME), LCA_STR_EXPAND(node->import_query.alias.string_value));
                }
            }
        } break;

        case LAYE_NODE_DECL_FUNCTION: {
            lca_string_append_format(print_context->output, " %s%.*s", COL(COL_NAME), LCA_STR_EXPAND(node->declared_name));
            laye_template_parameters_print_to_string(node->template_parameters, print_context->output, use_color);

            if (node->decl_function.body != NULL)
                lca_da_push(children, node->decl_function.body);
        } break;

        case LAYE_NODE_DECL_BINDING: {
            lca_string_append_format(print_context->output, " %s%.*s", COL(COL_NAME), LCA_STR_EXPAND(node->declared_name));
            laye_template_parameters_print_to_string(node->template_parameters, print_context->output, use_color);

            if (node->decl_binding.initializer != NULL)
                lca_da_push(children, node->decl_binding.initializer);
        } break;

        case LAYE_NODE_DECL_STRUCT: {
            lca_string_append_format(print_context->output, " %s%.*s", COL(COL_NAME), LCA_STR_EXPAND(node->declared_name));
            laye_template_parameters_print_to_string(node->template_parameters, print_context->output, use_color);

            for (int64_t i = 0, count = lca_da_count(node->decl_struct.field_declarations); i < count; i++)
                lca_da_push(children, node->decl_struct.field_declarations[i]);

            for (int64_t i = 0, count = lca_da_count(node->decl_struct.variant_declarations); i < count; i++)
                lca_da_push(children, node->decl_struct.variant_declarations[i]);
        } break;

        case LAYE_NODE_DECL_STRUCT_FIELD: {
            lca_string_append_format(print_context->output, " %s%.*s", COL(COL_NAME), LCA_STR_EXPAND(node->declared_name));
            laye_template_parameters_print_to_string(node->template_parameters, print_context->output, use_color);

            if (node->decl_binding.initializer != NULL)
                lca_da_push(children, node->decl_struct_field.initializer);
        } break;

        case LAYE_NODE_DECL_TEST: {
            lyir_source source = lyir_context_get_source(print_context->context->lyir_context, node->decl_test.description.location.sourceid);
            if (node->decl_test.is_named) {
                lca_string_append_format(print_context->output, " ");
                laye_nameref_print_to_string(node->decl_test.nameref, print_context->output, use_color);
            } else if (node->decl_test.description.kind != LAYE_TOKEN_INVALID) {
                lca_string_append_format(print_context->output, " %s%.*s", COL(COL_CONST), (int)node->decl_test.description.location.length, source.text.data + node->decl_test.description.location.offset);
            }

            assert(node->decl_test.body != NULL);
            lca_da_push(children, node->decl_test.body);
        } break;

        case LAYE_NODE_IF: {
            assert(lca_da_count(node->_if.conditions) == lca_da_count(node->_if.passes));
            for (int64_t i = 0, count = lca_da_count(node->_if.conditions); i < count; i++) {
                lca_da_push(children, node->_if.conditions[i]);
                lca_da_push(children, node->_if.passes[i]);
            }

            if (node->_if.fail != NULL) {
                lca_da_push(children, node->_if.fail);
            }
        } break;

        case LAYE_NODE_FOR: {
            if (node->_for.has_breaks) {
                lca_string_append_format(print_context->output, " %sHAS_BREAKS", COL(COL_TREE));
            }

            if (node->_for.has_continues) {
                lca_string_append_format(print_context->output, " %sHAS_CONTINUES", COL(COL_TREE));
            }

            if (node->_for.initializer != NULL) {
                lca_da_push(children, node->_for.initializer);
            }

            if (node->_for.condition != NULL) {
                lca_da_push(children, node->_for.condition);
            }

            if (node->_for.increment != NULL) {
                lca_da_push(children, node->_for.increment);
            }

            if (node->_for.pass != NULL) {
                lca_da_push(children, node->_for.pass);
            }

            if (node->_for.fail != NULL) {
                lca_da_push(children, node->_for.fail);
            }
        } break;

        case LAYE_NODE_FOREACH: {
            if (node->foreach.has_breaks) {
                lca_string_append_format(print_context->output, " %sHAS_BREAKS", COL(COL_TREE));
            }

            if (node->foreach.has_continues) {
                lca_string_append_format(print_context->output, " %sHAS_CONTINUES", COL(COL_TREE));
            }

            if (node->foreach.index_binding != NULL) {
                lca_da_push(children, node->foreach.index_binding);
            }

            lca_da_push(children, node->foreach.element_binding);

            if (node->foreach.iterable != NULL) {
                lca_da_push(children, node->foreach.iterable);
            }

            if (node->foreach.pass != NULL) {
                lca_da_push(children, node->foreach.pass);
            }
        } break;

        case LAYE_NODE_WHILE: {
            if (node->_while.has_breaks) {
                lca_string_append_format(print_context->output, " %sHAS_BREAKS", COL(COL_TREE));
            }

            if (node->_while.has_continues) {
                lca_string_append_format(print_context->output, " %sHAS_CONTINUES", COL(COL_TREE));
            }

            if (node->_while.condition != NULL) {
                lca_da_push(children, node->_while.condition);
            }

            if (node->_while.pass != NULL) {
                lca_da_push(children, node->_while.pass);
            }

            if (node->_while.fail != NULL) {
                lca_da_push(children, node->_while.fail);
            }
        } break;

        case LAYE_NODE_DOWHILE: {
            if (node->dowhile.has_breaks) {
                lca_string_append_format(print_context->output, " %sHAS_BREAKS", COL(COL_TREE));
            }

            if (node->dowhile.has_continues) {
                lca_string_append_format(print_context->output, " %sHAS_CONTINUES", COL(COL_TREE));
            }

            assert(node->dowhile.condition != NULL);
            lca_da_push(children, node->dowhile.condition);

            if (node->dowhile.pass != NULL) {
                lca_da_push(children, node->dowhile.pass);
            }
        } break;

        case LAYE_NODE_LABEL: {
            lca_string_append_format(print_context->output, " %s%.*s", COL(COL_NAME), LCA_STR_EXPAND(node->declared_name));
        } break;

        case LAYE_NODE_DEFER: {
            lca_da_push(children, node->defer.body);
        } break;

        case LAYE_NODE_RETURN: {
            if (node->_return.value != NULL) {
                lca_da_push(children, node->_return.value);
            }
        } break;

        case LAYE_NODE_BREAK: {
            if (node->_break.target.count != 0) {
                lca_string_append_format(print_context->output, " %s%.*s", COL(COL_NAME), LCA_STR_EXPAND(node->_break.target));
            }

            if (node->_break.target_node != NULL) {
                lca_string_append_format(
                    print_context->output,
                    " %s%s %s%016llX",
                    COL(COL_NODE),
                    laye_node_kind_to_cstring(node->_break.target_node->kind),
                    COL(COL_ADDR),
                    (size_t)node->_break.target_node
                );
            }
        } break;

        case LAYE_NODE_CONTINUE: {
            if (node->_continue.target.count != 0) {
                lca_string_append_format(print_context->output, " %s%.*s", COL(COL_NAME), LCA_STR_EXPAND(node->_continue.target));
            }

            if (node->_break.target_node != NULL) {
                lca_string_append_format(
                    print_context->output,
                    " %s%s %s%016llX",
                    COL(COL_NODE),
                    laye_node_kind_to_cstring(node->_continue.target_node->kind),
                    COL(COL_ADDR),
                    (size_t)node->_continue.target_node
                );
            }
        } break;

        case LAYE_NODE_GOTO: {
            lca_string_append_format(print_context->output, " %s%.*s", COL(COL_NAME), LCA_STR_EXPAND(node->_goto.label));
        } break;

        case LAYE_NODE_YIELD: {
            assert(node->yield.value != NULL);
            lca_da_push(children, node->yield.value);
        } break;

        case LAYE_NODE_ASSERT: {
            if (node->_assert.message.kind != LAYE_TOKEN_INVALID) {
                lyir_source source = lyir_context_get_source(print_context->context->lyir_context, node->_assert.message.location.sourceid);
                lca_string_append_format(print_context->output, " %s%.*s", COL(COL_CONST), (int)node->_assert.message.location.length, source.text.data + node->_assert.message.location.offset);
            }

            assert(node->_assert.condition != NULL);
            lca_da_push(children, node->_assert.condition);
        } break;

        case LAYE_NODE_COMPOUND: {
            for (int64_t i = 0, count = lca_da_count(node->compound.children); i < count; i++) {
                lca_da_push(children, node->compound.children[i]);
            }
        } break;

        case LAYE_NODE_EVALUATED_CONSTANT: {
            assert(node->evaluated_constant.expr != NULL);
            lca_da_push(children, node->evaluated_constant.expr);

            if (node->evaluated_constant.result.kind == LYIR_EVAL_INT) {
                lca_string_append_format(print_context->output, " %s%lld", COL(COL_CONST), node->evaluated_constant.result.int_value);
            }
        } break;

        case LAYE_NODE_CAST: {
            assert(node->cast.operand != NULL);
            lca_da_push(children, node->cast.operand);

            lca_string_append_format(print_context->output, " %s", COL(COL_NODE));
            switch (node->cast.kind) {
                case LAYE_CAST_SOFT: lca_string_append_format(print_context->output, "SOFT"); break;
                case LAYE_CAST_HARD: lca_string_append_format(print_context->output, "HARD"); break;
                case LAYE_CAST_STRUCT_BITCAST: lca_string_append_format(print_context->output, "STRUCT_BITCAST"); break;
                case LAYE_CAST_IMPLICIT: lca_string_append_format(print_context->output, "IMPLICIT"); break;
                case LAYE_CAST_LVALUE_TO_RVALUE: lca_string_append_format(print_context->output, "LVALUE_TO_RVALUE"); break;
                case LAYE_CAST_LVALUE_TO_REFERENCE: lca_string_append_format(print_context->output, "LVALUE_TO_REFERENCE"); break;
                case LAYE_CAST_REFERENCE_TO_LVALUE: lca_string_append_format(print_context->output, "REFERENCE_TO_LVALUE"); break;
            }
        } break;

        case LAYE_NODE_CALL: {
            assert(node->call.callee != NULL);
            lca_da_push(children, node->call.callee);

            for (int64_t i = 0, count = lca_da_count(node->call.arguments); i < count; i++) {
                lca_da_push(children, node->call.arguments[i]);
            }
        } break;

        case LAYE_NODE_INDEX: {
            assert(node->index.value != NULL);
            lca_da_push(children, node->index.value);

            for (int64_t i = 0, count = lca_da_count(node->index.indices); i < count; i++) {
                lca_da_push(children, node->index.indices[i]);
            }
        } break;

        case LAYE_NODE_MEMBER: {
            lca_string_append_format(print_context->output, " %s%.*s", COL(COL_NAME), LCA_STR_EXPAND(node->member.field_name.string_value));

            assert(node->member.value != NULL);
            lca_da_push(children, node->member.value);
        } break;

        case LAYE_NODE_UNARY: {
            lca_da_push(children, node->unary.operand);

            lyir_source source = lyir_context_get_source(print_context->context->lyir_context, node->location.sourceid);
            lca_string_append_format(print_context->output, " %s%.*s", COL(COL_NODE), (int)node->unary.operator.location.length, source.text.data + node->unary.operator.location.offset);
        } break;

        case LAYE_NODE_BINARY: {
            lca_da_push(children, node->binary.lhs);
            lca_da_push(children, node->binary.rhs);

            lyir_source source = lyir_context_get_source(print_context->context->lyir_context, node->location.sourceid);
            lca_string_append_format(print_context->output, " %s%.*s", COL(COL_NODE), (int)node->binary.operator.location.length, source.text.data + node->binary.operator.location.offset);
        } break;

        case LAYE_NODE_ASSIGNMENT: {
            lca_da_push(children, node->assignment.lhs);
            lca_da_push(children, node->assignment.rhs);

            lyir_source source = lyir_context_get_source(print_context->context->lyir_context, node->location.sourceid);
            lca_string_append_format(print_context->output, " %s%.*s", COL(COL_NODE), (int)node->location.length, source.text.data + node->location.offset);
        } break;

        case LAYE_NODE_NAMEREF: {
            lca_string_append_format(print_context->output, " ");
            laye_nameref_print_to_string(node->nameref, print_context->output, use_color);
        } break;

        case LAYE_NODE_LITINT: {
            lca_string_append_format(print_context->output, " %s%lld", COL(COL_CONST), node->litint.value);
        } break;

        case LAYE_NODE_LITBOOL: {
            lca_string_append_format(print_context->output, " %s%s", COL(COL_CONST), node->litbool.value ? "true" : "false");
        } break;

        case LAYE_NODE_LITSTRING: {
            lyir_source source = lyir_context_get_source(print_context->context->lyir_context, node->location.sourceid);
            lca_string_append_format(print_context->output, " %s%.*s", COL(COL_CONST), (int)node->location.length, source.text.data + node->location.offset);
        } break;
    }

    lca_string_append_format(print_context->output, "%s\n", COL(RESET));

    if (children != NULL) {
        laye_node_debug_print_children(print_context, children);
        lca_da_free(children);
    }
}

#define COL_KEYWORD        MAGENTA
#define COL_TEMPLATE_PARAM YELLOW
#define COL_UNREAL         RED

void laye_template_parameters_print_to_string(lca_da(laye_node*) template_params, lca_string* s, bool use_color) {
    assert(s != NULL);

    if (lca_da_count(template_params) == 0) {
        return;
    }

    lca_string_append_format(s, "%s<", COL(RESET));
    for (int64_t i = 0; i < lca_da_count(template_params); i++) {
        if (i > 0) {
            lca_string_append_format(s, "%s, ", COL(RESET));
        }

        laye_node* template_param = template_params[i];
        assert(template_param != NULL);

        if (template_param->kind == LAYE_NODE_DECL_TEMPLATE_TYPE) {
            if (template_param->decl_template_type.is_duckable) {
                lca_string_append_format(s, "%svar ", COL(COL_KEYWORD));
            }

            lca_string_append_format(s, "%s%.*s", COL(COL_TEMPLATE_PARAM), LCA_STR_EXPAND(template_param->declared_name));
        } else if (template_param->kind == LAYE_NODE_DECL_TEMPLATE_VALUE) {
            laye_type_print_to_string(template_param->declared_type, s, use_color);
            lca_string_append_format(s, " ");
            lca_string_append_format(s, "%s%.*s", COL(COL_TEMPLATE_PARAM), LCA_STR_EXPAND(template_param->declared_name));
        } else {
            fprintf(stderr, "for node kind %s\n", laye_node_kind_to_cstring(template_param->kind));
            assert(false && "invalid template parameter declaration kind");
        }
    }

    lca_string_append_format(s, "%s>", COL(RESET));
}

void laye_nameref_print_to_string(laye_nameref nameref, lca_string* s, bool use_color) {
    if (nameref.kind == LAYE_NAMEREF_HEADLESS) {
        lca_string_append_format(s, "%s::", COL(COL_DELIM));
    } else if (nameref.kind == LAYE_NAMEREF_GLOBAL) {
        lca_string_append_format(s, "%sglobal%s::", COL(COL_TREE), COL(COL_DELIM));
    }

    for (int64_t i = 0, count = lca_da_count(nameref.pieces); i < count; i++) {
        if (i > 0) {
            lca_string_append_format(s, "%s::", COL(COL_DELIM));
        }

        lca_string_append_format(s, "%s%.*s", COL(COL_NAME), LCA_STR_EXPAND(nameref.pieces[i].string_value));
    }

    if (0 != lca_da_count(nameref.template_arguments)) {
        lca_string_append_format(s, "%s<", COL(COL_DELIM));
        for (int64_t i = 0, count = lca_da_count(nameref.template_arguments); i < count; i++) {
            if (i > 0) {
                lca_string_append_format(s, "%s, ", COL(COL_DELIM));
            }

            laye_template_arg template_argument = nameref.template_arguments[i];
        retry_print_arg:;
            if (template_argument.is_type) {
                laye_type_print_to_string(template_argument.type, s, use_color);
            } else if (template_argument.node->kind == LAYE_NODE_EVALUATED_CONSTANT) {
                laye_constant_print_to_string(template_argument.node->evaluated_constant.result, s, use_color);
            } else {
                lyir_evaluated_constant check_eval_const;
                if (laye_expr_evaluate(template_argument.node, &check_eval_const, false)) {
                    laye_node* eval_const = laye_node_create(template_argument.node->module, LAYE_NODE_EVALUATED_CONSTANT, template_argument.node->location, template_argument.node->type);
                    eval_const->evaluated_constant.expr = template_argument.node;
                    eval_const->evaluated_constant.result = check_eval_const;
                    template_argument.node = eval_const;
                    goto retry_print_arg;
                }

                lca_string_append_format(s, "%s{? %s}", COL(COL_ERROR), laye_node_kind_to_cstring(template_argument.node->kind));
            }
        }

        lca_string_append_format(s, "%s>", COL(COL_DELIM));
    }

    if (nameref.referenced_declaration != NULL) {
        lca_string_append_format(s, " %s%016llX", COL(COL_ADDR), (size_t)nameref.referenced_declaration);
    }
}

void laye_constant_print_to_string(lyir_evaluated_constant constant, lca_string* s, bool use_color) {
    assert(s != NULL);

    switch (constant.kind) {
        default: assert(false && "unreachable"); break;

        case LYIR_EVAL_NULL: {
            lca_string_append_format(s, "%snil", COL(COL_CONST));
        } break;

        case LYIR_EVAL_VOID: {
            lca_string_append_format(s, "%svoid", COL(COL_CONST));
        } break;

        case LYIR_EVAL_BOOL: {
            if (constant.bool_value) {
                lca_string_append_format(s, "%strue", COL(COL_CONST));
            } else {
                lca_string_append_format(s, "%sfalse", COL(COL_CONST));
            }
        } break;

        case LYIR_EVAL_INT: {
            lca_string_append_format(s, "%s%lld", COL(COL_CONST), constant.int_value);
        } break;

        case LYIR_EVAL_FLOAT: {
            lca_string_append_format(s, "%s%f", COL(COL_CONST), constant.float_value);
        } break;

        case LYIR_EVAL_STRING: {
            lca_string_append_format(s, "%s\"%.*s\"", COL(COL_CONST), LCA_STR_EXPAND(constant.string_value));
        } break;
    }
}

void laye_type_print_to_string(laye_type type, lca_string* s, bool use_color) {
    assert(type.node != NULL);
    assert(laye_node_is_type(type.node));
    assert(s != NULL);

    switch (type.node->kind) {
        default: {
            fprintf(stderr, "unimplemented type kind %s\n", laye_node_kind_to_cstring(type.node->kind));
            assert(false && "unreachable");
        } break;

        case LAYE_NODE_TYPE_POISON: {
            lca_string_append_format(s, "%spoison", COL(COL_UNREAL));
        } break;

        case LAYE_NODE_TYPE_UNKNOWN: {
            lca_string_append_format(s, "%sunknown", COL(COL_UNREAL));
        } break;

        case LAYE_NODE_TYPE_VAR: {
            lca_string_append_format(s, "%svar", COL(COL_UNREAL));
        } break;

        case LAYE_NODE_TYPE_TYPE: {
            lca_string_append_format(s, "%stype", COL(COL_KEYWORD));
        } break;

        case LAYE_NODE_TYPE_VOID: {
            lca_string_append_format(s, "%svoid", COL(COL_KEYWORD));
        } break;

        case LAYE_NODE_TYPE_NORETURN: {
            lca_string_append_format(s, "%snoreturn", COL(COL_KEYWORD));
        } break;

        case LAYE_NODE_TYPE_BOOL: {
            lca_string_append_format(s, "%sbool", COL(COL_KEYWORD));
        } break;

        case LAYE_NODE_TYPE_INT: {
            if (type.node->type_primitive.is_platform_specified) {
                lca_string_append_format(s, "%s%sint", COL(COL_KEYWORD), (type.node->type_primitive.is_signed ? "" : "u"));
            } else {
                lca_string_append_format(s, "%s%s%d", COL(COL_KEYWORD), (type.node->type_primitive.is_signed ? "i" : "u"), type.node->type_primitive.bit_width);
            }
        } break;

        case LAYE_NODE_TYPE_FLOAT: {
            if (type.node->type_primitive.is_platform_specified) {
                lca_string_append_format(s, "%sfloat", COL(COL_KEYWORD));
            } else {
                lca_string_append_format(s, "%sf%d", COL(COL_KEYWORD), type.node->type_primitive.bit_width);
            }
        } break;

        case LAYE_NODE_TYPE_TEMPLATE_PARAMETER: {
            assert(type.node->type_template_parameter.declaration != NULL);
            lca_string_append_format(s, "%s%.*s", COL(COL_TEMPLATE_PARAM), LCA_STR_EXPAND(type.node->type_template_parameter.declaration->declared_name));
        } break;

        case LAYE_NODE_TYPE_ERROR_PAIR: {
            assert(type.node->type_error_pair.error_type.node != NULL);
            assert(type.node->type_error_pair.value_type.node != NULL);
            laye_type_print_to_string(type.node->type_error_pair.error_type, s, use_color);
            lca_string_append_format(s, "%s!", COL(COL_DELIM));
            laye_type_print_to_string(type.node->type_error_pair.value_type, s, use_color);
        } break;

        case LAYE_NODE_TYPE_NAMEREF: {
            laye_nameref_print_to_string(type.node->nameref, s, use_color);
            break;

            if (type.node->nameref.kind == LAYE_NAMEREF_HEADLESS) {
                lca_string_append_format(s, "%s::", COL(COL_DELIM));
            } else if (type.node->nameref.kind == LAYE_NAMEREF_GLOBAL) {
                lca_string_append_format(s, "%sglobal%s::", COL(COL_KEYWORD), COL(COL_DELIM));
            }

            for (int64_t i = 0, count = lca_da_count(type.node->nameref.pieces); i < count; i++) {
                if (i > 0) {
                    lca_string_append_format(s, "%s::", COL(COL_DELIM));
                }

                lca_string_append_format(s, "%s%.*s", COL(COL_NAME), LCA_STR_EXPAND(type.node->nameref.pieces[i].string_value));
            }

            if (0 != lca_da_count(type.node->nameref.template_arguments)) {
                lca_string_append_format(s, "%s<", COL(COL_DELIM));
                for (int64_t i = 0, count = lca_da_count(type.node->nameref.template_arguments); i < count; i++) {
                    if (i > 0) {
                        lca_string_append_format(s, "%s, ", COL(COL_DELIM));
                    }

                    laye_template_arg template_argument = type.node->nameref.template_arguments[i];
                    if (template_argument.is_type) {
                        laye_type_print_to_string(template_argument.type, s, use_color);
                    } else if (template_argument.node->kind == LAYE_NODE_EVALUATED_CONSTANT) {
                        laye_constant_print_to_string(template_argument.node->evaluated_constant.result, s, use_color);
                    } else {
                        lca_string_append_format(s, "%s{?}", COL(COL_ERROR));
                    }
                }

                lca_string_append_format(s, "%s>", COL(COL_DELIM));
            }

            if (type.node->nameref.referenced_type != NULL) {
                lca_string_append_format(s, " %s%016llX", COL(COL_ADDR), (size_t)type.node->nameref.referenced_type);
            }
        } break;

        case LAYE_NODE_TYPE_FUNCTION: {
            laye_type_print_to_string(type.node->type_function.return_type, s, use_color);

            lca_string_append_format(s, "%s(", COL(COL_DELIM));
            for (int64_t i = 0, count = lca_da_count(type.node->type_function.parameter_types); i < count; i++) {
                if (i > 0) {
                    lca_string_append_format(s, "%s, ", COL(COL_DELIM));
                }

                laye_type_print_to_string(type.node->type_function.parameter_types[i], s, use_color);
            }

            lca_string_append_format(s, "%s)", COL(COL_DELIM));
        } break;

        case LAYE_NODE_TYPE_REFERENCE: {
            laye_type_print_to_string(type.node->type_container.element_type, s, use_color);
            lca_string_append_format(s, "%s&", COL(COL_DELIM));
        } break;

        case LAYE_NODE_TYPE_POINTER: {
            laye_type_print_to_string(type.node->type_container.element_type, s, use_color);
            lca_string_append_format(s, "%s*", COL(COL_DELIM));
        } break;

        case LAYE_NODE_TYPE_BUFFER: {
            laye_type_print_to_string(type.node->type_container.element_type, s, use_color);
            lca_string_append_format(s, "%s[*]", COL(COL_DELIM));
        } break;

        case LAYE_NODE_TYPE_SLICE: {
            laye_type_print_to_string(type.node->type_container.element_type, s, use_color);
            lca_string_append_format(s, "%s[]", COL(COL_DELIM));
        } break;

        case LAYE_NODE_TYPE_ARRAY: {
            laye_type_print_to_string(type.node->type_container.element_type, s, use_color);
            lca_string_append_format(s, "%s[", COL(COL_DELIM));
            for (int i = 0, count = lca_da_count(type.node->type_container.length_values); i < count; i++) {
                laye_node* length_value = type.node->type_container.length_values[i];
                assert(length_value != NULL);

                if (length_value->kind == LAYE_NODE_EVALUATED_CONSTANT) {
                    lyir_evaluated_constant constant = length_value->evaluated_constant.result;
                    switch (constant.kind) {
                        default: {
                            // fprintf(stderr, "unimplemented evaluated constant kind %s\n", laye_node_kind_to_cstring(type.node->kind));
                            assert(false && "unreachable");
                        } break;

                        case LYIR_EVAL_INT: {
                            lca_string_append_format(s, "%s%lld", COL(COL_CONST), constant.int_value);
                        } break;

                        case LYIR_EVAL_FLOAT: {
                            lca_string_append_format(s, "%s%f", COL(COL_CONST), constant.float_value);
                        } break;
                    }
                } else if (length_value->kind == LAYE_NODE_LITINT) {
                    lca_string_append_format(s, "%s%lld", COL(COL_CONST), length_value->litint.value);
                } else {
                    lca_string_append_format(s, "%s<expr>", COL(COL_CONST), length_value->litint.value);
                }
            }
            lca_string_append_format(s, "%s]", COL(COL_DELIM));
        } break;

        case LAYE_NODE_TYPE_STRUCT: {
            lca_string_append_format(s, "%s%.*s", COL(COL_NAME), LCA_STR_EXPAND(type.node->type_struct.name));
        } break;
    }

    if (type.is_modifiable) {
        lca_string_append_format(s, " %smut", COL(COL_KEYWORD));
    }

    lca_string_append_format(s, "%s", COL(RESET));
}
