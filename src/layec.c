#include <assert.h>
#include <stdio.h>

#define LCA_DA_IMPLEMENTATION
#define LCA_MEM_IMPLEMENTATION
#define LCA_STR_IMPLEMENTATION
#include "layec.h"
#include "laye.h"

#define NOB_IMPLEMENTATION
#include "nob.h"

static char* shift(int* argc, char*** argv) {
    assert(*argc > 0);
    char *result = **argv;
    (*argv)++;
    (*argc)--;
    return result;
}

int main(int argc, char** argv) {
    int exit_code = 0;

    lca_temp_allocator_init(default_allocator, 1024 * 1024);

    //fprintf(stderr, "layec " LAYEC_VERSION "\n");

    layec_init_targets(default_allocator);

    layec_context* context = layec_context_create(default_allocator);
    assert(context != NULL);
    context->use_color = true;

    layec_sourceid sourceid = layec_context_get_or_add_source_from_file(context, SV_CONSTANT("./test/exit_code.laye"));
    laye_module* module = laye_parse(context, sourceid);
    assert(module != NULL);

    laye_analyse(module);

    string module_string = laye_module_debug_print(module);
    fprintf(stderr, "%.*s\n\n", STR_EXPAND(module_string));
    string_destroy(&module_string);

    layec_module* ir_module = laye_irgen(module);
    assert(ir_module != NULL);

    string ir_module_string = layec_module_print(ir_module);
    fprintf(stderr, "%.*s\n\n", STR_EXPAND(ir_module_string));
    string_destroy(&ir_module_string);

    layec_irpass_validate(ir_module);

    string llvm_module_string = layec_codegen_llvm(ir_module);
    fprintf(stderr, "%.*s\n\n", STR_EXPAND(llvm_module_string));
    bool ll_file_result = nob_write_entire_file("./test/exit_code.ll", llvm_module_string.data, llvm_module_string.count);
    string_destroy(&llvm_module_string);

    if (!ll_file_result) {
        exit_code = 1;
        goto program_exit;
    }

    Nob_Cmd clang_ll_cmd = {};
    nob_cmd_append(&clang_ll_cmd, "clang", "-o", "./test/exit_code", "./test/exit_code.ll");
    if (!nob_cmd_run_sync(clang_ll_cmd)) {
        nob_cmd_free(clang_ll_cmd);
        exit_code = 1;
        goto program_exit;
    }

    nob_cmd_free(clang_ll_cmd);

    // in release/unsafe builds, we don't need to worry about manually tearing
    // down all of our allocations. these should always be run in debug/safe
    // builds so the static analysers (like address sanitizer) can do their magic.
program_exit:;
#ifndef NDEBUG
    layec_module_destroy(ir_module);
    laye_module_destroy(module);
    layec_context_destroy(context);
    lca_temp_allocator_clear();
#endif // !NDEBUG

    return exit_code;
}
