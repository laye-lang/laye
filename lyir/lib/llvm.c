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

#define LCA_STR_NO_SHORT_NAMES
#include "lyir.h"

// declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)
#define LLVM_MEMCPY_INTRINSIC "llvm.memcpy.p0.p0.i64"

// declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)
#define LLVM_MEMSET_INTRINSIC "llvm.memset.p0.i64"

typedef struct llvm_codegen {
    lyir_context* context;
    bool use_color;
    lca_string* output;
} llvm_codegen;

static void llvm_print_module(llvm_codegen* codegen, lyir_module* module);

lca_string lyir_codegen_llvm(lyir_module* module) {
    assert(module != NULL);
    lyir_context* context = lyir_module_context(module);
    assert(context != NULL);

    lca_string output_string = lca_string_create(context->allocator);

    llvm_codegen codegen = {
        .context = context,
        .use_color = context->use_color,
        .output = &output_string,
    };

    llvm_print_module(&codegen, module);

    return output_string;
}

static void llvm_print_header(llvm_codegen* codegen, lyir_module* module);
static void llvm_print_struct_type(llvm_codegen* codegen, lyir_type* struct_type);
static void llvm_print_global(llvm_codegen* codegen, lyir_value* global);
static void llvm_print_function(llvm_codegen* codegen, lyir_value* function);

static void llvm_print_type_struct_literally(llvm_codegen* codegen, lyir_type* type);
static void llvm_print_type(llvm_codegen* codegen, lyir_type* type);
static void llvm_print_value(llvm_codegen* codegen, lyir_value* value, bool include_type);

static void llvm_print_block(llvm_codegen* codegen, lyir_value* block);
static void llvm_print_instruction(llvm_codegen* codegen, lyir_value* instruction);

static void llvm_print_module(llvm_codegen* codegen, lyir_module* module) {
    llvm_print_header(codegen, module);

    for (int64_t i = 0, count = lyir_module_global_count(module); i < count; i++) {
        if (i > 0) lca_string_append_format(codegen->output, "\n");
        lyir_value* global = lyir_module_get_global_at_index(module, i);
        llvm_print_global(codegen, global);
    }

    if (lyir_module_global_count(module) > 0) lca_string_append_format(codegen->output, "\n");

    for (int64_t i = 0, count = lyir_module_function_count(module); i < count; i++) {
        if (i > 0) lca_string_append_format(codegen->output,  "\n");
        lyir_value* function = lyir_module_get_function_at_index(module, i);
        llvm_print_function(codegen, function);
    }
}

static void llvm_print_header(llvm_codegen* codegen, lyir_module* module) {
    lca_string_append_format(codegen->output, "; ModuleID = '%.*s'\n", LCA_STR_EXPAND(lyir_module_name(module)));
    lca_string_append_format(codegen->output, "source_filename = \"%.*s\"\n", LCA_STR_EXPAND(lyir_module_name(module)));
    lca_string_append_format(codegen->output, "\n");

    for (int64_t i = 0; i < lyir_context_get_struct_type_count(codegen->context); i++) {
        lyir_type* struct_type = lyir_context_get_struct_type_at_index(codegen->context, i);
        if (lyir_type_struct_is_named(struct_type)) {
            lca_string_append_format(codegen->output, "%%%.*s = ", LCA_STR_EXPAND(lyir_type_struct_name_get(struct_type)));
            llvm_print_type_struct_literally(codegen, struct_type);
            lca_string_append_format(codegen->output, "\n");
        }
    }

    //lca_string_append_format(codegen->output, "declare void @%s(ptr, i8, i64, i1 immarg)\n", LLVM_MEMCPY_INTRINSIC);
    //lca_string_append_format(codegen->output, "\n");

    lca_string_append_format(codegen->output, "declare void @%s(ptr, i8, i64, i1 immarg)\n", LLVM_MEMSET_INTRINSIC);
    lca_string_append_format(codegen->output, "\n");
}

