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

    for (int64_t i = 0, count = layec_module_global_count(module); i < count; i++) {
        if (i > 0) lca_string_append_format(codegen->output, "\n");
        layec_value* global = layec_module_get_global_at_index(module, i);
        llvm_print_global(codegen, global);
    }

    if (layec_module_global_count(module) > 0) lca_string_append_format(codegen->output, "\n");

    for (int64_t i = 0, count = layec_module_function_count(module); i < count; i++) {
        if (i > 0) lca_string_append_format(codegen->output,  "\n");
        layec_value* function = layec_module_get_function_at_index(module, i);
        llvm_print_function(codegen, function);
    }
}

static void llvm_print_header(llvm_codegen* codegen, layec_module* module) {
    lca_string_append_format(codegen->output, "; ModuleID = '%.*s'\n", STR_EXPAND(layec_module_name(module)));
    lca_string_append_format(codegen->output, "source_filename = \"%.*s\"\n", STR_EXPAND(layec_module_name(module)));
    lca_string_append_format(codegen->output, "\n");
}

static void llvm_print_global(llvm_codegen* codegen, layec_value* global) {
    string_view name = layec_value_name(global);
    if (name.count == 0) {
        int64_t index = layec_value_index(global);
        lca_string_append_format(codegen->output, "@.global.%lld", index);
    } else {
        lca_string_append_format(codegen->output, "@%.*s", STR_EXPAND(name));
    }

    layec_linkage linkage = layec_value_linkage(global);
    lca_string_append_format(codegen->output, " = %s", linkage == LAYEC_LINK_IMPORTED ? "external" : "private");

    bool is_string = layec_global_is_string(global);

    if (is_string) {
        lca_string_append_format(codegen->output, " unnamed_addr constant");
    } else {
        lca_string_append_format(codegen->output, " global");
    }

    lca_string_append_format(codegen->output, " ");
    llvm_print_type(codegen, layec_instruction_alloca_type(global));

    layec_value* value = layec_instruction_value(global);
    if (value == NULL) {
        lca_string_append_format(codegen->output, " zeroinitializer");
    } else {
        lca_string_append_format(codegen->output, " ");
        llvm_print_value(codegen, value, false);
    }

    lca_string_append_format(codegen->output, ", align %d\n", layec_type_align_in_bytes(layec_value_get_type(global)));
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

    layec_type* function_type = layec_value_get_type(function);
    assert(layec_type_is_function(function_type));
    for (int64_t i = 0, count = layec_function_type_parameter_count(function_type); i < count; i++) {
        if (i > 0) {
            lca_string_append_format(codegen->output, ", ");
        }

        layec_type* parameter_type = layec_function_type_get_parameter_type_at_index(function_type, i);
        llvm_print_type(codegen, parameter_type);
        lca_string_append_format(codegen->output, " %%%lld", i);
    }

    if (layec_function_type_is_variadic(function_type)) {
        if (layec_function_type_parameter_count(function_type) != 0) {
            lca_string_append_format(codegen->output, ", ");
        }

        lca_string_append_format(codegen->output, "...");
    }

    lca_string_append_format(codegen->output, ")");

    if (block_count == 0) {
        lca_string_append_format(codegen->output, "\n\n");
        return;
    }

    lca_string_append_format(codegen->output, " {\n");

    for (int64_t i = 0; i < block_count; i++) {
        llvm_print_block(codegen, layec_function_get_block_at_index(function, i));
    }

    lca_string_append_format(codegen->output, "}\n");
}

