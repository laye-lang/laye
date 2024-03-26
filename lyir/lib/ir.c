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

#define LCA_STR_NO_SHORT_NAMES
#include "lyir.h"

#include <assert.h>

void layec_value_destroy(lyir_value* value);

struct lyir_module {
    lyir_context* context;
    lca_string_view name;

    lca_arena* arena;
    lca_da(lyir_value*) functions;
    lca_da(lyir_value*) globals;

    lca_da(lyir_value*) _all_values;
};

struct lyir_type {
    lyir_type_kind kind;
    lyir_context* context;

    union {
        int primitive_bit_width;

        struct {
            lyir_type* element_type;
            int64_t length;
        } array;

        struct {
            lyir_type* return_type;
            lca_da(lyir_type*) parameter_types;
            lyir_calling_convention calling_convention;
            bool is_variadic;
        } function;

        struct {
            lca_da(lyir_struct_member) members;
            bool named;

            union {
                lca_string_view name;
                int64_t index;
            };
        } _struct;
    };
};

typedef struct layec_incoming_value {
    lyir_value* value;
    lyir_value* block;
} layec_incoming_value;

struct lyir_value {
    lyir_value_kind kind;
    lyir_location location;
    lyir_module* module;
    lyir_context* context;

    lyir_type* type;

    lca_string_view name;
    int64_t index;
    lyir_linkage linkage;

    // for values which want their uses tracked, a list of all users of this value.
    lca_da(lyir_value*) users;

    lyir_value* parent_block;

    lyir_value* address;
    lyir_value* operand;

    union {
        int64_t int_value;
        double float_value;

        struct {
            lyir_builtin_kind kind;
            lca_da(lyir_value*) arguments;
        } builtin;

        struct {
            bool is_string_literal;
            char* data;
            int64_t length;
        } array;

        struct {
            lca_string_view name;
            int64_t index;
            lyir_value* parent_function;
            lca_da(lyir_value*) instructions;
        } block;

        struct {
            lca_string_view name;
            lca_da(lyir_value*) parameters;
            lca_da(lyir_value*) blocks;
        } function;

        int64_t parameter_index;

        struct {
            lyir_type* element_type;
            int64_t element_count;
        } alloca;

        lyir_value* return_value;

        struct {
            lyir_value* lhs;
            lyir_value* rhs;
        } binary;

        struct {
            lyir_value* pass;
            lyir_value* fail;
        } branch;

        lca_da(layec_incoming_value) incoming_values;

        struct {
            lyir_value* callee;
            // we may need to store a separate callee type, since opaque pointers are a thing
            // and we may be calling through a function pointer, for example
            lyir_type* callee_type;
            lyir_calling_convention calling_convention;
            lca_da(lyir_value*) arguments;
            bool is_tail_call : 1;
        } call;
    };
};

struct lyir_builder {
    lyir_context* context;

    lyir_value* function;
    lyir_value* block;
    int64_t insert_index;
};

static void layec_value_add_user(lyir_value* value, lyir_value* user) {
    assert(value != NULL);
    assert(user != NULL);

    for (int64_t i = 0, count = lca_da_count(value->users); i < count; i++) {
        if (value->users[i] == user) {
            return;
        }
    }

    lca_da_push(value->users, user);
}

static void layec_value_remove_user(lyir_value* value, lyir_value* user) {
    assert(value != NULL);
    assert(user != NULL);

    for (int64_t i = 0, count = lca_da_count(value->users); i < count; i++) {
        if (value->users[i] == user) {
            if (i != count - 1) {
                value->users[i] = value->users[count - 1];
            }

            lca_da_pop(value->users);
            return;
        }
    }
}

int64_t lyir_value_user_count_get(lyir_value* value) {
    assert(value != NULL);
    return lca_da_count(value->users);
}

lyir_value* lyir_value_user_get_at_index(lyir_value* value, int64_t user_index) {
    assert(value != NULL);
    assert(user_index >= 0 && user_index < lca_da_count(value->users));
    return value->users[user_index];
}

int64_t lyir_context_get_struct_type_count(lyir_context* context) {
    assert(context != NULL);
    return lca_da_count(context->_all_struct_types);
}

lyir_type* lyir_context_get_struct_type_at_index(lyir_context* context, int64_t index) {
    assert(context != NULL);
    assert(index >= 0 && index < lca_da_count(context->_all_struct_types));
    return context->_all_struct_types[index].type;
}

lyir_module* lyir_module_create(lyir_context* context, lca_string_view module_name) {
    assert(context != NULL);
    lyir_module* module = lca_allocate(context->allocator, sizeof *module);
    assert(module != NULL);
    module->context = context;
    module->name = lyir_context_intern_string_view(context, module_name);
    module->arena = lca_arena_create(context->allocator, 1024 * sizeof(lyir_value));
    assert(module->arena != NULL);
    return module;
}

void lyir_module_destroy(lyir_module* module) {
    if (module == NULL) return;
    assert(module->context != NULL);

    lca_allocator allocator = module->context->allocator;

    for (int64_t i = 0, count = lca_da_count(module->_all_values); i < count; i++) {
        layec_value_destroy(module->_all_values[i]);
    }

    assert(module->arena != NULL);
    lca_arena_destroy(module->arena);

    lca_da_free(module->globals);
    lca_da_free(module->functions);
    lca_da_free(module->_all_values);

    *module = (lyir_module){0};
    lca_deallocate(allocator, module);
}

void layec_value_destroy(lyir_value* value) {
    assert(value != NULL);

    switch (value->kind) {
        default: break;

        case LYIR_IR_FUNCTION: {
            lca_da_free(value->function.parameters);
            lca_da_free(value->function.blocks);
        } break;

        case LYIR_IR_BLOCK: {
            lca_da_free(value->block.instructions);
        } break;

        case LYIR_IR_CALL: {
            lca_da_free(value->call.arguments);
        } break;

        case LYIR_IR_PHI: {
            lca_da_free(value->incoming_values);
        } break;

        case LYIR_IR_BUILTIN: {
            lca_da_free(value->builtin.arguments);
        } break;
    }
}

static lyir_type* layec_type_create(lyir_context* context, lyir_type_kind kind) {
    assert(context != NULL);
    assert(context->type_arena != NULL);

    lyir_type* type = lca_arena_push(context->type_arena, sizeof *type);
    assert(type != NULL);
    type->context = context;
    lca_da_push(context->_all_types, type);
    type->kind = kind;

    return type;
}

void layec_type_destroy(lyir_type* type) {
    if (type == NULL) return;

    switch (type->kind) {
        default: break;

        case LYIR_TYPE_ARRAY: {
            layec_type_destroy(type->array.element_type);
        } break;

        case LYIR_TYPE_FUNCTION: {
            layec_type_destroy(type->function.return_type);
            for (int64_t i = 0, count = lca_da_count(type->function.parameter_types); i < count; i++) {
                layec_type_destroy(type->function.parameter_types[i]);
            }

            lca_da_free(type->function.parameter_types);
        } break;

        case LYIR_TYPE_STRUCT: {
            for (int64_t i = 0, count = lca_da_count(type->_struct.members); i < count; i++) {
                layec_type_destroy(type->_struct.members[i].type);
            }

            lca_da_free(type->_struct.members);
        } break;
    }
}

const char* lyir_type_kind_to_cstring(lyir_type_kind kind) {
    switch (kind) {
        default: return lca_temp_sprintf("<unknown %d>", (int)kind);
        case LYIR_TYPE_VOID: return "VOID";
        case LYIR_TYPE_ARRAY: return "ARRAY";
        case LYIR_TYPE_FLOAT: return "FLOAT";
        case LYIR_TYPE_FUNCTION: return "FUNCTION";
        case LYIR_TYPE_INTEGER: return "INTEGER";
        case LYIR_TYPE_POINTER: return "POINTER";
        case LYIR_TYPE_STRUCT: return "STRUCT";
    }
}

static lyir_value* layec_value_create(lyir_module* module, lyir_location location, lyir_value_kind kind, lyir_type* type, lca_string_view name) {
    assert(module != NULL);
    assert(module->context != NULL);
    assert(module->arena != NULL);
    assert(type != NULL);

    lyir_value* value = lca_arena_push(module->arena, sizeof *value);
    assert(value != NULL);
    value->kind = kind;
    value->module = module;
    value->context = module->context;
    value->location = location;
    value->type = type;
    lca_da_push(module->_all_values, value);

    return value;
}

lyir_context* lyir_module_context(lyir_module* module) {
    assert(module != NULL);
    return module->context;
}

lca_string_view lyir_module_name(lyir_module* module) {
    assert(module != NULL);
    return module->name;
}

int64_t lyir_module_global_count(lyir_module* module) {
    assert(module != NULL);
    return lca_da_count(module->globals);
}

lyir_value* lyir_module_get_global_at_index(lyir_module* module, int64_t global_index) {
    assert(module != NULL);
    assert(global_index >= 0);
    assert(global_index < lca_da_count(module->globals));
    return module->globals[global_index];
}

int64_t lyir_module_function_count(lyir_module* module) {
    assert(module != NULL);
    return lca_da_count(module->functions);
}

lyir_value* lyir_module_get_function_at_index(lyir_module* module, int64_t function_index) {
    assert(module != NULL);
    assert(function_index >= 0);
    assert(function_index < lca_da_count(module->functions));
    return module->functions[function_index];
}

// TODO(local): look up existing strings somewhere, somehow
lyir_value* lyir_module_create_global_string_ptr(lyir_module* module, lyir_location location, lca_string_view string_value) {
    assert(module != NULL);
    assert(module->context != NULL);

    lyir_type* array_type = lyir_array_type(module->context, string_value.count + 1, lyir_int_type(module->context, 8));

    char* data = lca_arena_push(module->arena, string_value.count + 1);
    memcpy(data, string_value.data, string_value.count);

    lyir_value* array_constant = lyir_array_constant_create(module->context, location, array_type, data, string_value.count + 1, true);

    lyir_value* global_string_ptr = layec_value_create(module, location, LYIR_IR_GLOBAL_VARIABLE, lyir_ptr_type(module->context), LCA_SV_EMPTY);
    assert(global_string_ptr != NULL);
    global_string_ptr->index = lca_da_count(module->globals);
    global_string_ptr->linkage = LYIR_LINK_INTERNAL;
    global_string_ptr->operand = array_constant;
    global_string_ptr->alloca.element_type = array_type;
    global_string_ptr->alloca.element_count = 1;
    lca_da_push(module->globals, global_string_ptr);

    return global_string_ptr;
}

lyir_type* lyir_value_function_return_type_get(lyir_value* function) {
    assert(function != NULL);
    assert(lyir_value_is_function(function));
    assert(lyir_type_is_function(function->type));
    return function->type->function.return_type;
}

