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
#include "c.h"
#include "laye.h"

#define NOB_NO_LOG_EXIT_STATUS
#define NOB_NO_CMD_RENDER

#define NOB_IMPLEMENTATION
#include "nob.h"

#define LAYE_HELP_TEXT_USAGE \
    "Usage: %.*s [%.*s] [--help] [--version] [<args>]\n"

#define LAYE_HELP_TEXT_NOCMD                                                                                          \
    "    --help               Print this help text, then exit.\n"                                                     \
    "    --version            Print version information, then exit.\n"                                                \
    "\n"                                                                                                              \
    "    -o <file>                 Write output to <file>. If <file> is '-` (a single dash), then output\n"           \
    "                              will be written to stdout.\n"                                                      \
    "    --x <source-kind>         Changes the default interpretation of an input source file.\n"                     \
    "                              By default, the source file extension determines what format the file is.\n"       \
    "                              When overriden, treats the following source files as a specific format instead.\n" \
    "                              One of 'default', 'c', 'laye' or 'lyir'.\n"                                        \
    "                              Default: 'default'.\n"                                                             \
    "    --backend <backend>       What code generation backend to use. One of 'c' or 'llvm'.\n"                      \
    "                              Default: 'c'.\n"                                                                   \
    "\n"                                                                                                              \
    "  actions:\n"                                                                                                    \
    "    -E, --preprocess          Run the preprocessor step (for C files). Writes the result to stdout.\n"           \
    "    --ast                     Run the parse step. Writes a representation of the AST to stdout.\n"               \
    "                              Running the parser implies running the preprocessor for C files.\n"                \
    "    -fsyntax-only, --sema     Run the parse and semantic analysis steps.\n"                                      \
    "    -S, --assemble            Run the parse, semantic analysis and assemble steps.\n"                            \
    "    -emit-lyir                Uses the LYIR representation for assembler and object files.\n"                    \
    "    -emit-llvm                Uses the LLVM representation for assembler and object files.\n"                    \
    "    -emit-c                   Rather than emiting typical IR, emits C source code instead.\n"                    \
    "\n"                                                                                                              \
    "  diagnostics and output:\n"                                                                                     \
    "    --nocolor            Explicitly disable output coloring. By default, colors are enabled only if \n"          \
    "                         writing to a terminal.\n"                                                               \
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

typedef enum source_file_kind {
    SOURCE_DEFAULT,
    SOURCE_LAYE,
    SOURCE_C,
    SOURCE_LYIR,
} source_file_kind;

typedef enum backend {
    BACKEND_C,
    BACKEND_LLVM,
} backend;

typedef struct source_file_info {
    lca_string_view path;
    source_file_kind kind;
} source_file_info;

typedef struct compiler_state {
    lca_string_view command;
    lca_string_view program_name;

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

    backend backend;

    bool emit_lyir;
    bool emit_llvm;
    bool emit_c;

    lca_string_view output_file;
    bool is_output_file_stdout;

    source_file_kind override_file_kind;
    lca_da(source_file_info) input_files;

    lca_da(c_translation_unit*) translation_units;

    lca_da(lca_string) total_intermediate_files;
    lca_da(lca_string) c_modules;
    lca_da(lca_string) llvm_modules;

    lca_da(lca_string_view) include_directories;
    lca_da(lca_string_view) library_directories;
    lca_da(lca_string_view) link_libraries;

    lyir_context* context;
} compiler_state;

static bool parse_args(compiler_state* driver, int* argc, char*** argv);

static lca_string_view file_name_only(lca_string_view full_path) {
    int i = full_path.count;
    for (; i >= 0; i--) {
        if (full_path.data[i - 1] == '/' || full_path.data[i - 1] == '\\')
            break;
    }

    full_path.data += i;
    full_path.count -= i;

    return full_path;
}

