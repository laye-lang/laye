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

#include "nob.h"

typedef struct cback_codegen {
    layec_context* context;
    bool use_color;
    string* output;
} cback_codegen;

static void cback_print_module(cback_codegen* codegen, layec_module* module);

string layec_codegen_c(layec_module* module) {
    assert(module != NULL);
    layec_context* context = layec_module_context(module);
    assert(context != NULL);

    string output_string = string_create(context->allocator);

    cback_codegen codegen = {
        .context = context,
        .use_color = context->use_color,
        .output = &output_string,
    };

    cback_print_module(&codegen, module);

    return output_string;
}

static void cback_print_header(cback_codegen* codegen, layec_module* module);
static void cback_declare_structs(cback_codegen* codegen, layec_context* context);
static void cback_define_structs(cback_codegen* codegen, layec_context* context);
static void cback_declare_function(cback_codegen* codegen, layec_value* function);
static void cback_define_function(cback_codegen* codegen, layec_value* function);

static void cback_print_global(cback_codegen* codegen, layec_value* global);

static void cback_print_type(cback_codegen* codegen, layec_type* type);
static void cback_print_value(cback_codegen* codegen, layec_value* value, bool include_type);

static void cback_print_module(cback_codegen* codegen, layec_module* module) {
    layec_context* context = layec_module_context(module);
    assert(context != NULL);

    cback_print_header(codegen, module);
    cback_declare_structs(codegen, context);
    cback_define_structs(codegen, context);

    for (int64_t i = 0, count = layec_module_global_count(module); i < count; i++) {
        if (i > 0) lca_string_append_format(codegen->output, "\n");
        layec_value* global = layec_module_get_global_at_index(module, i);
        cback_print_global(codegen, global);
    }

    if (layec_module_global_count(module) > 0) lca_string_append_format(codegen->output, "\n");

    for (int64_t i = 0, count = layec_module_function_count(module); i < count; i++) {
        //if (i > 0) lca_string_append_format(codegen->output,  "\n");
        layec_value* function = layec_module_get_function_at_index(module, i);
        cback_declare_function(codegen, function);
    }

    if (layec_module_function_count(module) > 0) lca_string_append_format(codegen->output,  "\n");

    for (int64_t i = 0, count = layec_module_function_count(module); i < count; i++) {
        if (i > 0) lca_string_append_format(codegen->output,  "\n");
        layec_value* function = layec_module_get_function_at_index(module, i);
        cback_define_function(codegen, function);
    }
}

static void cback_print_header(cback_codegen* codegen, layec_module* module) {
    lca_string_append_format(codegen->output, "// Source File: '%.*s'\n\n", STR_EXPAND(layec_module_name(module)));

    Nob_String_Builder builder = {0};
    nob_read_entire_file("./stage1/src/lyir_cir_preamble.h", &builder);
    string_append_format(codegen->output, "%.*s\n", (int)builder.count, builder.items);
    nob_sb_free(builder);
}

static void cback_declare_structs(cback_codegen* codegen, layec_context* context) {
    for (int64_t i = 0; i < layec_context_get_struct_type_count(codegen->context); i++) {
        layec_type* struct_type = layec_context_get_struct_type_at_index(codegen->context, i);
        if (layec_type_struct_is_named(struct_type)) {
            lca_string_append_format(codegen->output, "typedef struct %.*s %.*s;\n", STR_EXPAND(layec_type_struct_name(struct_type)), STR_EXPAND(layec_type_struct_name(struct_type)));
        }
    }

    if (layec_context_get_struct_type_count(codegen->context) > 0) {
        lca_string_append_format(codegen->output, "\n");
    }
}