int64_t lyir_value_function_block_count_get(lyir_value* function) {
    assert(function != NULL);
    assert(lyir_value_is_function(function));
    return lca_da_count(function->function.blocks);
}

lyir_value* lyir_value_function_block_get_at_index(lyir_value* function, int64_t block_index) {
    assert(function != NULL);
    assert(lyir_value_is_function(function));
    assert(block_index >= 0);
    assert(block_index < lca_da_count(function->function.blocks));
    return function->function.blocks[block_index];
}

int64_t lyir_value_function_parameter_count_get(lyir_value* function) {
    assert(function != NULL);
    assert(lyir_value_is_function(function));
    return lca_da_count(function->function.parameters);
}

lyir_value* lyir_value_function_parameter_get_at_index(lyir_value* function, int64_t parameter_index) {
    assert(function != NULL);
    assert(lyir_value_is_function(function));
    assert(parameter_index >= 0);
    assert(parameter_index < lca_da_count(function->function.parameters));
    return function->function.parameters[parameter_index];
}

bool lyir_value_function_is_variadic(lyir_value* function) {
    assert(function != NULL);
    assert(lyir_value_is_function(function));
    assert(function->type != NULL);
    assert(lyir_type_is_function(function->type));
    return function->type->function.is_variadic;
}

void lyir_value_function_parameter_type_set_at_index(lyir_value* function, int64_t parameter_index, lyir_type* param_type) {
    assert(function != NULL);
    assert(lyir_value_is_function(function));
    assert(function->type != NULL);
    assert(lyir_type_is_function(function->type));
    lyir_function_type_parameter_type_set_at_index(function->type, parameter_index, param_type);
    function->function.parameters[parameter_index]->type = param_type;
}

int64_t lyir_value_block_instruction_count_get(lyir_value* block) {
    assert(block != NULL);
    assert(lyir_value_is_block(block));
    return lca_da_count(block->block.instructions);
}

lyir_value* lyir_value_block_instruction_get_at_index(lyir_value* block, int64_t instruction_index) {
    assert(block != NULL);
    assert(lyir_value_is_block(block));
    assert(instruction_index >= 0);
    assert(instruction_index < lca_da_count(block->block.instructions));
    return block->block.instructions[instruction_index];
}

bool lyir_value_block_is_terminated(lyir_value* block) {
    assert(block != NULL);
    assert(lyir_value_is_block(block));

    if (lca_da_count(block->block.instructions) == 0) {
        return false;
    }

    return lyir_value_is_terminator(*lca_da_back(block->block.instructions));
}

bool lyir_value_is_terminator(lyir_value* instruction) {
    assert(instruction != NULL);

    switch (instruction->kind) {
        default: return false;

        case LYIR_IR_RETURN:
        case LYIR_IR_BRANCH:
        case LYIR_IR_COND_BRANCH:
        case LYIR_IR_UNREACHABLE: {
            return true;
        }
    }
}

lyir_value_kind lyir_value_kind_get(lyir_value* value) {
    assert(value != NULL);
    return value->kind;
}

lyir_context* lyir_value_context_get(lyir_value* value) {
    assert(value != NULL);
    return value->context;
}

lyir_location lyir_value_location_get(lyir_value* value) {
    assert(value != NULL);
    return value->location;
}

lyir_linkage lyir_value_linkage_get(lyir_value* value) {
    assert(value != NULL);
    return value->linkage;
}

lca_string_view lyir_value_name_get(lyir_value* value) {
    assert(value != NULL);
    return value->name;
}

int64_t lyir_value_index_get(lyir_value* value) {
    assert(value != NULL);
    return value->index;
}

bool lyir_value_block_has_name(lyir_value* block) {
    assert(block != NULL);
    assert(lyir_value_is_block(block));
    return block->block.name.count != 0;
}

lca_string_view lyir_value_block_name_get(lyir_value* block) {
    assert(block != NULL);
    assert(lyir_value_is_block(block));
    return block->block.name;
}

int64_t lyir_value_block_index_get(lyir_value* block) {
    assert(block != NULL);
    assert(lyir_value_is_block(block));
    return block->block.index;
}

lca_string_view lyir_value_function_name_get(lyir_value* function) {
    assert(function != NULL);
    assert(lyir_value_is_function(function));
    assert(function->function.name.count > 0);
    return function->function.name;
}

lyir_builtin_kind lyir_value_builtin_kind_get(lyir_value* instruction) {
    assert(instruction != NULL);
    assert(instruction->kind == LYIR_IR_BUILTIN);
    return instruction->builtin.kind;
}

bool lyir_value_global_is_string(lyir_value* global) {
    assert(global != NULL);
    assert(global->operand != NULL);
    assert(global->operand->kind == LYIR_IR_ARRAY_CONSTANT);
    return global->operand->array.is_string_literal;
}

bool lyir_value_return_has_value(lyir_value* _return) {
    assert(_return != NULL);
    assert(_return->kind == LYIR_IR_RETURN);
    return _return->return_value != NULL;
}

lyir_value* lyir_value_return_value_get(lyir_value* _return) {
    assert(_return != NULL);
    assert(_return->kind == LYIR_IR_RETURN);
    assert(_return->return_value != NULL);
    return _return->return_value;
}

lyir_type* lyir_value_alloca_type_get(lyir_value* alloca) {
    assert(alloca != NULL);
    assert(alloca->alloca.element_type != NULL);
    return alloca->alloca.element_type;
}

lyir_value* lyir_value_address_get(lyir_value* instruction) {
    assert(instruction != NULL);
    assert(instruction->address != NULL);
    return instruction->address;
}

lyir_value* lyir_value_operand_get(lyir_value* instruction) {
    assert(instruction != NULL);
    assert(instruction->operand != NULL);
    return instruction->operand;
}

lyir_value* lyir_value_lhs_get(lyir_value* instruction) {
    assert(instruction != NULL);
    assert(instruction->binary.lhs != NULL);
    return instruction->binary.lhs;
}

lyir_value* lyir_value_rhs_get(lyir_value* instruction) {
    assert(instruction != NULL);
    assert(instruction->binary.rhs != NULL);
    return instruction->binary.rhs;
}

lyir_value* lyir_value_branch_pass_get(lyir_value* instruction) {
    assert(instruction != NULL);
    assert(instruction->branch.pass != NULL);
    return instruction->branch.pass;
}

lyir_value* lyir_value_branch_fail_get(lyir_value* instruction) {
    assert(instruction != NULL);
    assert(instruction->branch.fail != NULL);
    return instruction->branch.fail;
}

lyir_value* lyir_value_callee_get(lyir_value* call) {
    assert(call != NULL);
    assert(call->kind == LYIR_IR_CALL);
    assert(call->call.callee != NULL);
    return call->call.callee;
}

int64_t lyir_value_call_argument_count_get(lyir_value* call) {
    assert(call != NULL);
    assert(call->kind == LYIR_IR_CALL);
    return lca_da_count(call->call.arguments);
}

lyir_value* lyir_value_call_argument_get_at_index(lyir_value* call, int64_t argument_index) {
    assert(call != NULL);
    assert(call->kind == LYIR_IR_CALL);
    assert(argument_index >= 0);
    int64_t count = lca_da_count(call->call.arguments);
    assert(argument_index < count);
    lyir_value* argument = call->call.arguments[argument_index];
    assert(argument != NULL);
    return argument;
}

void lyir_value_call_arguments_set(lyir_value* call, lca_da(lyir_value*) arguments) {
    assert(call != NULL);
    assert(call->kind == LYIR_IR_CALL);
    call->call.arguments = arguments;
}

int64_t lyir_value_builtin_argument_count_get(lyir_value* builtin) {
    assert(builtin != NULL);
    assert(builtin->kind == LYIR_IR_BUILTIN);
    return lca_da_count(builtin->builtin.arguments);
}

lyir_value* lyir_value_builtin_argument_set_at_index(lyir_value* builtin, int64_t argument_index) {
    assert(builtin != NULL);
    assert(builtin->kind == LYIR_IR_BUILTIN);
    assert(argument_index >= 0);
    int64_t count = lca_da_count(builtin->builtin.arguments);
    assert(argument_index < count);
    lyir_value* argument = builtin->builtin.arguments[argument_index];
    assert(argument != NULL);
    return argument;
}

void lyir_value_phi_incoming_value_add(lyir_value* phi, lyir_value* value, lyir_value* block) {
    assert(phi != NULL);
    assert(phi->kind == LYIR_IR_PHI);
    assert(value != NULL);
    assert(block != NULL);
    assert(block->kind == LYIR_IR_BLOCK);

    layec_incoming_value incoming_value = {
        .value = value,
        .block = block,
    };

    lca_da_push(phi->incoming_values, incoming_value);
}

int64_t lyir_value_phi_incoming_value_count_get(lyir_value* phi) {
    assert(phi != NULL);
    assert(phi->kind == LYIR_IR_PHI);
    return lca_da_count(phi->incoming_values);
}

lyir_value* lyir_phi_incoming_value_get_at_index(lyir_value* phi, int64_t index) {
    assert(phi != NULL);
    assert(phi->kind == LYIR_IR_PHI);
    lyir_value* value = phi->incoming_values[index].value;
    assert(value != NULL);
    return value;
}

lyir_value* lyir_phi_incoming_block_get_at_index(lyir_value* phi, int64_t index) {
    assert(phi != NULL);
    assert(phi->kind == LYIR_IR_PHI);
    lyir_value* block = phi->incoming_values[index].block;
    assert(block != NULL);
    assert(block->kind == LYIR_IR_BLOCK);
    return block;
}

int64_t lyir_value_integer_constant_get(lyir_value* value) {
    assert(value != NULL);
    assert(value->kind == LYIR_IR_INTEGER_CONSTANT);
    return value->int_value;
}

double lyir_value_float_constant_get(lyir_value* value) {
    assert(value != NULL);
    assert(value->kind == LYIR_IR_FLOAT_CONSTANT);
    return value->float_value;
}

