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

#include "nob.h"

typedef struct cback_codegen {
    lyir_context* context;
    bool use_color;
    lca_string* output;
} cback_codegen;

static void cback_print_module(cback_codegen* codegen, lyir_module* module);

lca_string lyir_codegen_c(lyir_module* module) {
    assert(module != NULL);
    lyir_context* context = lyir_module_context(module);
    assert(context != NULL);

    lca_string output_string = lca_string_create(context->allocator);

    cback_codegen codegen = {
        .context = context,
        .use_color = context->use_color,
        .output = &output_string,
    };

    cback_print_module(&codegen, module);

    return output_string;
}

static void cback_print_header(cback_codegen* codegen, lyir_module* module);
static void cback_declare_structs(cback_codegen* codegen, lyir_context* context);
static void cback_define_structs(cback_codegen* codegen, lyir_context* context);
static void cback_declare_function(cback_codegen* codegen, lyir_value* function);
static void cback_define_function(cback_codegen* codegen, lyir_value* function);

static void cback_print_global(cback_codegen* codegen, lyir_value* global);

static void cback_print_type(cback_codegen* codegen, lyir_type* type);
static void cback_print_value(cback_codegen* codegen, lyir_value* value, bool include_type);

static void cback_print_module(cback_codegen* codegen, lyir_module* module) {
    lyir_context* context = lyir_module_context(module);
    assert(context != NULL);

    cback_print_header(codegen, module);
    cback_declare_structs(codegen, context);
    cback_define_structs(codegen, context);

    for (int64_t i = 0, count = lyir_module_global_count(module); i < count; i++) {
        if (i > 0) lca_string_append_format(codegen->output, "\n");
        lyir_value* global = lyir_module_get_global_at_index(module, i);
        cback_print_global(codegen, global);
    }

    if (lyir_module_global_count(module) > 0) lca_string_append_format(codegen->output, "\n");

    for (int64_t i = 0, count = lyir_module_function_count(module); i < count; i++) {
        //if (i > 0) lca_string_append_format(codegen->output,  "\n");
        lyir_value* function = lyir_module_get_function_at_index(module, i);
        cback_declare_function(codegen, function);
    }

    if (lyir_module_function_count(module) > 0) lca_string_append_format(codegen->output,  "\n");

    for (int64_t i = 0, count = lyir_module_function_count(module); i < count; i++) {
        if (i > 0) lca_string_append_format(codegen->output,  "\n");
        lyir_value* function = lyir_module_get_function_at_index(module, i);
        cback_define_function(codegen, function);
    }
}

static void cback_print_header(cback_codegen* codegen, lyir_module* module) {
    lca_string_append_format(codegen->output, "// Source File: '%.*s'\n\n", LCA_STR_EXPAND(lyir_module_name(module)));

    Nob_String_Builder builder = {0};
    nob_read_entire_file("./stage1/src/lyir_cir_preamble.h", &builder);
    lca_string_append_format(codegen->output, "%.*s\n", (int)builder.count, builder.items);
    nob_sb_free(builder);
}

static void cback_declare_structs(cback_codegen* codegen, lyir_context* context) {
    for (int64_t i = 0; i < lyir_context_get_struct_type_count(codegen->context); i++) {
        lyir_type* struct_type = lyir_context_get_struct_type_at_index(codegen->context, i);
        if (lyir_type_struct_is_named(struct_type)) {
            lca_string_append_format(codegen->output, "typedef struct %.*s %.*s;\n", LCA_STR_EXPAND(lyir_type_struct_name_get(struct_type)), LCA_STR_EXPAND(lyir_type_struct_name_get(struct_type)));
        }
    }

    if (lyir_context_get_struct_type_count(codegen->context) > 0) {
        lca_string_append_format(codegen->output, "\n");
    }
}

static void cback_define_structs(cback_codegen* codegen, lyir_context* context) {
    for (int64_t i = 0; i < lyir_context_get_struct_type_count(codegen->context); i++) {
        lyir_type* struct_type = lyir_context_get_struct_type_at_index(codegen->context, i);
        if (lyir_type_struct_is_named(struct_type)) {
            lca_string_append_format(codegen->output, "struct %.*s {\n", LCA_STR_EXPAND(lyir_type_struct_name_get(struct_type)), LCA_STR_EXPAND(lyir_type_struct_name_get(struct_type)));

            for (int64_t member_index = 0; member_index < lyir_type_struct_member_count_get(struct_type); member_index++) {
                lca_string_append_format(codegen->output, "    ");

                lyir_type* member_type = lyir_type_struct_member_type_get_at_index(struct_type, member_index);
                cback_print_type(codegen, member_type);

                lca_string_append_format(codegen->output, " member_%d;\n", member_index);
            }
        
            lca_string_append_format(codegen->output, "};\n\n");
        }
    }
}

