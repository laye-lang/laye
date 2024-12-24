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

bool laye_node_is_dependent(laye_node* node) {
    return node->dependence != LAYE_DEPENDENCE_NONE;
}

static void copy_dependence(laye_node* to_node, laye_node* node) {
    if (node == NULL)
        return;

    to_node->dependence |= node->dependence;
}

static void copy_dependence_all(laye_node* to_node, lca_da(laye_node*) nodes) {
    for (int64_t i = 0; i < lca_da_count(nodes); i++) {
        to_node->dependence |= nodes[i]->dependence;
    }
}

static void copy_type_dependence(laye_node* to_node, lca_da(laye_node*) nodes) {
    for (int64_t i = 0; i < lca_da_count(nodes); i++) {
        if (laye_node_is_dependent(nodes[i])) {
            // types should not be value-dependent
            to_node->dependence |= nodes[i]->dependence & LAYE_DEPENDENCE_ERROR_DEPENDENT;
            to_node->dependence |= LAYE_DEPENDENCE_TYPE_DEPENDENT;
        }
    }
}

static void copy_inst_dependence_all(laye_node* to_node, lca_da(laye_node*) nodes) {
    for (int64_t i = 0; i < lca_da_count(nodes); i++) {
        if (laye_node_is_dependent(nodes[i])) {
            to_node->dependence |= LAYE_DEPENDENCE_INSTANTIATION;
            return;
        }
    }
}

static void copy_inst_dependence(laye_node* to_node, laye_node* node) {
    if (node == NULL)
        return;

    if (laye_node_is_dependent(node))
        to_node->dependence |= LAYE_DEPENDENCE_INSTANTIATION;
}

// preserve value-dependence and turn type dependence into value-dependence
static laye_dependence turn_type_to_value_dependence(laye_node* node) {
    if (node == NULL)
        return LAYE_DEPENDENCE_NONE;

    laye_dependence d = node->dependence;
    if (d & LAYE_DEPENDENCE_TYPE) {
        d &= ~LAYE_DEPENDENCE_TYPE;
        d |= LAYE_DEPENDENCE_VALUE;
    }
    return d;
}