const char* lyir_value_kind_to_cstring(lyir_value_kind kind) {
    switch (kind) {
        default: return "<unknown>";
        case LYIR_IR_INVALID: return "INVALID";
        case LYIR_IR_INTEGER_CONSTANT: return "INTEGER_CONSTANT";
        case LYIR_IR_ARRAY_CONSTANT: return "ARRAY_CONSTANT";
        case LYIR_IR_VOID_CONSTANT: return "VOID_CONSTANT";
        case LYIR_IR_POISON: return "POISON";
        case LYIR_IR_BLOCK: return "BLOCK";
        case LYIR_IR_FUNCTION: return "FUNCTION";
        case LYIR_IR_GLOBAL_VARIABLE: return "GLOBAL_VARIABLE";
        case LYIR_IR_PARAMETER: return "PARAMETER";
        case LYIR_IR_NOP: return "NOP";
        case LYIR_IR_ALLOCA: return "ALLOCA";
        case LYIR_IR_CALL: return "CALL";
        case LYIR_IR_PTRADD: return "PTRADD";
        case LYIR_IR_BUILTIN: return "INTRINSIC";
        case LYIR_IR_LOAD: return "LOAD";
        case LYIR_IR_PHI: return "PHI";
        case LYIR_IR_STORE: return "STORE";
        case LYIR_IR_BRANCH: return "BRANCH";
        case LYIR_IR_COND_BRANCH: return "COND_BRANCH";
        case LYIR_IR_RETURN: return "RETURN";
        case LYIR_IR_UNREACHABLE: return "UNREACHABLE";
        case LYIR_IR_ZEXT: return "ZEXT";
        case LYIR_IR_SEXT: return "SEXT";
        case LYIR_IR_TRUNC: return "TRUNC";
        case LYIR_IR_BITCAST: return "BITCAST";
        case LYIR_IR_NEG: return "NEG";
        case LYIR_IR_COPY: return "COPY";
        case LYIR_IR_COMPL: return "COMPL";
        case LYIR_IR_FPTOUI: return "FPTOUI";
        case LYIR_IR_FPTOSI: return "FPTOSI";
        case LYIR_IR_UITOFP: return "UITOFP";
        case LYIR_IR_SITOFP: return "SITOFP";
        case LYIR_IR_FPTRUNC: return "FPTRUNC";
        case LYIR_IR_FPEXT: return "FPEXT";
        case LYIR_IR_ADD: return "ADD";
        case LYIR_IR_SUB: return "SUB";
        case LYIR_IR_MUL: return "MUL";
        case LYIR_IR_SDIV: return "SDIV";
        case LYIR_IR_UDIV: return "UDIV";
        case LYIR_IR_SMOD: return "SREM";
        case LYIR_IR_UMOD: return "UREM";
        case LYIR_IR_SHL: return "SHL";
        case LYIR_IR_SAR: return "SAR";
        case LYIR_IR_SHR: return "SHR";
        case LYIR_IR_AND: return "AND";
        case LYIR_IR_OR: return "OR";
        case LYIR_IR_XOR: return "XOR";
        case LYIR_IR_ICMP_EQ: return "ICMP_EQ";
        case LYIR_IR_ICMP_NE: return "ICMP_NE";
        case LYIR_IR_ICMP_SLT: return "ICMP_SLT";
        case LYIR_IR_ICMP_SLE: return "ICMP_SLE";
        case LYIR_IR_ICMP_SGT: return "ICMP_SGT";
        case LYIR_IR_ICMP_SGE: return "ICMP_SGE";
        case LYIR_IR_ICMP_ULT: return "ICMP_ULT";
        case LYIR_IR_ICMP_ULE: return "ICMP_ULE";
        case LYIR_IR_ICMP_UGT: return "ICMP_UGT";
        case LYIR_IR_ICMP_UGE: return "ICMP_UGE";
        case LYIR_IR_FCMP_FALSE: return "FCMP_FALSE";
        case LYIR_IR_FCMP_OEQ: return "FCMP_OEQ";
        case LYIR_IR_FCMP_OGT: return "FCMP_OGT";
        case LYIR_IR_FCMP_OGE: return "FCMP_OGE";
        case LYIR_IR_FCMP_OLT: return "FCMP_OLT";
        case LYIR_IR_FCMP_OLE: return "FCMP_OLE";
        case LYIR_IR_FCMP_ONE: return "FCMP_ONE";
        case LYIR_IR_FCMP_ORD: return "FCMP_ORD";
        case LYIR_IR_FCMP_UEQ: return "FCMP_UEQ";
        case LYIR_IR_FCMP_UGT: return "FCMP_UGT";
        case LYIR_IR_FCMP_UGE: return "FCMP_UGE";
        case LYIR_IR_FCMP_ULT: return "FCMP_ULT";
        case LYIR_IR_FCMP_ULE: return "FCMP_ULE";
        case LYIR_IR_FCMP_UNE: return "FCMP_UNE";
        case LYIR_IR_FCMP_UNO: return "FCMP_UNO";
        case LYIR_IR_FCMP_TRUE: return "FCMP_TRUE";
    }
}

lyir_type_kind lyir_type_kind_get(lyir_type* type) {
    assert(type != NULL);
    return type->kind;
}

lyir_type* lyir_void_type(lyir_context* context) {
    assert(context != NULL);

    if (context->types._void == NULL) {
        lyir_type* void_type = layec_type_create(context, LYIR_TYPE_VOID);
        assert(void_type != NULL);
        context->types._void = void_type;
    }

    assert(context->types._void != NULL);
    return context->types._void;
}

lyir_type* lyir_ptr_type(lyir_context* context) {
    assert(context != NULL);

    if (context->types.ptr == NULL) {
        lyir_type* ptr_type = layec_type_create(context, LYIR_TYPE_POINTER);
        assert(ptr_type != NULL);
        context->types.ptr = ptr_type;
    }

    assert(context->types.ptr != NULL);
    return context->types.ptr;
}

lyir_type* lyir_int_type(lyir_context* context, int bit_width) {
    assert(context != NULL);
    assert(bit_width > 0);
    assert(bit_width <= 65535);

    for (int64_t i = 0, count = lca_da_count(context->types.int_types); i < count; i++) {
        lyir_type* int_type = context->types.int_types[i];
        assert(int_type != NULL);
        assert(lyir_type_is_integer(int_type));

        if (int_type->primitive_bit_width == bit_width) {
            return int_type;
        }
    }

    lyir_type* int_type = layec_type_create(context, LYIR_TYPE_INTEGER);
    assert(int_type != NULL);
    int_type->primitive_bit_width = bit_width;
    lca_da_push(context->types.int_types, int_type);
    return int_type;
}

static lyir_type* layec_create_float_type(lyir_context* context, int bit_width) {
    lyir_type* float_type = layec_type_create(context, LYIR_TYPE_FLOAT);
    assert(float_type != NULL);
    float_type->primitive_bit_width = bit_width;
    return float_type;
}

lyir_type* lyir_float_type(lyir_context* context, int bit_width) {
    assert(context != NULL);
    assert(bit_width > 0);
    assert(bit_width <= 128);

    switch (bit_width) {
        default: {
            fprintf(stderr, "for bitwidth %d\n", bit_width);
            assert(false && "unsupported bit width");
            return NULL;
        }

        case 32: {
            if (context->types.f32 == NULL) {
                context->types.f32 = layec_create_float_type(context, bit_width);
            }
            return context->types.f32;
        }

        case 64: {
            if (context->types.f64 == NULL) {
                context->types.f64 = layec_create_float_type(context, bit_width);
            }
            return context->types.f64;
        }
    }
}

lyir_type* lyir_array_type(lyir_context* context, int64_t length, lyir_type* element_type) {
    assert(context != NULL);
    assert(length >= 0);
    assert(element_type != NULL);

    lyir_type* array_type = layec_type_create(context, LYIR_TYPE_ARRAY);
    assert(array_type != NULL);
    array_type->array.element_type = element_type;
    array_type->array.length = length;
    return array_type;
}

lyir_type* lyir_function_type(
    lyir_context* context,
    lyir_type* return_type,
    lca_da(lyir_type*) parameter_types,
    lyir_calling_convention calling_convention,
    bool is_variadic
) {
    assert(context != NULL);
    assert(return_type != NULL);
    for (int64_t i = 0, count = lca_da_count(parameter_types); i < count; i++) {
        assert(parameter_types[i] != NULL);
    }
    assert(calling_convention != LYIR_DEFAULTCC);

    lyir_type* function_type = layec_type_create(context, LYIR_TYPE_FUNCTION);
    assert(function_type != NULL);
    function_type->function.return_type = return_type;
    function_type->function.parameter_types = parameter_types;
    function_type->function.calling_convention = calling_convention;
    function_type->function.is_variadic = is_variadic;

    return function_type;
}

lyir_type* lyir_struct_type(lyir_context* context, lca_string_view name, lca_da(lyir_struct_member) members) {
    assert(context != NULL);
    assert(name.count != 0);
    for (int64_t i = 0, count = lca_da_count(members); i < count; i++) {
        assert(members[i].type != NULL);
    }

    lyir_type* struct_type = layec_type_create(context, LYIR_TYPE_STRUCT);
    assert(struct_type != NULL);
    // TODO(local): unnamed struct types
    struct_type->_struct.named = true;
    struct_type->_struct.name = name;
    struct_type->_struct.members = members;
    return struct_type;
}

int lyir_type_size_in_bits(lyir_type* type) {
    assert(type->context != NULL);
    switch (type->kind) {
        default: {
            fprintf(stderr, "for type kind %s\n", lyir_type_kind_to_cstring(type->kind));
            assert(false && "unimplemented kind in layec_type_size_in_bits");
            return 0;
        }

        case LYIR_TYPE_POINTER: return type->context->target->size_of_pointer;

        case LYIR_TYPE_INTEGER:
        case LYIR_TYPE_FLOAT: return type->primitive_bit_width;

        case LYIR_TYPE_ARRAY: {
            return type->array.length * lyir_type_align_in_bytes(type->array.element_type) * 8;
        }

        case LYIR_TYPE_STRUCT: {
            int size = 0;
            // NOTE(local): generation of this struct should include padding, so we don't consider it here
            for (int64_t i = 0, count = lca_da_count(type->_struct.members); i < count; i++) {
                size += lyir_type_size_in_bits(type->_struct.members[i].type);
            }
            return size;
        }
    }
}

int lyir_type_size_in_bytes(lyir_type* type) {
    return (lyir_type_size_in_bits(type) + 7) / 8;
}

int lyir_type_align_in_bits(lyir_type* type) {
    assert(type->context != NULL);
    switch (type->kind) {
        default: {
            fprintf(stderr, "for type kind %s\n", lyir_type_kind_to_cstring(type->kind));
            assert(false && "unimplemented kind in layec_type_align_in_bits");
            return 0;
        }

        case LYIR_TYPE_POINTER: return type->context->target->align_of_pointer;

        case LYIR_TYPE_INTEGER:
        case LYIR_TYPE_FLOAT: return type->primitive_bit_width;

        case LYIR_TYPE_STRUCT: {
            int align = 1;
            for (int64_t i = 0, count = lca_da_count(type->_struct.members); i < count; i++) {
                int f_align = lyir_type_align_in_bits(type->_struct.members[i].type);
                if (f_align > align) {
                    align = f_align;
                }
            }

            return align;
        }

        case LYIR_TYPE_ARRAY: {
            assert(type->array.element_type != NULL);
            return lyir_type_align_in_bits(type->array.element_type);
        }
    }
}