static void cback_define_structs(cback_codegen* codegen, layec_context* context) {
    for (int64_t i = 0; i < layec_context_get_struct_type_count(codegen->context); i++) {
        layec_type* struct_type = layec_context_get_struct_type_at_index(codegen->context, i);
        if (layec_type_struct_is_named(struct_type)) {
            lca_string_append_format(codegen->output, "struct %.*s {\n", STR_EXPAND(layec_type_struct_name(struct_type)), STR_EXPAND(layec_type_struct_name(struct_type)));

            for (int64_t member_index = 0; member_index < layec_type_struct_member_count(struct_type); member_index++) {
                lca_string_append_format(codegen->output, "    ");

                layec_type* member_type = layec_type_struct_get_member_type_at_index(struct_type, member_index);
                cback_print_type(codegen, member_type);

                lca_string_append_format(codegen->output, " member_%d;\n", member_index);
            }
        
            lca_string_append_format(codegen->output, "};\n\n");
        }
    }
}

static void cback_print_block_name(cback_codegen* codegen, layec_value* block) {
    assert(codegen != NULL);
    assert(block != NULL);

    if (layec_block_has_name(block)) {
        string_view block_name = layec_block_name(block);
        // TODO(local): probably need to sanitize this
        lca_string_append_format(codegen->output, "%.*s", STR_EXPAND(block_name));
    } else {
        lca_string_append_format(codegen->output, "lyir_block_idx_%ld", layec_block_index(block));
    }
}

static void cback_print_global(cback_codegen* codegen, layec_value* global) {
}

static void cback_print_function_prototype(cback_codegen* codegen, layec_value* function) {
    cback_print_type(codegen, layec_function_return_type(function));
    lca_string_append_format(codegen->output, " %.*s(", STR_EXPAND(layec_function_name(function)));

    for (int64_t i = 0; i < layec_function_parameter_count(function); i++) {
        if (i > 0) lca_string_append_format(codegen->output, ", ");

        layec_value* param = layec_function_get_parameter_at_index(function, i);
        assert(param != NULL);

        layec_type* param_type = layec_value_get_type(param);
        assert(param_type != NULL);

        cback_print_type(codegen, param_type);
        lca_string_append_format(codegen->output, " %.*s", STR_EXPAND(layec_value_name(param)));
    }

    if (layec_function_is_variadic(function)) {
        if (layec_function_parameter_count(function) > 0) {
            lca_string_append_format(codegen->output, ", ...");
        } else {
            lca_string_append_format(codegen->output, "...");
        }
    }

    lca_string_append_format(codegen->output, ")");
}

static void cback_declare_function(cback_codegen* codegen, layec_value* function) {
    cback_print_function_prototype(codegen, function);
    lca_string_append_format(codegen->output, ";\n");
}

