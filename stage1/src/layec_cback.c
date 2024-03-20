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

typedef struct cback_codegen {
    layec_context* context;
    bool use_color;
    string* output;
} cback_codegen;

static void cback_print_module(cback_codegen* codegen, layec_module* module);

string layec_codegen_c(layec_module* module) {
    assert(module!= NULL);
    layec_context* context = layec_module_context(module);
    assert(context!= NULL);

    string output_string = string_create(context->allocator);

    cback_codegen codegen = {
       .context = context,
       .use_color = context->use_color,
       .output = &output_string,
    };

    cback_print_module(&codegen, module);

    return output_string;
}

static void cback_print_header(cback_codegen* codegen, layec_module* module) {
    lca_string_append_format(codegen->output, "#include <assert.h>\n");
    lca_string_append_format(codegen->output, "#include \"layec.h\"\n\n");
}

static void cback_declare_structs(cback_codegen* codegen, layec_context* context) {
    for (int64_t i = 0, count = layec_context_struct_count(context); i < count; i++) {
        layec_struct* struct_ = layec_context_get_struct_at_index(context, i);
        lca_string_append_format(codegen->output, "typedef struct %s {\n", struct_->name);
        for (int64_t j = 0, count_ = layec_struct_field_count(struct_); j < count_; j++) {
            layec_field* field = layec_struct_get_field_at_index(struct_, j);
            lca_string_append_format(codegen->output, "    %s %s%s;\n", field->type, field->name, j < count_ - 1? "," : "");
        }
        lca_string_append_format(codegen->output, "} %s;\n\n", struct_->name);
    }
}

static void cback_define_structs(cback_codegen* codegen, layec_context* context) {
    for (int64_t i = 0, count = layec_context_struct_count(context); i < count; i++) {
        layec_struct* struct_ = layec_context_get_struct_at_index(context, i);
        lca_string_append_format(codegen->output, "struct %s {\n", struct_->name);
        for (int64_t j = 0, count_ = layec_struct_field_count(struct_); j < count_; j++) {
            layec_field* field = layec_struct_get_field_at_index(struct_, j);
            lca_string_append_format(codegen->output, "    %s %s%s;\n", field->type, field->name, j < count_ - 1? "," : "");
        }
        lca_string_append_format(codegen->output, "};\n\n");
    }
}

static void cback_print_global(cback_codegen* codegen, layec_value* global) {
    switch (global->kind) {
        case LAYEC_VALUE_KIND_FUNCTION:
            cback_print_function(codegen, global);
            break;
        default:
            assert(false);
    }
}

static void cback_print_function(cback_codegen* codegen, layec_value* global) {
    layec_function* function = (layec_function*)global;
    lca_string_append_format(codegen->output, "static %s %s(", function->return_type, function->name);
    for (int64_t i = 0, count = layec_function_param_count(function); i < count; i++) {
        layec_param* param = layec_function_get_param_at_index(function, i);
        lca_string_append_format(codegen->output, "%s %s%s", param->type, param->name, i < count - 1? "," : "");
    }
    lca_string_append_format(codegen->output, ") {\n");
    for (int64_t i = 0, count = layec_function_block_count(function); i < count; i++) {
        layec_block* block = layec_function_get_block_at_index(function, i);
        for (int64_t j = 0, count_ = layec_block_instr_count(block); j < count_; j++) {
            layec_instr* instr = layec_block_get_instr_at_index(block, j);
            switch (instr->kind) {
                case LAYEC_INSTR_KIND_ASSIGN:
                    lca_string_append_format(codegen->output, "    %s = %s;\n", ((layec_assign_instr*)instr)->dest, ((layec_assign_instr*)instr)->src);
                    break;
                case LAYEC_INSTR_KIND_RETURN:
                    lca_string_append_format(codegen->output, "    return %s;\n", ((layec_return_instr*)instr)->value);
                    break;
                default:
                    assert(false);
            }
        }
    }
    lca_string_append_format(codegen->output, "}\n\n");
}

static void cback_print_module(cback_codegen* codegen, layec_module* module) {
    layec_context* context = layec_module_context(module);
    assert(context!= NULL);

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
        if (i > 0) lca_string_append_format(codegen->output,  "\n");
        layec_value* function = layec_module_get_function_at_index(module, i);
        cback_print_function(codegen, function);
    }
}