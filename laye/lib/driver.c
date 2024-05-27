#include "laye.h"

#define LAYE_HELP_TEXT_USAGE \
    "Usage: %.*s [%.*s] [--help] [--version] [<args>]\n"

#define LAYE_HELP_TEXT_NOCMD                                                                                          \
    "    --help                    Print this help text, then exit.\n"                                                \
    "    --version                 Print version information, then exit.\n"                                           \
    "\n"                                                                                                              \
    "    -o <file>                 Write output to <file>. If <file> is '-` (a single dash), then output\n"           \
    "                              will be written to stdout.\n"                                                      \
    "    -x <source-kind>          Changes the default interpretation of an input source file.\n"                     \
    "                              By default, the source file extension determines what format the file is.\n"       \
    "                              When overriden, treats the following source files as a specific format instead.\n" \
    "                              One of 'default', 'c', 'laye' or 'lyir'.\n"                                        \
    "                              Default: 'default'.\n"                                                             \
    "    --backend <backend>       What code generation backend to use. One of 'c' or 'llvm'.\n"                      \
    "                              Default: 'c'.\n"                                                                   \
    "\n"                                                                                                              \
    "  actions:\n"                                                                                                    \
    "    -E, --preprocess          Only run the preprocessor (for C files).\n"                                        \
    "                              Does nothing for source files which don't need preprocessed.\n"                    \
    "    --parse                   Run the parse step.\n"                                                             \
    "                              Since the parser and semantic analyser happen at the same time for C files,\n"     \
    "                              --parse implies -fsyntax-only for them instead.\n"                                 \
    "    -fsyntax-only, --sema     Run the preprocessor, parser and semantic analysis steps.\n"                       \
    "    -S, --codegen             Only run preprocess and compilation steps.\n"                                      \
    "    -c, --assemble            Only run preprocess, compile, and assemble steps.\n"                               \
    "    -emit-lyir                Uses the LYIR representation for assembler files.\n"                               \
    "    -emit-llvm                Uses the LLVM representation for assembler files.\n"                               \
    "    -emit-c                   Rather than emiting typical IR or assembler, emits C source code instead.\n"       \
    "\n"                                                                                                              \
    "  diagnostics and output:\n"                                                                                     \
    "    -fno-color-diagnostics    Disables colors in diagnostics.\n"                                                 \
    "    -fcolor-diagnostics, -fdiagnostics-color\n"                                                                  \
    "                              Enables colors in diagnostics.\n"                                                  \
    "    -fdiagnostics-color=<arg> Specify when to use colors in diagnostics. Use 'auto' to determine based\n"        \
    "                              on the output stream. One of 'never', 'always' or 'auto'.\n"                       \
    "    --byte-diagnostics        Report diagnostic information with a byte offset rather than line/column.\n"       \
    "\n"                                                                                                              \
    "  include path management:\n"                                                                                    \
    "    -I<dir>, --include-directory <arg>, --include-directory=<arg>\n"                                             \
    "                              Add directory to include search path.\n"                                           \
    "\n"                                                                                                              \
    "  linker options:\n"                                                                                             \
    "    -L<dir>, --library-directory <arg>, --library-directory=<arg>\n"                                             \
    "                              Add <dir> to linker's library search path.\n"                                      \
    "    -l<file>                  Tell the linker to search for and link against the library <file>.\n"

#define LAYE_HELP_TEXT_BUILD \
    "\n"

#define LAYE_HELP_TEXT_TEST \
    "\n"

#define LAYE_HELP_TEXT                                                                                       \
    "\nCommands:\n\n"                                                                                        \
    "<no command>                  By default, the program operates similar to a C compiler.\n"              \
    "                              The arguments may not be feature complete at any given time, but\n"       \
    "                              the end goal is to behave as close to a drop-in replacement for\n"        \
    "                              LLVM and GCC as possible to ease use in existing projects.\n"             \
    "                              This part of the compiler does not promise to faithfully emulate those\n" \
    "                              other compilers 100%. The intent is to use it as a friendly and\n"        \
    "                              comfortable interface to reduce learning curves.\n"                       \
    "                              It also, of course, has additional options specific to this compiler.\n"  \
    "\n" LAYE_HELP_TEXT_NOCMD                                                                                \
    ""