static void cback_define_function(cback_codegen* codegen, layec_value* function) {
    cback_print_function_prototype(codegen, function);
    lca_string_append_format(codegen->output, " {\n");

    for (int64_t block_index = 0; block_index < layec_function_block_count(function); block_index++) {
        layec_value* block = layec_function_get_block_at_index(function, block_index);
        assert(block != NULL);

        cback_print_block_name(codegen, block);
        lca_string_append_format(codegen->output, ":;\n");
        for (int64_t inst_index = 0; inst_index < layec_block_instruction_count(block); inst_index++) {
            layec_value* inst = layec_block_get_instruction_at_index(block, inst_index);
            assert(inst != NULL);

            lca_string_append_format(codegen->output, "    ");

            if (!layec_type_is_void(layec_value_get_type(inst))) {
                cback_print_value(codegen, inst, true);
                lca_string_append_format(codegen->output, " = ");
            }
            
            switch (layec_value_get_kind(inst)) {
                default: {
                    fprintf(stderr, "for lyir type '%s'\n", layec_value_kind_to_cstring(layec_value_get_kind(inst)));
                    //assert(false && "unhandled LYIR instruction in C backend\n");
                    lca_string_append_format(codegen->output, "<<%s>>;", layec_value_kind_to_cstring(layec_value_get_kind(inst)));
                } break;

                case LAYEC_IR_RETURN: {
                    lca_string_append_format(codegen->output, "return");

                    if (layec_instruction_return_has_value(inst)) {
                        lca_string_append_format(codegen->output, " ");
                        cback_print_value(codegen, layec_instruction_return_value(inst), false);
                    }

                    lca_string_append_format(codegen->output, ";");
                } break;

                case LAYEC_IR_BRANCH: {
                    lca_string_append_format(codegen->output, "goto ");
                    cback_print_block_name(codegen, layec_instruction_branch_get_pass(inst));
                    lca_string_append_format(codegen->output, ";");
                } break;

                case LAYEC_IR_COND_BRANCH: {
                    layec_value* condition_value = layec_instruction_get_value(inst);
                    layec_value* pass_block = layec_instruction_branch_get_pass(inst);
                    layec_value* fail_block = layec_instruction_branch_get_fail(inst);
                    lca_string_append_format(codegen->output, "if (");
                    cback_print_value(codegen, condition_value, false);
                    lca_string_append_format(codegen->output, ") { goto ");
                    cback_print_block_name(codegen, pass_block);
                    lca_string_append_format(codegen->output, "; } else { goto ");
                    cback_print_block_name(codegen, fail_block);
                    lca_string_append_format(codegen->output, "; }");
                } break;

                case LAYEC_IR_ALLOCA: {
                    lca_string_append_format(codegen->output, "{0};");
                } break;

                case LAYEC_IR_STORE: {
                    lca_string_append_format(codegen->output, "*(");
                    cback_print_type(codegen, layec_value_get_type(layec_instruction_get_operand(inst)));
                    lca_string_append_format(codegen->output, "*)(");
                    cback_print_value(codegen, layec_instruction_get_address(inst), false);
                    lca_string_append_format(codegen->output, ") = ");
                    cback_print_value(codegen, layec_instruction_get_operand(inst), false);
                    lca_string_append_format(codegen->output, ";");
                } break;

                case LAYEC_IR_LOAD: {
                    lca_string_append_format(codegen->output, "*(");
                    cback_print_type(codegen, layec_value_get_type(inst));
                    lca_string_append_format(codegen->output, "*)(");
                    cback_print_value(codegen, layec_instruction_get_address(inst), false);
                    lca_string_append_format(codegen->output, ");");
                } break;

                case LAYEC_IR_ADD: {
                    lca_string_append_format(codegen->output, "(");
                    cback_print_value(codegen, layec_instruction_binary_get_lhs(inst), false);
                    lca_string_append_format(codegen->output, ") + (");
                    cback_print_value(codegen, layec_instruction_binary_get_rhs(inst), false);
                    lca_string_append_format(codegen->output, ");");
                } break;

                case LAYEC_IR_SUB: {
                    lca_string_append_format(codegen->output, "(");
                    cback_print_value(codegen, layec_instruction_binary_get_lhs(inst), false);
                    lca_string_append_format(codegen->output, ") - (");
                    cback_print_value(codegen, layec_instruction_binary_get_rhs(inst), false);
                    lca_string_append_format(codegen->output, ");");
                } break;

                case LAYEC_IR_ICMP_SLT: {
                    lca_string_append_format(codegen->output, "(");
                    cback_print_value(codegen, layec_instruction_binary_get_lhs(inst), false);
                    lca_string_append_format(codegen->output, ") < (");
                    cback_print_value(codegen, layec_instruction_binary_get_rhs(inst), false);
                    lca_string_append_format(codegen->output, ");");
                } break;

                case LAYEC_IR_CALL: {
                    cback_print_value(codegen, layec_instruction_callee(inst), false);
                    lca_string_append_format(codegen->output, "(");

                    for (int64_t i = 0, count = layec_instruction_call_argument_count(inst); i < count; i++) {
                        if (i > 0) {
                            lca_string_append_format(codegen->output, ", ");
                        }

                        layec_value* argument = layec_instruction_call_get_argument_at_index(inst, i);
                        cback_print_value(codegen, argument, false);
                    }

                    lca_string_append_format(codegen->output, ");");
                } break;
            }

            lca_string_append_format(codegen->output, "\n");
        }
    }

    lca_string_append_format(codegen->output, "}\n");
}