int lyir_type_align_in_bytes(lyir_type* type) {
    return (lyir_type_align_in_bits(type) + 7) / 8;
}

lyir_type* lyir_type_element_type_get(lyir_type* type) {
    assert(type != NULL);
    assert(type->kind == LYIR_TYPE_ARRAY);
    return type->array.element_type;
}

int64_t lyir_type_array_length_get(lyir_type* type) {
    assert(type != NULL);
    assert(type->kind == LYIR_TYPE_ARRAY);
    return type->array.length;
}

bool lyir_type_struct_is_named(lyir_type* type) {
    assert(type != NULL);
    assert(type->kind == LYIR_TYPE_STRUCT);
    return type->_struct.named;
}

lca_string_view lyir_type_struct_name_get(lyir_type* type) {
    assert(type != NULL);
    assert(type->kind == LYIR_TYPE_STRUCT);
    return type->_struct.name;
}

int64_t lyir_type_struct_member_count_get(lyir_type* type) {
    assert(type != NULL);
    assert(type->kind == LYIR_TYPE_STRUCT);
    return lca_da_count(type->_struct.members);
}

lyir_struct_member lyir_type_struct_member_get_at_index(lyir_type* type, int64_t index) {
    assert(type != NULL);
    assert(type->kind == LYIR_TYPE_STRUCT);
    return type->_struct.members[index];
}

lyir_type* lyir_type_struct_member_type_get_at_index(lyir_type* type, int64_t index) {
    assert(type != NULL);
    assert(type->kind == LYIR_TYPE_STRUCT);
    return type->_struct.members[index].type;
}

int64_t lyir_function_type_parameter_count_get(lyir_type* function_type) {
    assert(function_type != NULL);
    assert(lyir_type_is_function(function_type));
    return lca_da_count(function_type->function.parameter_types);
}

lyir_type* lyir_function_type_parameter_type_get_at_index(lyir_type* function_type, int64_t parameter_index) {
    assert(function_type != NULL);
    assert(lyir_type_is_function(function_type));
    assert(parameter_index >= 0);
    assert(parameter_index < lca_da_count(function_type->function.parameter_types));
    return function_type->function.parameter_types[parameter_index];
}

bool lyir_function_type_is_variadic(lyir_type* function_type) {
    assert(function_type != NULL);
    assert(lyir_type_is_function(function_type));
    return function_type->function.is_variadic;
}

void lyir_function_type_parameter_type_set_at_index(lyir_type* function_type, int64_t parameter_index, lyir_type* param_type) {
    assert(function_type != NULL);
    assert(lyir_type_is_function(function_type));
    assert(parameter_index >= 0);
    assert(parameter_index < lca_da_count(function_type->function.parameter_types));
    assert(param_type != NULL);
    function_type->function.parameter_types[parameter_index] = param_type;
}

static lyir_value* layec_value_create_in_context(lyir_context* context, lyir_location location, lyir_value_kind kind, lyir_type* type, lca_string_view name) {
    assert(context != NULL);
    assert(type != NULL);

    lyir_value* value = lca_allocate(context->allocator, sizeof *value);
    assert(value != NULL);
    value->kind = kind;
    value->module = NULL;
    value->context = context;
    value->location = location;
    value->type = type;
    lca_da_push(context->_all_values, value);

    return value;
}

static int64_t layec_instruction_get_index_within_block(lyir_value* instruction) {
    assert(instruction != NULL);
    lyir_value* block = instruction->parent_block;
    assert(block != NULL);
    assert(lyir_value_is_block(block));

    for (int64_t i = 0, count = lca_da_count(block->block.instructions); i < count; i++) {
        if (instruction == block->block.instructions[i]) {
            return i;
        }
    }

    return -1;
}

bool lyir_type_is_ptr(lyir_type* type) {
    assert(type != NULL);
    return type->kind == LYIR_TYPE_POINTER;
}

bool lyir_type_is_void(lyir_type* type) {
    assert(type != NULL);
    return type->kind == LYIR_TYPE_VOID;
}

bool lyir_type_is_array(lyir_type* type) {
    assert(type != NULL);
    return type->kind == LYIR_TYPE_ARRAY;
}

bool lyir_type_is_function(lyir_type* type) {
    assert(type != NULL);
    return type->kind == LYIR_TYPE_FUNCTION;
}

bool lyir_type_is_integer(lyir_type* type) {
    assert(type != NULL);
    return type->kind == LYIR_TYPE_INTEGER;
}

bool lyir_type_is_float(lyir_type* type) {
    assert(type != NULL);
    return type->kind == LYIR_TYPE_FLOAT;
}

bool lyir_type_is_struct(lyir_type* type) {
    assert(type != NULL);
    return type->kind == LYIR_TYPE_STRUCT;
}

bool lyir_value_is_block(lyir_value* value) {
    assert(value != NULL);
    return value->kind == LYIR_IR_BLOCK;
}

bool lyir_value_is_function(lyir_value* value) {
    assert(value != NULL);
    return value->kind == LYIR_IR_FUNCTION;
}

bool lyir_value_is_instruction(lyir_value* value) {
    assert(value != NULL);
    return value->kind != LYIR_IR_INVALID &&
           value->kind != LYIR_IR_BLOCK;
}

lyir_type* lyir_value_type_get(lyir_value* value) {
    assert(value != NULL);
    assert(value->type != NULL);
    return value->type;
}

void lyir_value_type_set(lyir_value* value, lyir_type* type) {
    assert(value != NULL);
    assert(type != NULL);
    value->type = type;
}

lyir_value* lyir_module_create_function(lyir_module* module, lyir_location location, lca_string_view function_name, lyir_type* function_type, lca_da(lyir_value*) parameters, lyir_linkage linkage) {
    assert(module != NULL);
    assert(module->context != NULL);
    assert(function_type != NULL);
    assert(function_type->kind == LYIR_TYPE_FUNCTION);

    lyir_value* function = layec_value_create(module, location, LYIR_IR_FUNCTION, function_type, LCA_SV_EMPTY);
    assert(function != NULL);
    function->function.name = lyir_context_intern_string_view(module->context, function_name);
    function->linkage = linkage;
    function->function.parameters = parameters;

    lca_da_push(module->functions, function);
    return function;
}

lyir_value* lyir_value_function_block_append(lyir_value* function, lca_string_view name) {
    assert(function != NULL);
    assert(function->module != NULL);
    assert(function->context != NULL);
    assert(function->context == function->module->context);
    lyir_value* block = layec_value_create(function->module, (lyir_location){0}, LYIR_IR_BLOCK, lyir_void_type(function->context), LCA_SV_EMPTY);
    assert(block != NULL);
    block->block.name = lyir_context_intern_string_view(function->context, name);
    block->block.parent_function = function;
    block->block.index = lca_da_count(function->function.blocks);
    lca_da_push(function->function.blocks, block);
    return block;
}

lyir_value* lyir_void_constant_create(lyir_context* context) {
    assert(context != NULL);

    if (context->values._void == NULL) {
        lyir_value* void_value = layec_value_create_in_context(context, (lyir_location){0}, LYIR_IR_VOID_CONSTANT, lyir_void_type(context), LCA_SV_EMPTY);
        assert(void_value != NULL);
        context->values._void = void_value;
    }

    assert(context->values._void != NULL);
    return context->values._void;
}

lyir_value* lyir_int_constant_create(lyir_context* context, lyir_location location, lyir_type* type, int64_t value) {
    assert(context != NULL);
    assert(type != NULL);
    assert(lyir_type_is_integer(type));

    lyir_value* int_value = layec_value_create_in_context(context, location, LYIR_IR_INTEGER_CONSTANT, type, LCA_SV_EMPTY);
    assert(int_value != NULL);
    int_value->int_value = value;
    return int_value;
}

lyir_value* lyir_float_constant_create(lyir_context* context, lyir_location location, lyir_type* type, double value) {
    assert(context != NULL);
    assert(type != NULL);
    assert(lyir_type_is_float(type));

    lyir_value* float_value = layec_value_create_in_context(context, location, LYIR_IR_FLOAT_CONSTANT, type, LCA_SV_EMPTY);
    assert(float_value != NULL);
    float_value->float_value = value;
    return float_value;
}

lyir_value* lyir_array_constant_create(lyir_context* context, lyir_location location, lyir_type* type, void* data, int64_t length, bool is_string_literal) {
    assert(context != NULL);
    assert(type != NULL);
    assert(data != NULL);
    assert(length >= 0);

    lyir_value* array_value = layec_value_create_in_context(context, location, LYIR_IR_ARRAY_CONSTANT, type, LCA_SV_EMPTY);
    assert(array_value != NULL);
    array_value->array.is_string_literal = is_string_literal;
    array_value->array.data = data;
    array_value->array.length = length;
    return array_value;
}

bool lyir_array_constant_is_string(lyir_value* array_constant) {
    assert(array_constant != NULL);
    assert(array_constant->kind == LYIR_IR_ARRAY_CONSTANT);
    return array_constant->array.is_string_literal;
}

int64_t lyir_array_constant_length_get(lyir_value* array_constant) {
    assert(array_constant != NULL);
    assert(array_constant->kind == LYIR_IR_ARRAY_CONSTANT);
    return array_constant->array.length;
}

const char* lyir_array_constant_data_get(lyir_value* array_constant) {
    assert(array_constant != NULL);
    assert(array_constant->kind == LYIR_IR_ARRAY_CONSTANT);
    return array_constant->array.data;
}

lyir_builder* lyir_builder_create(lyir_context* context) {
    assert(context != NULL);

    lyir_builder* builder = lca_allocate(context->allocator, sizeof *builder);
    assert(builder != NULL);
    builder->context = context;

    return builder;
}

void lyir_builder_destroy(lyir_builder* builder) {
    if (builder == NULL) return;
    assert(builder->context != NULL);

    lca_allocator allocator = builder->context->allocator;

    *builder = (lyir_builder){0};
    lca_deallocate(allocator, builder);
}

lyir_module* lyir_builder_module_get(lyir_builder* builder) {
    assert(builder != NULL);
    assert(builder->function != NULL);
    return builder->function->module;
}

lyir_context* lyir_builder_context_get(lyir_builder* builder) {
    assert(builder != NULL);
    return builder->context;
}

lyir_value* lyir_builder_function_get(lyir_builder* builder) {
    assert(builder != NULL);
    return builder->function;
}

void lyir_builder_reset(lyir_builder* builder) {
    assert(builder != NULL);
    builder->function = NULL;
    builder->block = NULL;
    builder->insert_index = -1;
}

