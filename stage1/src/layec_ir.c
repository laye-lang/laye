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

#include <assert.h>

#include "layec.h"

void layec_value_destroy(layec_value* value);

struct layec_module {
    layec_context* context;
    string_view name;

    lca_arena* arena;
    dynarr(layec_value*) functions;
    dynarr(layec_value*) globals;

    dynarr(layec_value*) _all_values;
};

struct layec_type {
    layec_type_kind kind;
    layec_context* context;

    union {
        int primitive_bit_width;

        struct {
            layec_type* element_type;
            int64_t length;
        } array;

        struct {
            layec_type* return_type;
            dynarr(layec_type*) parameter_types;
            layec_calling_convention calling_convention;
            bool is_variadic;
        } function;

        struct {
            dynarr(layec_type*) members;
            bool named;

            union {
                string_view name;
                int64_t index;
            };
        } _struct;
    };
};

typedef struct layec_incoming_value {
    layec_value* value;
    layec_value* block;
} layec_incoming_value;

struct layec_value {
    layec_value_kind kind;
    layec_location location;
    layec_module* module;
    layec_context* context;

    layec_type* type;

    string_view name;
    int64_t index;
    layec_linkage linkage;

    // for values which want their uses tracked, a list of all users of this value.
    dynarr(layec_value*) users;

    layec_value* parent_block;

    layec_value* address;
    layec_value* operand;
    layec_value* value;

    union {
        int64_t int_value;
        double float_value;

        struct {
            layec_builtin_kind kind;
            dynarr(layec_value*) arguments;
        } builtin;

        struct {
            bool is_string_literal;
            char* data;
            int64_t length;
        } array;

        struct {
            string_view name;
            int64_t index;
            layec_value* parent_function;
            dynarr(layec_value*) instructions;
        } block;

        struct {
            string_view name;
            dynarr(layec_value*) parameters;
            dynarr(layec_value*) blocks;
        } function;

        int64_t parameter_index;

        struct {
            layec_type* element_type;
            int64_t element_count;
        } alloca;

        layec_value* return_value;

        struct {
            layec_value* lhs;
            layec_value* rhs;
        } binary;

        struct {
            layec_value* pass;
            layec_value* fail;
        } branch;

        dynarr(layec_incoming_value) incoming_values;

        struct {
            layec_value* callee;
            // we may need to store a separate callee type, since opaque pointers are a thing
            // and we may be calling through a function pointer, for example
            layec_type* callee_type;
            layec_calling_convention calling_convention;
            dynarr(layec_value*) arguments;
            bool is_tail_call : 1;
        } call;
    };
};

struct layec_builder {
    layec_context* context;

    layec_value* function;
    layec_value* block;
    int64_t insert_index;
};

int64_t layec_context_get_struct_type_count(layec_context* context) {
    assert(context != NULL);
    return arr_count(context->_all_struct_types);
}

layec_type* layec_context_get_struct_type_at_index(layec_context* context, int64_t index) {
    assert(context != NULL);
    assert(index >= 0 && index < arr_count(context->_all_struct_types));
    return context->_all_struct_types[index].type;
}

layec_module* layec_module_create(layec_context* context, string_view module_name) {
    assert(context != NULL);
    layec_module* module = lca_allocate(context->allocator, sizeof *module);
    assert(module != NULL);
    module->context = context;
    module->name = layec_context_intern_string_view(context, module_name);
    module->arena = lca_arena_create(context->allocator, 1024 * sizeof(layec_value));
    assert(module->arena != NULL);
    return module;
}

void layec_module_destroy(layec_module* module) {
    if (module == NULL) return;
    assert(module->context != NULL);

    lca_allocator allocator = module->context->allocator;

    for (int64_t i = 0, count = arr_count(module->_all_values); i < count; i++) {
        layec_value_destroy(module->_all_values[i]);
    }

    assert(module->arena != NULL);
    lca_arena_destroy(module->arena);

    arr_free(module->globals);
    arr_free(module->functions);
    arr_free(module->_all_values);

    *module = (layec_module){0};
    lca_deallocate(allocator, module);
}

void layec_value_destroy(layec_value* value) {
    assert(value != NULL);

    switch (value->kind) {
        default: break;

        case LAYEC_IR_FUNCTION: {
            arr_free(value->function.parameters);
            arr_free(value->function.blocks);
        } break;

        case LAYEC_IR_BLOCK: {
            arr_free(value->block.instructions);
        } break;

        case LAYEC_IR_CALL: {
            arr_free(value->call.arguments);
        } break;

        case LAYEC_IR_PHI: {
            arr_free(value->incoming_values);
        } break;

        case LAYEC_IR_BUILTIN: {
            arr_free(value->builtin.arguments);
        } break;
    }
}

static layec_type* layec_type_create(layec_context* context, layec_type_kind kind) {
    assert(context != NULL);
    assert(context->type_arena != NULL);

    layec_type* type = lca_arena_push(context->type_arena, sizeof *type);
    assert(type != NULL);
    type->context = context;
    arr_push(context->_all_types, type);
    type->kind = kind;

    return type;
}

void layec_type_destroy(layec_type* type) {
    if (type == NULL) return;

    switch (type->kind) {
        default: break;

        case LAYEC_TYPE_ARRAY: {
            layec_type_destroy(type->array.element_type);
        } break;

        case LAYEC_TYPE_FUNCTION: {
            layec_type_destroy(type->function.return_type);
            for (int64_t i = 0, count = arr_count(type->function.parameter_types); i < count; i++) {
                layec_type_destroy(type->function.parameter_types[i]);
            }

            arr_free(type->function.parameter_types);
        } break;

        case LAYEC_TYPE_STRUCT: {
            for (int64_t i = 0, count = arr_count(type->_struct.members); i < count; i++) {
                layec_type_destroy(type->_struct.members[i]);
            }

            arr_free(type->_struct.members);
        } break;
    }
}

const char* layec_type_kind_to_cstring(layec_type_kind kind) {
    switch (kind) {
        default: return lca_temp_sprintf("<unknown %d>", (int)kind);
        case LAYEC_TYPE_VOID: return "VOID";
        case LAYEC_TYPE_ARRAY: return "ARRAY";
        case LAYEC_TYPE_FLOAT: return "FLOAT";
        case LAYEC_TYPE_FUNCTION: return "FUNCTION";
        case LAYEC_TYPE_INTEGER: return "INTEGER";
        case LAYEC_TYPE_POINTER: return "POINTER";
        case LAYEC_TYPE_STRUCT: return "STRUCT";
    }
}

static layec_value* layec_value_create(layec_module* module, layec_location location, layec_value_kind kind, layec_type* type, string_view name) {
    assert(module != NULL);
    assert(module->context != NULL);
    assert(module->arena != NULL);
    assert(type != NULL);

    layec_value* value = lca_arena_push(module->arena, sizeof *value);
    assert(value != NULL);
    value->kind = kind;
    value->module = module;
    value->context = module->context;
    value->location = location;
    value->type = type;
    arr_push(module->_all_values, value);

    return value;
}

layec_context* layec_module_context(layec_module* module) {
    assert(module != NULL);
    return module->context;
}

string_view layec_module_name(layec_module* module) {
    assert(module != NULL);
    return module->name;
}

int64_t layec_module_global_count(layec_module* module) {
    assert(module != NULL);
    return arr_count(module->globals);
}

layec_value* layec_module_get_global_at_index(layec_module* module, int64_t global_index) {
    assert(module != NULL);
    assert(global_index >= 0);
    assert(global_index < arr_count(module->globals));
    return module->globals[global_index];
}

int64_t layec_module_function_count(layec_module* module) {
    assert(module != NULL);
    return arr_count(module->functions);
}

layec_value* layec_module_get_function_at_index(layec_module* module, int64_t function_index) {
    assert(module != NULL);
    assert(function_index >= 0);
    assert(function_index < arr_count(module->functions));
    return module->functions[function_index];
}

layec_value* layec_module_create_global_string_ptr(layec_module* module, layec_location location, string_view string_value) {
    assert(module != NULL);
    assert(module->context != NULL);

    layec_type* array_type = layec_array_type(module->context, string_value.count + 1, layec_int_type(module->context, 8));
    
    char* data = lca_arena_push(module->arena, string_value.count + 1);
    memcpy(data, string_value.data, string_value.count);
    
    layec_value* array_constant = layec_array_constant(module->context, location, array_type, data, string_value.count + 1, true);

    layec_value* global_string_ptr = layec_value_create(module, location, LAYEC_IR_GLOBAL_VARIABLE, layec_ptr_type(module->context), SV_EMPTY);
    assert(global_string_ptr != NULL);
    global_string_ptr->index = arr_count(module->globals);
    global_string_ptr->linkage = LAYEC_LINK_INTERNAL;
    global_string_ptr->value = array_constant;
    global_string_ptr->alloca.element_type = array_type;
    global_string_ptr->alloca.element_count = 1;
    arr_push(module->globals, global_string_ptr);

    return global_string_ptr;
}

layec_type* layec_function_return_type(layec_value* function) {
    assert(function != NULL);
    assert(layec_value_is_function(function));
    assert(layec_type_is_function(function->type));
    return function->type->function.return_type;
}

int64_t layec_function_block_count(layec_value* function) {
    assert(function != NULL);
    assert(layec_value_is_function(function));
    return arr_count(function->function.blocks);
}

layec_value* layec_function_get_block_at_index(layec_value* function, int64_t block_index) {
    assert(function != NULL);
    assert(layec_value_is_function(function));
    assert(block_index >= 0);
    assert(block_index < arr_count(function->function.blocks));
    return function->function.blocks[block_index];
}

int64_t layec_function_parameter_count(layec_value* function) {
    assert(function != NULL);
    assert(layec_value_is_function(function));
    return arr_count(function->function.parameters);
}

layec_value* layec_function_get_parameter_at_index(layec_value* function, int64_t parameter_index) {
    assert(function != NULL);
    assert(layec_value_is_function(function));
    assert(parameter_index >= 0);
    assert(parameter_index < arr_count(function->function.parameters));
    return function->function.parameters[parameter_index];
}

