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

#define LAYE_HELP_TEXT_USAGE \
    "Usage: %.*s [%.*s] [--help] [--version] [<args>]\n"

#define LAYE_HELP_TEXT_NOCMD                                                                                 \
    "    --help               Print this help text, then exit.\n"                                            \
    "    --version            Print version information, then exit.\n"                                       \
    "\n"                                                                                                     \
    "    -o <file>                 Write output to <file>. If <file> is '-` (a single dash), then output\n"  \
    "                              will be written to stdout.\n"                                             \
    "\n"                                                                                                     \
    "  actions:\n"                                                                                           \
    "    -E, --preprocess          Run the preprocessor step (for C files). Writes the result to stdout.\n"  \
    "    --ast                     Run the parse step. Writes a representation of the AST to stdout.\n"      \
    "                              Running the parser implies running the preprocessor for C files.\n"       \
    "    -fsyntax-only, --sema     Run the parse and semantic analysis steps.\n"                             \
    "    -S, --assemble            Run the parse, semantic analysis and assemble steps.\n"                   \
    "    -emit-lyir                Uses the LYIR representation for assembler and object files.\n"           \
    "    -emit-llvm                Uses the LLVM representation for assembler and object files.\n"           \
    "\n"                                                                                                     \
    "  diagnostics and output:\n"                                                                            \
    "    --nocolor            Explicitly disable output coloring. By default, colors are enabled only if \n" \
    "                         writing to a terminal.\n"                                                      \
    "    --byte-diagnostics   Report diagnostic information with a byte offset rather than line/column.\n"

#define LAYE_HELP_TEXT_BUILD \
    "\n"

#define LAYE_HELP_TEXT_TEST \
    "\n"

#define LAYE_HELP_TEXT                                                                                  \
    "\nCommands:\n\n"                                                                                   \
    "<no command>             By default, the program operates similar to a C compiler.\n"              \
    "                         The arguments may not be feature complete at any given time, but\n"       \
    "                         the end goal is to behave as close to a drop-in replacement for\n"        \
    "                         LLVM and GCC as possible to ease use in existing projects.\n"             \
    "                         This part of the compiler does not promise to faithfully emulate those\n" \
    "                         other compilers 100%. The intent is to use it as a friendly and\n"        \
    "                         comfortable interface to reduce learning curves.\n"                       \
    "                         It also, of course, has additional options specific to this compiler.\n"  \
    "\n" LAYE_HELP_TEXT_NOCMD                                                                           \
    ""

typedef struct compiler_state {
    string_view command;
    string_view program_name;

    bool help;
    bool verbose;

    enum {
        COLOR_AUTO,
        COLOR_ALWAYS,
        COLOR_NEVER,
    } use_color;

    bool use_byte_positions_in_diagnostics;

    bool preprocess_only;
    bool parse_only;
    bool sema_only;
    bool assemble_only;

    bool emit_lyir;
    bool emit_llvm;

    string_view output_file;
    bool is_output_file_stdout;

    dynarr(string_view) input_files;

    dynarr(string) total_intermediate_files;
    dynarr(string) llvm_modules;

    dynarr(string_view) include_directories;
    dynarr(string_view) library_directories;
    dynarr(string_view) link_libraries;

    layec_context* context;
} compiler_state;

static bool parse_args(compiler_state* driver, int* argc, char*** argv);

static string_view file_name_only(string_view full_path) {
    int i = full_path.count;
    for (; i >= 0; i--) {
        if (full_path.data[i - 1] == '/' || full_path.data[i - 1] == '\\')
            break;
    }

    full_path.data += i;
    full_path.count -= i;

    return full_path;
}

static string_view create_intermediate_file_name(compiler_state* state, string_view name, const char* extension) {
    name = file_name_only(name);
    string intermediate_file_name = string_view_change_extension(default_allocator, name, extension);
    arr_push(state->total_intermediate_files, intermediate_file_name);
    return string_as_view(intermediate_file_name);
}

static int preprocess_only(compiler_state* state) {
    fprintf(stderr, "Running just the preprocessor is not currently supported.\n");
    return 1;
}

static laye_module* parse_module(compiler_state* state, layec_context* context, string_view input_file_path) {
    layec_sourceid sourceid = layec_context_get_or_add_source_from_file(context, input_file_path);
    if (sourceid < 0) {
        return NULL;
    }

    laye_module* module = laye_parse(context, sourceid);
    assert(module != NULL);

    return module;
}