void lyir_builder_position_before(lyir_builder* builder, lyir_value* instruction) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(instruction != NULL);
    lyir_value* block = instruction->parent_block;
    assert(block != NULL);
    assert(lyir_value_is_block(block));
    lyir_value* function = block->block.parent_function;
    assert(function != NULL);
    assert(lyir_value_is_function(function));
    assert(function->module != NULL);
    assert(function->module->context == builder->context);

    builder->function = function;
    builder->block = block;
    builder->insert_index = layec_instruction_get_index_within_block(instruction);
    assert(builder->insert_index >= 0);
    assert(builder->insert_index < lca_da_count(block->block.instructions));
}

void lyir_builder_position_after(lyir_builder* builder, lyir_value* instruction) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(instruction != NULL);
    lyir_value* block = instruction->parent_block;
    assert(block != NULL);
    assert(lyir_value_is_block(block));
    lyir_value* function = block->block.parent_function;
    assert(function != NULL);
    assert(lyir_value_is_function(function));
    assert(function->module != NULL);
    assert(function->module->context == builder->context);

    builder->function = function;
    builder->block = block;
    builder->insert_index = layec_instruction_get_index_within_block(instruction) + 1;
    assert(builder->insert_index > 0);
    assert(builder->insert_index <= lca_da_count(block->block.instructions));
}

void lyir_builder_position_at_end(lyir_builder* builder, lyir_value* block) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(block != NULL);
    assert(lyir_value_is_block(block));
    lyir_value* function = block->block.parent_function;
    assert(function != NULL);
    assert(lyir_value_is_function(function));
    assert(function->module != NULL);
    assert(function->module->context == builder->context);

    builder->function = function;
    builder->block = block;
    builder->insert_index = lca_da_count(block->block.instructions);
    assert(builder->insert_index >= 0);
}

lyir_value* lyir_builder_insert_block_get(lyir_builder* builder) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    return builder->block;
}

static void layec_builder_recalculate_instruction_indices(lyir_builder* builder) {
    assert(builder != NULL);
    assert(builder->function != NULL);
    assert(lyir_value_is_function(builder->function));

    int64_t instruction_index = lyir_function_type_parameter_count_get(builder->function->type);

    for (int64_t b = 0, bcount = lca_da_count(builder->function->function.blocks); b < bcount; b++) {
        lyir_value* block = builder->function->function.blocks[b];
        assert(block != NULL);
        assert(lyir_value_is_block(block));

        for (int64_t i = 0, icount = lca_da_count(block->block.instructions); i < icount; i++) {
            lyir_value* instruction = block->block.instructions[i];
            assert(instruction != NULL);
            assert(lyir_value_is_instruction(instruction));

            if (instruction->type->kind == LYIR_TYPE_VOID) {
                instruction->index = 0;
                continue;
            }

            instruction->index = instruction_index;
            instruction_index++;
        }
    }
}

void lyir_builder_insert(lyir_builder* builder, lyir_value* instruction) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(instruction != NULL);
    assert(lyir_value_is_instruction(instruction));
    assert(instruction->parent_block == NULL);
    lyir_value* block = builder->block;
    assert(lyir_value_is_block(block));
    int64_t insert_index = builder->insert_index;
    assert(insert_index >= 0);
    assert(insert_index <= lca_da_count(block->block.instructions));

    instruction->parent_block = block;

    // reserve space for the new instruction
    lca_da_push(block->block.instructions, NULL);

    // move everything over if necessary
    for (int64_t i = insert_index, count = lca_da_count(block->block.instructions) - 1; i < count; i++) {
        block->block.instructions[i + 1] = block->block.instructions[i];
    }

    block->block.instructions[insert_index] = instruction;
    builder->insert_index++;

    instruction->index = -1;
    layec_builder_recalculate_instruction_indices(builder);
    assert(instruction->index >= 0);
}

void lyir_builder_insert_with_name(lyir_builder* builder, lyir_value* instruction, lca_string_view name) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(instruction != NULL);
    assert(lyir_value_is_instruction(instruction));
    assert(instruction->parent_block == NULL);

    instruction->name = lyir_context_intern_string_view(builder->context, name);
    lyir_builder_insert(builder, instruction);
}

lyir_value* layec_build_nop(lyir_builder* builder, lyir_location location) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);

    lyir_value* nop = layec_value_create(builder->function->module, location, LYIR_IR_NOP, lyir_void_type(builder->context), LCA_SV_EMPTY);
    assert(nop != NULL);

    lyir_builder_insert(builder, nop);
    return nop;
}

lyir_value* lyir_value_parameter_create(lyir_module* module, lyir_location location, lyir_type* type, lca_string_view name, int64_t index) {
    assert(module != NULL);
    assert(type != NULL);

    lyir_value* parameter = layec_value_create(module, location, LYIR_IR_PARAMETER, type, name);
    assert(parameter != NULL);

    parameter->index = index;

    return parameter;
}

lyir_value* layec_build_call(lyir_builder* builder, lyir_location location, lyir_value* callee, lyir_type* callee_type, lca_da(lyir_value*) arguments, lca_string_view name) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(callee != NULL);
    assert(callee_type != NULL);
    for (int64_t i = 0, count = lca_da_count(arguments); i < count; i++) {
        assert(arguments[i] != NULL);
    }

    assert(callee_type->kind == LYIR_TYPE_FUNCTION);
    lyir_type* result_type = callee_type->function.return_type;
    assert(result_type != NULL);

    lyir_value* call = layec_value_create(builder->function->module, location, LYIR_IR_CALL, result_type, name);
    assert(call != NULL);
    call->call.callee = callee;
    call->call.callee_type = callee_type;
    call->call.arguments = arguments;
    call->call.calling_convention = callee_type->function.calling_convention;
    call->call.is_tail_call = false;

    lyir_builder_insert(builder, call);
    return call;
}

lyir_value* layec_build_return(lyir_builder* builder, lyir_location location, lyir_value* value) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(value != NULL);

    lyir_value* ret = layec_value_create(builder->function->module, location, LYIR_IR_RETURN, lyir_void_type(builder->context), LCA_SV_EMPTY);
    assert(ret != NULL);
    ret->return_value = value;

    lyir_builder_insert(builder, ret);
    return ret;
}

lyir_value* layec_build_return_void(lyir_builder* builder, lyir_location location) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);

    lyir_value* ret = layec_value_create(builder->function->module, location, LYIR_IR_RETURN, lyir_void_type(builder->context), LCA_SV_EMPTY);
    assert(ret != NULL);

    lyir_builder_insert(builder, ret);
    return ret;
}

lyir_value* layec_build_unreachable(lyir_builder* builder, lyir_location location) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);

    lyir_value* unreachable = layec_value_create(builder->function->module, location, LYIR_IR_UNREACHABLE, lyir_void_type(builder->context), LCA_SV_EMPTY);
    assert(unreachable != NULL);

    lyir_builder_insert(builder, unreachable);
    return unreachable;
}

lyir_value* layec_build_alloca(lyir_builder* builder, lyir_location location, lyir_type* element_type, int64_t count) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(element_type != NULL);

    lyir_value* alloca = layec_value_create(builder->function->module, location, LYIR_IR_ALLOCA, lyir_ptr_type(builder->context), LCA_SV_EMPTY);
    assert(alloca != NULL);
    alloca->alloca.element_type = element_type;
    alloca->alloca.element_count = count;

    lyir_builder_insert(builder, alloca);
    return alloca;
}

lyir_value* layec_build_store(lyir_builder* builder, lyir_location location, lyir_value* address, lyir_value* value) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(address != NULL);
    assert(lyir_type_is_ptr(lyir_value_type_get(address)));
    assert(value != NULL);

    lyir_value* store = layec_value_create(builder->function->module, location, LYIR_IR_STORE, lyir_void_type(builder->context), LCA_SV_EMPTY);
    assert(store != NULL);
    store->address = address;
    store->operand = value;

    lyir_builder_insert(builder, store);
    return store;
}

lyir_value* layec_build_load(lyir_builder* builder, lyir_location location, lyir_value* address, lyir_type* type) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(address != NULL);
    assert(lyir_type_is_ptr(lyir_value_type_get(address)));
    assert(type != NULL);

    lyir_value* load = layec_value_create(builder->function->module, location, LYIR_IR_LOAD, type, LCA_SV_EMPTY);
    assert(load != NULL);
    load->address = address;

    lyir_builder_insert(builder, load);
    return load;
}

lyir_value* layec_build_branch(lyir_builder* builder, lyir_location location, lyir_value* block) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(block != NULL);
    assert(lyir_value_is_block(block));

    lyir_value* branch = layec_value_create(builder->function->module, location, LYIR_IR_BRANCH, lyir_void_type(builder->context), LCA_SV_EMPTY);
    assert(branch != NULL);
    branch->branch.pass = block;

    lyir_builder_insert(builder, branch);
    return branch;
}

lyir_value* layec_build_branch_conditional(lyir_builder* builder, lyir_location location, lyir_value* condition, lyir_value* pass_block, lyir_value* fail_block) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(condition != NULL);
    assert(lyir_type_is_integer(lyir_value_type_get(condition)));
    assert(lyir_type_size_in_bits(lyir_value_type_get(condition)) == 1);
    assert(pass_block != NULL);
    assert(lyir_value_is_block(pass_block));
    assert(fail_block != NULL);
    assert(lyir_value_is_block(fail_block));

    lyir_value* branch = layec_value_create(builder->function->module, location, LYIR_IR_COND_BRANCH, lyir_void_type(builder->context), LCA_SV_EMPTY);
    assert(branch != NULL);
    branch->operand = condition;
    branch->branch.pass = pass_block;
    branch->branch.fail = fail_block;

    lyir_builder_insert(builder, branch);
    return branch;
}

lyir_value* layec_build_phi(lyir_builder* builder, lyir_location location, lyir_type* type) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(type != NULL);

    lyir_value* phi = layec_value_create(builder->function->module, location, LYIR_IR_PHI, type, LCA_SV_EMPTY);
    assert(phi != NULL);

    lyir_builder_insert(builder, phi);
    return phi;
}

lyir_value* layec_build_unary(lyir_builder* builder, lyir_location location, lyir_value_kind kind, lyir_value* operand, lyir_type* type) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(operand != NULL);
    assert(type != NULL);

    lyir_value* unary = layec_value_create(builder->function->module, location, kind, type, LCA_SV_EMPTY);
    assert(unary != NULL);
    unary->operand = operand;

    lyir_builder_insert(builder, unary);
    return unary;
}

lyir_value* layec_build_bitcast(lyir_builder* builder, lyir_location location, lyir_value* value, lyir_type* type) {
    return layec_build_unary(builder, location, LYIR_IR_BITCAST, value, type);
}

lyir_value* layec_build_sign_extend(lyir_builder* builder, lyir_location location, lyir_value* value, lyir_type* type) {
    return layec_build_unary(builder, location, LYIR_IR_SEXT, value, type);
}

lyir_value* layec_build_zero_extend(lyir_builder* builder, lyir_location location, lyir_value* value, lyir_type* type) {
    return layec_build_unary(builder, location, LYIR_IR_ZEXT, value, type);
}