bool layec_function_is_variadic(layec_value* function) {
    assert(function != NULL);
    assert(layec_value_is_function(function));
    assert(function->type != NULL);
    assert(layec_type_is_function(function->type));
    return function->type->function.is_variadic;
}

int64_t layec_block_instruction_count(layec_value* block) {
    assert(block != NULL);
    assert(layec_value_is_block(block));
    return arr_count(block->block.instructions);
}

layec_value* layec_block_get_instruction_at_index(layec_value* block, int64_t instruction_index) {
    assert(block != NULL);
    assert(layec_value_is_block(block));
    assert(instruction_index >= 0);
    assert(instruction_index < arr_count(block->block.instructions));
    return block->block.instructions[instruction_index];
}

bool layec_block_is_terminated(layec_value* block) {
    assert(block != NULL);
    assert(layec_value_is_block(block));

    if (arr_count(block->block.instructions) == 0) {
        return false;
    }

    return layec_value_is_terminating_instruction(*arr_back(block->block.instructions));
}

bool layec_value_is_terminating_instruction(layec_value* instruction) {
    assert(instruction != NULL);
    
    switch (instruction->kind) {
        default: return false;

        case LAYEC_IR_RETURN:
        case LAYEC_IR_BRANCH:
        case LAYEC_IR_COND_BRANCH:
        case LAYEC_IR_UNREACHABLE: {
            return true;
        }
    }
}

layec_value_kind layec_value_get_kind(layec_value* value) {
    assert(value != NULL);
    return value->kind;
}

layec_context* layec_value_context(layec_value* value) {
    assert(value != NULL);
    return value->context;
}

layec_location layec_value_location(layec_value* value) {
    assert(value != NULL);
    return value->location;
}

layec_linkage layec_value_linkage(layec_value* value) {
    assert(value != NULL);
    return value->linkage;
}

string_view layec_value_name(layec_value* value) {
    assert(value != NULL);
    return value->name;
}

int64_t layec_value_index(layec_value* value) {
    assert(value != NULL);
    return value->index;
}

bool layec_block_has_name(layec_value* block) {
    assert(block != NULL);
    assert(layec_value_is_block(block));
    return block->block.name.count != 0;
}

string_view layec_block_name(layec_value* block) {
    assert(block != NULL);
    assert(layec_value_is_block(block));
    return block->block.name;
}

int64_t layec_block_index(layec_value* block) {
    assert(block != NULL);
    assert(layec_value_is_block(block));
    return block->block.index;
}

string_view layec_function_name(layec_value* function) {
    assert(function != NULL);
    assert(layec_value_is_function(function));
    assert(function->function.name.count > 0);
    return function->function.name;
}

layec_builtin_kind layec_instruction_builtin_kind(layec_value* instruction) {
    assert(instruction != NULL);
    assert(instruction->kind == LAYEC_IR_BUILTIN);
    return instruction->builtin.kind;
}

bool layec_instruction_global_is_string(layec_value* global) {
    assert(global != NULL);
    assert(global->value != NULL);
    assert(global->value->kind == LAYEC_IR_ARRAY_CONSTANT);
    return global->value->array.is_string_literal;
}

bool layec_instruction_return_has_value(layec_value* _return) {
    assert(_return != NULL);
    assert(_return->kind == LAYEC_IR_RETURN);
    return _return->return_value != NULL;
}

layec_value* layec_instruction_return_value(layec_value* _return) {
    assert(_return != NULL);
    assert(_return->kind == LAYEC_IR_RETURN);
    assert(_return->return_value != NULL);
    return _return->return_value;
}

layec_type* layec_instruction_get_alloca_type(layec_value* alloca) {
    assert(alloca != NULL);
    assert(alloca->alloca.element_type != NULL);
    return alloca->alloca.element_type;
}

layec_value* layec_instruction_get_address(layec_value* instruction) {
    assert(instruction != NULL);
    assert(instruction->address != NULL);
    return instruction->address;
}

layec_value* layec_instruction_get_operand(layec_value* instruction) {
    assert(instruction != NULL);
    assert(instruction->operand != NULL);
    return instruction->operand;
}

layec_value* layec_instruction_binary_get_lhs(layec_value* instruction) {
    assert(instruction != NULL);
    assert(instruction->binary.lhs != NULL);
    return instruction->binary.lhs;
}

layec_value* layec_instruction_get_binary_rhs(layec_value* instruction) {
    assert(instruction != NULL);
    assert(instruction->binary.rhs != NULL);
    return instruction->binary.rhs;
}

layec_value* layec_instruction_get_value(layec_value* instruction) {
    assert(instruction != NULL);
    assert(instruction->value != NULL);
    return instruction->value;
}

layec_value* layec_instruction_branch_get_pass(layec_value* instruction) {
    assert(instruction != NULL);
    assert(instruction->branch.pass != NULL);
    return instruction->branch.pass;
}

layec_value* layec_instruction_branch_get_fail(layec_value* instruction) {
    assert(instruction != NULL);
    assert(instruction->branch.fail != NULL);
    return instruction->branch.fail;
}

layec_value* layec_instruction_callee(layec_value* call) {
    assert(call != NULL);
    assert(call->kind == LAYEC_IR_CALL);
    assert(call->call.callee != NULL);
    return call->call.callee;
}

int64_t layec_instruction_call_argument_count(layec_value* call) {
    assert(call != NULL);
    assert(call->kind == LAYEC_IR_CALL);
    return arr_count(call->call.arguments);
}

layec_value* layec_instruction_call_get_argument_at_index(layec_value* call, int64_t argument_index) {
    assert(call != NULL);
    assert(call->kind == LAYEC_IR_CALL);
    assert(argument_index >= 0);
    int64_t count = arr_count(call->call.arguments);
    assert(argument_index < count);
    layec_value* argument = call->call.arguments[argument_index];
    assert(argument != NULL);
    return argument;
}

int64_t layec_instruction_builtin_argument_count(layec_value* builtin) {
    assert(builtin != NULL);
    assert(builtin->kind == LAYEC_IR_BUILTIN);
    return arr_count(builtin->builtin.arguments);
}

layec_value* layec_instruction_builtin_get_argument_at_index(layec_value* builtin, int64_t argument_index) {
    assert(builtin != NULL);
    assert(builtin->kind == LAYEC_IR_BUILTIN);
    assert(argument_index >= 0);
    int64_t count = arr_count(builtin->builtin.arguments);
    assert(argument_index < count);
    layec_value* argument = builtin->builtin.arguments[argument_index];
    assert(argument != NULL);
    return argument;
}

void layec_instruction_phi_add_incoming_value(layec_value* phi, layec_value* value, layec_value* block) {
    assert(phi != NULL);
    assert(phi->kind == LAYEC_IR_PHI);
    assert(value != NULL);
    assert(block != NULL);
    assert(block->kind == LAYEC_IR_BLOCK);

    layec_incoming_value incoming_value = {
        .value = value,
        .block = block,
    };

    arr_push(phi->incoming_values, incoming_value);
}

int64_t layec_instruction_phi_incoming_value_count(layec_value* phi) {
    assert(phi != NULL);
    assert(phi->kind == LAYEC_IR_PHI);
    return arr_count(phi->incoming_values);
}

layec_value* layec_instruction_phi_incoming_value_at_index(layec_value* phi, int64_t index) {
    assert(phi != NULL);
    assert(phi->kind == LAYEC_IR_PHI);
    layec_value* value = phi->incoming_values[index].value;
    assert(value != NULL);
    return value;
}

layec_value* layec_instruction_phi_incoming_block_at_index(layec_value* phi, int64_t index) {
    assert(phi != NULL);
    assert(phi->kind == LAYEC_IR_PHI);
    layec_value* block = phi->incoming_values[index].block;
    assert(block != NULL);
    assert(block->kind == LAYEC_IR_BLOCK);
    return block;
}

layec_value* layec_instruction_ptradd_get_address(layec_value* ptradd) {
    assert(ptradd != NULL);
    assert(ptradd->kind == LAYEC_IR_PTRADD);
    assert(layec_type_is_ptr(ptradd->address->type));
    return ptradd->address;
}

layec_value* layec_instruction_ptradd_get_offset(layec_value* ptradd) {
    assert(ptradd != NULL);
    assert(ptradd->kind == LAYEC_IR_PTRADD);
    assert(layec_type_is_integer(ptradd->operand->type));
    return ptradd->operand;
}

int64_t layec_value_integer_constant(layec_value* value) {
    assert(value != NULL);
    assert(value->kind == LAYEC_IR_INTEGER_CONSTANT);
    return value->int_value;
}

double layec_value_float_constant(layec_value* value) {
    assert(value != NULL);
    assert(value->kind == LAYEC_IR_FLOAT_CONSTANT);
    return value->float_value;
}