static int emit_lyir(compiler_state* state) {
    for (int64_t i = 0, count = arr_count(state->context->ir_modules); i < count; i++) {
        bool is_only_file = state->assemble_only && arr_count(state->input_files) == 1;
        bool is_output_file_stdout = state->is_output_file_stdout;
        bool use_color = is_output_file_stdout ? state->use_color : false;

        layec_module* ir_module = state->context->ir_modules[i];
        assert(ir_module != NULL);

        string ir_module_string = layec_module_print(ir_module, use_color);
        string_view intermediate_file_name = {0};
        assert(string_view_equals(layec_module_name(ir_module), state->input_files[0]));

        if (is_only_file && state->output_file.count != 0) {
            intermediate_file_name = state->output_file;
        } else {
            intermediate_file_name = create_intermediate_file_name(state, layec_module_name(ir_module), ".lyir");
        }

        if (is_output_file_stdout) {
            assert(is_only_file);
            fprintf(stdout, "%.*s", STR_EXPAND(ir_module_string));
        } else {
            if (!nob_write_entire_file(string_view_to_cstring(temp_allocator, intermediate_file_name), ir_module_string.data, (size_t)ir_module_string.count)) {
                string_destroy(&ir_module_string);
                return 1;
            }
        }

        // fprintf(stdout, "%.*s\n", STR_EXPAND(ir_module_string));
        string_destroy(&ir_module_string);

        if (is_only_file) {
            break;
        }
    }

    return 0;
}

static int emit_c(compiler_state* state) {
#if false
    for (int64_t i = 0, count = arr_count(state->llvm_modules); i < count; i++) {
        bool is_only_file = state->assemble_only && arr_count(state->input_files) == 1;
        bool is_output_file_stdout = state->is_output_file_stdout;

        string llvm_module = state->llvm_modules[i];

        string_view intermediate_file_name = {0};
        if (is_only_file && state->output_file.count != 0) {
            intermediate_file_name = state->output_file;
        } else {
            layec_module* ir_module = state->context->ir_modules[i];
            assert(ir_module != NULL);
            assert(string_view_equals(layec_module_name(ir_module), state->input_files[0]));
            intermediate_file_name = create_intermediate_file_name(state, layec_module_name(ir_module), ".ll");
        }

        if (is_output_file_stdout) {
            assert(is_only_file);
            fprintf(stdout, "%.*s", STR_EXPAND(llvm_module));
        } else {
            if (!nob_write_entire_file(string_view_to_cstring(temp_allocator, intermediate_file_name), llvm_module.data, (size_t)llvm_module.count)) {
                return 1;
            }
        }

        if (is_only_file) {
            break;
        }
    }
#endif

    return 0;
}

static int emit_llvm(compiler_state* state) {
    for (int64_t i = 0, count = arr_count(state->llvm_modules); i < count; i++) {
        bool is_only_file = state->assemble_only && arr_count(state->input_files) == 1;
        bool is_output_file_stdout = state->is_output_file_stdout;

        string llvm_module = state->llvm_modules[i];

        string_view intermediate_file_name = {0};
        if (is_only_file && state->output_file.count != 0) {
            intermediate_file_name = state->output_file;
        } else {
            layec_module* ir_module = state->context->ir_modules[i];
            assert(ir_module != NULL);
            assert(string_view_equals(layec_module_name(ir_module), state->input_files[0]));
            intermediate_file_name = create_intermediate_file_name(state, layec_module_name(ir_module), ".ll");
        }

        if (is_output_file_stdout) {
            assert(is_only_file);
            fprintf(stdout, "%.*s", STR_EXPAND(llvm_module));
        } else {
            if (!nob_write_entire_file(string_view_to_cstring(temp_allocator, intermediate_file_name), llvm_module.data, (size_t)llvm_module.count)) {
                return 1;
            }
        }

        if (is_only_file) {
            break;
        }
    }

    return 0;
}