static lca_string_view create_intermediate_file_name(compiler_state* state, lca_string_view name, const char* extension) {
    name = file_name_only(name);
    lca_string intermediate_file_name = lca_string_view_change_extension(default_allocator, name, extension);
    lca_da_push(state->total_intermediate_files, intermediate_file_name);
    return lca_string_as_view(intermediate_file_name);
}

static int preprocess_only(compiler_state* state) {
    fprintf(stderr, "Running just the preprocessor is not currently supported.\n");
    return 1;
}

static laye_module* parse_module(compiler_state* state, lyir_context* context, lca_string_view input_file_path) {
    lyir_sourceid sourceid = lyir_context_get_or_add_source_from_file(context, input_file_path);
    if (sourceid < 0) {
        const char* error_string = strerror(errno);
        fprintf(stderr, "Error when opening source file \"%.*s\": %s\n", LCA_STR_EXPAND(input_file_path), error_string);
        return NULL;
    }

    laye_module* module = laye_parse(context, sourceid);
    assert(module != NULL);

    return module;
}

static c_translation_unit* parse_translation_unit(compiler_state* state, lyir_context* context, lca_string_view input_file_path) {
    lyir_sourceid sourceid = lyir_context_get_or_add_source_from_file(context, input_file_path);
    if (sourceid < 0) {
        const char* error_string = strerror(errno);
        fprintf(stderr, "Error when opening source file \"%.*s\": %s\n", LCA_STR_EXPAND(input_file_path), error_string);
        return NULL;
    }

    c_translation_unit* tu = c_parse(context, sourceid);
    assert(tu != NULL);
    lca_da_push(state->translation_units, tu);

    return tu;
}

static int emit_lyir(compiler_state* state) {
    for (int64_t i = 0, count = lca_da_count(state->context->ir_modules); i < count; i++) {
        bool is_only_file = state->assemble_only && lca_da_count(state->input_files) == 1;
        bool is_output_file_stdout = state->is_output_file_stdout;
        bool use_color = is_output_file_stdout ? state->use_color : false;

        lyir_module* ir_module = state->context->ir_modules[i];
        assert(ir_module != NULL);

        lca_string ir_module_string = lyir_module_print(ir_module, use_color);
        lca_string_view intermediate_file_name = {0};
        assert(lca_string_view_equals(lyir_module_name(ir_module), state->input_files[0].path));

        if (is_only_file && state->output_file.count != 0) {
            intermediate_file_name = state->output_file;
        } else {
            intermediate_file_name = create_intermediate_file_name(state, lyir_module_name(ir_module), ".lyir");
        }

        if (is_output_file_stdout) {
            assert(is_only_file);
            fprintf(stdout, "%.*s", LCA_STR_EXPAND(ir_module_string));
        } else {
            if (!nob_write_entire_file(lca_string_view_to_cstring(temp_allocator, intermediate_file_name), ir_module_string.data, (size_t)ir_module_string.count)) {
                lca_string_destroy(&ir_module_string);
                return 1;
            }
        }

        // fprintf(stdout, "%.*s\n", LCA_STR_EXPAND(ir_module_string));
        lca_string_destroy(&ir_module_string);

        if (is_only_file) {
            break;
        }
    }

    return 0;
}

static int emit_c(compiler_state* state) {
    for (int64_t i = 0, count = lca_da_count(state->c_modules); i < count; i++) {
        bool is_only_file = state->assemble_only && lca_da_count(state->input_files) == 1;
        bool is_output_file_stdout = state->is_output_file_stdout;

        lca_string c_module = state->c_modules[i];

        lca_string_view intermediate_file_name = {0};
        if (is_only_file && state->output_file.count != 0) {
            intermediate_file_name = state->output_file;
        } else {
            lyir_module* ir_module = state->context->ir_modules[i];
            assert(ir_module != NULL);
            assert(lca_string_view_equals(lyir_module_name(ir_module), state->input_files[0].path));
            intermediate_file_name = create_intermediate_file_name(state, lyir_module_name(ir_module), ".ir.c");
        }

        if (is_output_file_stdout) {
            assert(is_only_file);
            fprintf(stdout, "%.*s", LCA_STR_EXPAND(c_module));
        } else {
            if (!nob_write_entire_file(lca_string_view_to_cstring(temp_allocator, intermediate_file_name), c_module.data, (size_t)c_module.count)) {
                return 1;
            }
        }

        if (is_only_file) {
            break;
        }
    }

    return 0;
}