const char* layec_value_kind_to_cstring(layec_value_kind kind) {
    switch (kind) {
        default: return "<unknown>";
        case LAYEC_IR_INVALID: return "INVALID";
        case LAYEC_IR_INTEGER_CONSTANT: return "INTEGER_CONSTANT";
        case LAYEC_IR_ARRAY_CONSTANT: return "ARRAY_CONSTANT";
        case LAYEC_IR_VOID_CONSTANT: return "VOID_CONSTANT";
        case LAYEC_IR_POISON: return "POISON";
        case LAYEC_IR_BLOCK: return "BLOCK";
        case LAYEC_IR_FUNCTION: return "FUNCTION";
        case LAYEC_IR_GLOBAL_VARIABLE: return "GLOBAL_VARIABLE";
        case LAYEC_IR_PARAMETER: return "PARAMETER";
        case LAYEC_IR_NOP: return "NOP";
        case LAYEC_IR_ALLOCA: return "ALLOCA";
        case LAYEC_IR_CALL: return "CALL";
        case LAYEC_IR_PTRADD: return "PTRADD";
        case LAYEC_IR_BUILTIN: return "INTRINSIC";
        case LAYEC_IR_LOAD: return "LOAD";
        case LAYEC_IR_PHI: return "PHI";
        case LAYEC_IR_STORE: return "STORE";
        case LAYEC_IR_BRANCH: return "BRANCH";
        case LAYEC_IR_COND_BRANCH: return "COND_BRANCH";
        case LAYEC_IR_RETURN: return "RETURN";
        case LAYEC_IR_UNREACHABLE: return "UNREACHABLE";
        case LAYEC_IR_ZEXT: return "ZEXT";
        case LAYEC_IR_SEXT: return "SEXT";
        case LAYEC_IR_TRUNC: return "TRUNC";
        case LAYEC_IR_BITCAST: return "BITCAST";
        case LAYEC_IR_NEG: return "NEG";
        case LAYEC_IR_COPY: return "COPY";
        case LAYEC_IR_COMPL: return "COMPL";
        case LAYEC_IR_FPTOUI: return "FPTOUI";
        case LAYEC_IR_FPTOSI: return "FPTOSI";
        case LAYEC_IR_UITOFP: return "UITOFP";
        case LAYEC_IR_SITOFP: return "SITOFP";
        case LAYEC_IR_FPTRUNC: return "FPTRUNC";
        case LAYEC_IR_FPEXT: return "FPEXT";
        case LAYEC_IR_ADD: return "ADD";
        case LAYEC_IR_SUB: return "SUB";
        case LAYEC_IR_MUL: return "MUL";
        case LAYEC_IR_SDIV: return "SDIV";
        case LAYEC_IR_UDIV: return "UDIV";
        case LAYEC_IR_SMOD: return "SREM";
        case LAYEC_IR_UMOD: return "UREM";
        case LAYEC_IR_SHL: return "SHL";
        case LAYEC_IR_SAR: return "SAR";
        case LAYEC_IR_SHR: return "SHR";
        case LAYEC_IR_AND: return "AND";
        case LAYEC_IR_OR: return "OR";
        case LAYEC_IR_XOR: return "XOR";
        case LAYEC_IR_ICMP_EQ: return "ICMP_EQ";
        case LAYEC_IR_ICMP_NE: return "ICMP_NE";
        case LAYEC_IR_ICMP_SLT: return "ICMP_SLT";
        case LAYEC_IR_ICMP_SLE: return "ICMP_SLE";
        case LAYEC_IR_ICMP_SGT: return "ICMP_SGT";
        case LAYEC_IR_ICMP_SGE: return "ICMP_SGE";
        case LAYEC_IR_ICMP_ULT: return "ICMP_ULT";
        case LAYEC_IR_ICMP_ULE: return "ICMP_ULE";
        case LAYEC_IR_ICMP_UGT: return "ICMP_UGT";
        case LAYEC_IR_ICMP_UGE: return "ICMP_UGE";
        case LAYEC_IR_FCMP_FALSE: return "FCMP_FALSE";
        case LAYEC_IR_FCMP_OEQ: return "FCMP_OEQ";
        case LAYEC_IR_FCMP_OGT: return "FCMP_OGT";
        case LAYEC_IR_FCMP_OGE: return "FCMP_OGE";
        case LAYEC_IR_FCMP_OLT: return "FCMP_OLT";
        case LAYEC_IR_FCMP_OLE: return "FCMP_OLE";
        case LAYEC_IR_FCMP_ONE: return "FCMP_ONE";
        case LAYEC_IR_FCMP_ORD: return "FCMP_ORD";
        case LAYEC_IR_FCMP_UEQ: return "FCMP_UEQ";
        case LAYEC_IR_FCMP_UGT: return "FCMP_UGT";
        case LAYEC_IR_FCMP_UGE: return "FCMP_UGE";
        case LAYEC_IR_FCMP_ULT: return "FCMP_ULT";
        case LAYEC_IR_FCMP_ULE: return "FCMP_ULE";
        case LAYEC_IR_FCMP_UNE: return "FCMP_UNE";
        case LAYEC_IR_FCMP_UNO: return "FCMP_UNO";
        case LAYEC_IR_FCMP_TRUE: return "FCMP_TRUE";
    }
}

layec_type_kind layec_type_get_kind(layec_type* type) {
    assert(type != NULL);
    return type->kind;
}

layec_type* layec_void_type(layec_context* context) {
    assert(context != NULL);

    if (context->types._void == NULL) {
        layec_type* void_type = layec_type_create(context, LAYEC_TYPE_VOID);
        assert(void_type != NULL);
        context->types._void = void_type;
    }

    assert(context->types._void != NULL);
    return context->types._void;
}

layec_type* layec_ptr_type(layec_context* context) {
    assert(context != NULL);

    if (context->types.ptr == NULL) {
        layec_type* ptr_type = layec_type_create(context, LAYEC_TYPE_POINTER);
        assert(ptr_type != NULL);
        context->types.ptr = ptr_type;
    }

    assert(context->types.ptr != NULL);
    return context->types.ptr;
}

layec_type* layec_int_type(layec_context* context, int bit_width) {
    assert(context != NULL);
    assert(bit_width > 0);
    assert(bit_width <= 65535);

    for (int64_t i = 0, count = arr_count(context->types.int_types); i < count; i++) {
        layec_type* int_type = context->types.int_types[i];
        assert(int_type != NULL);
        assert(layec_type_is_integer(int_type));

        if (int_type->primitive_bit_width == bit_width) {
            return int_type;
        }
    }

    layec_type* int_type = layec_type_create(context, LAYEC_TYPE_INTEGER);
    assert(int_type != NULL);
    int_type->primitive_bit_width = bit_width;
    arr_push(context->types.int_types, int_type);
    return int_type;
}

static layec_type* layec_create_float_type(layec_context* context, int bit_width) {
    layec_type* float_type = layec_type_create(context, LAYEC_TYPE_FLOAT);
    assert(float_type != NULL);
    float_type->primitive_bit_width = bit_width;
    return float_type;
}