int laye_main(laye_args args) {
    int result = 0;

    fprintf(stderr, "Hello, %.*s!\n", LCA_STR_EXPAND(args.program_name));

defer:;
    return 0;
}

#define LAYE_ARGS_PRINTF(Message, ...)                                                \
    do {                                                                              \
        if (logger) { logger(lca_temp_sprintf(Message __VA_OPT__(, ) __VA_ARGS__)); } \
    } while (0)
#define LAYE_ARGS_LOG(Message, ...)                                                                 \
    do {                                                                                            \
        if (logger) { logger(lca_temp_sprintf("laye: " Message "\n" __VA_OPT__(, ) __VA_ARGS__)); } \
    } while (0)
#define LAYE_ARGS_ERR(Message, ...)                                                                                                           \
    do {                                                                                                                                      \
        if (logger) { logger(lca_temp_sprintf("laye: " ANSI_COLOR_RED "error: " ANSI_COLOR_RESET Message "\n" __VA_OPT__(, ) __VA_ARGS__)); } \
    } while (0)

void laye_args_usage_print(laye_args* args, laye_args_parse_logger logger) {
    lca_string_view command = args->command;
    if (command.count == 0) {
        command = LCA_SV_CONSTANT("<command>");
    }

    LAYE_ARGS_PRINTF(LAYE_HELP_TEXT_USAGE, LCA_STR_EXPAND(args->program_name), LCA_STR_EXPAND(command));
    LAYE_ARGS_PRINTF("%s\n", LAYE_HELP_TEXT);
}

