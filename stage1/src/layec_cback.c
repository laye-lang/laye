#include <assert.h>

#include "layec.h"

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

static void cback_print_global(cback_codegen* codegen, layec_value* global);
static void cback_print_function(cback_codegen* codegen, layec_value* global);

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
        if (i > 0) lca_string_append_format(codegen->output,  "\n");
        layec_value* function = layec_module_get_function_at_index(module, i);
        cback_print_function(codegen, function);
    }
}