// Compute the dependence of a node. A few notes:
//
// - A node is type-dependent if it isn't itself a type and its
//   type is dependent (in any way).
//
// - Types are never value-dependent because they are not values.
//
// - We must always propagate at least instantiation dependence,
//   even if containing a template parameter doesn't make a node
//   type-dependent or value-dependent.
void laye_compute_dependence(laye_node* node) {
    assert(node);

    // a node is type-dependent if its type is dependent.
    if (!laye_node_is_type(node) && node->type.node != NULL) {
        if (laye_node_is_dependent(node->type.node))
            node->dependence |= LAYE_DEPENDENCE_TYPE_DEPENDENT;
    }

    switch (node->kind) {
        // never dependent.
        case LAYE_NODE_DECL_ALIAS:
        case LAYE_NODE_LABEL:
        case LAYE_NODE_EMPTY:
        case LAYE_NODE_BREAK:
        case LAYE_NODE_CONTINUE:
        case LAYE_NODE_FALLTHROUGH:
        case LAYE_NODE_UNREACHABLE:
        case LAYE_NODE_GOTO:
        case LAYE_NODE_XYZZY:
        case LAYE_NODE_LITNIL:
        case LAYE_NODE_LITBOOL:
        case LAYE_NODE_LITINT:
        case LAYE_NODE_LITFLOAT:
        case LAYE_NODE_LITSTRING:
        case LAYE_NODE_LITRUNE:
        case LAYE_NODE_TYPE_POISON:
        case LAYE_NODE_TYPE_UNKNOWN:
        case LAYE_NODE_TYPE_VAR:
        case LAYE_NODE_TYPE_TYPE:
        case LAYE_NODE_TYPE_VOID:
        case LAYE_NODE_TYPE_NORETURN:
        case LAYE_NODE_TYPE_BOOL:
        case LAYE_NODE_TYPE_INT:
        case LAYE_NODE_TYPE_FLOAT:
            return;

        case LAYE_NODE_INVALID:
            node->dependence |= LAYE_DEPENDENCE_ERROR_DEPENDENT;
            return;

        case LAYE_NODE_DECL_IMPORT:
            // TODO: can this ever be dependent?
            return;

        case LAYE_NODE_DECL_OVERLOADS:
            copy_dependence_all(node, node->decl_overloads.declarations);
            return;

        case LAYE_NODE_DECL_FUNCTION:
            copy_dependence(node, node->declared_type.node);
            copy_dependence(node, node->decl_function.return_type.node);
            copy_dependence_all(node, node->decl_function.parameter_declarations);
            return;

        case LAYE_NODE_DECL_FUNCTION_PARAMETER:
            copy_dependence(node, node->declared_type.node);
            copy_inst_dependence(node, node->decl_function_parameter.default_value);
            return;

        case LAYE_NODE_DECL_BINDING:
            copy_dependence(node, node->declared_type.node);
            copy_dependence(node, node->decl_binding.initializer);
            return;

        case LAYE_NODE_DECL_STRUCT:
            copy_dependence(node, node->declared_type.node);
            copy_type_dependence(node, node->decl_struct.field_declarations);
            copy_type_dependence(node, node->decl_struct.variant_declarations);
            return;

        case LAYE_NODE_DECL_STRUCT_FIELD:
            copy_dependence(node, node->declared_type.node);
            copy_inst_dependence(node, node->decl_struct_field.initializer);
            return;

        case LAYE_NODE_DECL_ENUM:
            copy_dependence(node, node->decl_enum.underlying_type);
            copy_inst_dependence_all(node, node->decl_enum.variants);

            // rare case of a child inheriting type-dependence from its parent
            if (node->dependence & LAYE_DEPENDENCE_TYPE) {
                for (int64_t i = 0; i < lca_da_count(node->decl_enum.variants); i++) {
                    node->decl_enum.variants[i]->dependence |= LAYE_DEPENDENCE_TYPE_DEPENDENT;
                }
            }
            return;

        case LAYE_NODE_DECL_ENUM_VARIANT:
            node->dependence |= turn_type_to_value_dependence(node->decl_enum_variant.value);
            return;

        case LAYE_NODE_DECL_TEMPLATE_TYPE:
            if (lca_da_count(node->template_parameters) != 0)
                node->dependence |= LAYE_DEPENDENCE_TYPE_DEPENDENT;
            return;

        case LAYE_NODE_DECL_TEMPLATE_VALUE:
            node->dependence |= LAYE_DEPENDENCE_VALUE_DEPENDENT;
            return;

        case LAYE_NODE_DECL_TEST:
            // TODO(Sirraide): Is this right? Not sure about the semantics of this candidly.
            copy_inst_dependence(node, node->decl_test.body);
            copy_inst_dependence(node, node->decl_test.referenced_decl_node);
            return;

        case LAYE_NODE_IMPORT_QUERY:
            // TODO: can this ever be dependent?
            return;

        case LAYE_NODE_COMPOUND:
            if (node->compound.is_expr) {
                // TODO: Should only be type- or value-dependent if the expression whose value
                //       this yields is type- or value-dependent.
                copy_dependence_all(node, node->compound.children);
            } else {
                copy_inst_dependence_all(node, node->compound.children);
            }
            return;

        case LAYE_NODE_ASSIGNMENT:
            copy_dependence(node, node->assignment.lhs);
            copy_dependence(node, node->assignment.rhs);
            return;

        case LAYE_NODE_DELETE:
            copy_inst_dependence(node, node->delete.operand);
            return;

        case LAYE_NODE_IF:
            if (node->_if.is_expr) {
                copy_dependence_all(node, node->_if.conditions);
                copy_dependence_all(node, node->_if.passes);
                copy_dependence(node, node->_if.fail);
            } else {
                copy_inst_dependence_all(node, node->_if.conditions);
                copy_inst_dependence_all(node, node->_if.passes);
                copy_inst_dependence(node, node->_if.fail);
            }
            return;

        case LAYE_NODE_FOR:
            copy_inst_dependence(node, node->_for.initializer);
            copy_inst_dependence(node, node->_for.condition);
            copy_inst_dependence(node, node->_for.increment);
            copy_inst_dependence(node, node->_for.fail);
            copy_inst_dependence(node, node->_for.pass);
            return;

        case LAYE_NODE_FOREACH:
            copy_inst_dependence(node, node->foreach.element_binding);
            copy_inst_dependence(node, node->foreach.index_binding);
            copy_inst_dependence(node, node->foreach.iterable);
            copy_inst_dependence(node, node->foreach.pass);
            return;

        case LAYE_NODE_WHILE:
            copy_inst_dependence(node, node->_while.condition);
            copy_inst_dependence(node, node->_while.pass);
            copy_inst_dependence(node, node->_while.fail);
            return;

        case LAYE_NODE_DOWHILE:
            copy_inst_dependence(node, node->dowhile.condition);
            copy_inst_dependence(node, node->dowhile.pass);
            return;

        case LAYE_NODE_SWITCH:
            copy_inst_dependence(node, node->_switch.value);
            copy_inst_dependence_all(node, node->_switch.cases);
            return;

        case LAYE_NODE_CASE:
            copy_dependence(node, node->_case.value);
            copy_inst_dependence(node, node->_case.body);
            return;

        case LAYE_NODE_RETURN:
            copy_inst_dependence(node, node->_return.value);
            return;

        case LAYE_NODE_YIELD:
            copy_inst_dependence(node, node->yield.value);
            return;

        case LAYE_NODE_DEFER:
            copy_inst_dependence(node, node->defer.body);
            return;

        case LAYE_NODE_DISCARD:
            copy_inst_dependence(node, node->discard.value);
            return;

        case LAYE_NODE_ASSERT:
            copy_inst_dependence(node, node->_assert.condition);
            return;

        case LAYE_NODE_EVALUATED_CONSTANT:
            assert(node->evaluated_constant.expr->dependence == LAYE_DEPENDENCE_NONE && "evaluated constant should not be dependent");
            return;

        case LAYE_NODE_TEMPLATE_PARAMETER:
            node->dependence |= LAYE_DEPENDENCE_VALUE_DEPENDENT;
            return;

        case LAYE_NODE_SIZEOF:
            node->dependence |= turn_type_to_value_dependence(node->_sizeof.query);
            return;

        case LAYE_NODE_OFFSETOF:
            node->dependence |= turn_type_to_value_dependence(node->_offsetof.query);
            return;

        case LAYE_NODE_ALIGNOF:
            node->dependence |= turn_type_to_value_dependence(node->_alignof_.query);
            return;

        case LAYE_NODE_NAMEREF:
        case LAYE_NODE_TYPE_NAMEREF:
            // FIXME: is this right? can these refer to function templates?
            if (node->nameref.referenced_declaration != NULL) {
                if (node->nameref.referenced_declaration->kind == LAYE_NODE_DECL_FUNCTION) {
                    node->dependence |= LAYE_DEPENDENCE_TYPE_DEPENDENT;
                } else {
                    copy_dependence(node, node->nameref.referenced_declaration);
                }
            }

            copy_dependence(node, node->nameref.referenced_type);
            return;

        case LAYE_NODE_MEMBER:
            copy_dependence(node, node->member.value);
            return;

        case LAYE_NODE_INDEX:
            copy_dependence(node, node->index.value);
            copy_dependence_all(node, node->index.indices);
            return;

        case LAYE_NODE_SLICE:
            copy_dependence(node, node->slice.value);
            copy_dependence(node, node->slice.length_value);
            copy_dependence(node, node->slice.offset_value);
            return;

        case LAYE_NODE_CALL:
            copy_dependence(node, node->call.callee);
            copy_dependence_all(node, node->call.arguments);
            return;

        case LAYE_NODE_CTOR:
            copy_inst_dependence_all(node, node->ctor.initializers);
            return;

        case LAYE_NODE_NEW:
            copy_dependence(node, node->new.type.node);
            copy_inst_dependence_all(node, node->new.arguments);
            copy_inst_dependence_all(node, node->new.initializers);
            return;

        case LAYE_NODE_MEMBER_INITIALIZER:
            copy_inst_dependence(node, node->member_initializer.value);
            copy_inst_dependence(node, node->member_initializer.index);
            return;

        case LAYE_NODE_UNARY:
            copy_dependence(node, node->unary.operand);
            return;

        case LAYE_NODE_BINARY:
            copy_dependence(node, node->binary.lhs);
            copy_dependence(node, node->binary.rhs);
            return;

        case LAYE_NODE_CAST:
            copy_dependence(node, node->cast.operand);
            return;

        case LAYE_NODE_UNWRAP_NILABLE:
            copy_dependence(node, node->unwrap_nilable.operand);
            return;

        case LAYE_NODE_TRY:
            copy_dependence(node, node->try.operand);
            return;

        case LAYE_NODE_CATCH:
            copy_inst_dependence(node, node->catch.operand);
            copy_inst_dependence(node, node->catch.body);
            return;

        case LAYE_NODE_TYPE_TEMPLATE_PARAMETER:
            node->dependence |= LAYE_DEPENDENCE_TYPE_DEPENDENT;
            return;

        case LAYE_NODE_TYPE_ERROR_PAIR:
            copy_dependence(node, node->type_error_pair.error_type.node);
            copy_dependence(node, node->type_error_pair.value_type.node);
            return;

        case LAYE_NODE_TYPE_NILABLE:
        case LAYE_NODE_TYPE_ARRAY:
        case LAYE_NODE_TYPE_SLICE:
        case LAYE_NODE_TYPE_REFERENCE:
        case LAYE_NODE_TYPE_POINTER:
        case LAYE_NODE_TYPE_BUFFER:
            copy_dependence(node, node->type_container.element_type.node);
            return;

        case LAYE_NODE_TYPE_FUNCTION:
            if (lca_da_count(node->template_parameters) != 0)
                node->dependence |= LAYE_DEPENDENCE_TYPE_DEPENDENT;

            copy_dependence(node, node->type_function.return_type.node);
            for (int64_t i = 0; i < lca_da_count(node->type_function.parameter_types); i++)
                copy_dependence(node, node->type_function.parameter_types[i].node);
            return;

        case LAYE_NODE_TYPE_STRUCT:
            if (lca_da_count(node->template_parameters) != 0)
                node->dependence |= LAYE_DEPENDENCE_TYPE_DEPENDENT;

            for (int64_t i = 0; i < lca_da_count(node->type_struct.fields); i++)
                copy_dependence(node, node->type_struct.fields[i].type.node);
            for (int64_t i = 0; i < lca_da_count(node->type_struct.variants); i++)
                copy_dependence(node, node->type_struct.variants[i].type.node);
            return;

        case LAYE_NODE_TYPE_ENUM:
            copy_dependence(node, node->type_enum.underlying_type);
            return;

        case LAYE_NODE_META_PATTERN:
        case LAYE_NODE_PATTERN_MATCH:
        case LAYE_NODE_META_ATTRIBUTE:
        case LAYE_NODE_TYPE_ALIAS:
        case LAYE_NODE_TYPE_VARIANT:
        case LAYE_NODE_TYPE_OVERLOADS:
        case LAYE_NODE_TYPE_STRICT_ALIAS:
            assert(false && "Unimplemented");
    }

    assert(false && "unreachable: invalid node kind");
}