laye_args_parse_result laye_args_parse(laye_args* args, int argc, char** argv, laye_args_parse_logger logger) {
    LCA_ASSERT(args != NULL, "args are required");

    laye_args_parse_result result = LAYE_ARGS_OK;

    args->requested_compile_step = LAYE_COMPILE_STEP_LINK;
    args->backend = LYIR_BACKEND_LLVM;

    if (argc == 0) return LAYE_ARGS_NO_ARGS;

    args->program_name = lca_string_view_from_cstring(argv[0]);
    lca_shift_args(&argc, &argv);

    bool help = false;

    bool emit_c = false;
    bool emit_llvm = false;
    bool emit_lyir = false;
    bool emit_asm = false;

    bool force_value_args = false;

    laye_source_file_kind override_file_kind = LAYE_SOURCE_DEFAULT;
    while (argc > 0) {
        lca_string_view arg = lca_string_view_from_cstring(lca_shift_args(&argc, &argv));
        if (force_value_args) {
            goto arg_is_value;
        }

        if (lca_string_view_equals_cstring(arg, "--")) {
            force_value_args = true;
        } else if (lca_string_view_equals_cstring(arg, "--help")) {
            help = true;
        } else if (lca_string_view_equals_cstring(arg, "--verbose")) {
            args->verbose = true;
        } else if (lca_string_view_equals_cstring(arg, "--byte-diagnostics")) {
            args->use_byte_positions_in_diagnostics = true;
        } else if (lca_string_view_equals_cstring(arg, "-fcolor-diagnostics")) {
            args->use_color = LAYE_COLOR_ALWAYS;
        } else if (lca_string_view_equals_cstring(arg, "-fno-color-diagnostics")) {
            args->use_color = LAYE_COLOR_NEVER;
        } else if (lca_string_view_starts_with(arg, LCA_SV_CONSTANT("-fdiagnostics-color"))) {
            // TODO(local): properly handle `=`???

            args->use_color = LAYE_COLOR_ALWAYS;

            lca_string_view color_arg = lca_string_view_slice(arg, 19, -1);
            if (color_arg.count != 0) {
                if (lca_string_view_equals_cstring(color_arg, "=auto")) {
                    args->use_color = LAYE_COLOR_AUTO;
                } else if (lca_string_view_equals_cstring(color_arg, "=always")) {
                    args->use_color = LAYE_COLOR_ALWAYS;
                } else if (lca_string_view_equals_cstring(color_arg, "=never")) {
                    args->use_color = LAYE_COLOR_NEVER;
                } else {
                    LAYE_ARGS_ERR("unknown argument: '%.*s'", LCA_STR_EXPAND(arg));
                }
            }
        } else if (lca_string_view_equals_cstring(arg, "-E") || lca_string_view_equals_cstring(arg, "--preprocess")) {
            args->requested_compile_step = LAYE_COMPILE_STEP_PREPROCESS;
        } else if (lca_string_view_equals_cstring(arg, "--parse")) {
            args->requested_compile_step = LAYE_COMPILE_STEP_PARSE;
        } else if (lca_string_view_equals_cstring(arg, "-fsyntax-only") || lca_string_view_equals_cstring(arg, "--sema")) {
            args->requested_compile_step = LAYE_COMPILE_STEP_SEMA;
        } else if (lca_string_view_equals_cstring(arg, "-S") || lca_string_view_equals_cstring(arg, "--codegen")) {
            args->requested_compile_step = LAYE_COMPILE_STEP_CODEGEN;
        } else if (lca_string_view_equals_cstring(arg, "-c") || lca_string_view_equals_cstring(arg, "--assemble")) {
            args->requested_compile_step = LAYE_COMPILE_STEP_ASSEMBLE;
        } else if (lca_string_view_equals_cstring(arg, "-emit-c")) {
            emit_c = true;
            args->backend = LYIR_BACKEND_C;
        } else if (lca_string_view_equals_cstring(arg, "-emit-llvm")) {
            emit_llvm = true;
            args->backend = LYIR_BACKEND_LLVM;
        } else if (lca_string_view_equals_cstring(arg, "-emit-lyir")) {
            emit_lyir = true;
            args->backend = LYIR_BACKEND_NONE;
        } else if (lca_string_view_equals_cstring(arg, "-emit-asm")) {
            emit_asm = true;
            args->backend = LYIR_BACKEND_ASM;
        } else if (lca_string_view_equals_cstring(arg, "--backend")) {
            if (argc == 0) {
                result = LAYE_ARGS_ERR_UNKNOWN;
                LAYE_ARGS_ERR("argument to '--backend' is missing (expected 1 value)");
            } else {
                const char* backend = lca_shift_args(&argc, &argv);
                LCA_ASSERT(backend != NULL, "where backend");

                if (0 == strcmp(backend, "c")) {
                    args->backend = LYIR_BACKEND_C;
                } else if (0 == strcmp(backend, "llvm")) {
                    args->backend = LYIR_BACKEND_LLVM;
                } else if (0 == strcmp(backend, "asm")) {
                    args->backend = LYIR_BACKEND_ASM;
                } else {
                    result = LAYE_ARGS_ERR_UNKNOWN;
                    LAYE_ARGS_ERR("backend not recognized: '%s'", backend);
                }
            }
        } else if (lca_string_view_equals_cstring(arg, "-x")) {
            if (argc == 0) {
                result = LAYE_ARGS_ERR_UNKNOWN;
                LAYE_ARGS_ERR("argument to '-x' is missing (expected 1 value)");
            } else {
                const char* language = lca_shift_args(&argc, &argv);
                LCA_ASSERT(language != NULL, "where language");

                if (0 == strcmp(language, "c")) {
                    override_file_kind = LAYE_SOURCE_C;
                } else if (0 == strcmp(language, "laye")) {
                    override_file_kind = LAYE_SOURCE_LAYE;
                } else if (0 == strcmp(language, "lyir")) {
                    override_file_kind = LAYE_SOURCE_LYIR;
                } else if (0 == strcmp(language, "default")) {
                    override_file_kind = LAYE_SOURCE_DEFAULT;
                } else {
                    result = LAYE_ARGS_ERR_UNKNOWN;
                    LAYE_ARGS_ERR("language not recognized: '%s'", language);
                }
            }
        } else if (lca_string_view_equals_cstring(arg, "-o")) {
            if (argc == 0) {
                result = LAYE_ARGS_ERR_UNKNOWN;
                LAYE_ARGS_ERR("argument to '-o' is missing (expected 1 value)");
            } else {
                args->output_file = lca_string_view_from_cstring(lca_shift_args(&argc, &argv));
                if (lca_string_view_equals(args->output_file, LCA_SV_CONSTANT("-"))) {
                    args->is_output_file_stdout = true;
                }
            }
        } else if (lca_string_view_equals_cstring(arg, "--include-directory")) {
            if (argc == 0) {
                result = LAYE_ARGS_ERR_UNKNOWN;
                LAYE_ARGS_ERR("argument to '--include-directory' is missing (expected 1 value)");
            } else {
                lca_da_push(args->include_directories, lca_string_view_from_cstring(lca_shift_args(&argc, &argv)));
            }
        } else if (lca_string_view_starts_with(arg, LCA_SV_CONSTANT("--include-directory="))) {
            lca_string_view include_directory_name = lca_string_view_slice(arg, 20, -1);
            if (include_directory_name.count == 0) {
                result = LAYE_ARGS_ERR_UNKNOWN;
                LAYE_ARGS_ERR("argument to '--include-directory' is missing (expected 1 value)");
            } else {
                lca_da_push(args->include_directories, include_directory_name);
            }
        } else if (lca_string_view_equals_cstring(arg, "-I")) {
            if (argc == 0) {
                result = LAYE_ARGS_ERR_UNKNOWN;
                LAYE_ARGS_ERR("argument to '-I' is missing (expected 1 value)");
            } else {
                lca_da_push(args->include_directories, lca_string_view_from_cstring(lca_shift_args(&argc, &argv)));
            }
        } else if (lca_string_view_starts_with(arg, LCA_SV_CONSTANT("-I"))) {
            lca_string_view include_directory_name = lca_string_view_slice(arg, 2, -1);
            if (include_directory_name.count == 0) {
                result = LAYE_ARGS_ERR_UNKNOWN;
                LAYE_ARGS_ERR("argument to '-I' is missing (expected 1 value)");
            } else {
                lca_da_push(args->include_directories, include_directory_name);
            }
        } else if (lca_string_view_equals_cstring(arg, "--library-directory")) {
            if (argc == 0) {
                result = LAYE_ARGS_ERR_UNKNOWN;
                LAYE_ARGS_ERR("argument to '--library-directory' is missing (expected 1 value)");
            } else {
                lca_da_push(args->library_directories, lca_string_view_from_cstring(lca_shift_args(&argc, &argv)));
            }
        } else if (lca_string_view_starts_with(arg, LCA_SV_CONSTANT("--library-directory="))) {
            lca_string_view library_directory_name = lca_string_view_slice(arg, 20, -1);
            if (library_directory_name.count == 0) {
                result = LAYE_ARGS_ERR_UNKNOWN;
                LAYE_ARGS_ERR("argument to '--library-directory' is missing (expected 1 value)");
            } else {
                lca_da_push(args->library_directories, library_directory_name);
            }
        } else if (lca_string_view_equals_cstring(arg, "-L")) {
            if (argc == 0) {
                result = LAYE_ARGS_ERR_UNKNOWN;
                LAYE_ARGS_ERR("argument to '-L' is missing (expected 1 value)");
            } else {
                lca_da_push(args->library_directories, lca_string_view_from_cstring(lca_shift_args(&argc, &argv)));
            }
        } else if (lca_string_view_starts_with(arg, LCA_SV_CONSTANT("-L"))) {
            lca_string_view library_directory_name = lca_string_view_slice(arg, 2, -1);
            if (library_directory_name.count == 0) {
                result = LAYE_ARGS_ERR_UNKNOWN;
                LAYE_ARGS_ERR("argument to '-L' is missing (expected 1 value)");
            } else {
                lca_da_push(args->library_directories, library_directory_name);
            }
        } else if (lca_string_view_equals_cstring(arg, "-l")) {
            if (argc == 0) {
                result = LAYE_ARGS_ERR_UNKNOWN;
                LAYE_ARGS_ERR("argument to '-l' is missing (expected 1 value)");
            } else {
                lca_da_push(args->link_libraries, lca_string_view_from_cstring(lca_shift_args(&argc, &argv)));
            }
        } else if (lca_string_view_starts_with(arg, LCA_SV_CONSTANT("-l"))) {
            lca_string_view library_name = lca_string_view_slice(arg, 2, -1);
            if (library_name.count == 0) {
                result = LAYE_ARGS_ERR_UNKNOWN;
                LAYE_ARGS_ERR("argument to '-l' is missing (expected 1 value)");
            } else {
                lca_da_push(args->link_libraries, library_name);
            }
        } else if (lca_string_view_starts_with(arg, LCA_SV_CONSTANT("-"))) {
            result = LAYE_ARGS_ERR_UNKNOWN;
            LAYE_ARGS_ERR("unknown argument: '%.*s'", LCA_STR_EXPAND(arg));
        } else {
        arg_is_value:;
            if (!lca_file_exists(lca_string_view_to_cstring(temp_allocator, arg))) {
                result = LAYE_ARGS_ERR_UNKNOWN;
                LAYE_ARGS_ERR("no such file: '%.*s'", LCA_STR_EXPAND(arg));
            } else {
                laye_source_file_info info = {
                    .path = arg,
                    .kind = override_file_kind,
                };
                lca_da_push(args->input_files, info);
            }
        }
    }

    if (args->output_file.count != 0 && lca_da_count(args->input_files) > 1 && args->requested_compile_step != LAYE_COMPILE_STEP_LINK) {
        result = LAYE_ARGS_ERR_UNKNOWN;
        LAYE_ARGS_ERR("cannot specify -o when generating multiple output files");
    }

    if (emit_c) {
        if (args->requested_compile_step != LAYE_COMPILE_STEP_CODEGEN) {
            result = LAYE_ARGS_ERR_UNKNOWN;
            LAYE_ARGS_ERR("-emit-c cannot be used when %s", laye_compile_step_to_verb_cstring(args->requested_compile_step));
        }
    }

    if (emit_llvm) {
        if (args->requested_compile_step != LAYE_COMPILE_STEP_CODEGEN) {
            result = LAYE_ARGS_ERR_UNKNOWN;
            LAYE_ARGS_ERR("-emit-llvm cannot be used when %s", laye_compile_step_to_verb_cstring(args->requested_compile_step));
        }
    }

    if (emit_lyir) {
        if (args->requested_compile_step != LAYE_COMPILE_STEP_CODEGEN) {
            result = LAYE_ARGS_ERR_UNKNOWN;
            LAYE_ARGS_ERR("-emit-lyir cannot be used when %s", laye_compile_step_to_verb_cstring(args->requested_compile_step));
        }
    }

    if (emit_asm) {
        if (args->requested_compile_step != LAYE_COMPILE_STEP_CODEGEN) {
            result = LAYE_ARGS_ERR_UNKNOWN;
            LAYE_ARGS_ERR("-emit-asm cannot be used when %s", laye_compile_step_to_verb_cstring(args->requested_compile_step));
        }
    }

    if (result == LAYE_ARGS_OK && lca_da_count(args->input_files) == 0 && !help) {
        result = LAYE_ARGS_ERR_UNKNOWN;
        LAYE_ARGS_ERR("no input file(s) given");
    }

    if (help) {
        laye_args_usage_print(args, laye_args_parse_logger_default);
        exit(0);
    }

    return result;
}

void laye_args_destroy(laye_args* args) {
    lca_da_free(args->include_directories);
    lca_da_free(args->input_files);
    lca_da_free(args->library_directories);
    lca_da_free(args->link_libraries);
}

void laye_args_parse_logger_default(char* message) {
    fprintf(stderr, "%s", message);
}

const char* laye_compile_step_to_verb_cstring(laye_compilation_step step) {
    switch (step) {
        default: LCA_ASSERT(false, "unknown or unhandled laye_compilation_step");
        case LAYE_COMPILE_STEP_PREPROCESS: return "preprocessing";
        case LAYE_COMPILE_STEP_PARSE: return "parsing";
        case LAYE_COMPILE_STEP_SEMA: return "analysing";
        case LAYE_COMPILE_STEP_CODEGEN: return "generating code";
        case LAYE_COMPILE_STEP_ASSEMBLE: return "assembling";
        case LAYE_COMPILE_STEP_LINK: return "linking";
    }
}