static int emit_llvm(compiler_state* state) {
    for (int64_t i = 0, count = lca_da_count(state->llvm_modules); i < count; i++) {
        bool is_only_file = state->assemble_only && lca_da_count(state->input_files) == 1;
        bool is_output_file_stdout = state->is_output_file_stdout;

        lca_string llvm_module = state->llvm_modules[i];

        lca_string_view intermediate_file_name = {0};
        if (is_only_file && state->output_file.count != 0) {
            intermediate_file_name = state->output_file;
        } else {
            lyir_module* ir_module = state->context->ir_modules[i];
            assert(ir_module != NULL);
            assert(lca_string_view_equals(lyir_module_name(ir_module), state->input_files[0].path));
            intermediate_file_name = create_intermediate_file_name(state, lyir_module_name(ir_module), ".ll");
        }

        if (is_output_file_stdout) {
            assert(is_only_file);
            fprintf(stdout, "%.*s", LCA_STR_EXPAND(llvm_module));
        } else {
            if (!nob_write_entire_file(lca_string_view_to_cstring(temp_allocator, intermediate_file_name), llvm_module.data, (size_t)llvm_module.count)) {
                return 1;
            }
        }

        if (is_only_file) {
            break;
        }
    }

    return 0;
}

static void add_stdlib_includes(lyir_context* context, lca_string_view stdlib_root) {
    lca_string_view liblaye_dir = lca_string_view_from_cstring(lca_temp_sprintf("%.*s/%s", LCA_STR_EXPAND(stdlib_root), "liblaye"));
    if (nob_file_exists(liblaye_dir.data) && nob_get_file_type(liblaye_dir.data) == NOB_FILE_DIRECTORY) {
        lca_da_push(context->include_directories, liblaye_dir);
    } else {
        fprintf(stderr, ANSI_COLOR_MAGENTA "warning" ANSI_COLOR_RESET ": Unfortunately, while the Laye compiler found the standard `lib/laye` directory, it did not find the `liblaye` standard library directory within it. Because of this, the compiler will be unable to add it as a default include location and you will need to manually include it: `-I path/to/lib/laye`.\n");
    }
}

static int backend_c(compiler_state* state);
static int backend_llvm(compiler_state* state);