int main(int argc, char** argv) {
    int exit_code = 0;

    // setup

    compiler_state state = {
        .use_color = COLOR_AUTO,
    };
    if (!parse_args(&state, &argc, &argv) || state.help) {
        string_view command = state.command;
        if (command.count == 0) {
            command = SV_CONSTANT("<command>");
        }
        fprintf(stderr, LAYE_HELP_TEXT_USAGE, STR_EXPAND(state.program_name), STR_EXPAND(command));
        fprintf(stderr, "%s\n", LAYE_HELP_TEXT);
        return 1;
    }

    if (state.verbose) {
        fprintf(stderr, "Laye Compiler " LAYEC_VERSION "\n");
    }

    if (state.is_output_file_stdout && arr_count(state.input_files) != 1) {
        fprintf(stderr, "When requesting output to stdout, exactly one input file must be provided.\n");
        return 1;
    }

    lca_temp_allocator_init(default_allocator, 1024 * 1024);
    layec_init_targets(default_allocator);

    layec_context* context = layec_context_create(default_allocator);
    assert(context != NULL);
    state.context = context;

    if (state.use_color == COLOR_ALWAYS) {
        context->use_color = true;
    } else if (state.use_color == COLOR_NEVER) {
        context->use_color = false;
    } else {
        context->use_color = lca_plat_stdout_isatty();
    }

    context->include_directories = state.include_directories;
    context->library_directories = state.library_directories;
    context->link_libraries = state.link_libraries;

    context->use_byte_positions_in_diagnostics = state.use_byte_positions_in_diagnostics;

    if (state.output_file.count == 0) {
#if _WIN32
        state.output_file = SV_CONSTANT("./a.exe");
#else
        state.output_file = SV_CONSTANT("./a.out");
#endif
    }

    // setup complete

    if (state.preprocess_only) {
        exit_code = preprocess_only(&state);
        if (exit_code != 0) goto program_exit;
    }

    for (int64_t i = 0; i < arr_count(state.input_files); i++) {
        string_view input_file_path = state.input_files[i];

        laye_module* module = parse_module(&state, context, input_file_path);
        if (module == NULL) {
            if (!state.sema_only && !state.parse_only) exit_code = 1;
            goto program_exit;
        }

        assert(module != NULL);
    }

    if (context->has_reported_errors) {
        if (!state.sema_only && !state.parse_only) exit_code = 1;
        goto program_exit;
    }

    // parse-only means no sema, print the untyped tree
    if (state.parse_only) {
        for (int64_t i = 0; i < arr_count(context->laye_modules); i++) {
            laye_module* module = context->laye_modules[i];
            assert(module != NULL);
            string module_string = laye_module_debug_print(module);
            fprintf(stdout, "%.*s\n", STR_EXPAND(module_string));
            string_destroy(&module_string);
        }

        goto program_exit;
    }

    laye_analyse(context);

    if (context->has_reported_errors) {
        if (!state.sema_only) exit_code = 1;
        goto program_exit;
    }

    if (state.sema_only) {
        for (int64_t i = 0; i < arr_count(context->laye_modules); i++) {
            laye_module* module = context->laye_modules[i];
            assert(module != NULL);
            string module_string = laye_module_debug_print(module);
            fprintf(stdout, "%.*s\n", STR_EXPAND(module_string));
            string_destroy(&module_string);
        }

        goto program_exit;
    }

    // no matter what, if we're instructed to get past sema then we want to generate LYIR modules
    laye_generate_ir(context);

    if (context->has_reported_errors) {
        exit_code = 1;
        goto program_exit;
    }

    // from this point forward, the concept of "Laye" is no more; we deal exclusively in LYIR and beyond.
    // everything after generating LYIR should be identical for other frontends ideally.
    // IF IT IS NOT, then we need to refactor to support that *somehow*, but that time is not now.
    for (int64_t i = 0; i < arr_count(context->ir_modules); i++) {
        layec_module* ir_module = context->ir_modules[i];
        assert(ir_module != NULL);

        layec_irpass_validate(ir_module);
        layec_irpass_fix_abi(ir_module);
    }

    if (context->has_reported_errors) {
        exit_code = 1;
        goto program_exit;
    }

    if (state.emit_lyir) {
        if (!state.assemble_only) {
            fprintf(stderr, "-emit-lyir cannot be used when linking.\n");
            exit_code = 1;
        } else {
            emit_lyir(&state);
        }

        goto program_exit;
    }

    for (int64_t i = 0; i < arr_count(context->ir_modules); i++) {
        layec_module* ir_module = context->ir_modules[i];
        assert(ir_module != NULL);

        string llvm_module_string = layec_codegen_llvm(ir_module);
        arr_push(state.llvm_modules, llvm_module_string);
    }

    assert(arr_count(state.llvm_modules) == arr_count(context->ir_modules));

    if (state.emit_llvm) {
        if (!state.assemble_only) {
            fprintf(stderr, "-emit-llvm cannot be used when linking.\n");
            exit_code = 1;
        } else {
            emit_llvm(&state);
        }

        goto program_exit;
    }

    for (int64_t i = 0; i < arr_count(state.llvm_modules); i++) {
        string llvm_module_string = state.llvm_modules[i];
        string_view source_input_file_path = layec_module_name(context->ir_modules[i]);

        string output_file_path_intermediate = string_view_change_extension(default_allocator, source_input_file_path, ".ll");
        arr_push(state.total_intermediate_files, output_file_path_intermediate);

        bool ll_file_result = nob_write_entire_file(lca_string_as_cstring(output_file_path_intermediate), llvm_module_string.data, llvm_module_string.count);
        if (!ll_file_result) {
            exit_code = 1;
            goto program_exit;
        }
    }

    Nob_Cmd clang_ll_cmd = {0};
    nob_cmd_append(
        &clang_ll_cmd,
        "clang",
        "-Wno-override-module",
        "-o",
        lca_string_view_to_cstring(temp_allocator, state.output_file)
    );

    for (int64_t i = 0; i < arr_count(state.link_libraries); i++) {
        const char* s = lca_temp_sprintf("-l%.*s", STR_EXPAND(state.link_libraries[i]));
        nob_cmd_append(&clang_ll_cmd, s);
    }

    for (int64_t i = 0; i < arr_count(state.total_intermediate_files); i++) {
        const char* s = string_as_cstring(state.total_intermediate_files[i]);
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
    if (!state.assemble_only) {
        for (int64_t i = 0; i < arr_count(state.total_intermediate_files); i++) {
            const char* path_cstr = string_as_cstring(state.total_intermediate_files[i]);
            if (nob_file_exists(path_cstr)) {
                remove(path_cstr);
            }
        }
    }

#ifndef NDEBUG
    for (int64_t i = 0; i < arr_count(state.total_intermediate_files); i++) {
        string_destroy(&state.total_intermediate_files[i]);
    }

    for (int64_t i = 0; i < arr_count(state.llvm_modules); i++) {
        string_destroy(&state.llvm_modules[i]);
    }

    arr_free(state.total_intermediate_files);
    arr_free(state.llvm_modules);
    arr_free(state.input_files);

    layec_context_destroy(context);
    lca_temp_allocator_clear();
#endif // !NDEBUG

    return exit_code;
}

static bool parse_args(compiler_state* args, int* argc, char*** argv) {
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
        } else if (string_view_equals(arg, SV_CONSTANT("-E")) || string_view_equals(arg, SV_CONSTANT("--preprocess"))) {
            args->preprocess_only = true;
        } else if (string_view_equals(arg, SV_CONSTANT("--ast"))) {
            args->parse_only = true;
        } else if (string_view_equals(arg, SV_CONSTANT("-fsyntax-only")) || string_view_equals(arg, SV_CONSTANT("--sema"))) {
            args->sema_only = true;
        } else if (string_view_equals(arg, SV_CONSTANT("-S")) || string_view_equals(arg, SV_CONSTANT("--assemble"))) {
            args->assemble_only = true;
        } else if (string_view_equals(arg, SV_CONSTANT("-emit-lyir"))) {
            args->emit_lyir = true;
        } else if (string_view_equals(arg, SV_CONSTANT("-emit-llvm"))) {
            args->emit_llvm = true;
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
                if (string_view_equals(args->output_file, SV_CONSTANT("-"))) {
                    args->is_output_file_stdout = true;
                }
            }
        } else if (string_view_equals(arg, SV_CONSTANT("-l"))) {
            if (argc == 0) {
                fprintf(stderr, "'-l' requires a file path as the output file, but no additional arguments were provided\n");
                return false;
            } else {
                arr_push(args->link_libraries, string_view_from_cstring(nob_shift_args(argc, argv)));
            }
        } else if (string_view_equals(arg, SV_CONSTANT("-I"))) {
            if (argc == 0) {
                fprintf(stderr, "'-I' requires a file path as an include directory, but no additional arguments were provided\n");
                return false;
            } else {
                arr_push(args->include_directories, string_view_from_cstring(nob_shift_args(argc, argv)));
            }
        } else if (string_view_equals(arg, SV_CONSTANT("-L"))) {
            if (argc == 0) {
                fprintf(stderr, "'-L' requires a file path as a library directory, but no additional arguments were provided\n");
                return false;
            } else {
                arr_push(args->library_directories, string_view_from_cstring(nob_shift_args(argc, argv)));
            }
        } else if (string_view_starts_with(arg, SV_CONSTANT("-l"))) {
            arr_push(args->link_libraries, string_view_slice(arg, 2, -1));
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