static void cback_print_block_name(cback_codegen* codegen, lyir_value* block) {
    assert(codegen != NULL);
    assert(block != NULL);

    if (lyir_value_block_has_name(block)) {
        lca_string_view block_name = lyir_value_block_name_get(block);
        // TODO(local): probably need to sanitize this
        lca_string_append_format(codegen->output, "%.*s", LCA_STR_EXPAND(block_name));
    } else {
        lca_string_append_format(codegen->output, "lyir_bb_%ld", lyir_value_block_index_get(block));
    }
}

static void cback_print_global(cback_codegen* codegen, lyir_value* global) {
}

static void cback_print_function_prototype(cback_codegen* codegen, lyir_value* function) {
    cback_print_type(codegen, lyir_value_function_return_type_get(function));
    lca_string_append_format(codegen->output, " %.*s(", LCA_STR_EXPAND(lyir_value_function_name_get(function)));

    for (int64_t i = 0; i < lyir_value_function_parameter_count_get(function); i++) {
        if (i > 0) lca_string_append_format(codegen->output, ", ");

        lyir_value* param = lyir_value_function_parameter_get_at_index(function, i);
        assert(param != NULL);

        lyir_type* param_type = lyir_value_type_get(param);
        assert(param_type != NULL);

        cback_print_type(codegen, param_type);
        lca_string_append_format(codegen->output, " %.*s", LCA_STR_EXPAND(lyir_value_name_get(param)));
    }

    if (lyir_value_function_is_variadic(function)) {
        if (lyir_value_function_parameter_count_get(function) > 0) {
            lca_string_append_format(codegen->output, ", ...");
        } else {
            lca_string_append_format(codegen->output, "...");
        }
    }

    lca_string_append_format(codegen->output, ")");
}

static void cback_declare_function(cback_codegen* codegen, lyir_value* function) {
    cback_print_function_prototype(codegen, function);
    lca_string_append_format(codegen->output, ";\n");
}

