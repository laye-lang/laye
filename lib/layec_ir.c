#include "layec.h"

#include <assert.h>

void layec_value_destroy(layec_value* value);

struct layec_module {
    layec_context* context;
    string name;

    lca_arena* arena;
    dynarr(layec_value*) functions;

    dynarr(layec_value*) _all_values;
};

struct layec_type {
    layec_type_kind kind;

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
                string name;
                int64_t index;
            };
        } _struct;
    };
};

struct layec_value {
    layec_value_kind kind;
    layec_location location;
    layec_module* module;
    layec_context* context;

    layec_type* type;

    string name;
    int64_t index;

    // for values which want their uses tracked, a list of all users of this value.
    dynarr(layec_value*) users;

    layec_value* parent_block;

    union {
        int64_t int_value;

        struct {
            string name;
            int64_t index;
            layec_value* parent_function;
            dynarr(layec_value*) instructions;
        } block;

        struct {
            string name;
            dynarr(layec_value*) parameters;
            layec_linkage linkage;
            layec_calling_convention calling_convention;
            dynarr(layec_value*) blocks;
        } function;

        int64_t parameter_index;

        layec_type* allocated_type;

        layec_value* return_value;

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

    arr_free(module->functions);
    arr_free(module->_all_values);

    *module = (layec_module){};
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
    }
}