layec_type* layec_float_type(layec_context* context, int bit_width) {
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

layec_type* layec_array_type(layec_context* context, int64_t length, layec_type* element_type) {
    assert(context != NULL);
    assert(length >= 0);
    assert(element_type != NULL);

    layec_type* array_type = layec_type_create(context, LAYEC_TYPE_ARRAY);
    assert(array_type != NULL);
    array_type->array.element_type = element_type;
    array_type->array.length = length;
    return array_type;
}

layec_type* layec_function_type(
    layec_context* context,
    layec_type* return_type,
    dynarr(layec_type*) parameter_types,
    layec_calling_convention calling_convention,
    bool is_variadic
) {
    assert(context != NULL);
    assert(return_type != NULL);
    for (int64_t i = 0, count = arr_count(parameter_types); i < count; i++) {
        assert(parameter_types[i] != NULL);
    }
    assert(calling_convention != LAYEC_DEFAULTCC);

    layec_type* function_type = layec_type_create(context, LAYEC_TYPE_FUNCTION);
    assert(function_type != NULL);
    function_type->function.return_type = return_type;
    function_type->function.parameter_types = parameter_types;
    function_type->function.calling_convention = calling_convention;
    function_type->function.is_variadic = is_variadic;

    return function_type;
}

layec_type* layec_struct_type(layec_context* context, string_view name, dynarr(layec_type*) field_types) {
    assert(context != NULL);
    assert(name.count != 0);
    for (int64_t i = 0, count = arr_count(field_types); i < count; i++) {
        assert(field_types[i] != NULL);
    }

    layec_type* struct_type = layec_type_create(context, LAYEC_TYPE_STRUCT);
    assert(struct_type != NULL);
    // TODO(local): unnamed struct types
    struct_type->_struct.named = true;
    struct_type->_struct.name = name;
    struct_type->_struct.members = field_types;
    return struct_type;
}

int layec_type_size_in_bits(layec_type* type) {
    assert(type->context != NULL);
    switch (type->kind) {
        default: {
            fprintf(stderr, "for type kind %s\n", layec_type_kind_to_cstring(type->kind));
            assert(false && "unimplemented kind in layec_type_size_in_bits");
            return 0;
        }

        case LAYEC_TYPE_POINTER: return type->context->target->size_of_pointer;

        case LAYEC_TYPE_INTEGER:
        case LAYEC_TYPE_FLOAT: return type->primitive_bit_width;

        case LAYEC_TYPE_ARRAY: {
            return type->array.length * layec_type_align_in_bytes(type->array.element_type) * 8;
        }

        case LAYEC_TYPE_STRUCT: {
            int size = 0;
            // NOTE(local): generation of this struct should include padding, so we don't consider it here
            for (int64_t i = 0, count = arr_count(type->_struct.members); i < count; i++) {
                size += layec_type_size_in_bits(type->_struct.members[i]);
            }
            return size;
        }
    }
}

int layec_type_size_in_bytes(layec_type* type) {
    return (layec_type_size_in_bits(type) + 7) / 8;
}

int layec_type_align_in_bits(layec_type* type) {
    assert(type->context != NULL);
    switch (type->kind) {
        default: {
            fprintf(stderr, "for type kind %s\n", layec_type_kind_to_cstring(type->kind));
            assert(false && "unimplemented kind in layec_type_align_in_bits");
            return 0;
        }

        case LAYEC_TYPE_POINTER: return type->context->target->align_of_pointer;

        case LAYEC_TYPE_INTEGER:
        case LAYEC_TYPE_FLOAT: return type->primitive_bit_width;
    }
}

int layec_type_align_in_bytes(layec_type* type) {
    return (layec_type_align_in_bits(type) + 7) / 8;
}

layec_type* layec_type_element_type(layec_type* type) {
    assert(type != NULL);
    assert(type->kind == LAYEC_TYPE_ARRAY);
    return type->array.element_type;
}

int64_t layec_type_array_length(layec_type* type) {
    assert(type != NULL);
    assert(type->kind == LAYEC_TYPE_ARRAY);
    return type->array.length;
}

bool layec_type_struct_is_named(layec_type* type) {
    assert(type != NULL);
    assert(type->kind == LAYEC_TYPE_STRUCT);
    return type->_struct.named;
}

string_view layec_type_struct_name(layec_type* type) {
    assert(type != NULL);
    assert(type->kind == LAYEC_TYPE_STRUCT);
    return type->_struct.name;
}

int64_t layec_type_struct_member_count(layec_type* type) {
    assert(type != NULL);
    assert(type->kind == LAYEC_TYPE_STRUCT);
    return arr_count(type->_struct.members);
}

layec_type* layec_type_struct_get_member_at_index(layec_type* type, int64_t index) {
    assert(type != NULL);
    assert(type->kind == LAYEC_TYPE_STRUCT);
    return type->_struct.members[index];
}

int64_t layec_function_type_parameter_count(layec_type* function_type) {
    assert(function_type != NULL);
    assert(layec_type_is_function(function_type));
    return arr_count(function_type->function.parameter_types);
}

layec_type* layec_function_type_get_parameter_type_at_index(layec_type* function_type, int64_t parameter_index) {
    assert(function_type != NULL);
    assert(layec_type_is_function(function_type));
    assert(parameter_index >= 0);
    assert(parameter_index < arr_count(function_type->function.parameter_types));
    return function_type->function.parameter_types[parameter_index];
}

bool layec_function_type_is_variadic(layec_type* function_type) {
    assert(function_type != NULL);
    assert(layec_type_is_function(function_type));
    return function_type->function.is_variadic;
}

static layec_value* layec_value_create_in_context(layec_context* context, layec_location location, layec_value_kind kind, layec_type* type, string_view name) {
    assert(context != NULL);
    assert(type != NULL);

    layec_value* value = lca_allocate(context->allocator, sizeof *value);
    assert(value != NULL);
    value->kind = kind;
    value->module = NULL;
    value->context = context;
    value->location = location;
    value->type = type;
    arr_push(context->_all_values, value);

    return value;
}

static int64_t layec_instruction_get_index_within_block(layec_value* instruction) {
    assert(instruction != NULL);
    layec_value* block = instruction->parent_block;
    assert(block != NULL);
    assert(layec_value_is_block(block));

    for (int64_t i = 0, count = arr_count(block->block.instructions); i < count; i++) {
        if (instruction == block->block.instructions[i]) {
            return i;
        }
    }

    return -1;
}

bool layec_type_is_ptr(layec_type* type) {
    assert(type != NULL);
    return type->kind == LAYEC_TYPE_POINTER;
}

bool layec_type_is_void(layec_type* type) {
    assert(type != NULL);
    return type->kind == LAYEC_TYPE_VOID;
}

bool layec_type_is_array(layec_type* type) {
    assert(type != NULL);
    return type->kind == LAYEC_TYPE_ARRAY;
}

bool layec_type_is_function(layec_type* type) {
    assert(type != NULL);
    return type->kind == LAYEC_TYPE_FUNCTION;
}

bool layec_type_is_integer(layec_type* type) {
    assert(type != NULL);
    return type->kind == LAYEC_TYPE_INTEGER;
}

bool layec_type_is_float(layec_type* type) {
    assert(type != NULL);
    return type->kind == LAYEC_TYPE_FLOAT;
}

bool layec_type_is_struct(layec_type* type) {
    assert(type != NULL);
    return type->kind == LAYEC_TYPE_STRUCT;
}

bool layec_value_is_block(layec_value* value) {
    assert(value != NULL);
    return value->kind == LAYEC_IR_BLOCK;
}

bool layec_value_is_function(layec_value* value) {
    assert(value != NULL);
    return value->kind == LAYEC_IR_FUNCTION;
}

bool layec_value_is_instruction(layec_value* value) {
    assert(value != NULL);
    return value->kind != LAYEC_IR_INVALID &&
           value->kind != LAYEC_IR_BLOCK;
}

layec_type* layec_value_get_type(layec_value* value) {
    assert(value != NULL);
    assert(value->type != NULL);
    return value->type;
}

layec_value* layec_module_create_function(layec_module* module, layec_location location, string_view function_name, layec_type* function_type, dynarr(layec_value*) parameters, layec_linkage linkage) {
    assert(module != NULL);
    assert(module->context != NULL);
    assert(function_type != NULL);
    assert(function_type->kind == LAYEC_TYPE_FUNCTION);

    layec_value* function = layec_value_create(module, location, LAYEC_IR_FUNCTION, function_type, SV_EMPTY);
    assert(function != NULL);
    function->function.name = layec_context_intern_string_view(module->context, function_name);
    function->linkage = linkage;
    function->function.parameters = parameters;

    arr_push(module->functions, function);
    return function;
}

layec_value* layec_function_append_block(layec_value* function, string_view name) {
    assert(function != NULL);
    assert(function->module != NULL);
    assert(function->context != NULL);
    assert(function->context == function->module->context);
    layec_value* block = layec_value_create(function->module, (layec_location){0}, LAYEC_IR_BLOCK, layec_void_type(function->context), SV_EMPTY);
    assert(block != NULL);
    block->block.name = layec_context_intern_string_view(function->context, name);
    block->block.parent_function = function;
    block->block.index = arr_count(function->function.blocks);
    arr_push(function->function.blocks, block);
    return block;
}

layec_value* layec_void_constant(layec_context* context) {
    assert(context != NULL);

    if (context->values._void == NULL) {
        layec_value* void_value = layec_value_create_in_context(context, (layec_location){0}, LAYEC_IR_VOID_CONSTANT, layec_void_type(context), SV_EMPTY);
        assert(void_value != NULL);
        context->values._void = void_value;
    }

    assert(context->values._void != NULL);
    return context->values._void;
}

layec_value* layec_int_constant(layec_context* context, layec_location location, layec_type* type, int64_t value) {
    assert(context != NULL);
    assert(type != NULL);
    assert(layec_type_is_integer(type));

    layec_value* int_value = layec_value_create_in_context(context, location, LAYEC_IR_INTEGER_CONSTANT, type, SV_EMPTY);
    assert(int_value != NULL);
    int_value->int_value = value;
    return int_value;
}

layec_value* layec_float_constant(layec_context* context, layec_location location, layec_type* type, double value) {
    assert(context != NULL);
    assert(type != NULL);
    assert(layec_type_is_float(type));

    layec_value* float_value = layec_value_create_in_context(context, location, LAYEC_IR_FLOAT_CONSTANT, type, SV_EMPTY);
    assert(float_value != NULL);
    float_value->float_value = value;
    return float_value;
}

layec_value* layec_array_constant(layec_context* context, layec_location location, layec_type* type, void* data, int64_t length, bool is_string_literal) {
    assert(context != NULL);
    assert(type != NULL);
    assert(data != NULL);
    assert(length >= 0);

    layec_value* array_value = layec_value_create_in_context(context, location, LAYEC_IR_ARRAY_CONSTANT, type, SV_EMPTY);
    assert(array_value != NULL);
    array_value->array.is_string_literal = is_string_literal;
    array_value->array.data = data;
    array_value->array.length = length;
    return array_value;
}

bool layec_array_constant_is_string(layec_value* array_constant) {
    assert(array_constant != NULL);
    assert(array_constant->kind == LAYEC_IR_ARRAY_CONSTANT);
    return array_constant->array.is_string_literal;
}

int64_t layec_array_constant_length(layec_value* array_constant) {
    assert(array_constant != NULL);
    assert(array_constant->kind == LAYEC_IR_ARRAY_CONSTANT);
    return array_constant->array.length;
}

const char* layec_array_constant_data(layec_value* array_constant) {
    assert(array_constant != NULL);
    assert(array_constant->kind == LAYEC_IR_ARRAY_CONSTANT);
    return array_constant->array.data;
}

layec_builder* layec_builder_create(layec_context* context) {
    assert(context != NULL);

    layec_builder* builder = lca_allocate(context->allocator, sizeof *builder);
    assert(builder != NULL);
    builder->context = context;

    return builder;
}

void layec_builder_destroy(layec_builder* builder) {
    if (builder == NULL) return;
    assert(builder->context != NULL);

    lca_allocator allocator = builder->context->allocator;

    *builder = (layec_builder){0};
    lca_deallocate(allocator, builder);
}

layec_module* layec_builder_get_module(layec_builder* builder) {
    assert(builder != NULL);
    assert(builder->function != NULL);
    return builder->function->module;
}

layec_context* layec_builder_get_context(layec_builder* builder) {
    assert(builder != NULL);
    return builder->context;
}

layec_value* layec_builder_get_function(layec_builder* builder) {
    assert(builder != NULL);
    return builder->function;
}

void layec_builder_reset(layec_builder* builder) {
    assert(builder != NULL);
    builder->function = NULL;
    builder->block = NULL;
    builder->insert_index = -1;
}

void layec_builder_position_before(layec_builder* builder, layec_value* instruction) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(instruction != NULL);
    layec_value* block = instruction->parent_block;
    assert(block != NULL);
    assert(layec_value_is_block(block));
    layec_value* function = block->block.parent_function;
    assert(function != NULL);
    assert(layec_value_is_function(function));
    assert(function->module != NULL);
    assert(function->module->context == builder->context);

    builder->function = function;
    builder->block = block;
    builder->insert_index = layec_instruction_get_index_within_block(instruction);
    assert(builder->insert_index >= 0);
    assert(builder->insert_index < arr_count(block->block.instructions));
}

void layec_builder_position_after(layec_builder* builder, layec_value* instruction) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(instruction != NULL);
    layec_value* block = instruction->parent_block;
    assert(block != NULL);
    assert(layec_value_is_block(block));
    layec_value* function = block->block.parent_function;
    assert(function != NULL);
    assert(layec_value_is_function(function));
    assert(function->module != NULL);
    assert(function->module->context == builder->context);

    builder->function = function;
    builder->block = block;
    builder->insert_index = layec_instruction_get_index_within_block(instruction) + 1;
    assert(builder->insert_index > 0);
    assert(builder->insert_index <= arr_count(block->block.instructions));
}