static void llvm_print_global(llvm_codegen* codegen, lyir_value* global) {
    lca_string_view name = lyir_value_name_get(global);
    if (name.count == 0) {
        int64_t index = lyir_value_index_get(global);
        lca_string_append_format(codegen->output, "@.global.%lld", index);
    } else {
        lca_string_append_format(codegen->output, "@%.*s", LCA_STR_EXPAND(name));
    }

    lyir_linkage linkage = lyir_value_linkage_get(global);
    lca_string_append_format(codegen->output, " = %s", linkage == LYIR_LINK_IMPORTED ? "external" : "private");

    bool is_string = lyir_value_global_is_string(global);

    if (is_string) {
        lca_string_append_format(codegen->output, " unnamed_addr constant");
    } else {
        lca_string_append_format(codegen->output, " global");
    }

    lca_string_append_format(codegen->output, " ");
    llvm_print_type(codegen, lyir_value_alloca_type_get(global));

    lyir_value* value = lyir_value_operand_get(global);
    if (value == NULL) {
        lca_string_append_format(codegen->output, " zeroinitializer");
    } else {
        lca_string_append_format(codegen->output, " ");
        llvm_print_value(codegen, value, false);
    }

    lca_string_append_format(codegen->output, ", align %d\n", lyir_type_align_in_bytes(lyir_value_type_get(global)));
}

