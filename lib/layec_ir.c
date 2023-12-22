#include "layec.h"

#include <assert.h>

static void layec_value_destroy(layec_value* value);

struct layec_module {
    layec_context* context;
    string name;

    lca_arena* arena;
    dynarr(layec_value*) functions;
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

    // for values which want their uses tracked, a list of all users of this value.
    dynarr(layec_value*) users;

    layec_value* parent_block;

    union {
        int64_t int_value;

        struct {
            string name;
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

    assert(module->arena != NULL);
    lca_arena_destroy(module->arena);

    arr_free(module->functions);

    *module = (layec_module){};
    lca_deallocate(allocator, module);
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
    }

    block->block.instructions[insert_index] = instruction;
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

// IR Printer

#define COL_COMMENT WHITE
#define COL_DELIM WHITE
#define COL_KEYWORD RED
#define COL_NAME GREEN
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
        is_declare ? "" : " {\n",
        COL(RESET)
    );

    if (!is_declare) {
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
    assert(false && "todo layec_value_print_to_string");
}
