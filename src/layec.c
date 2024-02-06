#include <assert.h>
#include <stdio.h>

#define LCA_DA_IMPLEMENTATION
#define LCA_MEM_IMPLEMENTATION
#define LCA_STR_IMPLEMENTATION
#define LCA_PLAT_IMPLEMENTATION
#include "laye.h"
#include "layec.h"

#define NOB_NO_LOG_EXIT_STATUS
#define NOB_NO_CMD_RENDER

#define NOB_IMPLEMENTATION
#include "nob.h"

typedef struct args {
    string_view program_name;

    bool help;
    bool verbose;

    enum {
        COLOR_AUTO,
        COLOR_ALWAYS,
        COLOR_NEVER,
    } use_color;

    bool use_byte_positions_in_diagnostics;

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
    if (sourceid < 0) {
        return NULL;
    }

    laye_module* module = laye_parse(context, sourceid);
    assert(module != NULL);

    if (!args->syntax_only && !context->has_reported_errors) {
        laye_analyse(module);
    }

    return module;
}

int main(int argc, char** argv) {
    int exit_code = 0;

    args args = {
        .use_color = COLOR_AUTO,
    };
    if (!parse_args(&args, &argc, &argv) || args.help) {
        fprintf(stderr, "Usage: %.*s [options...] files...\n", STR_EXPAND(args.program_name));
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  --help               Display this information.\n");
        fprintf(stderr, "  --verbose            Print additional information during compilation.\n");
        fprintf(stderr, "  --ast                Print the ASTs for the input source files, then exit.\n");
        fprintf(stderr, "  --ir                 Print the generated LYIR modules for the input source files, then exit.\n");
        fprintf(stderr, "  --llvm               Print the generated LLVM modules for the input source files, then exit.\n");
        fprintf(stderr, "  --syntax             Do not perform type checking, exiting after all files have been parsed.\n");
        fprintf(stderr, "  --nocolor            Explicitly disable output coloring. The default detects automatically if colors can be used.\n");
        fprintf(stderr, "  --byte-diagnostics   Report diagnostic information about a source file with byte positions instead of line/column information.\n");
        return 1;
    }

    if (args.verbose) {
        fprintf(stderr, "Laye Compiler " LAYEC_VERSION "\n");
    }

    lca_temp_allocator_init(default_allocator, 1024 * 1024);
    layec_init_targets(default_allocator);

    layec_context* context = layec_context_create(default_allocator);
    assert(context != NULL);
    if (args.use_color == COLOR_ALWAYS) {
        context->use_color = true;
    } else if (args.use_color == COLOR_NEVER) {
        context->use_color = false;
    } else {
        context->use_color = lca_plat_stdout_isatty();
    }

    context->use_byte_positions_in_diagnostics = args.use_byte_positions_in_diagnostics;

    dynarr(laye_module*) source_modules = NULL;
    dynarr(layec_module*) ir_modules = NULL;
    dynarr(string) llvm_modules = NULL;

    dynarr(string) output_file_paths_intermediate = NULL;
    string output_file_path = {0};

    for (int64_t i = 0; i < arr_count(args.input_files); i++) {
        string_view input_file_path = args.input_files[i];

        laye_module* module = parse_module(&args, context, input_file_path);
        if (module == NULL) {
            if (!args.print_ast) exit_code = 1;
            goto program_exit;
        }

        assert(module != NULL);
        arr_push(source_modules, module);
    }

    if (context->has_reported_errors) {
        if (!args.print_ast) exit_code = 1;
        goto program_exit;
    }

    if (args.print_ast) {
        for (int64_t i = 0; i < arr_count(args.input_files); i++) {
            laye_module* module = source_modules[i];
            assert(module != NULL);
            string module_string = laye_module_debug_print(module);
            fprintf(stdout, "%.*s\n", STR_EXPAND(module_string));
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

    if (context->has_reported_errors) {
        goto program_exit;
    }

    for (int64_t i = 0; i < arr_count(args.input_files); i++) {
        layec_module* ir_module = ir_modules[i];
        assert(ir_module != NULL);

        layec_irpass_validate(ir_module);
    }

    if (context->has_reported_errors) {
        for (int64_t i = 0; i < arr_count(args.input_files); i++) {
            layec_module* ir_module = ir_modules[i];
            assert(ir_module != NULL);
            string ir_module_string = layec_module_print(ir_module);
            fprintf(stdout, "%.*s\n", STR_EXPAND(ir_module_string));
            string_destroy(&ir_module_string);
        }
        
        goto program_exit;
    }

    if (args.print_ir) {
        for (int64_t i = 0; i < arr_count(args.input_files); i++) {
            layec_module* ir_module = ir_modules[i];
            assert(ir_module != NULL);
            string ir_module_string = layec_module_print(ir_module);
            fprintf(stdout, "%.*s\n", STR_EXPAND(ir_module_string));
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
            fprintf(stdout, "%.*s", STR_EXPAND(llvm_modules[i]));
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

    if (args.output_file.count != 0) {
        output_file_path = string_view_to_string(default_allocator, args.output_file);
    } else {
        if (arr_count(args.input_files) == 1) {
            output_file_path = string_view_change_extension(default_allocator, args.input_files[0], "");
        } else {
#if _WIN32
            output_file_path = string_view_to_string(default_allocator, SV_CONSTANT("./a.exe"));
#else
            output_file_path = string_view_to_string(default_allocator, SV_CONSTANT("./a.out"));
#endif
        }
    }

    Nob_Cmd clang_ll_cmd = {0};
    nob_cmd_append(
        &clang_ll_cmd,
        "clang",
        "-Wno-override-module",
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
        const char* path_cstr = string_as_cstring(output_file_paths_intermediate[i]);
        if (nob_file_exists(path_cstr)) {
            remove(path_cstr);
        }
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
        } else if (string_view_equals(arg, SV_CONSTANT("--nocolor"))) {
            args->use_color = COLOR_NEVER;
        } else if (string_view_equals(arg, SV_CONSTANT("--byte-diagnostics"))) {
            args->use_byte_positions_in_diagnostics = true;
        } else if (string_view_equals(arg, SV_CONSTANT("-o"))) {
            if (argc == 0) {
                fprintf(stderr, "'-o' requires a file path as the output file, but no additional arguments were provided\n");
                return false;
            } else {
                args->output_file = string_view_from_cstring(nob_shift_args(argc, argv));
            }
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