static void cback_define_function(cback_codegen* codegen, lyir_value* function) {
    cback_print_function_prototype(codegen, function);
    lca_string_append_format(codegen->output, " {\n");

    for (int64_t block_index = 0; block_index < lyir_value_function_block_count_get(function); block_index++) {
        lyir_value* block = lyir_value_function_block_get_at_index(function, block_index);
        assert(block != NULL);

        cback_print_block_name(codegen, block);
        lca_string_append_format(codegen->output, ":;\n");
        for (int64_t inst_index = 0; inst_index < lyir_value_block_instruction_count_get(block); inst_index++) {
            lyir_value* inst = lyir_value_block_instruction_get_at_index(block, inst_index);
            assert(inst != NULL);

            lca_string_append_format(codegen->output, "    ");

            if (!lyir_type_is_void(lyir_value_type_get(inst))) {
                cback_print_value(codegen, inst, true);
                lca_string_append_format(codegen->output, " = ");
            }
            
            switch (lyir_value_kind_get(inst)) {
                default: {
                    fprintf(stderr, "for lyir type '%s'\n", lyir_value_kind_to_cstring(lyir_value_kind_get(inst)));
                    //assert(false && "unhandled LYIR instruction in C backend\n");
                    lca_string_append_format(codegen->output, "<<%s>>;", lyir_value_kind_to_cstring(lyir_value_kind_get(inst)));
                } break;

                case LYIR_IR_RETURN: {
                    lca_string_append_format(codegen->output, "return");

                    if (lyir_value_return_has_value(inst)) {
                        lca_string_append_format(codegen->output, " ");
                        cback_print_value(codegen, lyir_value_return_value_get(inst), false);
                    }

                    lca_string_append_format(codegen->output, ";");
                } break;

                case LYIR_IR_BRANCH: {
                    lca_string_append_format(codegen->output, "goto ");
                    cback_print_block_name(codegen, lyir_value_branch_pass_get(inst));
                    lca_string_append_format(codegen->output, ";");
                } break;

                case LYIR_IR_COND_BRANCH: {
                    lyir_value* condition_value = lyir_value_operand_get(inst);
                    lyir_value* pass_block = lyir_value_branch_pass_get(inst);
                    lyir_value* fail_block = lyir_value_branch_fail_get(inst);
                    lca_string_append_format(codegen->output, "if (");
                    cback_print_value(codegen, condition_value, false);
                    lca_string_append_format(codegen->output, ") { goto ");
                    cback_print_block_name(codegen, pass_block);
                    lca_string_append_format(codegen->output, "; } else { goto ");
                    cback_print_block_name(codegen, fail_block);
                    lca_string_append_format(codegen->output, "; }");
                } break;

                case LYIR_IR_ALLOCA: {
                    lca_string_append_format(codegen->output, "{0};");
                } break;

                case LYIR_IR_STORE: {
                    lca_string_append_format(codegen->output, "*(");
                    cback_print_type(codegen, lyir_value_type_get(lyir_value_operand_get(inst)));
                    lca_string_append_format(codegen->output, "*)(");
                    cback_print_value(codegen, lyir_value_address_get(inst), false);
                    lca_string_append_format(codegen->output, ") = ");
                    cback_print_value(codegen, lyir_value_operand_get(inst), false);
                    lca_string_append_format(codegen->output, ";");
                } break;

                case LYIR_IR_LOAD: {
                    lca_string_append_format(codegen->output, "*(");
                    cback_print_type(codegen, lyir_value_type_get(inst));
                    lca_string_append_format(codegen->output, "*)(");
                    cback_print_value(codegen, lyir_value_address_get(inst), false);
                    lca_string_append_format(codegen->output, ");");
                } break;

                case LYIR_IR_ADD: {
                    lca_string_append_format(codegen->output, "(");
                    cback_print_value(codegen, lyir_value_lhs_get(inst), false);
                    lca_string_append_format(codegen->output, ") + (");
                    cback_print_value(codegen, lyir_value_rhs_get(inst), false);
                    lca_string_append_format(codegen->output, ");");
                } break;

                case LYIR_IR_SUB: {
                    lca_string_append_format(codegen->output, "(");
                    cback_print_value(codegen, lyir_value_lhs_get(inst), false);
                    lca_string_append_format(codegen->output, ") - (");
                    cback_print_value(codegen, lyir_value_rhs_get(inst), false);
                    lca_string_append_format(codegen->output, ");");
                } break;

                case LYIR_IR_ICMP_SLT: {
                    lca_string_append_format(codegen->output, "(");
                    cback_print_value(codegen, lyir_value_lhs_get(inst), false);
                    lca_string_append_format(codegen->output, ") < (");
                    cback_print_value(codegen, lyir_value_rhs_get(inst), false);
                    lca_string_append_format(codegen->output, ");");
                } break;

                case LYIR_IR_CALL: {
                    cback_print_value(codegen, lyir_value_callee_get(inst), false);
                    lca_string_append_format(codegen->output, "(");

                    for (int64_t i = 0, count = lyir_value_call_argument_count_get(inst); i < count; i++) {
                        if (i > 0) {
                            lca_string_append_format(codegen->output, ", ");
                        }

                        lyir_value* argument = lyir_value_call_argument_get_at_index(inst, i);
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

static void cback_print_type(cback_codegen* codegen, lyir_type* type) {
    switch (lyir_type_kind_get(type)) {
        default: {
            fprintf(stderr, "for lyir type '%s'\n", lyir_type_kind_to_cstring(lyir_type_kind_get(type)));
            assert(false && "unhandled type kind in C backend");
        } break;

        case LYIR_TYPE_VOID: {
            lca_string_append_format(codegen->output, "void");
        } break;

        case LYIR_TYPE_POINTER: {
            lca_string_append_format(codegen->output, "lyir_ptr");
        } break;

        case LYIR_TYPE_INTEGER: {
            int bit_width = lyir_type_size_in_bits(type);
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

        case LYIR_TYPE_FLOAT: {
            int bit_width = lyir_type_size_in_bits(type);
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

static void cback_print_value(cback_codegen* codegen, lyir_value* value, bool include_type) {
    if (include_type) {
        cback_print_type(codegen, lyir_value_type_get(value));
        lca_string_append_format(codegen->output, " ");
    }

    switch (lyir_value_kind_get(value)) {
        default: {
            lca_string_view name = lyir_value_name_get(value);
            if (name.count == 0) {
                int64_t index = lyir_value_index_get(value);
                lca_string_append_format(codegen->output, "lyir_inst_%lld", index);
            } else {
                lca_string_append_format(codegen->output, "%.*s", LCA_STR_EXPAND(name));
            }
        } break;

        case LYIR_IR_FUNCTION: {
            lca_string_append_format(codegen->output, "%.*s", LCA_STR_EXPAND(lyir_value_function_name_get(value)));
        } break;

        case LYIR_IR_GLOBAL_VARIABLE: {
            lca_string_view name = lyir_value_name_get(value);
            if (name.count == 0) {
                int64_t index = lyir_value_index_get(value);
                lca_string_append_format(codegen->output, "lyir_glbl_%lld", index);
            } else {
                lca_string_append_format(codegen->output, "%.*s", LCA_STR_EXPAND(name));
            }
        } break;

        case LYIR_IR_INTEGER_CONSTANT: {
            int64_t ival = lyir_value_integer_constant_get(value);
            if (lyir_type_is_ptr(lyir_value_type_get(value)) && ival == 0)
                lca_string_append_format(codegen->output, "NULL");
            else lca_string_append_format(codegen->output, "%lld", ival);
        } break;

        case LYIR_IR_FLOAT_CONSTANT: {
            double float_value = lyir_value_float_constant_get(value);
            lca_string_append_format(codegen->output, "%f", float_value);
        } break;

        case LYIR_IR_ALLOCA: {
            if (!include_type) lca_string_append_format(codegen->output, "&");
            lca_string_view name = lyir_value_name_get(value);
            if (name.count == 0) {
                int64_t index = lyir_value_index_get(value);
                lca_string_append_format(codegen->output, "lyir_inst_%lld", index);
            } else {
                lca_string_append_format(codegen->output, "%.*s", LCA_STR_EXPAND(name));
            }
        } break;
    }
}
