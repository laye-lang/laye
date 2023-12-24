#include <assert.h>
#include <stdio.h>

#define LCA_DA_IMPLEMENTATION
#define LCA_MEM_IMPLEMENTATION
#define LCA_STR_IMPLEMENTATION
#include "layec.h"

static char* shift(int* argc, char*** argv) {
    assert(*argc > 0);
    char *result = **argv;
    (*argv)++;
    (*argc)--;
    return result;
}

static void write_file(const char* file_name, string_view file_text);

int main(int argc, char** argv) {
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
    write_file("./test/exit_code.ll", string_as_view(llvm_module_string));
    string_destroy(&llvm_module_string);

    // in release/unsafe builds, we don't need to worry about manually tearing
    // down all of our allocations. these should always be run in debug/safe
    // builds so the static analysers (like address sanitizer) can do their magic.
#ifndef NDEBUG
    layec_module_destroy(ir_module);
    laye_module_destroy(module);
    layec_context_destroy(context);
    lca_temp_allocator_clear();
#endif // !NDEBUG

    return 0;
}

static void write_file(const char* file_name, string_view file_text) {
    FILE* stream = fopen(file_name, "w");
    if (stream == NULL) {
        fprintf(stderr, "unable to open output file '%s'\n", file_name);
        exit(1);
    }

    fwrite(file_text.data, sizeof *file_text.data, file_text.count, stream);
    fclose(stream);
}