lyir_value* layec_build_truncate(lyir_builder* builder, lyir_location location, lyir_value* value, lyir_type* type) {
    return layec_build_unary(builder, location, LYIR_IR_TRUNC, value, type);
}

static lyir_value* layec_build_binary(lyir_builder* builder, lyir_location location, lyir_value_kind kind, lyir_value* lhs, lyir_value* rhs, lyir_type* type) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(lhs != NULL);
    lyir_type* lhs_type = lyir_value_type_get(lhs);
    assert(rhs != NULL);
    lyir_type* rhs_type = lyir_value_type_get(rhs);
    assert(lhs_type == rhs_type); // primitive types should be reference equal if done correctly

    lyir_value* cmp = layec_value_create(builder->function->module, location, kind, type, LCA_SV_EMPTY);
    assert(cmp != NULL);
    cmp->binary.lhs = lhs;
    cmp->binary.rhs = rhs;

    lyir_builder_insert(builder, cmp);
    return cmp;
}

lyir_value* layec_build_icmp_eq(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_ICMP_EQ, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_icmp_ne(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_ICMP_NE, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_icmp_slt(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_ICMP_SLT, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_icmp_ult(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_ICMP_ULT, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_icmp_sle(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_ICMP_SLE, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_icmp_ule(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_ICMP_ULE, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_icmp_sgt(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_ICMP_SGT, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_icmp_ugt(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_ICMP_UGT, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_icmp_sge(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_ICMP_SGE, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_icmp_uge(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_ICMP_UGE, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_fcmp_false(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_FCMP_FALSE, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_fcmp_oeq(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_FCMP_OEQ, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_fcmp_ogt(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_FCMP_OGT, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_fcmp_oge(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_FCMP_OGE, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_fcmp_olt(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_FCMP_OLT, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_fcmp_ole(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_FCMP_OLE, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_fcmp_one(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_FCMP_ONE, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_fcmp_ord(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_FCMP_ORD, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_fcmp_ueq(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_FCMP_UEQ, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_fcmp_ugt(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_FCMP_UGT, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_fcmp_uge(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_FCMP_UGE, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_fcmp_ult(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_FCMP_ULT, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_fcmp_ule(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_FCMP_ULE, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_fcmp_une(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_FCMP_UNE, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_fcmp_uno(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_FCMP_UNO, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_fcmp_true(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_FCMP_TRUE, lhs, rhs, lyir_int_type(builder->context, 1));
}

lyir_value* layec_build_add(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_ADD, lhs, rhs, lhs->type);
}

lyir_value* layec_build_fadd(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_FADD, lhs, rhs, lhs->type);
}

lyir_value* layec_build_sub(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_SUB, lhs, rhs, lhs->type);
}

lyir_value* layec_build_fsub(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_FSUB, lhs, rhs, lhs->type);
}

lyir_value* layec_build_mul(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_MUL, lhs, rhs, lhs->type);
}

lyir_value* layec_build_fmul(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_FMUL, lhs, rhs, lhs->type);
}

lyir_value* layec_build_sdiv(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_SDIV, lhs, rhs, lhs->type);
}

lyir_value* layec_build_udiv(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_UDIV, lhs, rhs, lhs->type);
}

lyir_value* layec_build_fdiv(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_FDIV, lhs, rhs, lhs->type);
}

lyir_value* layec_build_smod(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_SMOD, lhs, rhs, lhs->type);
}

lyir_value* layec_build_umod(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_UMOD, lhs, rhs, lhs->type);
}

lyir_value* layec_build_fmod(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_FMOD, lhs, rhs, lhs->type);
}

lyir_value* layec_build_and(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_AND, lhs, rhs, lhs->type);
}

lyir_value* layec_build_or(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_OR, lhs, rhs, lhs->type);
}

lyir_value* layec_build_xor(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_XOR, lhs, rhs, lhs->type);
}

lyir_value* layec_build_shl(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_SHL, lhs, rhs, lhs->type);
}

lyir_value* layec_build_shr(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_SHR, lhs, rhs, lhs->type);
}

lyir_value* layec_build_sar(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs) {
    return layec_build_binary(builder, location, LYIR_IR_SAR, lhs, rhs, lhs->type);
}

lyir_value* layec_build_neg(lyir_builder* builder, lyir_location location, lyir_value* operand) {
    return layec_build_unary(builder, location, LYIR_IR_NEG, operand, operand->type);
}

lyir_value* layec_build_compl(lyir_builder* builder, lyir_location location, lyir_value* operand) {
    return layec_build_unary(builder, location, LYIR_IR_COMPL, operand, operand->type);
}

lyir_value* layec_build_fptoui(lyir_builder* builder, lyir_location location, lyir_value* operand, lyir_type* to) {
    return layec_build_unary(builder, location, LYIR_IR_FPTOUI, operand, to);
}

lyir_value* layec_build_fptosi(lyir_builder* builder, lyir_location location, lyir_value* operand, lyir_type* to) {
    return layec_build_unary(builder, location, LYIR_IR_FPTOSI, operand, to);
}

lyir_value* layec_build_uitofp(lyir_builder* builder, lyir_location location, lyir_value* operand, lyir_type* to) {
    return layec_build_unary(builder, location, LYIR_IR_UITOFP, operand, to);
}

lyir_value* layec_build_sitofp(lyir_builder* builder, lyir_location location, lyir_value* operand, lyir_type* to) {
    return layec_build_unary(builder, location, LYIR_IR_SITOFP, operand, to);
}

lyir_value* layec_build_fpext(lyir_builder* builder, lyir_location location, lyir_value* operand, lyir_type* to) {
    return layec_build_unary(builder, location, LYIR_IR_FPEXT, operand, to);
}

lyir_value* layec_build_fptrunc(lyir_builder* builder, lyir_location location, lyir_value* operand, lyir_type* to) {
    return layec_build_unary(builder, location, LYIR_IR_FPTRUNC, operand, to);
}

static lyir_value* layec_build_builtin(lyir_builder* builder, lyir_location location, lyir_builtin_kind kind) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);

    lyir_value* builtin = layec_value_create(builder->function->module, location, LYIR_IR_BUILTIN, lyir_void_type(builder->context), LCA_SV_EMPTY);
    assert(builtin != NULL);
    builtin->builtin.kind = kind;

    lyir_builder_insert(builder, builtin);
    return builtin;
}

lyir_value* layec_build_builtin_memset(lyir_builder* builder, lyir_location location, lyir_value* address, lyir_value* value, lyir_value* count) {
    lyir_value* builtin = layec_build_builtin(builder, location, LYIR_BUILTIN_MEMSET);
    assert(builtin != NULL);
    lca_da_push(builtin->builtin.arguments, address);
    lca_da_push(builtin->builtin.arguments, value);
    lca_da_push(builtin->builtin.arguments, count);
    return builtin;
}

lyir_value* layec_build_builtin_memcpy(lyir_builder* builder, lyir_location location, lyir_value* source_address, lyir_value* dest_address, lyir_value* count) {
    lyir_value* builtin = layec_build_builtin(builder, location, LYIR_BUILTIN_MEMSET);
    assert(builtin != NULL);
    lca_da_push(builtin->builtin.arguments, source_address);
    lca_da_push(builtin->builtin.arguments, dest_address);
    lca_da_push(builtin->builtin.arguments, count);
    return builtin;
}

lyir_value* layec_build_ptradd(lyir_builder* builder, lyir_location location, lyir_value* address, lyir_value* offset_value) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(address != NULL);
    assert(lyir_type_is_ptr(lyir_value_type_get(address)));
    assert(offset_value != NULL);
    assert(lyir_type_is_integer(lyir_value_type_get(offset_value)));

    lyir_value* ptradd = layec_value_create(builder->function->module, location, LYIR_IR_PTRADD, lyir_ptr_type(builder->context), LCA_SV_EMPTY);
    assert(ptradd != NULL);
    ptradd->address = address;
    ptradd->operand = offset_value;

    lyir_builder_insert(builder, ptradd);
    return ptradd;
}

// IR Printer

#define COL_COMMENT  WHITE
#define COL_DELIM    WHITE
#define COL_KEYWORD  RED
#define COL_NAME     GREEN
#define COL_CONSTANT BLUE

typedef struct layec_print_context {
    lyir_context* context;
    bool use_color;
    lca_string* output;
} layec_print_context;

static void layec_global_print(layec_print_context* print_context, lyir_value* global);
static void layec_function_print(layec_print_context* print_context, lyir_value* function);
static void layec_type_print_struct_type_to_string_literally(lyir_type* type, lca_string* s, bool use_color);

lca_string lyir_module_print(lyir_module* module, bool use_color) {
    assert(module != NULL);
    assert(module->context != NULL);

    lca_string output_string = lca_string_create(module->context->allocator);

    layec_print_context print_context = {
        .context = module->context,
        .use_color = use_color,
        .output = &output_string,
    };

    // bool use_color = print_context.use_color;

    lca_string_append_format(
        print_context.output,
        "%s; LayeC IR Module: %.*s%s\n",
        COL(COL_COMMENT),
        LCA_STR_EXPAND(module->name),
        COL(RESET)
    );

    for (int64_t i = 0; i < lyir_context_get_struct_type_count(module->context); i++) {
        lyir_type* struct_type = lyir_context_get_struct_type_at_index(module->context, i);
        if (lyir_type_struct_is_named(struct_type)) {
            lca_string_append_format(print_context.output, "%sdefine %s%.*s %s= ", COL(COL_KEYWORD), COL(COL_NAME), LCA_STR_EXPAND(lyir_type_struct_name_get(struct_type)), COL(RESET));
            layec_type_print_struct_type_to_string_literally(struct_type, print_context.output, use_color);
            lca_string_append_format(print_context.output, "%s\n", COL(RESET));
        }
    }

    for (int64_t i = 0, count = lca_da_count(module->globals); i < count; i++) {
        if (i > 0) lca_string_append_format(print_context.output, "\n");
        layec_global_print(&print_context, module->globals[i]);
    }

    if (lca_da_count(module->globals) > 0) lca_string_append_format(print_context.output, "\n");

    for (int64_t i = 0, count = lca_da_count(module->functions); i < count; i++) {
        if (i > 0) lca_string_append_format(print_context.output, "\n");
        layec_function_print(&print_context, module->functions[i]);
    }

    return output_string;
}

static const char* ir_calling_convention_to_cstring(lyir_calling_convention calling_convention) {
    switch (calling_convention) {
        case LYIR_DEFAULTCC: assert(false && "default calling convention is not valid within the IR");
        case LYIR_CCC: return "ccc";
        case LYIR_LAYECC: return "layecc";
    }

    assert(false && "unsupported/unimplemented calling convetion");
    return "";
}

static void layec_instruction_print_name_if_required(layec_print_context* print_context, lyir_value* instruction) {
    assert(print_context != NULL);
    assert(print_context->context != NULL);
    assert(instruction != NULL);

    bool use_color = print_context->use_color;

    if (lyir_type_is_void(instruction->type)) {
        return;
    }

    if (instruction->name.count == 0) {
        lca_string_append_format(
            print_context->output,
            "%s%%%lld %s= %s",
            COL(COL_NAME),
            instruction->index,
            COL(COL_DELIM),
            COL(RESET)
        );
    } else {
        lca_string_append_format(
            print_context->output,
            "%s%%%.*s %s= %s",
            COL(COL_NAME),
            LCA_STR_EXPAND(instruction->name),
            COL(COL_DELIM),
            COL(RESET)
        );
    }
}

static void layec_instruction_print(layec_print_context* print_context, lyir_value* instruction) {
    assert(print_context != NULL);
    assert(print_context->context != NULL);
    assert(instruction != NULL);

    bool use_color = print_context->use_color;

    lca_string_append_format(print_context->output, "  ");
    layec_instruction_print_name_if_required(print_context, instruction);

    switch (instruction->kind) {
        default: {
            fprintf(stderr, "for value kind %s\n", lyir_value_kind_to_cstring(instruction->kind));
            assert(false && "todo layec_instruction_print");
        } break;

        case LYIR_IR_NOP: {
            lca_string_append_format(print_context->output, "%snop", COL(COL_KEYWORD));
        } break;

        case LYIR_IR_ALLOCA: {
            lca_string_append_format(print_context->output, "%salloca ", COL(COL_KEYWORD));
            lyir_type_print_to_string(instruction->alloca.element_type, print_context->output, use_color);
            if (instruction->alloca.element_count != 1) {
                lca_string_append_format(print_context->output, "%s, %s%lld", COL(COL_DELIM), COL(COL_CONSTANT), instruction->alloca.element_count);
            }
        } break;

        case LYIR_IR_STORE: {
            lca_string_append_format(print_context->output, "%sstore ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->address, print_context->output, false, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->operand, print_context->output, true, use_color);
        } break;

        case LYIR_IR_LOAD: {
            lca_string_append_format(print_context->output, "%sload ", COL(COL_KEYWORD));
            lyir_type_print_to_string(instruction->type, print_context->output, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->address, print_context->output, false, use_color);
        } break;

        case LYIR_IR_BRANCH: {
            lca_string_append_format(print_context->output, "%sbranch ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->branch.pass, print_context->output, false, use_color);
        } break;

        case LYIR_IR_COND_BRANCH: {
            lca_string_append_format(print_context->output, "%sbranch ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->operand, print_context->output, false, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->branch.pass, print_context->output, false, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->branch.fail, print_context->output, false, use_color);
        } break;

        case LYIR_IR_PHI: {
            lca_string_append_format(print_context->output, "%sphi ", COL(COL_KEYWORD));
            lyir_type_print_to_string(instruction->type, print_context->output, use_color);

            for (int64_t i = 0, count = lyir_value_phi_incoming_value_count_get(instruction); i < count; i++) {
                if (i > 0) lca_string_append_format(print_context->output, "%s,", COL(RESET));
                lca_string_append_format(print_context->output, "%s [ ", COL(RESET));
                lyir_value_print_to_string(lyir_phi_incoming_value_get_at_index(instruction, i), print_context->output, false, use_color);
                lca_string_append_format(print_context->output, "%s, ", COL(RESET));
                lyir_value_print_to_string(lyir_phi_incoming_block_get_at_index(instruction, i), print_context->output, false, use_color);
                lca_string_append_format(print_context->output, "%s ]", COL(RESET));
            }
        } break;

        case LYIR_IR_RETURN: {
            lca_string_append_format(print_context->output, "%sreturn", COL(COL_KEYWORD));
            if (instruction->return_value != NULL) {
                lca_string_append_format(print_context->output, " ", COL(COL_KEYWORD));
                lyir_value_print_to_string(instruction->return_value, print_context->output, true, use_color);
            }
        } break;

        case LYIR_IR_UNREACHABLE: {
            lca_string_append_format(print_context->output, "%sunreachable", COL(COL_KEYWORD));
        } break;

        case LYIR_IR_CALL: {
            lca_string_append_format(print_context->output, "%s%scall %s ", COL(COL_KEYWORD), (instruction->call.is_tail_call ? "tail " : ""), ir_calling_convention_to_cstring(instruction->call.calling_convention));
            lyir_type_print_to_string(instruction->type, print_context->output, use_color);
            lca_string_append_format(print_context->output, " ");
            lyir_value_print_to_string(instruction->call.callee, print_context->output, false, use_color);
            lca_string_append_format(print_context->output, "%s(", COL(COL_DELIM));

            for (int64_t i = 0, count = lca_da_count(instruction->call.arguments); i < count; i++) {
                if (i > 0) {
                    lca_string_append_format(print_context->output, "%s, ", COL(COL_DELIM));
                }

                lyir_value* argument = instruction->call.arguments[i];
                lyir_value_print_to_string(argument, print_context->output, true, use_color);
            }

            lca_string_append_format(print_context->output, "%s)", COL(COL_DELIM));
        } break;

        case LYIR_IR_BUILTIN: {
            const char* builtin_name = "";
            switch (instruction->builtin.kind) {
                default: builtin_name = "unknown"; break;
                case LYIR_BUILTIN_MEMSET: builtin_name = "memset"; break;
                case LYIR_BUILTIN_MEMCOPY: builtin_name = "memcopy"; break;
            }

            lca_string_append_format(print_context->output, "%sbuiltin ", COL(COL_KEYWORD));
            lca_string_append_format(print_context->output, "%s@%s%s(", COL(COL_NAME), builtin_name, COL(COL_DELIM));

            for (int64_t i = 0, count = lca_da_count(instruction->builtin.arguments); i < count; i++) {
                if (i > 0) {
                    lca_string_append_format(print_context->output, "%s, ", COL(COL_DELIM));
                }

                lyir_value* argument = instruction->builtin.arguments[i];
                lyir_value_print_to_string(argument, print_context->output, true, use_color);
            }

            lca_string_append_format(print_context->output, "%s)", COL(COL_DELIM));
        } break;

        case LYIR_IR_BITCAST: {
            lca_string_append_format(print_context->output, "%sbitcast ", COL(COL_KEYWORD));
            lyir_type_print_to_string(instruction->type, print_context->output, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->operand, print_context->output, true, use_color);
        } break;

        case LYIR_IR_SEXT: {
            lca_string_append_format(print_context->output, "%ssext ", COL(COL_KEYWORD));
            lyir_type_print_to_string(instruction->type, print_context->output, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->operand, print_context->output, true, use_color);
        } break;

        case LYIR_IR_ZEXT: {
            lca_string_append_format(print_context->output, "%szext ", COL(COL_KEYWORD));
            lyir_type_print_to_string(instruction->type, print_context->output, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->operand, print_context->output, true, use_color);
        } break;

        case LYIR_IR_TRUNC: {
            lca_string_append_format(print_context->output, "%strunc ", COL(COL_KEYWORD));
            lyir_type_print_to_string(instruction->type, print_context->output, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->operand, print_context->output, true, use_color);
        } break;

        case LYIR_IR_FPEXT: {
            lca_string_append_format(print_context->output, "%sfpext ", COL(COL_KEYWORD));
            lyir_type_print_to_string(instruction->type, print_context->output, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->operand, print_context->output, true, use_color);
        } break;

        case LYIR_IR_NEG: {
            lca_string_append_format(print_context->output, "%sneg ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->operand, print_context->output, true, use_color);
        } break;

        case LYIR_IR_COMPL: {
            lca_string_append_format(print_context->output, "%scompl ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->operand, print_context->output, true, use_color);
        } break;

        case LYIR_IR_ADD: {
            lca_string_append_format(print_context->output, "%sadd ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_FADD: {
            lca_string_append_format(print_context->output, "%sfadd ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_SUB: {
            lca_string_append_format(print_context->output, "%ssub ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_FSUB: {
            lca_string_append_format(print_context->output, "%sfsub ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_MUL: {
            lca_string_append_format(print_context->output, "%smul ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_FMUL: {
            lca_string_append_format(print_context->output, "%sfmul ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_SDIV: {
            lca_string_append_format(print_context->output, "%ssdiv ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_UDIV: {
            lca_string_append_format(print_context->output, "%sudiv ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_FDIV: {
            lca_string_append_format(print_context->output, "%sfdiv ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_SMOD: {
            lca_string_append_format(print_context->output, "%ssmod ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_UMOD: {
            lca_string_append_format(print_context->output, "%sumod ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_FMOD: {
            lca_string_append_format(print_context->output, "%sfmod ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_AND: {
            lca_string_append_format(print_context->output, "%sand ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_OR: {
            lca_string_append_format(print_context->output, "%sor ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_XOR: {
            lca_string_append_format(print_context->output, "%sxor ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_SHL: {
            lca_string_append_format(print_context->output, "%sshl ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_SHR: {
            lca_string_append_format(print_context->output, "%sshr ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_SAR: {
            lca_string_append_format(print_context->output, "%ssar ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_ICMP_EQ: {
            lca_string_append_format(print_context->output, "%sicmp eq ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_ICMP_NE: {
            lca_string_append_format(print_context->output, "%sicmp ne ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_ICMP_SLT: {
            lca_string_append_format(print_context->output, "%sicmp slt ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_ICMP_ULT: {
            lca_string_append_format(print_context->output, "%sicmp ult ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_ICMP_SLE: {
            lca_string_append_format(print_context->output, "%sicmp sle ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_ICMP_ULE: {
            lca_string_append_format(print_context->output, "%sicmp ule ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_ICMP_SGT: {
            lca_string_append_format(print_context->output, "%sicmp sgt ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_ICMP_UGT: {
            lca_string_append_format(print_context->output, "%sicmp ugt ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_ICMP_SGE: {
            lca_string_append_format(print_context->output, "%sicmp sge ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_ICMP_UGE: {
            lca_string_append_format(print_context->output, "%sicmp uge ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_FCMP_FALSE: {
            lca_string_append_format(print_context->output, "%sfcmp false ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_FCMP_OEQ: {
            lca_string_append_format(print_context->output, "%sfcmp oeq ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_FCMP_OGT: {
            lca_string_append_format(print_context->output, "%sfcmp ogt ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_FCMP_OGE: {
            lca_string_append_format(print_context->output, "%sfcmp oge ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_FCMP_OLT: {
            lca_string_append_format(print_context->output, "%sfcmp olt ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_FCMP_OLE: {
            lca_string_append_format(print_context->output, "%sfcmp ole ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_FCMP_ONE: {
            lca_string_append_format(print_context->output, "%sfcmp one ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_FCMP_ORD: {
            lca_string_append_format(print_context->output, "%sfcmp ord ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_FCMP_UEQ: {
            lca_string_append_format(print_context->output, "%sfcmp ueq ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_FCMP_UGT: {
            lca_string_append_format(print_context->output, "%sfcmp ugt ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_FCMP_UGE: {
            lca_string_append_format(print_context->output, "%sfcmp uge ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_FCMP_ULT: {
            lca_string_append_format(print_context->output, "%sfcmp ult ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_FCMP_ULE: {
            lca_string_append_format(print_context->output, "%sfcmp ule ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_FCMP_UNE: {
            lca_string_append_format(print_context->output, "%sfcmp une ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_FCMP_UNO: {
            lca_string_append_format(print_context->output, "%sfcmp uno ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_FCMP_TRUE: {
            lca_string_append_format(print_context->output, "%sfcmp true ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LYIR_IR_PTRADD: {
            lca_string_append_format(print_context->output, "%sptradd ptr ", COL(COL_KEYWORD));
            lyir_value_print_to_string(instruction->address, print_context->output, false, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            lyir_value_print_to_string(instruction->operand, print_context->output, true, use_color);
        } break;
    }

    lca_string_append_format(print_context->output, "%s\n", COL(RESET));
}

static void layec_print_linkage(layec_print_context* print_context, lyir_linkage linkage) {
    assert(print_context != NULL);
    assert(print_context->context != NULL);

    bool use_color = print_context->use_color;

    switch (linkage) {
        default: break;

        case LYIR_LINK_EXPORTED: {
            lca_string_append_format(print_context->output, "%sexported ", COL(COL_KEYWORD));
        } break;

        case LYIR_LINK_REEXPORTED: {
            lca_string_append_format(print_context->output, "%sreexported ", COL(COL_KEYWORD));
        } break;
    }
}

static void layec_global_print(layec_print_context* print_context, lyir_value* global) {
    assert(print_context != NULL);
    assert(print_context->context != NULL);
    assert(global != NULL);
    assert(lyir_type_is_ptr(global->type));

    lyir_type* global_type = global->alloca.element_type;
    assert(global_type != NULL);

    bool use_color = print_context->use_color;

    lca_string_append_format(print_context->output, "%sdefine ", COL(COL_KEYWORD));
    layec_print_linkage(print_context, global->linkage);

    if (global->name.count == 0) {
        lca_string_append_format(print_context->output, "%sglobal.%lld", COL(COL_NAME), global->index);
    } else {
        lca_string_append_format(print_context->output, "%s%.*s", COL(COL_NAME), LCA_STR_EXPAND(global->name));
    }

    lca_string_append_format(print_context->output, " %s= ", COL(COL_DELIM));
    lyir_value_print_to_string(global->operand, print_context->output, true, use_color);

    lca_string_append_format(print_context->output, "%s\n", COL(RESET));
}

static void layec_function_print(layec_print_context* print_context, lyir_value* function) {
    assert(print_context != NULL);
    assert(print_context->context != NULL);
    assert(function != NULL);

    lyir_type* function_type = function->type;
    assert(function_type != NULL);
    assert(lyir_type_is_function(function_type));

    lyir_type* return_type = function_type->function.return_type;
    assert(return_type != NULL);

    lca_da(lyir_type*) parameter_types = function_type->function.parameter_types;

    bool use_color = print_context->use_color;
    bool is_declare = lca_da_count(function->function.blocks) == 0;

    lca_string_append_format(print_context->output, "%s%s ", COL(COL_KEYWORD), is_declare ? "declare" : "define");
    layec_print_linkage(print_context, function->linkage);

    lca_string_append_format(
        print_context->output,
        "%s %s%.*s%s(",
        ir_calling_convention_to_cstring(function->type->function.calling_convention),
        COL(COL_NAME),
        LCA_STR_EXPAND(function->function.name),
        COL(COL_DELIM)
    );

    for (int64_t i = 0, count = lca_da_count(parameter_types); i < count; i++) {
        if (i > 0) lca_string_append_format(print_context->output, "%s, ", COL(COL_DELIM));
        lyir_type_print_to_string(parameter_types[i], print_context->output, use_color);
        lca_string_append_format(print_context->output, " %s%%%lld", COL(COL_NAME), i);
    }

    lca_string_append_format(print_context->output, "%s)", COL(COL_DELIM));

    if (function_type->function.is_variadic) {
        lca_string_append_format(print_context->output, " %svariadic", COL(COL_KEYWORD));
    }

    if (!lyir_type_is_void(return_type)) {
        lca_string_append_format(print_context->output, " %s-> ", COL(COL_DELIM));
        lyir_type_print_to_string(return_type, print_context->output, use_color);
    }

    lca_string_append_format(
        print_context->output,
        "%s%s%s\n",
        COL(COL_DELIM),
        is_declare ? "" : " {",
        COL(RESET)
    );

    if (!is_declare) {
        for (int64_t i = 0, count = lca_da_count(function->function.blocks); i < count; i++) {
            lyir_value* block = function->function.blocks[i];
            assert(block != NULL);
            assert(lyir_value_is_block(block));

            if (block->block.name.count == 0) {
                lca_string_append_format(print_context->output, "%s_bb%lld%s:\n", COL(COL_NAME), i, COL(COL_DELIM));
            } else {
                lca_string_append_format(print_context->output, "%s%.*s%s:\n", COL(COL_NAME), LCA_STR_EXPAND(block->block.name), COL(COL_DELIM));
            }

            for (int64_t j = 0, count2 = lca_da_count(block->block.instructions); j < count2; j++) {
                lyir_value* instruction = block->block.instructions[j];
                assert(instruction != NULL);
                assert(lyir_value_is_instruction(instruction));

                layec_instruction_print(print_context, instruction);
            }
        }

        lca_string_append_format(print_context->output, "%s}%s\n", COL(COL_DELIM), COL(RESET));
    }
}

static void layec_type_print_struct_type_to_string_literally(lyir_type* type, lca_string* s, bool use_color) {
    lca_string_append_format(s, "%sstruct %s{", COL(COL_KEYWORD), COL(RESET));

    for (int64_t i = 0, count = lca_da_count(type->_struct.members); i < count; i++) {
        if (i > 0) {
            lca_string_append_format(s, "%s, ", COL(RESET));
        } else {
            lca_string_append_format(s, " ");
        }

        lyir_type* member_type = type->_struct.members[i].type;
        lyir_type_print_to_string(member_type, s, use_color);
    }

    lca_string_append_format(s, "%s }", COL(RESET));
}

void lyir_type_print_to_string(lyir_type* type, lca_string* s, bool use_color) {
    assert(type != NULL);
    assert(s != NULL);

    switch (type->kind) {
        default: {
            fprintf(stderr, "for type %s\n", lyir_type_kind_to_cstring(type->kind));
            assert(false && "unhandled type in lyir_type_print_to_string");
        } break;

        case LYIR_TYPE_POINTER: {
            lca_string_append_format(s, "%sptr", COL(COL_KEYWORD));
        } break;

        case LYIR_TYPE_VOID: {
            lca_string_append_format(s, "%svoid", COL(COL_KEYWORD));
        } break;

        case LYIR_TYPE_INTEGER: {
            lca_string_append_format(s, "%sint%d", COL(COL_KEYWORD), type->primitive_bit_width);
        } break;

        case LYIR_TYPE_FLOAT: {
            lca_string_append_format(s, "%sfloat%d", COL(COL_KEYWORD), type->primitive_bit_width);
        } break;

        case LYIR_TYPE_ARRAY: {
            lyir_type_print_to_string(type->array.element_type, s, use_color);
            lca_string_append_format(s, "%s[%s%lld%s]", COL(COL_DELIM), COL(COL_CONSTANT), type->array.length, COL(COL_DELIM));
        } break;

        case LYIR_TYPE_STRUCT: {
            if (type->_struct.named) {
                lca_string_append_format(s, "%s@%.*s", COL(COL_NAME), LCA_STR_EXPAND(type->_struct.name));
            } else {
                layec_type_print_struct_type_to_string_literally(type, s, use_color);
            }
        } break;
    }

    lca_string_append_format(s, "%s", COL(RESET));
}

void lyir_value_print_to_string(lyir_value* value, lca_string* s, bool print_type, bool use_color) {
    assert(value != NULL);
    assert(s != NULL);

    if (print_type) {
        lyir_type_print_to_string(value->type, s, use_color);
        lca_string_append_format(s, "%s ", COL(RESET));
    }

    switch (value->kind) {
        default: {
            if (value->name.count == 0) {
                lca_string_append_format(s, "%s%%%lld", COL(COL_NAME), value->index);
            } else {
                lca_string_append_format(s, "%s%%%.*s", COL(COL_NAME), LCA_STR_EXPAND(value->name));
            }
        } break;

        case LYIR_IR_FUNCTION: {
            lca_string_append_format(s, "%s@%.*s", COL(COL_NAME), LCA_STR_EXPAND(value->function.name));
        } break;

        case LYIR_IR_BLOCK: {
            if (value->block.name.count == 0) {
                lca_string_append_format(s, "%s%%_bb%lld", COL(COL_NAME), value->block.index);
            } else {
                lca_string_append_format(s, "%s%%%.*s", COL(COL_NAME), LCA_STR_EXPAND(value->block.name));
            }
        } break;

        case LYIR_IR_INTEGER_CONSTANT: {
            lca_string_append_format(s, "%s%lld", COL(COL_CONSTANT), value->int_value);
        } break;

        case LYIR_IR_FLOAT_CONSTANT: {
            lca_string_append_format(s, "%s%f", COL(COL_CONSTANT), value->float_value);
        } break;

        case LYIR_IR_GLOBAL_VARIABLE: {
            if (value->name.count == 0) {
                lca_string_append_format(s, "%s@global.%lld", COL(COL_NAME), value->index);
            } else {
                lca_string_append_format(s, "%s@%.*s", COL(COL_NAME), LCA_STR_EXPAND(value->name));
            }
        } break;

        case LYIR_IR_ARRAY_CONSTANT: {
            if (value->array.is_string_literal) {
                lca_string_append_format(s, "%s\"", COL(COL_CONSTANT));
                for (int64_t i = 0; i < value->array.length; i++) {
                    uint8_t c = (uint8_t)value->array.data[i];
                    if (c < 32 || c > 127) {
                        lca_string_append_format(s, "\\%02X", (int)c);
                    } else {
                        lca_string_append_format(s, "%c", c);
                    }
                }
                lca_string_append_format(s, "\"");
            } else {
                assert(false && "todo lyir_value_print_to_string non-string arrays");
            }
        } break;
    }

    lca_string_append_format(s, "%s", COL(RESET));
}