static void llvm_print_type(llvm_codegen* codegen, layec_type* type) {
    switch (layec_type_get_kind(type)) {
        default: {
            fprintf(stderr, "for type kind %s\n", layec_type_kind_to_cstring(layec_type_get_kind(type)));
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

        case LAYEC_TYPE_ARRAY: {
            layec_type* element_type = layec_type_element_type(type);
            lca_string_append_format(codegen->output, "[%lld x ", layec_type_array_length(type));
            llvm_print_type(codegen, element_type);
            lca_string_append_format(codegen->output, "]");
        } break;
    }
}

static void llvm_print_block(llvm_codegen* codegen, layec_value* block) {
    int64_t instruction_count = layec_block_instruction_count(block);

    if (layec_block_has_name(block)) {
        lca_string_append_format(codegen->output, "%.*s:\n", STR_EXPAND(layec_block_name(block)));
    } else {
        lca_string_append_format(codegen->output, "_bb%lld:\n", layec_block_index(block));
    }

    for (int64_t i = 0; i < instruction_count; i++) {
        layec_value* instruction = layec_block_get_instruction_at_index(block, i);
        llvm_print_instruction(codegen, instruction);
    }
}

static void llvm_print_instruction(llvm_codegen* codegen, layec_value* instruction) {
    layec_value_kind kind = layec_value_get_kind(instruction);
    if (kind == LAYEC_IR_NOP) {
        return;
    }

    lca_string_append_format(codegen->output, "  ");

    if (!layec_type_is_void(layec_value_get_type(instruction))) {
        llvm_print_value(codegen, instruction, false);
        lca_string_append_format(codegen->output, " = ");
    }

    switch (kind) {
        default: {
            fprintf(stderr, "for value of kind %s\n", layec_value_kind_to_cstring(kind));
            assert(false && "unimplemented instruction in llvm_print_instruction");
        } break;

        case LAYEC_IR_UNREACHABLE: {
            lca_string_append_format(codegen->output, "unreachable");
        } break;

        case LAYEC_IR_RETURN: {
            lca_string_append_format(codegen->output, "ret ");
            if (layec_instruction_return_has_value(instruction)) {
                llvm_print_value(codegen, layec_instruction_return_value(instruction), true);
            } else {
                lca_string_append_format(codegen->output, "void");
            }
        } break;

        case LAYEC_IR_ALLOCA: {
            lca_string_append_format(codegen->output, "alloca ");
            llvm_print_type(codegen, layec_instruction_alloca_type(instruction));
            lca_string_append_format(codegen->output, ", i64 1");
        } break;

        case LAYEC_IR_STORE: {
            lca_string_append_format(codegen->output, "store ");
            llvm_print_value(codegen, layec_instruction_operand(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_instruction_address(instruction), true);
        } break;

        case LAYEC_IR_LOAD: {
            lca_string_append_format(codegen->output, "load ");
            llvm_print_type(codegen, layec_value_get_type(instruction));
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_instruction_address(instruction), true);
        } break;

        case LAYEC_IR_CALL: {
            lca_string_append_format(codegen->output, "call ");
            llvm_print_type(codegen, layec_value_type(instruction));
            lca_string_append_format(codegen->output, " ");
            llvm_print_value(codegen, layec_instruction_callee(instruction), false);
            lca_string_append_format(codegen->output, "(");

            for (int64_t i = 0, count = layec_instruction_call_argument_count(instruction); i < count; i++) {
                if (i > 0) {
                    lca_string_append_format(codegen->output, ", ");
                }

                layec_value* argument = layec_instruction_call_get_argument_at_index(instruction, i);
                llvm_print_value(codegen, argument, true);
            }

            lca_string_append_format(codegen->output, ")");
        } break;

        case LAYEC_IR_BRANCH: {
            lca_string_append_format(codegen->output, "br label");
            llvm_print_value(codegen, layec_branch_pass(instruction), false);
        } break;

        case LAYEC_IR_COND_BRANCH: {
            lca_string_append_format(codegen->output, "br i1 ");
            llvm_print_value(codegen, layec_instruction_value(instruction), false);
            lca_string_append_format(codegen->output, ", label ");
            llvm_print_value(codegen, layec_branch_pass(instruction), false);
            lca_string_append_format(codegen->output, ", label ");
            llvm_print_value(codegen, layec_branch_fail(instruction), false);
        } break;

        case LAYEC_IR_PHI: {
            lca_string_append_format(codegen->output, "phi ");
            llvm_print_type(codegen, layec_value_get_type(instruction));

            for (int64_t i = 0, count = layec_phi_incoming_value_count(instruction); i < count; i++) {
                if (i > 0) lca_string_append_format(codegen->output, ",");
                lca_string_append_format(codegen->output, " [ ");
                llvm_print_value(codegen, layec_phi_incoming_value_at_index(instruction, i), false);
                lca_string_append_format(codegen->output, ", ");
                llvm_print_value(codegen, layec_phi_incoming_block_at_index(instruction, i), false);
                lca_string_append_format(codegen->output, " ]");
            }
        } break;

        case LAYEC_IR_NEG: {
            layec_value* operand = layec_instruction_operand(instruction);
            lca_string_append_format(codegen->output, "sub ");
            llvm_print_type(codegen, layec_value_get_type(operand));
            lca_string_append_format(codegen->output, " 0, ");
            llvm_print_value(codegen, operand, false);
        } break;

        case LAYEC_IR_COMPL: {
            lca_string_append_format(codegen->output, "xor ");
            llvm_print_value(codegen, layec_instruction_operand(instruction), true);
            lca_string_append_format(codegen->output, ", -1");
        } break;

        case LAYEC_IR_ADD: {
            lca_string_append_format(codegen->output, "add ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_FADD: {
            lca_string_append_format(codegen->output, "fadd ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_SUB: {
            lca_string_append_format(codegen->output, "sub ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_FSUB: {
            lca_string_append_format(codegen->output, "fsub ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_MUL: {
            lca_string_append_format(codegen->output, "mul ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_FMUL: {
            lca_string_append_format(codegen->output, "fmul ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_SDIV: {
            lca_string_append_format(codegen->output, "sdiv ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_UDIV: {
            lca_string_append_format(codegen->output, "udiv ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_FDIV: {
            lca_string_append_format(codegen->output, "fdiv ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_SMOD: {
            lca_string_append_format(codegen->output, "srem ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_UMOD: {
            lca_string_append_format(codegen->output, "urem ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_FMOD: {
            lca_string_append_format(codegen->output, "frem ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_AND: {
            lca_string_append_format(codegen->output, "and ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_OR: {
            lca_string_append_format(codegen->output, "or ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_XOR: {
            lca_string_append_format(codegen->output, "xor ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_SHL: {
            lca_string_append_format(codegen->output, "shl ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_SAR: {
            lca_string_append_format(codegen->output, "ashr ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_SHR: {
            lca_string_append_format(codegen->output, "lshr ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_EQ: {
            lca_string_append_format(codegen->output, "icmp eq ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_NE: {
            lca_string_append_format(codegen->output, "icmp ne ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_SLT: {
            lca_string_append_format(codegen->output, "icmp slt ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_ULT: {
            lca_string_append_format(codegen->output, "icmp ult ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_SLE: {
            lca_string_append_format(codegen->output, "icmp sle ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_ULE: {
            lca_string_append_format(codegen->output, "icmp ule ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_SGT: {
            lca_string_append_format(codegen->output, "icmp sgt ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_UGT: {
            lca_string_append_format(codegen->output, "icmp ugt ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_SGE: {
            lca_string_append_format(codegen->output, "icmp sge ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
        } break;

        case LAYEC_IR_UGE: {
            lca_string_append_format(codegen->output, "icmp uge ");
            llvm_print_value(codegen, layec_binary_lhs(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, layec_binary_rhs(instruction), false);
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
            string_view name = layec_value_name(value);
            if (name.count == 0) {
                int64_t index = layec_value_index(value);
                lca_string_append_format(codegen->output, "%%%lld", index);
            } else {
                lca_string_append_format(codegen->output, "%%%.*s", STR_EXPAND(name));
            }
        } break;

        case LAYEC_IR_FUNCTION: {
            lca_string_append_format(codegen->output, "@%.*s", STR_EXPAND(layec_function_name(value)));
        } break;

        case LAYEC_IR_INTEGER_CONSTANT: {
            lca_string_append_format(codegen->output, "%lld", layec_value_integer_constant(value));
        } break;

        case LAYEC_IR_GLOBAL_VARIABLE: {
            string_view name = layec_value_name(value);
            if (name.count == 0) {
                int64_t index = layec_value_index(value);
                lca_string_append_format(codegen->output, "@.global.%lld", index);
            } else {
                lca_string_append_format(codegen->output, "@%.*s", STR_EXPAND(name));
            }
        } break;

        case LAYEC_IR_BLOCK: {
            if (layec_block_has_name(value)) {
                lca_string_append_format(codegen->output, "%%%.*s", STR_EXPAND(layec_block_name(value)));
            } else {
                lca_string_append_format(codegen->output, "%%_bb%lld", layec_block_index(value));
            }
        } break;

        case LAYEC_IR_ARRAY_CONSTANT: {
            bool is_string = layec_array_constant_is_string(value);
            if (is_string) {
                lca_string_append_format(codegen->output, "c\"");
                const uint8_t* data = (const uint8_t*)layec_array_constant_data(value);
                for (int64_t i = 0, count = layec_array_constant_length(value); i < count; i++) {
                    uint8_t c = data[i];
                    if (c < 32 || c > 127) {
                        lca_string_append_format(codegen->output, "\\%02X", (int)c);
                    } else {
                        lca_string_append_format(codegen->output, "%c", c);
                    }
                }
                lca_string_append_format(codegen->output, "\"");
            } else {
                lca_string_append_format(codegen->output, "{}");
                assert(false && "todo llvm_print_value non-string arrays");
            }
        } break;
    }
}