static void llvm_print_function(llvm_codegen* codegen, lyir_value* function) {
    int64_t block_count = lyir_value_function_block_count_get(function);

    lca_string_append_format(
        codegen->output,
        "%s ",
        (block_count == 0 ? "declare" : "define")
    );

    llvm_print_type(codegen, lyir_value_function_return_type_get(function));

    lca_string_append_format(
        codegen->output,
        " @%.*s(",
        LCA_STR_EXPAND(lyir_value_function_name_get(function))
    );

    lyir_type* function_type = lyir_value_type_get(function);
    assert(lyir_type_is_function(function_type));
    for (int64_t i = 0, count = lyir_function_type_parameter_count_get(function_type); i < count; i++) {
        if (i > 0) {
            lca_string_append_format(codegen->output, ", ");
        }

        lyir_type* parameter_type = lyir_function_type_parameter_type_get_at_index(function_type, i);
        llvm_print_type(codegen, parameter_type);
        lca_string_append_format(codegen->output, " %%%lld", i);
    }

    if (lyir_function_type_is_variadic(function_type)) {
        if (lyir_function_type_parameter_count_get(function_type) != 0) {
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
        llvm_print_block(codegen, lyir_value_function_block_get_at_index(function, i));
    }

    lca_string_append_format(codegen->output, "}\n");
}

static void llvm_print_type_struct_literally(llvm_codegen* codegen, lyir_type* type) {
    lca_string_append_format(codegen->output, "type { ");

    for (int64_t i = 0; i < lyir_type_struct_member_count_get(type); i++) {
        if (i > 0) {
            lca_string_append_format(codegen->output, ", ");
        }

        lyir_type* member_type = lyir_type_struct_member_type_get_at_index(type, i);
        llvm_print_type(codegen, member_type);
    }

    lca_string_append_format(codegen->output, "}");
}

static void llvm_print_type(llvm_codegen* codegen, lyir_type* type) {
    switch (lyir_type_kind_get(type)) {
        default: {
            fprintf(stderr, "for type kind %s\n", lyir_type_kind_to_cstring(lyir_type_kind_get(type)));
            assert(false && "unimplemented kind in llvm_print_type");
        } break;

        case LYIR_TYPE_POINTER: {
            lca_string_append_format(codegen->output, "ptr");
        } break;

        case LYIR_TYPE_VOID: {
            lca_string_append_format(codegen->output, "void");
        } break;

        case LYIR_TYPE_INTEGER: {
            lca_string_append_format(codegen->output, "i%d", lyir_type_size_in_bits(type));
        } break;

        case LYIR_TYPE_FLOAT: {
            switch (lyir_type_size_in_bits(type)) {
                default: {
                    fprintf(stderr, "for bitwidth %d\n", lyir_type_size_in_bits(type));
                    assert(false && "unsupported float width");
                } break;

                case 32: {
                    lca_string_append_format(codegen->output, "float");
                } break;

                case 64: {
                    lca_string_append_format(codegen->output, "double");
                } break;
            }
        } break;

        case LYIR_TYPE_ARRAY: {
            lyir_type* element_type = lyir_type_element_type_get(type);
            lca_string_append_format(codegen->output, "[%lld x ", lyir_type_array_length_get(type));
            llvm_print_type(codegen, element_type);
            lca_string_append_format(codegen->output, "]");
        } break;

        case LYIR_TYPE_STRUCT: {
            if (lyir_type_struct_is_named(type)) {
                lca_string_append_format(codegen->output, "%%%.*s", LCA_STR_EXPAND(lyir_type_struct_name_get(type)));
            } else {
                llvm_print_type_struct_literally(codegen, type);
            }
        }
    }
}

static void llvm_print_block(llvm_codegen* codegen, lyir_value* block) {
    int64_t instruction_count = lyir_value_block_instruction_count_get(block);

    if (lyir_value_block_has_name(block)) {
        lca_string_append_format(codegen->output, "%.*s:\n", LCA_STR_EXPAND(lyir_value_block_name_get(block)));
    } else {
        lca_string_append_format(codegen->output, "_bb%lld:\n", lyir_value_block_index_get(block));
    }

    for (int64_t i = 0; i < instruction_count; i++) {
        lyir_value* instruction = lyir_value_block_instruction_get_at_index(block, i);
        llvm_print_instruction(codegen, instruction);
    }
}

static void llvm_print_instruction(llvm_codegen* codegen, lyir_value* instruction) {
    lyir_value_kind kind = lyir_value_kind_get(instruction);
    if (kind == LYIR_IR_NOP) {
        return;
    }

    lca_string_append_format(codegen->output, "  ");

    if (!lyir_type_is_void(lyir_value_type_get(instruction))) {
        llvm_print_value(codegen, instruction, false);
        lca_string_append_format(codegen->output, " = ");
    }

    switch (kind) {
        default: {
            fprintf(stderr, "for value of kind %s\n", lyir_value_kind_to_cstring(kind));
            assert(false && "unimplemented instruction in llvm_print_instruction");
        } break;

        case LYIR_IR_UNREACHABLE: {
            lca_string_append_format(codegen->output, "unreachable");
        } break;

        case LYIR_IR_RETURN: {
            lca_string_append_format(codegen->output, "ret ");
            if (lyir_value_return_has_value(instruction)) {
                llvm_print_value(codegen, lyir_value_return_value_get(instruction), true);
            } else {
                lca_string_append_format(codegen->output, "void");
            }
        } break;

        case LYIR_IR_ALLOCA: {
            lca_string_append_format(codegen->output, "alloca ");
            llvm_print_type(codegen, lyir_value_alloca_type_get(instruction));
            lca_string_append_format(codegen->output, ", i64 1");
        } break;

        case LYIR_IR_STORE: {
            lca_string_append_format(codegen->output, "store ");
            llvm_print_value(codegen, lyir_value_operand_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_address_get(instruction), true);
            lca_string_append_format(codegen->output, ", align %d", lyir_type_align_in_bytes(lyir_value_type_get(lyir_value_operand_get(instruction))));
        } break;

        case LYIR_IR_LOAD: {
            lca_string_append_format(codegen->output, "load ");
            llvm_print_type(codegen, lyir_value_type_get(instruction));
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_address_get(instruction), true);
        } break;

        case LYIR_IR_CALL: {
            lca_string_append_format(codegen->output, "call ");
            llvm_print_type(codegen, lyir_value_type_get(instruction));
            lca_string_append_format(codegen->output, " ");
            llvm_print_value(codegen, lyir_value_callee_get(instruction), false);
            lca_string_append_format(codegen->output, "(");

            for (int64_t i = 0, count = lyir_value_call_argument_count_get(instruction); i < count; i++) {
                if (i > 0) {
                    lca_string_append_format(codegen->output, ", ");
                }

                lyir_value* argument = lyir_value_call_argument_get_at_index(instruction, i);
                llvm_print_value(codegen, argument, true);
            }

            lca_string_append_format(codegen->output, ")");
        } break;

        case LYIR_IR_PTRADD: {
            lca_string_append_format(codegen->output, "getelementptr inbounds i8, ");
            llvm_print_value(codegen, lyir_value_address_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_operand_get(instruction), true);
        } break;

        case LYIR_IR_BUILTIN: {
            lyir_builtin_kind builtin_kind = lyir_value_builtin_kind_get(instruction);

            const char* intrinsic_name = "";
            switch (builtin_kind) {
                default: {
                    assert(false && "unsupported intrinsic in LLVM IR backend");
                }

                case LYIR_BUILTIN_MEMCOPY: intrinsic_name = LLVM_MEMCPY_INTRINSIC; break;
                case LYIR_BUILTIN_MEMSET: intrinsic_name = LLVM_MEMSET_INTRINSIC; break;
            }

            lca_string_append_format(codegen->output, "call void @%s(", intrinsic_name);

            for (int64_t i = 0, count = lyir_value_builtin_argument_count_get(instruction); i < count; i++) {
                if (i > 0) {
                    lca_string_append_format(codegen->output, ", ");
                }

                lyir_value* argument = lyir_value_builtin_argument_set_at_index(instruction, i);
                llvm_print_value(codegen, argument, true);
            }

            switch (builtin_kind) {
                default: {
                    assert(false && "unsupported intrinsic in LLVM IR backend");
                }

                case LYIR_BUILTIN_MEMCOPY:
                case LYIR_BUILTIN_MEMSET: lca_string_append_format(codegen->output, ", i1 false"); break;
            }

            lca_string_append_format(codegen->output, ")");
        } break;

        case LYIR_IR_BRANCH: {
            lca_string_append_format(codegen->output, "br label ");
            llvm_print_value(codegen, lyir_value_branch_pass_get(instruction), false);
        } break;

        case LYIR_IR_COND_BRANCH: {
            lca_string_append_format(codegen->output, "br i1 ");
            llvm_print_value(codegen, lyir_value_operand_get(instruction), false);
            lca_string_append_format(codegen->output, ", label ");
            llvm_print_value(codegen, lyir_value_branch_pass_get(instruction), false);
            lca_string_append_format(codegen->output, ", label ");
            llvm_print_value(codegen, lyir_value_branch_fail_get(instruction), false);
        } break;

        case LYIR_IR_PHI: {
            lca_string_append_format(codegen->output, "phi ");
            llvm_print_type(codegen, lyir_value_type_get(instruction));

            for (int64_t i = 0, count = lyir_value_phi_incoming_value_count_get(instruction); i < count; i++) {
                if (i > 0) lca_string_append_format(codegen->output, ",");
                lca_string_append_format(codegen->output, " [ ");
                llvm_print_value(codegen, lyir_phi_incoming_value_get_at_index(instruction, i), false);
                lca_string_append_format(codegen->output, ", ");
                llvm_print_value(codegen, lyir_phi_incoming_block_get_at_index(instruction, i), false);
                lca_string_append_format(codegen->output, " ]");
            }
        } break;

        case LYIR_IR_SEXT: {
            lca_string_append_format(codegen->output, "sext ");
            llvm_print_value(codegen, lyir_value_operand_get(instruction), true);
            lca_string_append_format(codegen->output, " to ");
            llvm_print_type(codegen, lyir_value_type_get(instruction));
        } break;

        case LYIR_IR_ZEXT: {
            lca_string_append_format(codegen->output, "zext ");
            llvm_print_value(codegen, lyir_value_operand_get(instruction), true);
            lca_string_append_format(codegen->output, " to ");
            llvm_print_type(codegen, lyir_value_type_get(instruction));
        } break;

        case LYIR_IR_TRUNC: {
            lca_string_append_format(codegen->output, "trunc ");
            llvm_print_value(codegen, lyir_value_operand_get(instruction), true);
            lca_string_append_format(codegen->output, " to ");
            llvm_print_type(codegen, lyir_value_type_get(instruction));
        } break;

        case LYIR_IR_BITCAST: {
            lca_string_append_format(codegen->output, "bitcast ");
            llvm_print_value(codegen, lyir_value_operand_get(instruction), true);
            lca_string_append_format(codegen->output, " to ");
            llvm_print_type(codegen, lyir_value_type_get(instruction));
        } break;

        case LYIR_IR_NEG: {
            lyir_value* operand = lyir_value_operand_get(instruction);
            if (lyir_type_is_float(lyir_value_type_get(operand))) {
                lca_string_append_format(codegen->output, "fsub ");
                llvm_print_type(codegen, lyir_value_type_get(operand));
                lca_string_append_format(codegen->output, " 0.0, ");
            } else {
                lca_string_append_format(codegen->output, "sub ");
                llvm_print_type(codegen, lyir_value_type_get(operand));
                lca_string_append_format(codegen->output, " 0, ");
            }
            llvm_print_value(codegen, operand, false);
        } break;

        case LYIR_IR_COMPL: {
            lca_string_append_format(codegen->output, "xor ");
            llvm_print_value(codegen, lyir_value_operand_get(instruction), true);
            lca_string_append_format(codegen->output, ", -1");
        } break;

        case LYIR_IR_FPTOUI: {
            lca_string_append_format(codegen->output, "fptoui ");
            llvm_print_value(codegen, lyir_value_operand_get(instruction), true);
            lca_string_append_format(codegen->output, " to ");
            llvm_print_type(codegen, lyir_value_type_get(instruction));
        } break;

        case LYIR_IR_FPTOSI: {
            lca_string_append_format(codegen->output, "fptosi ");
            llvm_print_value(codegen, lyir_value_operand_get(instruction), true);
            lca_string_append_format(codegen->output, " to ");
            llvm_print_type(codegen, lyir_value_type_get(instruction));
        } break;

        case LYIR_IR_UITOFP: {
            lca_string_append_format(codegen->output, "uitofp ");
            llvm_print_value(codegen, lyir_value_operand_get(instruction), true);
            lca_string_append_format(codegen->output, " to ");
            llvm_print_type(codegen, lyir_value_type_get(instruction));
        } break;

        case LYIR_IR_SITOFP: {
            lca_string_append_format(codegen->output, "sitofp ");
            llvm_print_value(codegen, lyir_value_operand_get(instruction), true);
            lca_string_append_format(codegen->output, " to ");
            llvm_print_type(codegen, lyir_value_type_get(instruction));
        } break;

        case LYIR_IR_FPEXT: {
            lca_string_append_format(codegen->output, "fpext ");
            llvm_print_value(codegen, lyir_value_operand_get(instruction), true);
            lca_string_append_format(codegen->output, " to ");
            llvm_print_type(codegen, lyir_value_type_get(instruction));
        } break;

        case LYIR_IR_FPTRUNC: {
            lca_string_append_format(codegen->output, "fptrunc ");
            llvm_print_value(codegen, lyir_value_operand_get(instruction), true);
            lca_string_append_format(codegen->output, " to ");
            llvm_print_type(codegen, lyir_value_type_get(instruction));
        } break;

        case LYIR_IR_ADD: {
            lca_string_append_format(codegen->output, "add ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_FADD: {
            lca_string_append_format(codegen->output, "fadd ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_SUB: {
            lca_string_append_format(codegen->output, "sub ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_FSUB: {
            lca_string_append_format(codegen->output, "fsub ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_MUL: {
            lca_string_append_format(codegen->output, "mul ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_FMUL: {
            lca_string_append_format(codegen->output, "fmul ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_SDIV: {
            lca_string_append_format(codegen->output, "sdiv ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_UDIV: {
            lca_string_append_format(codegen->output, "udiv ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_FDIV: {
            lca_string_append_format(codegen->output, "fdiv ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_SMOD: {
            lca_string_append_format(codegen->output, "srem ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_UMOD: {
            lca_string_append_format(codegen->output, "urem ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_FMOD: {
            lca_string_append_format(codegen->output, "frem ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_AND: {
            lca_string_append_format(codegen->output, "and ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_OR: {
            lca_string_append_format(codegen->output, "or ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_XOR: {
            lca_string_append_format(codegen->output, "xor ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_SHL: {
            lca_string_append_format(codegen->output, "shl ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_SAR: {
            lca_string_append_format(codegen->output, "ashr ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_SHR: {
            lca_string_append_format(codegen->output, "lshr ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_ICMP_EQ: {
            lca_string_append_format(codegen->output, "icmp eq ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_ICMP_NE: {
            lca_string_append_format(codegen->output, "icmp ne ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_ICMP_SLT: {
            lca_string_append_format(codegen->output, "icmp slt ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_ICMP_ULT: {
            lca_string_append_format(codegen->output, "icmp ult ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_ICMP_SLE: {
            lca_string_append_format(codegen->output, "icmp sle ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_ICMP_ULE: {
            lca_string_append_format(codegen->output, "icmp ule ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_ICMP_SGT: {
            lca_string_append_format(codegen->output, "icmp sgt ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_ICMP_UGT: {
            lca_string_append_format(codegen->output, "icmp ugt ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_ICMP_SGE: {
            lca_string_append_format(codegen->output, "icmp sge ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_ICMP_UGE: {
            lca_string_append_format(codegen->output, "icmp uge ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_FCMP_FALSE: {
            lca_string_append_format(codegen->output, "fcmp false ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_FCMP_OEQ: {
            lca_string_append_format(codegen->output, "fcmp oeq ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_FCMP_OGT: {
            lca_string_append_format(codegen->output, "fcmp ogt ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_FCMP_OGE: {
            lca_string_append_format(codegen->output, "fcmp oge ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_FCMP_OLT: {
            lca_string_append_format(codegen->output, "fcmp olt ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_FCMP_OLE: {
            lca_string_append_format(codegen->output, "fcmp ole ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_FCMP_ONE: {
            lca_string_append_format(codegen->output, "fcmp one ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_FCMP_ORD: {
            lca_string_append_format(codegen->output, "fcmp ord ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_FCMP_UEQ: {
            lca_string_append_format(codegen->output, "fcmp ueq ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_FCMP_UGT: {
            lca_string_append_format(codegen->output, "fcmp ugt ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_FCMP_UGE: {
            lca_string_append_format(codegen->output, "fcmp uge ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_FCMP_ULT: {
            lca_string_append_format(codegen->output, "fcmp ult ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_FCMP_ULE: {
            lca_string_append_format(codegen->output, "fcmp ule ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_FCMP_UNE: {
            lca_string_append_format(codegen->output, "fcmp une ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_FCMP_UNO: {
            lca_string_append_format(codegen->output, "fcmp uno ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;

        case LYIR_IR_FCMP_TRUE: {
            lca_string_append_format(codegen->output, "fcmp true ");
            llvm_print_value(codegen, lyir_value_lhs_get(instruction), true);
            lca_string_append_format(codegen->output, ", ");
            llvm_print_value(codegen, lyir_value_rhs_get(instruction), false);
        } break;
    }

    lca_string_append_format(codegen->output, "\n");
}

static void llvm_print_value(llvm_codegen* codegen, lyir_value* value, bool include_type) {
    lyir_value_kind kind = lyir_value_kind_get(value);

    if (include_type) {
        llvm_print_type(codegen, lyir_value_type_get(value));
        lca_string_append_format(codegen->output, " ");
    }

    switch (kind) {
        default: {
            lca_string_view name = lyir_value_name_get(value);
            if (name.count == 0) {
                int64_t index = lyir_value_index_get(value);
                lca_string_append_format(codegen->output, "%%%lld", index);
            } else {
                lca_string_append_format(codegen->output, "%%%.*s", LCA_STR_EXPAND(name));
            }
        } break;

        case LYIR_IR_FUNCTION: {
            lca_string_append_format(codegen->output, "@%.*s", LCA_STR_EXPAND(lyir_value_function_name_get(value)));
        } break;

        case LYIR_IR_INTEGER_CONSTANT: {
            int64_t ival = lyir_value_integer_constant_get(value);
            if (lyir_type_is_ptr(lyir_value_type_get(value)) && ival == 0)
                lca_string_append_format(codegen->output, "null");
            else lca_string_append_format(codegen->output, "%lld", ival);
        } break;

        case LYIR_IR_FLOAT_CONSTANT: {
            double float_value = lyir_value_float_constant_get(value);
            if (float_value == 0.0) {
                lca_string_append_format(codegen->output, "0.0");
            } else {
                lca_string_append_format(codegen->output, "%f", float_value);
            }
        } break;

        case LYIR_IR_GLOBAL_VARIABLE: {
            lca_string_view name = lyir_value_name_get(value);
            if (name.count == 0) {
                int64_t index = lyir_value_index_get(value);
                lca_string_append_format(codegen->output, "@.global.%lld", index);
            } else {
                lca_string_append_format(codegen->output, "@%.*s", LCA_STR_EXPAND(name));
            }
        } break;

        case LYIR_IR_BLOCK: {
            if (lyir_value_block_has_name(value)) {
                lca_string_append_format(codegen->output, "%%%.*s", LCA_STR_EXPAND(lyir_value_block_name_get(value)));
            } else {
                lca_string_append_format(codegen->output, "%%_bb%lld", lyir_value_block_index_get(value));
            }
        } break;

        case LYIR_IR_ARRAY_CONSTANT: {
            bool is_string = lyir_array_constant_is_string(value);
            if (is_string) {
                lca_string_append_format(codegen->output, "c\"");
                const uint8_t* data = (const uint8_t*)lyir_array_constant_data_get(value);
                for (int64_t i = 0, count = lyir_array_constant_length_get(value); i < count; i++) {
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
