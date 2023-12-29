#include <assert.h>
#include <stdio.h>

#define LCA_DA_IMPLEMENTATION
#define LCA_MEM_IMPLEMENTATION
#define LCA_STR_IMPLEMENTATION
#include "laye.h"
#include "layec.h"

#define NOB_IMPLEMENTATION
#include "nob.h"

typedef struct args {
    string_view program_name;

    bool help;
    bool verbose;

    bool print_ast;
    bool print_ir;
    bool print_llvm;

    bool syntax_only;

    string_view output_file;
    dynarr(string_view) input_files;
} args;

static bool parse_args(args* args, int* argc, char*** argv);

static laye_module* parse_module(args* args, layec_context* context, string_view input_file_path) {
    layec_sourceid sourceid = layec_context_get_or_add_source_from_file(context, input_file_path);
    laye_module* module = laye_parse(context, sourceid);
    assert(module != NULL);

    if (!args->syntax_only) {
        laye_analyse(module);
    }

    return module;
}

int main(int argc, char** argv) {
    int exit_code = 0;

    args args;
    if (!parse_args(&args, &argc, &argv) || args.help) {
        fprintf(stderr, "Usage:\n  %.*s [options...] files...\n", STR_EXPAND(args.program_name));
        return 1;
    }

    if (args.verbose) {
        fprintf(stderr, "Laye Compiler " LAYEC_VERSION "\n");
    }

    lca_temp_allocator_init(default_allocator, 1024 * 1024);
    layec_init_targets(default_allocator);

    layec_context* context = layec_context_create(default_allocator);
    assert(context != NULL);
    context->use_color = true;

    dynarr(laye_module*) source_modules = NULL;
    dynarr(layec_module*) ir_modules = NULL;
    dynarr(string) llvm_modules = NULL;

    dynarr(string) output_file_paths_intermediate = NULL;
    string output_file_path = {};

    for (int64_t i = 0; i < arr_count(args.input_files); i++) {
        string_view input_file_path = args.input_files[i];

        laye_module* module = parse_module(&args, context, input_file_path);
        assert(module != NULL);
        arr_push(source_modules, module);
    }

    if (args.print_ast) {
        for (int64_t i = 0; i < arr_count(args.input_files); i++) {
            laye_module* module = source_modules[i];
            assert(module != NULL);
            string module_string = laye_module_debug_print(module);
            fprintf(stderr, "%.*s\n\n", STR_EXPAND(module_string));
            string_destroy(&module_string);
        }

        goto program_exit;
    }

    if (args.syntax_only) {
        goto program_exit;
    }

    for (int64_t i = 0; i < arr_count(args.input_files); i++) {
        laye_module* module = source_modules[i];
        assert(module != NULL);

        layec_module* ir_module = laye_irgen(module);
        assert(ir_module != NULL);
        arr_push(ir_modules, ir_module);
    }

    for (int64_t i = 0; i < arr_count(args.input_files); i++) {
        layec_module* ir_module = ir_modules[i];
        assert(ir_module != NULL);

        layec_irpass_validate(ir_module);
    }

    if (args.print_ir) {
        for (int64_t i = 0; i < arr_count(args.input_files); i++) {
            layec_module* ir_module = ir_modules[i];
            assert(ir_module != NULL);
            string ir_module_string = layec_module_print(ir_module);
            fprintf(stderr, "%.*s\n\n", STR_EXPAND(ir_module_string));
            string_destroy(&ir_module_string);
        }

        goto program_exit;
    }

    for (int64_t i = 0; i < arr_count(args.input_files); i++) {
        layec_module* ir_module = ir_modules[i];
        assert(ir_module != NULL);
        
        string llvm_module_string = layec_codegen_llvm(ir_module);
        arr_push(llvm_modules, llvm_module_string);
    }

    if (args.print_llvm) {
        for (int64_t i = 0; i < arr_count(args.input_files); i++) {
            fprintf(stderr, "%.*s\n\n", STR_EXPAND(llvm_modules[i]));
        }

        goto program_exit;
    }

    for (int64_t i = 0; i < arr_count(args.input_files); i++) {
        string llvm_module_string = llvm_modules[i];
        string_view source_input_file_path = args.input_files[i];

        string output_file_path_intermediate = string_view_change_extension(default_allocator, source_input_file_path, ".ll");
        arr_push(output_file_paths_intermediate, output_file_path_intermediate);

        bool ll_file_result = nob_write_entire_file(lca_string_as_cstring(output_file_path_intermediate), llvm_module_string.data, llvm_module_string.count);
        if (!ll_file_result) {
            exit_code = 1;
            goto program_exit;
        }
    }

    if (arr_count(args.input_files) == 1) {
        output_file_path = string_view_change_extension(default_allocator, args.input_files[0], "");
    } else {
        output_file_path = string_view_to_string(default_allocator, args.output_file);
    }

    Nob_Cmd clang_ll_cmd = {};
    nob_cmd_append(
        &clang_ll_cmd,
        "clang",
        "-o",
        lca_string_as_cstring(output_file_path)
    );

    for (int64_t i = 0; i < arr_count(args.input_files); i++) {
        const char* s = string_as_cstring(output_file_paths_intermediate[i]);
        nob_cmd_append(&clang_ll_cmd, s);
    }

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
    string_destroy(&output_file_path);

    for (int64_t i = 0; i < arr_count(output_file_paths_intermediate); i++) {
        string_destroy(&output_file_paths_intermediate[i]);
    }

    for (int64_t i = 0; i < arr_count(llvm_modules); i++) {
        string_destroy(&llvm_modules[i]);
    }

    for (int64_t i = 0; i < arr_count(ir_modules); i++) {
        layec_module_destroy(ir_modules[i]);
    }

    for (int64_t i = 0; i < arr_count(source_modules); i++) {
        laye_module_destroy(source_modules[i]);
    }

    arr_free(output_file_paths_intermediate);
    arr_free(llvm_modules);
    arr_free(ir_modules);
    arr_free(source_modules);
    arr_free(args.input_files);

    layec_context_destroy(context);
    lca_temp_allocator_clear();
#endif // !NDEBUG

    return exit_code;
}

static bool parse_args(args* args, int* argc, char*** argv) {
    assert(args != NULL);
    assert(argc != NULL);
    assert(argv != NULL);

    args->program_name = string_view_from_cstring(nob_shift_args(argc, argv));

    while (*argc > 0) {
        string_view arg = string_view_from_cstring(nob_shift_args(argc, argv));
        if (string_view_equals(arg, SV_CONSTANT("--help"))) {
            args->help = true;
        } else if (string_view_equals(arg, SV_CONSTANT("--verbose"))) {
            args->verbose = true;
        } else if (string_view_equals(arg, SV_CONSTANT("--ast"))) {
            args->print_ast = true;
        } else if (string_view_equals(arg, SV_CONSTANT("--ir"))) {
            args->print_ir = true;
        } else if (string_view_equals(arg, SV_CONSTANT("--llvm"))) {
            args->print_llvm = true;
        } else if (string_view_equals(arg, SV_CONSTANT("--syntax"))) {
            args->syntax_only = true;
        } else {
            arr_push(args->input_files, arg);
        }
    }

    if (arr_count(args->input_files) == 0 && !args->help) {
        fprintf(stderr, "%.*s: no input file(s) given\n", STR_EXPAND(args->program_name));
        return false;
    }

    return true;
}