void layec_builder_position_at_end(layec_builder* builder, layec_value* block) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(block != NULL);
    assert(layec_value_is_block(block));
    layec_value* function = block->block.parent_function;
    assert(function != NULL);
    assert(layec_value_is_function(function));
    assert(function->module != NULL);
    assert(function->module->context == builder->context);

    builder->function = function;
    builder->block = block;
    builder->insert_index = arr_count(block->block.instructions);
    assert(builder->insert_index >= 0);
}

layec_value* layec_builder_get_insert_block(layec_builder* builder) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    return builder->block;
}

static void layec_builder_recalculate_instruction_indices(layec_builder* builder) {
    assert(builder != NULL);
    assert(builder->function != NULL);
    assert(layec_value_is_function(builder->function));

    int64_t instruction_index = layec_function_type_parameter_count(builder->function->type);
    
    for (int64_t b = 0, bcount = arr_count(builder->function->function.blocks); b < bcount; b++) {
        layec_value* block = builder->function->function.blocks[b];
        assert(block != NULL);
        assert(layec_value_is_block(block));

        for (int64_t i = 0, icount = arr_count(block->block.instructions); i < icount; i++) {
            layec_value* instruction = block->block.instructions[i];
            assert(instruction != NULL);
            assert(layec_value_is_instruction(instruction));

            if (instruction->type->kind == LAYEC_TYPE_VOID) {
                instruction->index = 0;
                continue;
            }

            instruction->index = instruction_index;
            instruction_index++;
        }
    }
}

void layec_builder_insert(layec_builder* builder, layec_value* instruction) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(instruction != NULL);
    assert(layec_value_is_instruction(instruction));
    assert(instruction->parent_block == NULL);
    layec_value* block = builder->block;
    assert(layec_value_is_block(block));
    int64_t insert_index = builder->insert_index;
    assert(insert_index >= 0);
    assert(insert_index <= arr_count(block->block.instructions));

    // reserve space for the new instruction
    arr_push(block->block.instructions, NULL);

    block->block.instructions[insert_index] = instruction;
    builder->insert_index++;

    instruction->index = -1;
    layec_builder_recalculate_instruction_indices(builder);
    assert(instruction->index >= 0);
}

void layec_builder_insert_with_name(layec_builder* builder, layec_value* instruction, string_view name) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(instruction != NULL);
    assert(layec_value_is_instruction(instruction));
    assert(instruction->parent_block == NULL);

    instruction->name = layec_context_intern_string_view(builder->context, name);
    layec_builder_insert(builder, instruction);
}

layec_value* layec_build_nop(layec_builder* builder, layec_location location) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);

    layec_value* nop = layec_value_create(builder->function->module, location, LAYEC_IR_NOP, layec_void_type(builder->context), SV_EMPTY);
    assert(nop != NULL);

    layec_builder_insert(builder, nop);
    return nop;
}

layec_value* layec_create_parameter(layec_module* module, layec_location location, layec_type* type, string_view name, int64_t index) {
    assert(module != NULL);
    assert(type != NULL);

    layec_value* parameter = layec_value_create(module, location, LAYEC_IR_PARAMETER, type, name);
    assert(parameter != NULL);

    parameter->index = index;

    return parameter;
}

layec_value* layec_build_call(layec_builder* builder, layec_location location, layec_value* callee, layec_type* callee_type, dynarr(layec_value*) arguments, string_view name) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(callee != NULL);
    assert(callee_type != NULL);
    for (int64_t i = 0, count = arr_count(arguments); i < count; i++) {
        assert(arguments[i] != NULL);
    }

    assert(callee_type->kind == LAYEC_TYPE_FUNCTION);
    layec_type* result_type = callee_type->function.return_type;
    assert(result_type != NULL);

    layec_value* call = layec_value_create(builder->function->module, location, LAYEC_IR_CALL, result_type, name);
    assert(call != NULL);
    call->call.callee = callee;
    call->call.callee_type = callee_type;
    call->call.arguments = arguments;
    call->call.calling_convention = callee_type->function.calling_convention;
    call->call.is_tail_call = false;

    layec_builder_insert(builder, call);
    return call;
}

layec_value* layec_build_return(layec_builder* builder, layec_location location, layec_value* value) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(value != NULL);

    layec_value* ret = layec_value_create(builder->function->module, location, LAYEC_IR_RETURN, layec_void_type(builder->context), SV_EMPTY);
    assert(ret != NULL);
    ret->return_value = value;

    layec_builder_insert(builder, ret);
    return ret;
}

layec_value* layec_build_return_void(layec_builder* builder, layec_location location) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);

    layec_value* ret = layec_value_create(builder->function->module, location, LAYEC_IR_RETURN, layec_void_type(builder->context), SV_EMPTY);
    assert(ret != NULL);

    layec_builder_insert(builder, ret);
    return ret;
}

layec_value* layec_build_unreachable(layec_builder* builder, layec_location location) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);

    layec_value* unreachable = layec_value_create(builder->function->module, location, LAYEC_IR_UNREACHABLE, layec_void_type(builder->context), SV_EMPTY);
    assert(unreachable != NULL);

    layec_builder_insert(builder, unreachable);
    return unreachable;
}

layec_value* layec_build_alloca(layec_builder* builder, layec_location location, layec_type* element_type, int64_t count) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(element_type != NULL);

    layec_value* alloca = layec_value_create(builder->function->module, location, LAYEC_IR_ALLOCA, layec_ptr_type(builder->context), SV_EMPTY);
    assert(alloca != NULL);
    alloca->alloca.element_type = element_type;
    alloca->alloca.element_count = count;

    layec_builder_insert(builder, alloca);
    return alloca;
}

layec_value* layec_build_store(layec_builder* builder, layec_location location, layec_value* address, layec_value* value) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(address != NULL);
    assert(layec_type_is_ptr(layec_value_get_type(address)));
    assert(value != NULL);

    layec_value* store = layec_value_create(builder->function->module, location, LAYEC_IR_STORE, layec_void_type(builder->context), SV_EMPTY);
    assert(store != NULL);
    store->address = address;
    store->operand = value;

    layec_builder_insert(builder, store);
    return store;
}

layec_value* layec_build_load(layec_builder* builder, layec_location location, layec_value* address, layec_type* type) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(address != NULL);
    assert(layec_type_is_ptr(layec_value_get_type(address)));
    assert(type != NULL);

    layec_value* load = layec_value_create(builder->function->module, location, LAYEC_IR_LOAD, type, SV_EMPTY);
    assert(load != NULL);
    load->address = address;

    layec_builder_insert(builder, load);
    return load;
}

layec_value* layec_build_branch(layec_builder* builder, layec_location location, layec_value* block) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(block != NULL);
    assert(layec_value_is_block(block));

    layec_value* branch = layec_value_create(builder->function->module, location, LAYEC_IR_BRANCH, layec_void_type(builder->context), SV_EMPTY);
    assert(branch != NULL);
    branch->branch.pass = block;

    layec_builder_insert(builder, branch);
    return branch;
}

layec_value* layec_build_branch_conditional(layec_builder* builder, layec_location location, layec_value* condition, layec_value* pass_block, layec_value* fail_block) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(condition != NULL);
    assert(layec_type_is_integer(layec_value_get_type(condition)));
    assert(layec_type_size_in_bits(layec_value_get_type(condition)) == 1);
    assert(pass_block != NULL);
    assert(layec_value_is_block(pass_block));
    assert(fail_block != NULL);
    assert(layec_value_is_block(fail_block));

    layec_value* branch = layec_value_create(builder->function->module, location, LAYEC_IR_COND_BRANCH, layec_void_type(builder->context), SV_EMPTY);
    assert(branch != NULL);
    branch->value = condition;
    branch->branch.pass = pass_block;
    branch->branch.fail = fail_block;

    layec_builder_insert(builder, branch);
    return branch;
}

layec_value* layec_build_phi(layec_builder* builder, layec_location location, layec_type* type) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(type != NULL);

    layec_value* phi = layec_value_create(builder->function->module, location, LAYEC_IR_PHI, type, SV_EMPTY);
    assert(phi != NULL);

    layec_builder_insert(builder, phi);
    return phi;
}

layec_value* layec_build_unary(layec_builder* builder, layec_location location, layec_value_kind kind, layec_value* operand, layec_type* type) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(operand != NULL);
    assert(type != NULL);

    layec_value* unary = layec_value_create(builder->function->module, location, kind, type, SV_EMPTY);
    assert(unary != NULL);
    unary->operand = operand;

    layec_builder_insert(builder, unary);
    return unary;
}

layec_value* layec_build_bitcast(layec_builder* builder, layec_location location, layec_value* value, layec_type* type) {
    return layec_build_unary(builder, location, LAYEC_IR_BITCAST, value, type);
}

layec_value* layec_build_sign_extend(layec_builder* builder, layec_location location, layec_value* value, layec_type* type) {
    return layec_build_unary(builder, location, LAYEC_IR_SEXT, value, type);
}

layec_value* layec_build_zero_extend(layec_builder* builder, layec_location location, layec_value* value, layec_type* type) {
    return layec_build_unary(builder, location, LAYEC_IR_ZEXT, value, type);
}

layec_value* layec_build_truncate(layec_builder* builder, layec_location location, layec_value* value, layec_type* type) {
    return layec_build_unary(builder, location, LAYEC_IR_TRUNC, value, type);
}

static layec_value* layec_build_binary(layec_builder* builder, layec_location location, layec_value_kind kind, layec_value* lhs, layec_value* rhs, layec_type* type) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(lhs != NULL);
    layec_type* lhs_type = layec_value_get_type(lhs);
    assert(rhs != NULL);
    layec_type* rhs_type = layec_value_get_type(rhs);
    assert(lhs_type == rhs_type); // primitive types should be reference equal if done correctly

    layec_value* cmp = layec_value_create(builder->function->module, location, kind, type, SV_EMPTY);
    assert(cmp != NULL);
    cmp->binary.lhs = lhs;
    cmp->binary.rhs = rhs;

    layec_builder_insert(builder, cmp);
    return cmp;
}