int main(int argc, char** argv) {
    int exit_code = 0;

    lca_temp_allocator_init(default_allocator, 1024 * 1024);

    // setup

    lyir_init_targets(default_allocator);

    compiler_state state = {
        .use_color = COLOR_AUTO,
        .backend = BACKEND_LLVM,
    };
    if (!parse_args(&state, &argc, &argv) || state.help) {
        lca_string_view command = state.command;
        if (command.count == 0) {
            command = LCA_SV_CONSTANT("<command>");
        }
        fprintf(stderr, LAYE_HELP_TEXT_USAGE, LCA_STR_EXPAND(state.program_name), LCA_STR_EXPAND(command));
        fprintf(stderr, "%s\n", LAYE_HELP_TEXT);
        return 1;
    }

    if (state.verbose) {
        fprintf(stderr, "Laye Compiler " LAYEC_VERSION "\n");
    }

    if (state.is_output_file_stdout && lca_da_count(state.input_files) != 1) {
        fprintf(stderr, "When requesting output to stdout, exactly one input file must be provided.\n");
        return 1;
    }

    lyir_context* context = lyir_context_create(default_allocator);
    assert(context != NULL);
    state.context = context;

    if (state.emit_lyir) {
        if (!state.assemble_only) {
            fprintf(stderr, "-emit-lyir cannot be used when linking.\n");
            exit_code = 1;
            goto program_exit;
        }
    }

    if (state.emit_c) {
        if (!state.assemble_only) {
            fprintf(stderr, "-emit-c cannot be used when linking.\n");
            exit_code = 1;
            goto program_exit;
        }
    }

    if (state.emit_llvm) {
        if (!state.assemble_only) {
            fprintf(stderr, "-emit-llvm cannot be used when linking.\n");
            exit_code = 1;
            goto program_exit;
        }
    }

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

    const char* self_exe = lca_plat_self_exe();
    if (self_exe != NULL) {
        lca_string libdir_builder = lca_string_create(default_allocator);
        lca_string_append_format(&libdir_builder, "%s", self_exe);
        lca_string_path_parent(&libdir_builder);

        while (libdir_builder.count > 0) {
            int64_t last_count = libdir_builder.count;

            lca_string_path_append_view(&libdir_builder, LCA_SV_CONSTANT("lib/laye"));
            // fprintf(stderr, "looking for '%.*s'\n", STR_EXPAND(libdir_builder));

            if (nob_file_exists(libdir_builder.data) && nob_get_file_type(libdir_builder.data) == NOB_FILE_DIRECTORY) {
                lca_string_view libdir_root = lyir_context_intern_string_view(context, lca_string_as_view(libdir_builder));
                lca_string_destroy(&libdir_builder);
                add_stdlib_includes(context, libdir_root);
                goto found_stdlib;
            }

            memset(libdir_builder.data + last_count, 0, (size_t)(libdir_builder.count - last_count));
            libdir_builder.count = last_count;

            lca_string_path_parent(&libdir_builder);
        }

        lca_string_destroy(&libdir_builder);
        fprintf(stderr, ANSI_COLOR_MAGENTA "warning" ANSI_COLOR_RESET ": Unfortunately, the Laye compiler could not locate the standard library directory. Either it does not exist or is not in a place the compiler looks for it. The Laye compiler searches for the standard library in `./lib/laye`, `../lib/laye`, `../../lib/laye` etc relative to the executable file itself.\n");
    } else {
        fprintf(stderr, ANSI_COLOR_MAGENTA "warning" ANSI_COLOR_RESET ": Unfortunately, either Laye does not support getting the current executable path on this platform or it could not be obtained for some reason. Because of this, the compiler will be unable to easily infer the location of the standard library and you may need to manually include the standard library path: `-I path/to/lib/laye`.\n");
    }

found_stdlib:;

    if (state.output_file.count == 0) {
#if _WIN32
        state.output_file = LCA_SV_CONSTANT("./a.exe");
#else
        state.output_file = LCA_SV_CONSTANT("./a.out");
#endif
    }

    // setup complete

    if (state.preprocess_only) {
        exit_code = preprocess_only(&state);
        if (exit_code != 0) goto program_exit;
    }

    for (int64_t i = 0; i < lca_da_count(state.input_files); i++) {
        source_file_info input_file_info = state.input_files[i];
        lca_string_view input_file_path = input_file_info.path;
        source_file_kind input_file_kind = input_file_info.kind;

        if (input_file_kind == SOURCE_LAYE || (input_file_kind == SOURCE_DEFAULT && lca_string_view_ends_with_cstring(input_file_path, ".laye"))) {
            laye_module* module = parse_module(&state, context, input_file_path);
            if (module == NULL) {
                if (!state.sema_only && !state.parse_only) exit_code = 1;
                goto program_exit;
            }

            assert(module != NULL);
        } else if (input_file_kind == SOURCE_C || (input_file_kind == SOURCE_DEFAULT && lca_string_view_ends_with_cstring(input_file_path, ".c"))) {
            c_translation_unit* tu = parse_translation_unit(&state, context, input_file_path);
            if (tu == NULL) {
                if (!state.sema_only && !state.parse_only) exit_code = 1;
                goto program_exit;
            }

            assert(tu != NULL);
        } else {
            fprintf(stderr, "Unknown source file type for file %.*s\n", LCA_STR_EXPAND(input_file_path));
            exit_code = 1;
            goto program_exit;
        }
    }

    if (context->has_reported_errors) {
        if (!state.sema_only && !state.parse_only) exit_code = 1;
        goto program_exit;
    }

    // parse-only means no sema, print the untyped tree
    if (state.parse_only) {
        for (int64_t i = 0; i < lca_da_count(context->laye_modules); i++) {
            laye_module* module = context->laye_modules[i];
            assert(module != NULL);
            lca_string module_string = laye_module_debug_print(module);
            fprintf(stdout, "%.*s\n", LCA_STR_EXPAND(module_string));
            lca_string_destroy(&module_string);
        }

        for (int64_t i = 0; i < lca_da_count(state.translation_units); i++) {
            c_translation_unit* tu = state.translation_units[i];
            assert(tu != NULL);
            lca_string tu_string = c_translation_unit_debug_print(tu);
            fprintf(stdout, "%.*s\n", LCA_STR_EXPAND(tu_string));
            lca_string_destroy(&tu_string);
        }

        goto program_exit;
    }

    laye_analyse(context);

    if (context->has_reported_errors) {
        if (!state.sema_only) exit_code = 1;
        goto program_exit;
    }

    if (state.sema_only) {
        for (int64_t i = 0; i < lca_da_count(context->laye_modules); i++) {
            laye_module* module = context->laye_modules[i];
            assert(module != NULL);
            lca_string module_string = laye_module_debug_print(module);
            fprintf(stdout, "%.*s\n", LCA_STR_EXPAND(module_string));
            lca_string_destroy(&module_string);
        }

        for (int64_t i = 0; i < lca_da_count(state.translation_units); i++) {
            c_translation_unit* tu = state.translation_units[i];
            assert(tu != NULL);
            lca_string tu_string = c_translation_unit_debug_print(tu);
            fprintf(stdout, "%.*s\n", LCA_STR_EXPAND(tu_string));
            lca_string_destroy(&tu_string);
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
    for (int64_t i = 0; i < lca_da_count(context->ir_modules); i++) {
        lyir_module* ir_module = context->ir_modules[i];
        assert(ir_module != NULL);

        lyir_irpass_validate(ir_module);
        lyir_irpass_fix_abi(ir_module);
    }

    if (context->has_reported_errors) {
        exit_code = 1;
        goto program_exit;
    }

    //

    if (state.assemble_only) {
        if (state.emit_llvm) {
            exit_code = backend_llvm(&state);
        } else if (state.emit_lyir) {
            exit_code = emit_lyir(&state);
        } else if (state.emit_c) {
            exit_code = backend_c(&state);
        } else {
            exit_code = 1;
            fprintf(stderr, "No explicit assembler format provided when -S option was given.\n");
        }

        goto program_exit;
    }

    if (state.backend == BACKEND_LLVM) {
        exit_code = backend_llvm(&state);
    } else if (state.backend == BACKEND_C) {
        exit_code = backend_c(&state);
    } else {
        exit_code = 1;
        fprintf(stderr, "Unknown backend\n");
        goto program_exit;
    }

    // in release/unsafe builds, we don't need to worry about manually tearing
    // down all of our allocations. these should always be run in debug/safe
    // builds so the static analysers (like address sanitizer) can do their magic.
program_exit:;
    if (!state.assemble_only) {
        for (int64_t i = 0; i < lca_da_count(state.total_intermediate_files); i++) {
            const char* path_cstr = lca_string_as_cstring(state.total_intermediate_files[i]);
            if (nob_file_exists(path_cstr)) {
                remove(path_cstr);
            }
        }
    }

#ifndef NDEBUG
    for (int64_t i = 0; i < lca_da_count(state.total_intermediate_files); i++) {
        lca_string_destroy(&state.total_intermediate_files[i]);
    }

    for (int64_t i = 0; i < lca_da_count(state.translation_units); i++) {
        c_translation_unit_destroy(state.translation_units[i]);
    }

    lca_da_free(state.total_intermediate_files);
    lca_da_free(state.translation_units);
    lca_da_free(state.input_files);

    lyir_context_destroy(context);
    lca_temp_allocator_clear();
#endif // !NDEBUG

    return exit_code;
}

static int backend_c(compiler_state* state) {
    int exit_code = 0;

    lyir_context* context = state->context;

    for (int64_t i = 0; i < lca_da_count(context->ir_modules); i++) {
        lyir_module* ir_module = context->ir_modules[i];
        assert(ir_module != NULL);

        lca_string c_module_string = lyir_codegen_c(ir_module);
        lca_da_push(state->c_modules, c_module_string);
    }

    assert(lca_da_count(state->c_modules) == lca_da_count(context->ir_modules));

    if (state->emit_c) {
        assert(state->assemble_only);
        emit_c(state);
        goto backend_exit;
    }

    for (int64_t i = 0; i < lca_da_count(state->c_modules); i++) {
        lca_string c_module_string = state->c_modules[i];
        lca_string_view source_input_file_path = lca_string_view_path_file_name(lyir_module_name(context->ir_modules[i]));

        lca_string output_file_path_intermediate = lca_string_view_change_extension(default_allocator, source_input_file_path, ".ir.c");
        lca_da_push(state->total_intermediate_files, output_file_path_intermediate);

        bool c_file_result = nob_write_entire_file(lca_string_as_cstring(output_file_path_intermediate), c_module_string.data, c_module_string.count);
        if (!c_file_result) {
            exit_code = 1;
            goto backend_exit;
        }
    }

    Nob_Cmd clang_cc_cmd = {0};
    nob_cmd_append(
        &clang_cc_cmd,
        "clang",
        "-Wno-override-module",
        "-O3",
        "-o",
        lca_string_view_to_cstring(temp_allocator, state->output_file)
    );

    for (int64_t i = 0; i < lca_da_count(state->link_libraries); i++) {
        const char* s = lca_temp_sprintf("-l%.*s", LCA_STR_EXPAND(state->link_libraries[i]));
        nob_cmd_append(&clang_cc_cmd, s);
    }

    for (int64_t i = 0; i < lca_da_count(state->total_intermediate_files); i++) {
        const char* s = lca_string_as_cstring(state->total_intermediate_files[i]);
        nob_cmd_append(&clang_cc_cmd, s);
    }

    if (!nob_cmd_run_sync(clang_cc_cmd)) {
        nob_cmd_free(clang_cc_cmd);
        exit_code = 1;
        goto backend_exit;
    }

    nob_cmd_free(clang_cc_cmd);

backend_exit:;
    for (int64_t i = 0; i < lca_da_count(state->c_modules); i++) {
        lca_string_destroy(&state->c_modules[i]);
    }

    lca_da_free(state->c_modules);

    return exit_code;
}

static int backend_llvm(compiler_state* state) {
    int exit_code = 0;

    lyir_context* context = state->context;

    for (int64_t i = 0; i < lca_da_count(context->ir_modules); i++) {
        lyir_module* ir_module = context->ir_modules[i];
        assert(ir_module != NULL);

        lca_string llvm_module_string = lyir_codegen_llvm(ir_module);
        lca_da_push(state->llvm_modules, llvm_module_string);
    }

    assert(lca_da_count(state->llvm_modules) == lca_da_count(context->ir_modules));

    if (state->emit_llvm) {
        assert(state->assemble_only);
        emit_llvm(state);
        goto backend_exit;
    }

    for (int64_t i = 0; i < lca_da_count(state->llvm_modules); i++) {
        lca_string llvm_module_string = state->llvm_modules[i];
        lca_string_view source_input_file_path = lca_string_view_path_file_name(lyir_module_name(context->ir_modules[i]));

        lca_string output_file_path_intermediate = lca_string_view_change_extension(default_allocator, source_input_file_path, ".ll");
        lca_da_push(state->total_intermediate_files, output_file_path_intermediate);

        bool ll_file_result = nob_write_entire_file(lca_string_as_cstring(output_file_path_intermediate), llvm_module_string.data, llvm_module_string.count);
        if (!ll_file_result) {
            exit_code = 1;
            goto backend_exit;
        }
    }

    Nob_Cmd clang_ll_cmd = {0};
    nob_cmd_append(
        &clang_ll_cmd,
        "clang",
        "-Wno-override-module",
        "-O3",
        "-o",
        lca_string_view_to_cstring(temp_allocator, state->output_file)
    );

    for (int64_t i = 0; i < lca_da_count(state->link_libraries); i++) {
        const char* s = lca_temp_sprintf("-l%.*s", LCA_STR_EXPAND(state->link_libraries[i]));
        nob_cmd_append(&clang_ll_cmd, s);
    }

    for (int64_t i = 0; i < lca_da_count(state->total_intermediate_files); i++) {
        const char* s = lca_string_as_cstring(state->total_intermediate_files[i]);
        nob_cmd_append(&clang_ll_cmd, s);
    }

    if (!nob_cmd_run_sync(clang_ll_cmd)) {
        nob_cmd_free(clang_ll_cmd);
        exit_code = 1;
        goto backend_exit;
    }

    nob_cmd_free(clang_ll_cmd);

backend_exit:;
    for (int64_t i = 0; i < lca_da_count(state->llvm_modules); i++) {
        lca_string_destroy(&state->llvm_modules[i]);
    }

    lca_da_free(state->llvm_modules);

    return exit_code;
}

static bool parse_args(compiler_state* args, int* argc, char*** argv) {
    assert(args != NULL);
    assert(argc != NULL);
    assert(argv != NULL);

    args->program_name = lca_string_view_from_cstring(nob_shift_args(argc, argv));

    while (*argc > 0) {
        lca_string_view arg = lca_string_view_from_cstring(nob_shift_args(argc, argv));
        if (lca_string_view_equals(arg, LCA_SV_CONSTANT("--help"))) {
            args->help = true;
        } else if (lca_string_view_equals(arg, LCA_SV_CONSTANT("--verbose"))) {
            args->verbose = true;
        } else if (lca_string_view_equals(arg, LCA_SV_CONSTANT("-E")) || lca_string_view_equals(arg, LCA_SV_CONSTANT("--preprocess"))) {
            args->preprocess_only = true;
        } else if (lca_string_view_equals(arg, LCA_SV_CONSTANT("--ast"))) {
            args->parse_only = true;
        } else if (lca_string_view_equals(arg, LCA_SV_CONSTANT("-fsyntax-only")) || lca_string_view_equals(arg, LCA_SV_CONSTANT("--sema"))) {
            args->sema_only = true;
        } else if (lca_string_view_equals(arg, LCA_SV_CONSTANT("-S")) || lca_string_view_equals(arg, LCA_SV_CONSTANT("--assemble"))) {
            args->assemble_only = true;
        } else if (lca_string_view_equals(arg, LCA_SV_CONSTANT("-emit-c"))) {
            args->emit_c = true;
        } else if (lca_string_view_equals(arg, LCA_SV_CONSTANT("-emit-lyir"))) {
            args->emit_lyir = true;
        } else if (lca_string_view_equals(arg, LCA_SV_CONSTANT("-emit-llvm"))) {
            args->emit_llvm = true;
        } else if (lca_string_view_equals(arg, LCA_SV_CONSTANT("--nocolor"))) {
            args->use_color = COLOR_NEVER;
        } else if (lca_string_view_equals(arg, LCA_SV_CONSTANT("--byte-diagnostics"))) {
            args->use_byte_positions_in_diagnostics = true;
        } else if (lca_string_view_equals(arg, LCA_SV_CONSTANT("--backend"))) {
            if (argc == 0) {
                fprintf(stderr, "'--backend' requires an argument\n");
                return false;
            }

            const char* backend = nob_shift_args(argc, argv);
            assert(backend != NULL);

            if (0 == strcmp(backend, "c")) {
                args->backend = BACKEND_C;
            } else if (0 == strcmp(backend, "llvm")) {
                args->backend = BACKEND_LLVM;
            } else {
                fprintf(stderr, "Unknown value for option '--backend': %s\n", backend);
                return false;
            }
        } else if (lca_string_view_equals(arg, LCA_SV_CONSTANT("-x"))) {
            if (argc == 0) {
                fprintf(stderr, "'-x' requires an argument\n");
                return false;
            }

            const char* backend = nob_shift_args(argc, argv);
            assert(backend != NULL);

            if (0 == strcmp(backend, "c")) {
                args->override_file_kind = SOURCE_C;
            } else if (0 == strcmp(backend, "laye")) {
                args->override_file_kind = SOURCE_LAYE;
            } else if (0 == strcmp(backend, "lyir")) {
                args->override_file_kind = SOURCE_LYIR;
            } else if (0 == strcmp(backend, "default")) {
                args->override_file_kind = SOURCE_DEFAULT;
            } else {
                fprintf(stderr, "Unknown value for option '-x': %s\n", backend);
                return false;
            }
        } else if (lca_string_view_equals(arg, LCA_SV_CONSTANT("-o"))) {
            if (argc == 0) {
                fprintf(stderr, "'-o' requires a file path as the output file, but no additional arguments were provided\n");
                return false;
            } else {
                args->output_file = lca_string_view_from_cstring(nob_shift_args(argc, argv));
                if (lca_string_view_equals(args->output_file, LCA_SV_CONSTANT("-"))) {
                    args->is_output_file_stdout = true;
                }
            }
        } else if (lca_string_view_equals(arg, LCA_SV_CONSTANT("-l"))) {
            if (argc == 0) {
                fprintf(stderr, "'-l' requires a file path as the output file, but no additional arguments were provided\n");
                return false;
            } else {
                lca_da_push(args->link_libraries, lca_string_view_from_cstring(nob_shift_args(argc, argv)));
            }
        } else if (lca_string_view_equals(arg, LCA_SV_CONSTANT("-I"))) {
            if (argc == 0) {
                fprintf(stderr, "'-I' requires a file path as an include directory, but no additional arguments were provided\n");
                return false;
            } else {
                lca_da_push(args->include_directories, lca_string_view_from_cstring(nob_shift_args(argc, argv)));
            }
        } else if (lca_string_view_equals(arg, LCA_SV_CONSTANT("-L"))) {
            if (argc == 0) {
                fprintf(stderr, "'-L' requires a file path as a library directory, but no additional arguments were provided\n");
                return false;
            } else {
                lca_da_push(args->library_directories, lca_string_view_from_cstring(nob_shift_args(argc, argv)));
            }
        } else if (lca_string_view_starts_with(arg, LCA_SV_CONSTANT("-l"))) {
            lca_da_push(args->link_libraries, lca_string_view_slice(arg, 2, -1));
        } else {
            source_file_info info = {
                .path = arg,
                .kind = args->override_file_kind,
            };
            lca_da_push(args->input_files, info);
        }
    }

    if (lca_da_count(args->input_files) == 0 && !args->help) {
        fprintf(stderr, "%.*s: no input file(s) given\n", LCA_STR_EXPAND(args->program_name));
        return false;
    }

    return true;
}
