#include <assert.h>

#include "layec.h"

#if 0
#define LLVM_MEMCPY_INTRINSIC "llvm.memcpy.p0.p0.i64"

typedef struct llvm_codegen {
    layec_context* context;
    bool use_color;
    string* output;
} llvm_codegen;

static void llvm_print_module(llvm_codegen* codegen, laye_module* module);

string laye_codegen_llvm(laye_module* module) {
    assert(module != NULL);
    assert(module->context != NULL);

    string output_string = string_create(module->context->allocator);

    llvm_codegen codegen = {
        .context = module->context,
        .use_color = module->context->use_color,
        .output = &output_string,
    };

    llvm_print_module(&codegen, module);
}

static void llvm_print_header(llvm_codegen* codegen, laye_module* module);
static void llvm_print_struct_type(llvm_codegen* codegen, laye_node* struct_decl);
static void llvm_print_global(llvm_codegen* codegen, laye_node* global_decl);
static void llvm_print_function(llvm_codegen* codegen, laye_node* function_decl);

static void llvm_print_type(llvm_codegen* codegen, laye_node* type);

static void llvm_print_module(llvm_codegen* codegen, laye_module* module) {
    assert(codegen != NULL);
    assert(codegen->context != NULL);
    assert(codegen->output != NULL);
    assert(module != NULL);
    assert(module->context != NULL);
    assert(module->context == codegen->context);

    llvm_print_header(codegen, module);
}

static void llvm_print_header(llvm_codegen* codegen, laye_module* module) {
    assert(codegen != NULL);
    assert(codegen->context != NULL);
    assert(codegen->output != NULL);

    lca_string_append_format(codegen->output, "; Function Attrs: \n");
    lca_string_append_format(codegen->output, "declare void @" LLVM_MEMCPY_INTRINSIC "(ptr, ptr, i64, i1)\n");
}

static void llvm_print_name(llvm_codegen* codegen, string_view name) {
    assert(codegen != NULL);
    assert(codegen->context != NULL);
    assert(codegen->output != NULL);

    lca_string_append_format(codegen->output, "@%.*s", STR_EXPAND(name));
}

static void llvm_print_function_header(llvm_codegen* codegen, laye_node* function_decl) {
    assert(codegen != NULL);
    assert(codegen->context != NULL);
    assert(codegen->output != NULL);
    assert(function_decl != NULL);
    assert(function_decl->module != NULL);
    assert(function_decl->module->context != NULL);
    assert(function_decl->module->context == codegen->context);

    if (function_decl->decl_function.body == NULL) {
        lca_string_append_format(codegen->output, "declare ");
    } else {
        lca_string_append_format(codegen->output, "define ");
    }

    if (function_decl->attributes.linkage == LAYEC_LINK_INTERNAL) {
        lca_string_append_format(codegen->output, "private ");
    } else {
        lca_string_append_format(codegen->output, "external ");
    }

    llvm_print_type(codegen, function_decl->decl_function.return_type);
    lca_string_append_format(codegen->output, " ");
    if (function_decl->attributes.foreign_name.count != 0) {
        llvm_print_name(codegen, string_as_view(function_decl->attributes.foreign_name));
    } else {
        llvm_print_name(codegen, string_as_view(function_decl->declared_name));
    }

    lca_string_append_format(codegen->output, "(");

    for (int64_t i = 0, count = arr_count(function_decl->decl_function.parameter_declarations); i < count; i++) {
        if (i > 0) {
            lca_string_append_format(codegen->output, ", ");
        }

        llvm_print_type(codegen, function_decl->decl_function.parameter_declarations[i]->declared_type);
        lca_string_append_format(codegen->output, " %%%d", (int)i);
    }

    assert(function_decl->declared_type != NULL);
    assert(function_decl->declared_type->kind == LAYE_NODE_TYPE_FUNCTION);
    if (function_decl->declared_type->type_function.varargs_style == LAYE_VARARGS_C) {
        if (0 != arr_count(function_decl->decl_function.parameter_declarations)) {
            lca_string_append_format(codegen->output, ", ");
        }

        lca_string_append_format(codegen->output, "...");
    }

    lca_string_append_format(codegen->output, ")");
}

static void llvm_print_function(llvm_codegen* codegen, laye_node* function_decl) {
    assert(codegen != NULL);
    assert(codegen->context != NULL);
    assert(codegen->output != NULL);
    assert(function_decl != NULL);
    assert(function_decl->module != NULL);
    assert(function_decl->module->context != NULL);
    assert(function_decl->module->context == codegen->context);

    llvm_print_function_header(codegen, function_decl);
    if (function_decl->decl_function.body == NULL) {
        lca_string_append_format(codegen->output, "\n");
        return;
    }
}

static void llvm_print_type(llvm_codegen* codegen, laye_node* type) {
    assert(codegen != NULL);
    assert(codegen->context != NULL);
    assert(codegen->output != NULL);
    assert(type != NULL);
    assert(type->module != NULL);
    assert(type->module->context != NULL);
    assert(type->module->context == codegen->context);
    assert(laye_node_is_type(type));

}
#endif