layec_value* layec_build_icmp_eq(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_ICMP_EQ, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_icmp_ne(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_ICMP_NE, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_icmp_slt(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_ICMP_SLT, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_icmp_ult(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_ICMP_ULT, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_icmp_sle(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_ICMP_SLE, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_icmp_ule(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_ICMP_ULE, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_icmp_sgt(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_ICMP_SGT, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_icmp_ugt(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_ICMP_UGT, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_icmp_sge(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_ICMP_SGE, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_icmp_uge(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_ICMP_UGE, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_fcmp_false(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_FCMP_FALSE, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_fcmp_oeq(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_FCMP_OEQ, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_fcmp_ogt(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_FCMP_OGT, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_fcmp_oge(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_FCMP_OGE, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_fcmp_olt(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_FCMP_OLT, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_fcmp_ole(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_FCMP_OLE, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_fcmp_one(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_FCMP_ONE, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_fcmp_ord(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_FCMP_ORD, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_fcmp_ueq(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_FCMP_UEQ, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_fcmp_ugt(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_FCMP_UGT, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_fcmp_uge(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_FCMP_UGE, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_fcmp_ult(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_FCMP_ULT, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_fcmp_ule(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_FCMP_ULE, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_fcmp_une(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_FCMP_UNE, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_fcmp_uno(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_FCMP_UNO, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_fcmp_true(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_FCMP_TRUE, lhs, rhs, layec_int_type(builder->context, 1));
}

layec_value* layec_build_add(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_ADD, lhs, rhs, lhs->type);
}

layec_value* layec_build_fadd(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_FADD, lhs, rhs, lhs->type);
}

layec_value* layec_build_sub(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_SUB, lhs, rhs, lhs->type);
}

layec_value* layec_build_fsub(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_FSUB, lhs, rhs, lhs->type);
}

layec_value* layec_build_mul(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_MUL, lhs, rhs, lhs->type);
}

layec_value* layec_build_fmul(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_FMUL, lhs, rhs, lhs->type);
}

layec_value* layec_build_sdiv(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_SDIV, lhs, rhs, lhs->type);
}

layec_value* layec_build_udiv(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_UDIV, lhs, rhs, lhs->type);
}

layec_value* layec_build_fdiv(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_FDIV, lhs, rhs, lhs->type);
}

layec_value* layec_build_smod(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_SMOD, lhs, rhs, lhs->type);
}

layec_value* layec_build_umod(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_UMOD, lhs, rhs, lhs->type);
}

layec_value* layec_build_fmod(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_FMOD, lhs, rhs, lhs->type);
}

layec_value* layec_build_and(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_AND, lhs, rhs, lhs->type);
}

layec_value* layec_build_or(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_OR, lhs, rhs, lhs->type);
}

layec_value* layec_build_xor(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_XOR, lhs, rhs, lhs->type);
}

layec_value* layec_build_shl(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_SHL, lhs, rhs, lhs->type);
}

layec_value* layec_build_shr(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_SHR, lhs, rhs, lhs->type);
}

layec_value* layec_build_sar(layec_builder* builder, layec_location location, layec_value* lhs, layec_value* rhs) {
    return layec_build_binary(builder, location, LAYEC_IR_SAR, lhs, rhs, lhs->type);
}

layec_value* layec_build_neg(layec_builder* builder, layec_location location, layec_value* operand) {
    return layec_build_unary(builder, location, LAYEC_IR_NEG, operand, operand->type);
}

layec_value* layec_build_compl(layec_builder* builder, layec_location location, layec_value* operand) {
    return layec_build_unary(builder, location, LAYEC_IR_COMPL, operand, operand->type);
}

layec_value* layec_build_fptoui(layec_builder* builder, layec_location location, layec_value* operand, layec_type* to) {
    return layec_build_unary(builder, location, LAYEC_IR_FPTOUI, operand, to);
}

layec_value* layec_build_fptosi(layec_builder* builder, layec_location location, layec_value* operand, layec_type* to) {
    return layec_build_unary(builder, location, LAYEC_IR_FPTOSI, operand, to);
}

layec_value* layec_build_uitofp(layec_builder* builder, layec_location location, layec_value* operand, layec_type* to) {
    return layec_build_unary(builder, location, LAYEC_IR_UITOFP, operand, to);
}

layec_value* layec_build_sitofp(layec_builder* builder, layec_location location, layec_value* operand, layec_type* to) {
    return layec_build_unary(builder, location, LAYEC_IR_SITOFP, operand, to);
}

layec_value* layec_build_fpext(layec_builder* builder, layec_location location, layec_value* operand, layec_type* to) {
    return layec_build_unary(builder, location, LAYEC_IR_FPEXT, operand, to);
}

layec_value* layec_build_fptrunc(layec_builder* builder, layec_location location, layec_value* operand, layec_type* to) {
    return layec_build_unary(builder, location, LAYEC_IR_FPTRUNC, operand, to);
}

static layec_value* layec_build_builtin(layec_builder* builder, layec_location location, layec_builtin_kind kind) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);

    layec_value* builtin = layec_value_create(builder->function->module, location, LAYEC_IR_BUILTIN, layec_void_type(builder->context), SV_EMPTY);
    assert(builtin != NULL);
    builtin->builtin.kind = kind;

    layec_builder_insert(builder, builtin);
    return builtin;
}

layec_value* layec_build_builtin_memset(layec_builder* builder, layec_location location, layec_value* address, layec_value* value, layec_value* count) {
    layec_value* builtin = layec_build_builtin(builder, location, LAYEC_BUILTIN_MEMSET);
    assert(builtin != NULL);
    arr_push(builtin->builtin.arguments, address);
    arr_push(builtin->builtin.arguments, value);
    arr_push(builtin->builtin.arguments, count);
    return builtin;
}

layec_value* layec_build_builtin_memcpy(layec_builder* builder, layec_location location, layec_value* source_address, layec_value* dest_address, layec_value* count) {
    layec_value* builtin = layec_build_builtin(builder, location, LAYEC_BUILTIN_MEMSET);
    assert(builtin != NULL);
    arr_push(builtin->builtin.arguments, source_address);
    arr_push(builtin->builtin.arguments, dest_address);
    arr_push(builtin->builtin.arguments, count);
    return builtin;
}

layec_value* layec_build_ptradd(layec_builder* builder, layec_location location, layec_value* address, layec_value* offset_value) {
    assert(builder != NULL);
    assert(builder->context != NULL);
    assert(builder->function != NULL);
    assert(builder->function->module != NULL);
    assert(builder->block != NULL);
    assert(address != NULL);
    assert(layec_type_is_ptr(layec_value_get_type(address)));
    assert(offset_value != NULL);
    assert(layec_type_is_integer(layec_value_get_type(offset_value)));

    layec_value* ptradd = layec_value_create(builder->function->module, location, LAYEC_IR_PTRADD, layec_ptr_type(builder->context), SV_EMPTY);
    assert(ptradd != NULL);
    ptradd->address = address;
    ptradd->operand = offset_value;

    layec_builder_insert(builder, ptradd);
    return ptradd;
}

// IR Printer

#define COL_COMMENT  WHITE
#define COL_DELIM    WHITE
#define COL_KEYWORD  RED
#define COL_NAME     GREEN
#define COL_CONSTANT BLUE

typedef struct layec_print_context {
    layec_context* context;
    bool use_color;
    string* output;
} layec_print_context;

static void layec_global_print(layec_print_context* print_context, layec_value* global);
static void layec_function_print(layec_print_context* print_context, layec_value* function);
static void layec_type_print_struct_type_to_string_literally(layec_type* type, string* s, bool use_color);

string layec_module_print(layec_module* module, bool use_color) {
    assert(module != NULL);
    assert(module->context != NULL);

    string output_string = string_create(module->context->allocator);

    layec_print_context print_context = {
        .context = module->context,
        .use_color = use_color,
        .output = &output_string,
    };

    //bool use_color = print_context.use_color;

    lca_string_append_format(
        print_context.output,
        "%s; LayeC IR Module: %.*s%s\n",
        COL(COL_COMMENT),
        STR_EXPAND(module->name),
        COL(RESET)
    );

    for (int64_t i = 0; i < layec_context_get_struct_type_count(module->context); i++) {
        layec_type* struct_type = layec_context_get_struct_type_at_index(module->context, i);
        if (layec_type_struct_is_named(struct_type)) {
            lca_string_append_format(print_context.output, "%sdefine %s%.*s %s= ", COL(COL_KEYWORD), COL(COL_NAME), STR_EXPAND(layec_type_struct_name(struct_type)), COL(RESET));
            layec_type_print_struct_type_to_string_literally(struct_type, print_context.output, use_color);
            lca_string_append_format(print_context.output, "%s\n", COL(RESET));
        }
    }

    for (int64_t i = 0, count = arr_count(module->globals); i < count; i++) {
        if (i > 0) lca_string_append_format(print_context.output, "\n");
        layec_global_print(&print_context, module->globals[i]);
    }

    if (arr_count(module->globals) > 0) lca_string_append_format(print_context.output, "\n");

    for (int64_t i = 0, count = arr_count(module->functions); i < count; i++) {
        if (i > 0) lca_string_append_format(print_context.output, "\n");
        layec_function_print(&print_context, module->functions[i]);
    }

    return output_string;
}

static const char* ir_calling_convention_to_cstring(layec_calling_convention calling_convention) {
    switch (calling_convention) {
        case LAYEC_DEFAULTCC: assert(false && "default calling convention is not valid within the IR");
        case LAYEC_CCC: return "ccc";
        case LAYEC_LAYECC: return "layecc";
    }

    assert(false && "unsupported/unimplemented calling convetion");
    return "";
}

static void layec_instruction_print_name_if_required(layec_print_context* print_context, layec_value* instruction) {
    assert(print_context != NULL);
    assert(print_context->context != NULL);
    assert(instruction != NULL);

    bool use_color = print_context->use_color;

    if (layec_type_is_void(instruction->type)) {
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
            STR_EXPAND(instruction->name),
            COL(COL_DELIM),
            COL(RESET)
        );
    }
}

static void layec_instruction_print(layec_print_context* print_context, layec_value* instruction) {
    assert(print_context != NULL);
    assert(print_context->context != NULL);
    assert(instruction != NULL);

    bool use_color = print_context->use_color;

    lca_string_append_format(print_context->output, "  ");
    layec_instruction_print_name_if_required(print_context, instruction);

    switch (instruction->kind) {
        default: {
            fprintf(stderr, "for value kind %s\n", layec_value_kind_to_cstring(instruction->kind));
            assert(false && "todo layec_instruction_print");
        } break;
        
        case LAYEC_IR_NOP: {
            lca_string_append_format(print_context->output, "%snop", COL(COL_KEYWORD));
        } break;

        case LAYEC_IR_ALLOCA: {
            lca_string_append_format(print_context->output, "%salloca ", COL(COL_KEYWORD));
            layec_type_print_to_string(instruction->alloca.element_type, print_context->output, use_color);
            if (instruction->alloca.element_count != 1) {
                lca_string_append_format(print_context->output, "%s, %s%lld", COL(COL_DELIM), COL(COL_CONSTANT), instruction->alloca.element_count);
            }
        } break;

        case LAYEC_IR_STORE: {
            lca_string_append_format(print_context->output, "%sstore ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->address, print_context->output, false, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->operand, print_context->output, true, use_color);
        } break;

        case LAYEC_IR_LOAD: {
            lca_string_append_format(print_context->output, "%sload ", COL(COL_KEYWORD));
            layec_type_print_to_string(instruction->type, print_context->output, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->address, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_BRANCH: {
            lca_string_append_format(print_context->output, "%sbranch ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->branch.pass, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_COND_BRANCH: {
            lca_string_append_format(print_context->output, "%sbranch ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->value, print_context->output, false, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->branch.pass, print_context->output, false, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->branch.fail, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_PHI: {
            lca_string_append_format(print_context->output, "%sphi ", COL(COL_KEYWORD));
            layec_type_print_to_string(instruction->type, print_context->output, use_color);

            for (int64_t i = 0, count = layec_instruction_phi_incoming_value_count(instruction); i < count; i++) {
                if (i > 0) lca_string_append_format(print_context->output, "%s,", COL(RESET));
                lca_string_append_format(print_context->output, "%s [ ", COL(RESET));
                layec_value_print_to_string(layec_instruction_phi_incoming_value_at_index(instruction, i), print_context->output, false, use_color);
                lca_string_append_format(print_context->output, "%s, ", COL(RESET));
                layec_value_print_to_string(layec_instruction_phi_incoming_block_at_index(instruction, i), print_context->output, false, use_color);
                lca_string_append_format(print_context->output, "%s ]", COL(RESET));
            }
        } break;
        
        case LAYEC_IR_RETURN: {
            lca_string_append_format(print_context->output, "%sreturn", COL(COL_KEYWORD));
            if (instruction->return_value != NULL) {
                lca_string_append_format(print_context->output, " ", COL(COL_KEYWORD));
                layec_value_print_to_string(instruction->return_value, print_context->output, true, use_color);
            }
        } break;
        
        case LAYEC_IR_UNREACHABLE: {
            lca_string_append_format(print_context->output, "%sunreachable", COL(COL_KEYWORD));
        } break;

        case LAYEC_IR_CALL: {
            lca_string_append_format(print_context->output, "%s%scall %s ", COL(COL_KEYWORD), (instruction->call.is_tail_call ? "tail " : ""), ir_calling_convention_to_cstring(instruction->call.calling_convention));
            layec_type_print_to_string(instruction->type, print_context->output, use_color);
            lca_string_append_format(print_context->output, " ");
            layec_value_print_to_string(instruction->call.callee, print_context->output, false, use_color);
            lca_string_append_format(print_context->output, "%s(", COL(COL_DELIM));

            for (int64_t i = 0, count = arr_count(instruction->call.arguments); i < count; i++) {
                if (i > 0) {
                    lca_string_append_format(print_context->output, "%s, ", COL(COL_DELIM));
                }

                layec_value* argument = instruction->call.arguments[i];
                layec_value_print_to_string(argument, print_context->output, true, use_color);
            }

            lca_string_append_format(print_context->output, "%s)", COL(COL_DELIM));
        } break;

        case LAYEC_IR_BUILTIN: {
            const char* builtin_name = "";
            switch (instruction->builtin.kind) {
                default: builtin_name = "unknown"; break;
                case LAYEC_BUILTIN_MEMSET: builtin_name = "memset"; break;
                case LAYEC_BUILTIN_MEMCOPY: builtin_name = "memcopy"; break;
            }

            lca_string_append_format(print_context->output, "%sbuiltin ", COL(COL_KEYWORD));
            lca_string_append_format(print_context->output, "%s@%s%s(", COL(COL_NAME), builtin_name, COL(COL_DELIM));

            for (int64_t i = 0, count = arr_count(instruction->builtin.arguments); i < count; i++) {
                if (i > 0) {
                    lca_string_append_format(print_context->output, "%s, ", COL(COL_DELIM));
                }

                layec_value* argument = instruction->builtin.arguments[i];
                layec_value_print_to_string(argument, print_context->output, true, use_color);
            }

            lca_string_append_format(print_context->output, "%s)", COL(COL_DELIM));
        } break;

        case LAYEC_IR_BITCAST: {
            lca_string_append_format(print_context->output, "%sbitcast ", COL(COL_KEYWORD));
            layec_type_print_to_string(instruction->type, print_context->output, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->operand, print_context->output, true, use_color);
        } break;

        case LAYEC_IR_SEXT: {
            lca_string_append_format(print_context->output, "%ssext ", COL(COL_KEYWORD));
            layec_type_print_to_string(instruction->type, print_context->output, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->operand, print_context->output, true, use_color);
        } break;

        case LAYEC_IR_ZEXT: {
            lca_string_append_format(print_context->output, "%szext ", COL(COL_KEYWORD));
            layec_type_print_to_string(instruction->type, print_context->output, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->operand, print_context->output, true, use_color);
        } break;

        case LAYEC_IR_TRUNC: {
            lca_string_append_format(print_context->output, "%strunc ", COL(COL_KEYWORD));
            layec_type_print_to_string(instruction->type, print_context->output, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->operand, print_context->output, true, use_color);
        } break;

        case LAYEC_IR_NEG: {
            lca_string_append_format(print_context->output, "%sneg ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->operand, print_context->output, true, use_color);
        } break;

        case LAYEC_IR_COMPL: {
            lca_string_append_format(print_context->output, "%scompl ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->operand, print_context->output, true, use_color);
        } break;

        case LAYEC_IR_ADD: {
            lca_string_append_format(print_context->output, "%sadd ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_FADD: {
            lca_string_append_format(print_context->output, "%sfadd ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_SUB: {
            lca_string_append_format(print_context->output, "%ssub ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_FSUB: {
            lca_string_append_format(print_context->output, "%sfsub ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_MUL: {
            lca_string_append_format(print_context->output, "%smul ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_FMUL: {
            lca_string_append_format(print_context->output, "%sfmul ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_SDIV: {
            lca_string_append_format(print_context->output, "%ssdiv ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_UDIV: {
            lca_string_append_format(print_context->output, "%sudiv ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_FDIV: {
            lca_string_append_format(print_context->output, "%sfdiv ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_SMOD: {
            lca_string_append_format(print_context->output, "%ssmod ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_UMOD: {
            lca_string_append_format(print_context->output, "%sumod ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_FMOD: {
            lca_string_append_format(print_context->output, "%sfmod ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_AND: {
            lca_string_append_format(print_context->output, "%sand ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_OR: {
            lca_string_append_format(print_context->output, "%sor ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_XOR: {
            lca_string_append_format(print_context->output, "%sxor ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_SHL: {
            lca_string_append_format(print_context->output, "%sshl ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_SHR: {
            lca_string_append_format(print_context->output, "%sshr ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_SAR: {
            lca_string_append_format(print_context->output, "%ssar ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_ICMP_EQ: {
            lca_string_append_format(print_context->output, "%sicmp eq ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_ICMP_NE: {
            lca_string_append_format(print_context->output, "%sicmp ne ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_ICMP_SLT: {
            lca_string_append_format(print_context->output, "%sicmp slt ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_ICMP_ULT: {
            lca_string_append_format(print_context->output, "%sicmp ult ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_ICMP_SLE: {
            lca_string_append_format(print_context->output, "%sicmp sle ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_ICMP_ULE: {
            lca_string_append_format(print_context->output, "%sicmp ule ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_ICMP_SGT: {
            lca_string_append_format(print_context->output, "%sicmp sgt ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_ICMP_UGT: {
            lca_string_append_format(print_context->output, "%sicmp ugt ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_ICMP_SGE: {
            lca_string_append_format(print_context->output, "%sicmp sge ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_ICMP_UGE: {
            lca_string_append_format(print_context->output, "%sicmp uge ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_FCMP_FALSE: {
            lca_string_append_format(print_context->output, "%sfcmp false ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_FCMP_OEQ: {
            lca_string_append_format(print_context->output, "%sfcmp oeq ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_FCMP_OGT: {
            lca_string_append_format(print_context->output, "%sfcmp ogt ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_FCMP_OGE: {
            lca_string_append_format(print_context->output, "%sfcmp oge ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_FCMP_OLT: {
            lca_string_append_format(print_context->output, "%sfcmp olt ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_FCMP_OLE: {
            lca_string_append_format(print_context->output, "%sfcmp ole ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_FCMP_ONE: {
            lca_string_append_format(print_context->output, "%sfcmp one ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_FCMP_ORD: {
            lca_string_append_format(print_context->output, "%sfcmp ord ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_FCMP_UEQ: {
            lca_string_append_format(print_context->output, "%sfcmp ueq ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_FCMP_UGT: {
            lca_string_append_format(print_context->output, "%sfcmp ugt ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_FCMP_UGE: {
            lca_string_append_format(print_context->output, "%sfcmp uge ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_FCMP_ULT: {
            lca_string_append_format(print_context->output, "%sfcmp ult ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_FCMP_ULE: {
            lca_string_append_format(print_context->output, "%sfcmp ule ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_FCMP_UNE: {
            lca_string_append_format(print_context->output, "%sfcmp une ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_FCMP_UNO: {
            lca_string_append_format(print_context->output, "%sfcmp uno ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_FCMP_TRUE: {
            lca_string_append_format(print_context->output, "%sfcmp true ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->binary.lhs, print_context->output, true, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->binary.rhs, print_context->output, false, use_color);
        } break;

        case LAYEC_IR_PTRADD: {
            lca_string_append_format(print_context->output, "%sptradd ptr ", COL(COL_KEYWORD));
            layec_value_print_to_string(instruction->address, print_context->output, false, use_color);
            lca_string_append_format(print_context->output, "%s, ", COL(RESET));
            layec_value_print_to_string(instruction->operand, print_context->output, true, use_color);
        } break;
    }

    lca_string_append_format(print_context->output, "%s\n", COL(RESET));
}

static void layec_print_linkage(layec_print_context* print_context, layec_linkage linkage) {
    assert(print_context != NULL);
    assert(print_context->context != NULL);

    bool use_color = print_context->use_color;

    switch (linkage) {
        default: break;

        case LAYEC_LINK_EXPORTED: {
            lca_string_append_format(print_context->output, "%sexported ", COL(COL_KEYWORD));
        } break;

        case LAYEC_LINK_REEXPORTED: {
            lca_string_append_format(print_context->output, "%sreexported ", COL(COL_KEYWORD));
        } break;
    }
}

static void layec_global_print(layec_print_context* print_context, layec_value* global) {
    assert(print_context != NULL);
    assert(print_context->context != NULL);
    assert(global != NULL);
    assert(layec_type_is_ptr(global->type));

    layec_type* global_type = global->alloca.element_type;
    assert(global_type != NULL);

    bool use_color = print_context->use_color;

    lca_string_append_format(print_context->output, "%sdefine ", COL(COL_KEYWORD));
    layec_print_linkage(print_context, global->linkage);

    if (global->name.count == 0) {
        lca_string_append_format(print_context->output, "%sglobal.%lld", COL(COL_NAME), global->index);
    } else {
        lca_string_append_format(print_context->output, "%s%.*s", COL(COL_NAME), STR_EXPAND(global->name));
    }

    lca_string_append_format(print_context->output, " %s= ", COL(COL_DELIM));
    layec_value_print_to_string(global->value, print_context->output, true, use_color);

    lca_string_append_format(print_context->output, "%s\n", COL(RESET));
}

static void layec_function_print(layec_print_context* print_context, layec_value* function) {
    assert(print_context != NULL);
    assert(print_context->context != NULL);
    assert(function != NULL);

    layec_type* function_type = function->type;
    assert(function_type != NULL);
    assert(layec_type_is_function(function_type));

    layec_type* return_type = function_type->function.return_type;
    assert(return_type != NULL);

    dynarr(layec_type*) parameter_types = function_type->function.parameter_types;

    bool use_color = print_context->use_color;
    bool is_declare = arr_count(function->function.blocks) == 0;

    lca_string_append_format(print_context->output, "%s%s ", COL(COL_KEYWORD), is_declare ? "declare" : "define");
    layec_print_linkage(print_context, function->linkage);

    lca_string_append_format(
        print_context->output,
        "%s %s%.*s%s(",
        ir_calling_convention_to_cstring(function->type->function.calling_convention),
        COL(COL_NAME),
        STR_EXPAND(function->function.name),
        COL(COL_DELIM)
    );

    for (int64_t i = 0, count = arr_count(parameter_types); i < count; i++) {
        if (i > 0) lca_string_append_format(print_context->output, "%s, ", COL(COL_DELIM));
        layec_type_print_to_string(parameter_types[i], print_context->output, use_color);
        lca_string_append_format(print_context->output, " %s%%%lld", COL(COL_NAME), i);
    }

    lca_string_append_format(print_context->output, "%s)", COL(COL_DELIM));

    if (function_type->function.is_variadic) {
        lca_string_append_format(print_context->output, " %svariadic", COL(COL_KEYWORD));
    }

    if (!layec_type_is_void(return_type)) {
        lca_string_append_format(print_context->output, " %s-> ", COL(COL_DELIM));
        layec_type_print_to_string(return_type, print_context->output, use_color);
    }

    lca_string_append_format(
        print_context->output,
        "%s%s%s\n",
        COL(COL_DELIM),
        is_declare ? "" : " {",
        COL(RESET)
    );

    if (!is_declare) {
        for (int64_t i = 0, count = arr_count(function->function.blocks); i < count; i++) {
            layec_value* block = function->function.blocks[i];
            assert(block != NULL);
            assert(layec_value_is_block(block));

            if (block->block.name.count == 0) {
                lca_string_append_format(print_context->output, "%s_bb%lld%s:\n", COL(COL_NAME), i, COL(COL_DELIM));
            } else {
                lca_string_append_format(print_context->output, "%s%.*s%s:\n", COL(COL_NAME), STR_EXPAND(block->block.name), COL(COL_DELIM));
            }

            for (int64_t j = 0, count2 = arr_count(block->block.instructions); j < count2; j++) {
                layec_value* instruction = block->block.instructions[j];
                assert(instruction != NULL);
                assert(layec_value_is_instruction(instruction));

                layec_instruction_print(print_context, instruction);
            }
        }

        lca_string_append_format(print_context->output, "%s}%s\n", COL(COL_DELIM), COL(RESET));
    }
}

static void layec_type_print_struct_type_to_string_literally(layec_type* type, string* s, bool use_color) {
    lca_string_append_format(s, "%sstruct %s{", COL(COL_KEYWORD), COL(RESET));

    for (int64_t i = 0, count = arr_count(type->_struct.members); i < count; i++) {
        if (i > 0) {
            lca_string_append_format(s, "%s, ", COL(RESET));
        } else {
            lca_string_append_format(s, " ");
        }

        layec_type* member_type = type->_struct.members[i];
        layec_type_print_to_string(member_type, s, use_color);
    }

    lca_string_append_format(s, "%s }", COL(RESET));
}

void layec_type_print_to_string(layec_type* type, string* s, bool use_color) {
    assert(type != NULL);
    assert(s != NULL);

    switch (type->kind) {
        default: {
            fprintf(stderr, "for type %s\n", layec_type_kind_to_cstring(type->kind));
            assert(false && "unhandled type in layec_type_print_to_string");
        } break;

        case LAYEC_TYPE_POINTER: {
            lca_string_append_format(s, "%sptr", COL(COL_KEYWORD));
        } break;

        case LAYEC_TYPE_VOID: {
            lca_string_append_format(s, "%svoid", COL(COL_KEYWORD));
        } break;

        case LAYEC_TYPE_INTEGER: {
            lca_string_append_format(s, "%sint%d", COL(COL_KEYWORD), type->primitive_bit_width);
        } break;

        case LAYEC_TYPE_FLOAT: {
            lca_string_append_format(s, "%sfloat%d", COL(COL_KEYWORD), type->primitive_bit_width);
        } break;

        case LAYEC_TYPE_ARRAY: {
            layec_type_print_to_string(type->array.element_type, s, use_color);
            lca_string_append_format(s, "%s[%s%lld%s]", COL(COL_DELIM), COL(COL_CONSTANT), type->array.length, COL(COL_DELIM));
        } break;

        case LAYEC_TYPE_STRUCT: {
            if (type->_struct.named) {
                lca_string_append_format(s, "%s@%.*s", COL(COL_NAME), STR_EXPAND(type->_struct.name));
            } else {
                layec_type_print_struct_type_to_string_literally(type, s, use_color);
            }
        } break;
    }

    lca_string_append_format(s, "%s", COL(RESET));
}

void layec_value_print_to_string(layec_value* value, string* s, bool print_type, bool use_color) {
    assert(value != NULL);
    assert(s != NULL);

    if (print_type) {
        layec_type_print_to_string(value->type, s, use_color);
        lca_string_append_format(s, "%s ", COL(RESET));
    }

    switch (value->kind) {
        default: {
            if (value->name.count == 0) {
                lca_string_append_format(s, "%s%%%lld", COL(COL_NAME), value->index);
            } else {
                lca_string_append_format(s, "%s%%%.*s", COL(COL_NAME), STR_EXPAND(value->name));
            }
        } break;

        case LAYEC_IR_FUNCTION: {
            lca_string_append_format(s, "%s@%.*s", COL(COL_NAME), STR_EXPAND(value->function.name));
        } break;

        case LAYEC_IR_BLOCK: {
            if (value->block.name.count == 0) {
                lca_string_append_format(s, "%s%%_bb%lld", COL(COL_NAME), value->block.index);
            } else {
                lca_string_append_format(s, "%s%%%.*s", COL(COL_NAME), STR_EXPAND(value->block.name));
            }
        } break;

        case LAYEC_IR_INTEGER_CONSTANT: {
            lca_string_append_format(s, "%s%lld", COL(COL_CONSTANT), value->int_value);
        } break;

        case LAYEC_IR_GLOBAL_VARIABLE: {
            if (value->name.count == 0) {
                lca_string_append_format(s, "%s@global.%lld", COL(COL_NAME), value->index);
            } else {
                lca_string_append_format(s, "%s@%.*s", COL(COL_NAME), STR_EXPAND(value->name));
            }
        } break;

        case LAYEC_IR_ARRAY_CONSTANT: {
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
                assert(false && "todo layec_value_print_to_string non-string arrays");
            }
        } break;
    }

    lca_string_append_format(s, "%s", COL(RESET));
}