static void cback_print_type(cback_codegen* codegen, layec_type* type) {
    switch (layec_type_get_kind(type)) {
        default: {
            fprintf(stderr, "for lyir type '%s'\n", layec_type_kind_to_cstring(layec_type_get_kind(type)));
            assert(false && "unhandled type kind in C backend");
        } break;

        case LAYEC_TYPE_VOID: {
            lca_string_append_format(codegen->output, "void");
        } break;

        case LAYEC_TYPE_POINTER: {
            lca_string_append_format(codegen->output, "lyir_ptr");
        } break;

        case LAYEC_TYPE_INTEGER: {
            int bit_width = layec_type_size_in_bits(type);
            if (bit_width == 1) {
                lca_string_append_format(codegen->output, "lyir_bool");
            } else if (bit_width == 8) {
                lca_string_append_format(codegen->output, "lyir_i8");
            } else if (bit_width == 16) {
                lca_string_append_format(codegen->output, "lyir_i16");
            } else if (bit_width == 32) {
                lca_string_append_format(codegen->output, "lyir_i32");
            } else if (bit_width == 64) {
                lca_string_append_format(codegen->output, "lyir_i64");
            } else {
                lca_string_append_format(codegen->output, "_BitInt(%d)", bit_width);
                //fprintf(stderr, "unsupported bit width: %d\n", bit_width);
                //assert(false && "unsupported int bit width in C backend");
            }
        } break;

        case LAYEC_TYPE_FLOAT: {
            int bit_width = layec_type_size_in_bits(type);
            if (bit_width == 32) {
                lca_string_append_format(codegen->output, "lyir_f32");
            } else if (bit_width == 64) {
                lca_string_append_format(codegen->output, "lyir_f64");
            } else {
                fprintf(stderr, "unsupported bit width: %d\n", bit_width);
                assert(false && "unsupported float bit width in C backend");
            }
        } break;
    }
}

static void cback_print_value(cback_codegen* codegen, layec_value* value, bool include_type) {
    if (include_type) {
        cback_print_type(codegen, layec_value_get_type(value));
        lca_string_append_format(codegen->output, " ");
    }

    switch (layec_value_get_kind(value)) {
        default: {
            string_view name = layec_value_name(value);
            if (name.count == 0) {
                int64_t index = layec_value_index(value);
                lca_string_append_format(codegen->output, "lyir_anon_value_%lld", index);
            } else {
                lca_string_append_format(codegen->output, "%.*s", STR_EXPAND(name));
            }
        } break;

        case LAYEC_IR_FUNCTION: {
            lca_string_append_format(codegen->output, "%.*s", STR_EXPAND(layec_function_name(value)));
        } break;

        case LAYEC_IR_GLOBAL_VARIABLE: {
            string_view name = layec_value_name(value);
            if (name.count == 0) {
                int64_t index = layec_value_index(value);
                lca_string_append_format(codegen->output, "lyir_global_idx_%lld", index);
            } else {
                lca_string_append_format(codegen->output, "%.*s", STR_EXPAND(name));
            }
        } break;

        case LAYEC_IR_INTEGER_CONSTANT: {
            int64_t ival = layec_value_integer_constant(value);
            if (layec_type_is_ptr(layec_value_get_type(value)) && ival == 0)
                lca_string_append_format(codegen->output, "NULL");
            else lca_string_append_format(codegen->output, "%lld", ival);
        } break;

        case LAYEC_IR_FLOAT_CONSTANT: {
            double float_value = layec_value_float_constant(value);
            lca_string_append_format(codegen->output, "%f", float_value);
        } break;

        case LAYEC_IR_ALLOCA: {
            if (!include_type) lca_string_append_format(codegen->output, "&");
            string_view name = layec_value_name(value);
            if (name.count == 0) {
                int64_t index = layec_value_index(value);
                lca_string_append_format(codegen->output, "lyir_anon_value_%lld", index);
            } else {
                lca_string_append_format(codegen->output, "%.*s", STR_EXPAND(name));
            }
        } break;
    }
}