static layec_type* layec_type_create(layec_context* context, layec_type_kind kind) {
    assert(context != NULL);
    assert(context->type_arena != NULL);

    layec_type* type = lca_arena_push(context->type_arena, sizeof *type);
    assert(type != NULL);
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

const char* layec_type_kind_to_string(layec_type_kind kind) {
    switch (kind) {
        case LAYEC_TYPE_VOID: return "VOID";
        case LAYEC_TYPE_ARRAY: return "ARRAY";
        case LAYEC_TYPE_FLOAT: return "FLOAT";
        case LAYEC_TYPE_FUNCTION: return "FUNCTION";
        case LAYEC_TYPE_INTEGER: return "INTEGER";
        case LAYEC_TYPE_POINTER: return "POINTER";
        case LAYEC_TYPE_STRUCT: return "STRUCT";
    }
}

layec_context* layec_module_context(layec_module* module) {
    assert(module != NULL);
    return module->context;
}

string_view layec_module_name(layec_module* module) {
    assert(module != NULL);
    return string_as_view(module->name);
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

layec_type* layec_value_type(layec_value* value) {
    assert(value != NULL);
    return value->type;
}

string_view layec_value_name(layec_value* value) {
    assert(value != NULL);
    return string_as_view(value->name);
}

bool layec_block_has_name(layec_value* block) {
    assert(block != NULL);
    assert(layec_value_is_block(block));
    return block->block.name.count != 0;
}

string_view layec_block_name(layec_value* block) {
    assert(block != NULL);
    assert(layec_value_is_block(block));
    return string_as_view(block->block.name);
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
    return string_as_view(function->function.name);
}

layec_value* layec_value_callee(layec_value* call) {
    assert(call != NULL);
    assert(call->kind == LAYEC_IR_CALL);
    assert(call->call.callee != NULL);
    return call->call.callee;
}
int64_t layec_value_call_argument_count(layec_value* call) {
    assert(call != NULL);
    assert(call->kind == LAYEC_IR_CALL);
    return arr_count(call->call.arguments);
}

layec_value* layec_value_call_get_argument_at_index(layec_value* call, int64_t argument_index) {
    assert(call != NULL);
    assert(call->kind == LAYEC_IR_CALL);
    assert(argument_index >= 0);
    int64_t count = arr_count(call->call.arguments);
    assert(argument_index < count);
    layec_value* argument = call->call.arguments[argument_index];
    assert(argument != NULL);
    return argument;
}

int64_t layec_integer_constant_value(layec_value* value) {
    assert(value != NULL);
    assert(value->kind == LAYEC_IR_INTEGER_CONSTANT);
    return value->int_value;
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
        case LAYEC_IR_ALLOCA: return "ALLOCA";
        case LAYEC_IR_CALL: return "CALL";
        case LAYEC_IR_GET_ELEMENT_PTR: return "GET_ELEMENT_PTR";
        case LAYEC_IR_GET_MEMBER_PTR: return "GET_MEMBER_PTR";
        case LAYEC_IR_INTRINSIC: return "INTRINSIC";
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
        case LAYEC_IR_ADD: return "ADD";
        case LAYEC_IR_SUB: return "SUB";
        case LAYEC_IR_MUL: return "MUL";
        case LAYEC_IR_SDIV: return "SDIV";
        case LAYEC_IR_UDIV: return "UDIV";
        case LAYEC_IR_SREM: return "SREM";
        case LAYEC_IR_UREM: return "UREM";
        case LAYEC_IR_SHL: return "SHL";
        case LAYEC_IR_SAR: return "SAR";
        case LAYEC_IR_SHR: return "SHR";
        case LAYEC_IR_AND: return "AND";
        case LAYEC_IR_OR: return "OR";
        case LAYEC_IR_XOR: return "XOR";
        case LAYEC_IR_EQ: return "EQ";
        case LAYEC_IR_NE: return "NE";
        case LAYEC_IR_SLT: return "SLT";
        case LAYEC_IR_SLE: return "SLE";
        case LAYEC_IR_SGT: return "SGT";
        case LAYEC_IR_SGE: return "SGE";
        case LAYEC_IR_ULT: return "ULT";
        case LAYEC_IR_ULE: return "ULE";
        case LAYEC_IR_UGT: return "UGT";
        case LAYEC_IR_UGE: return "UGE";
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

int layec_type_size_in_bits(layec_type* type) {
    switch (type->kind) {
        default: {
            fprintf(stderr, "for type kind %s\n", layec_type_kind_to_string(type->kind));
            assert(false && "unimplemented kind in layec_type_size_in_bits");
            return 0;
        }

        case LAYEC_TYPE_INTEGER: return type->primitive_bit_width;
    }
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

layec_value* layec_module_create_function(layec_module* module, layec_location location, string_view function_name, layec_type* function_type, layec_linkage linkage) {
    assert(module != NULL);
    assert(module->context != NULL);
    assert(function_type != NULL);
    assert(function_type->kind == LAYEC_TYPE_FUNCTION);

    layec_value* function = layec_value_create(module, location, LAYEC_IR_FUNCTION, function_type, SV_EMPTY);
    assert(function != NULL);
    function->function.name = layec_context_intern_string_view(module->context, function_name);
    function->function.linkage = linkage;
    function->function.calling_convention = function_type->function.calling_convention;

    arr_push(module->functions, function);
    return function;
}

layec_value* layec_function_append_block(layec_value* function, string_view name) {
    assert(function != NULL);
    assert(function->module != NULL);
    assert(function->context != NULL);
    assert(function->context == function->module->context);
    layec_value* block = layec_value_create(function->module, (layec_location){}, LAYEC_IR_BLOCK, layec_void_type(function->context), SV_EMPTY);
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
        layec_value* void_value = layec_value_create_in_context(context, (layec_location){}, LAYEC_IR_VOID_CONSTANT, layec_void_type(context), SV_EMPTY);
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

    *builder = (layec_builder){};
    lca_deallocate(allocator, builder);
}

layec_context* layec_builder_get_context(layec_builder* builder) {
    assert(builder != NULL);
    return builder->context;
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

    for (int64_t i = insert_index, count = arr_count(block->block.instructions) - 1; i < count; i++) {
        block->block.instructions[i + 1] = block->block.instructions[i];
        block->block.instructions[i + 1]->index = i + 1;
    }

    block->block.instructions[insert_index] = instruction;
    instruction->index = insert_index;

    builder->insert_index++;
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

static void layec_function_print(layec_print_context* print_context, layec_value* function);

string layec_module_print(layec_module* module) {
    assert(module != NULL);
    assert(module->context != NULL);

    string output_string = string_create(module->context->allocator);

    layec_print_context print_context = {
        .context = module->context,
        .use_color = module->context->use_color,
        .output = &output_string,
    };

    bool use_color = print_context.use_color;

    lca_string_append_format(
        print_context.output,
        "%s; LayeC IR Module: %.*s%s\n",
        COL(COL_COMMENT),
        STR_EXPAND(module->name),
        COL(RESET)
    );

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
        
        case LAYEC_IR_RETURN: {
            lca_string_append_format(print_context->output, "%sreturn", COL(COL_KEYWORD));
            if (instruction->return_value != NULL) {
                lca_string_append_format(print_context->output, " ", COL(COL_KEYWORD));
                layec_value_print_to_string(instruction->return_value, print_context->output, use_color);
            }
        } break;
        
        case LAYEC_IR_UNREACHABLE: {
            lca_string_append_format(print_context->output, "%sunreachable", COL(COL_KEYWORD));
        } break;

        case LAYEC_IR_CALL: {
            lca_string_append_format(print_context->output, "%s%scall %s ", COL(COL_KEYWORD), (instruction->call.is_tail_call ? "tail " : ""), ir_calling_convention_to_cstring(instruction->call.calling_convention));
            layec_type_print_to_string(instruction->type, print_context->output, use_color);
            lca_string_append_format(print_context->output, " ");
            layec_value_print_to_string(instruction->call.callee, print_context->output, use_color);
            lca_string_append_format(print_context->output, "%s(", COL(COL_DELIM));

            for (int64_t i = 0, count = arr_count(instruction->call.arguments); i < count; i++) {
                if (i > 0) {
                    lca_string_append_format(print_context->output, "%s, ", COL(COL_DELIM));
                }

                layec_value* argument = instruction->call.arguments[i];
                layec_value_print_to_string(argument, print_context->output, use_color);
            }

            lca_string_append_format(print_context->output, "%s)", COL(COL_DELIM));
        } break;
    }

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

    lca_string_append_format(
        print_context->output,
        "%s%s %s %s%.*s%s(",
        COL(COL_KEYWORD),
        is_declare ? "declare" : "define",
        ir_calling_convention_to_cstring(function->function.calling_convention),
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

void layec_type_print_to_string(layec_type* type, string* s, bool use_color) {
    assert(type != NULL);
    assert(s != NULL);

    switch (type->kind) {
        case LAYEC_TYPE_POINTER: {
            lca_string_append_format(s, "%sptr", COL(COL_KEYWORD));
        } break;

        case LAYEC_TYPE_VOID: {
            lca_string_append_format(s, "%svoid", COL(COL_KEYWORD));
        } break;

        case LAYEC_TYPE_ARRAY: {
            layec_type_print_to_string(type->array.element_type, s, use_color);
            lca_string_append_format(s, "%s[%s%lld%s]", COL(COL_DELIM), COL(COL_CONSTANT), type->array.length, COL(COL_DELIM));
        } break;

        case LAYEC_TYPE_INTEGER: {
            lca_string_append_format(s, "%sint%d", COL(COL_KEYWORD), type->primitive_bit_width);
        } break;

        case LAYEC_TYPE_FLOAT: {
            lca_string_append_format(s, "%sfloat%d", COL(COL_KEYWORD), type->primitive_bit_width);
        } break;
    }

    lca_string_append_format(s, "%s", COL(RESET));
}

void layec_value_print_to_string(layec_value* value, string* s, bool use_color) {
    assert(value != NULL);
    assert(s != NULL);

    switch (value->kind) {
        default: {
            if (value->name.count == 0) {
                lca_string_append_format(s, "%%%lld", COL(COL_NAME), value->index);
            } else {
                lca_string_append_format(s, "%%%.*s", COL(COL_NAME), STR_EXPAND(value->name));
            }
        } break;

        case LAYEC_IR_FUNCTION: {
            lca_string_append_format(s, "%s@%.*s", COL(COL_NAME), STR_EXPAND(value->function.name));
        } break;

        case LAYEC_IR_BLOCK: {
            if (value->block.name.count == 0) {
                lca_string_append_format(s, "%%%lld", COL(COL_NAME), value->block.index);
            } else {
                lca_string_append_format(s, "%%%.*s", COL(COL_NAME), STR_EXPAND(value->block.name));
            }
        } break;

        case LAYEC_IR_INTEGER_CONSTANT: {
            layec_type_print_to_string(value->type, s, use_color);
            lca_string_append_format(s, " %s%lld", COL(COL_CONSTANT), value->int_value);
        } break;
    }

    lca_string_append_format(s, "%s", COL(RESET));
}
