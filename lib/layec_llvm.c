#include <assert.h>

#include "layec.h"

#define LLVM_MEMCPY_INTRINSIC "llvm.memcpy.p0.p0.i64"

typedef struct llvm_codegen {
    layec_context* context;
    bool use_color;
    string* output;
} llvm_codegen;

static void llvm_print_module(llvm_codegen* codegen, layec_module* module);

string layec_codegen_llvm(layec_module* module) {
    assert(module != NULL);
    layec_context* context = layec_module_context(module);
    assert(context != NULL);

    string output_string = string_create(context->allocator);

    llvm_codegen codegen = {
        .context = context,
        .use_color = context->use_color,
        .output = &output_string,
    };

    llvm_print_module(&codegen, module);

    return output_string;
}

static void llvm_print_header(llvm_codegen* codegen, layec_module* module);
static void llvm_print_struct_type(llvm_codegen* codegen, layec_type* struct_type);
static void llvm_print_global(llvm_codegen* codegen, layec_value* global);
static void llvm_print_function(llvm_codegen* codegen, layec_value* function);

static void llvm_print_type(llvm_codegen* codegen, layec_type* type);
static void llvm_print_value(llvm_codegen* codegen, layec_value* value, bool include_type);

static void llvm_print_block(llvm_codegen* codegen, layec_value* block);
static void llvm_print_instruction(llvm_codegen* codegen, layec_value* instruction);

static void llvm_print_module(llvm_codegen* codegen, layec_module* module) {
    llvm_print_header(codegen, module);

    for (int64_t i = 0, count = layec_module_function_count(module); i < count; i++) {
        layec_value* function = layec_module_get_function_at_index(module, i);
        llvm_print_function(codegen, function);
    }
}

static void llvm_print_header(llvm_codegen* codegen, layec_module* module) {
    lca_string_append_format(codegen->output, "; ModuleID = '%.*s'\n", STR_EXPAND(layec_module_name(module)));
    lca_string_append_format(codegen->output, "source_filename = \"%.*s\"\n", STR_EXPAND(layec_module_name(module)));
    lca_string_append_format(codegen->output, "\n");
}

static void llvm_print_function(llvm_codegen* codegen, layec_value* function) {
    int64_t block_count = layec_function_block_count(function);

    lca_string_append_format(
        codegen->output,
        "%s ",
        (block_count == 0 ? "declare" : "define")
    );

    llvm_print_type(codegen, layec_function_return_type(function));

    lca_string_append_format(
        codegen->output,
        " @%.*s(",
        STR_EXPAND(layec_function_name(function))
    );

    lca_string_append_format(codegen->output, ")");

    if (block_count == 0) {
        lca_string_append_format(codegen->output, "\n\n");
        return;
    }

    lca_string_append_format(codegen->output, " {\n");

    for (int64_t i = 0; i < block_count; i++) {
        llvm_print_block(codegen, layec_function_get_block_at_index(function, i));
    }

    lca_string_append_format(codegen->output, "}\n\n");
}

static void llvm_print_type(llvm_codegen* codegen, layec_type* type) {
    switch (layec_type_get_kind(type)) {
        default: {
            fprintf(stderr, "for type kind %s\n", layec_type_kind_to_string(layec_type_get_kind(type)));
            assert(false && "unimplemented kind in llvm_print_type");
        } break;

        case LAYEC_TYPE_POINTER: {
            lca_string_append_format(codegen->output, "ptr");
        } break;

        case LAYEC_TYPE_VOID: {
            lca_string_append_format(codegen->output, "void");
        } break;

        case LAYEC_TYPE_INTEGER: {
            lca_string_append_format(codegen->output, "i%d", layec_type_size_in_bits(type));
        } break;
    }
}

static void llvm_print_block(llvm_codegen* codegen, layec_value* block) {
    int64_t instruction_count = layec_block_instruction_count(block);

    if (layec_block_has_name(block)) {
        lca_string_append_format(codegen->output, "%.*s:\n", STR_EXPAND(layec_block_name(block)));
    } else {
        lca_string_append_format(codegen->output, "%lld:\n", layec_block_index(block));
    }

    for (int64_t i = 0; i < instruction_count; i++) {
        layec_value* instruction = layec_block_get_instruction_at_index(block, i);
        llvm_print_instruction(codegen, instruction);
    }
}

static void llvm_print_instruction(llvm_codegen* codegen, layec_value* instruction) {
    layec_value_kind kind = layec_value_get_kind(instruction);

    lca_string_append_format(codegen->output, "  ");

    switch (kind) {
        default: {
            fprintf(stderr, "for value of kind %s\n", layec_value_kind_to_cstring(kind));
            assert(false && "unimplemented instruction in llvm_print_instruction");
        } break;

        case LAYEC_IR_UNREACHABLE: {
            lca_string_append_format(codegen->output, "unreachable");
        } break;

        case LAYEC_IR_CALL: {
            lca_string_append_format(codegen->output, "call ");
            llvm_print_type(codegen, layec_value_type(instruction));
            lca_string_append_format(codegen->output, " ");
            llvm_print_value(codegen, layec_value_callee(instruction), false);
            lca_string_append_format(codegen->output, "(");

            for (int64_t i = 0, count = layec_value_call_argument_count(instruction); i < count; i++) {
                if (i > 0) {
                    lca_string_append_format(codegen->output, ", ");
                }

                layec_value* argument = layec_value_call_get_argument_at_index(instruction, i);
                llvm_print_value(codegen, argument, true);
            }

            lca_string_append_format(codegen->output, ")");
        } break;
    }

    lca_string_append_format(codegen->output, "\n");
}

static void llvm_print_value(llvm_codegen* codegen, layec_value* value, bool include_type) {
    layec_value_kind kind = layec_value_get_kind(value);

    if (include_type) {
        llvm_print_type(codegen, layec_value_type(value));
        lca_string_append_format(codegen->output, " ");
    }

    switch (kind) {
        default: {
            lca_string_append_format(codegen->output, "%%%.*s", STR_EXPAND(layec_value_name(value)));
        } break;

        case LAYEC_IR_FUNCTION: {
            lca_string_append_format(codegen->output, "@%.*s", STR_EXPAND(layec_function_name(value)));
        } break;

        case LAYEC_IR_INTEGER_CONSTANT: {
            lca_string_append_format(codegen->output, "%lld", layec_integer_constant_value(value));
        } break;
    }
}
