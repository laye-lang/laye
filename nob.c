#if defined(_WIN32)
#    define NOB_WINDOWS
#    define NOB_PATH_SEP "\\"
#elif defined(__unix__)
#    define NOB_UNIX
#    define NOB_PATH_SEP "/"
#else
#    error "This build script currently only supports Windows and Unix, sorry"
#endif

#define BUILD_DIR  "." NOB_PATH_SEP "out"
#define OBJECT_DIR BUILD_DIR NOB_PATH_SEP "o"

#define STAGE1_OUT_PATH BUILD_DIR NOB_PATH_SEP "laye1"
#define STAGE2_OUT_PATH BUILD_DIR NOB_PATH_SEP "laye"

#ifndef CC
#    if defined(__clang__)
#        define CC "clang"
#    elif defined(__GNUC__)
#        define CC "gcc"
#    elif defined(_MSC_VER)
#        define NOB_CL_EXE
#        define CC "cl.exe"
#    else
#        define CC "cc"
#    endif
#endif

#ifdef NOB_CL_EXE
#    define NOB_REBUILD_URSELF(binary_path, source_path) CC, source_path
#else
#    define NOB_REBUILD_URSELF(binary_path, source_path) CC, "-o", binary_path, source_path
#endif

#define NOB_IMPLEMENTATION
#include "stage1/include/nob.h"

static bool no_asan;

static const char* stage1_laye_headers_dir = "./stage1/include/";

static const char* stage1_laye_sources[] = {
    "./stage1/src/layec_shared.c",
    "./stage1/src/layec_context.c",
    "./stage1/src/layec_depgraph.c",
    "./stage1/src/layec_ir.c",
    "./stage1/src/irpass/validate.c",
    "./stage1/src/irpass/abi.c",
    "./stage1/src/layec_cback.c",
    "./stage1/src/layec_llvm.c",
    "./stage1/src/c/c_data.c",
    "./stage1/src/c/c_debug.c",
    "./stage1/src/c/c_parser.c",
    "./stage1/src/laye/laye_data.c",
    "./stage1/src/laye/laye_debug.c",
    "./stage1/src/laye/laye_parser.c",
    "./stage1/src/laye/laye_sema.c",
    "./stage1/src/laye/laye_irgen.c",
    // Main file always has to be the last
    "./stage1/src/compiler.c"
};

static int64_t stage1_laye_sources_count = sizeof(stage1_laye_sources) / sizeof(stage1_laye_sources[0]);
static int64_t stage1_laye_sources_count_without_main = (sizeof(stage1_laye_sources) / sizeof(stage1_laye_sources[0])) - 1;

// if the path ends in a slash, this returns an empty string
static const char* basename(const char* path) {
    for (int64_t i = (int64_t)strlen(path) - 1; i >= 0; i--) {
        if (path[i] == '/' || path[i] == '\\')
            return &path[i + 1];
    }

    return path;
}

static const char* path_contact(const char* lhs, const char* rhs) {
    if (rhs[0] == '/' || rhs[0] == '\\') {
        rhs++;
    }

    size_t lhs_len = strlen(lhs);
    if (lhs[lhs_len - 1] == '/' || lhs[lhs_len - 1] == '\\') {
        return nob_temp_sprintf("%s%s", lhs, rhs);
    } else {
        return nob_temp_sprintf("%s" NOB_PATH_SEP "%s", lhs, rhs);
    }
}

static void cflags(Nob_Cmd* cmd) {
    nob_cmd_append(cmd, "-I", stage1_laye_headers_dir);
    nob_cmd_append(cmd, "-std=c17");
    nob_cmd_append(cmd, "-pedantic");
    nob_cmd_append(cmd, "-pedantic-errors");
    nob_cmd_append(cmd, "-ggdb");
    nob_cmd_append(cmd, "-Werror=return-type");
    if (!no_asan) {
        nob_cmd_append(cmd, "-fsanitize=address");
    }
    nob_cmd_append(cmd, "-D__USE_POSIX");
    nob_cmd_append(cmd, "-D_XOPEN_SOURCE=600");
}

static void layeflags(Nob_Cmd* cmd) {
    nob_cmd_append(cmd, "-I", "./lib/laye/liblaye");
}

static char* to_object_file_path(const char* filepath) {
    const char* filename = basename(filepath);
    char* objectfile_path = nob_temp_sprintf(OBJECT_DIR "/%s.o", filename);

    return objectfile_path;
}

static bool build_stage1_laye_object_files(bool complete_rebuild) {
    Nob_Proc processes[stage1_laye_sources_count];
    bool compiled_anything = false;

    // If no build directory exits it needs to be fully rebuild anyways
    if (nob_file_exists(BUILD_DIR) != 1) complete_rebuild = true;

    // checking for change in the header files
    if (!complete_rebuild) {
        Nob_File_Paths header_files = {0};

        // TODO: Support nesting
        // currently we check everything in the `stage1_laye_headers_dir` we can find, so
        // no proper handling of non-header files and no handling of directories
        if (!nob_read_entire_dir(stage1_laye_headers_dir, &header_files)) {
            nob_log(NOB_WARNING, "Couldn't read header directory. Forcing rebuild");
            complete_rebuild = true;
            goto build;
        }

        // We need to rebuild, if any header file is newer than *some* object file.
        // It can be *some* object file, because the newest object file will always
        // be as old as the last built, so if a header file gets modified it will be
        // newer than all object files
        char* output_path = to_object_file_path(stage1_laye_sources[0]);

        // add directory and file together as `nob_read_entire_dir` doesn't do this
        size_t checkpoint = nob_temp_save();
        for (int64_t i = 0; i < header_files.count; i++) {
            header_files.items[i] = nob_temp_sprintf("%s/%s", stage1_laye_headers_dir, header_files.items[i]);
        }

        int64_t rebuild_is_needed = nob_needs_rebuild(output_path, header_files.items, header_files.count);
        nob_temp_rewind(checkpoint);

        if (rebuild_is_needed < 0) {
            nob_log(NOB_WARNING, "Couldn't detected if headers changed. Forcing rebuild");
        } else if (rebuild_is_needed) complete_rebuild = true;
    }

build:
    for (int64_t i = 0; i < stage1_laye_sources_count; i++) {
        char* output_path = to_object_file_path(stage1_laye_sources[i]);

        if (!complete_rebuild) {
            int64_t rebuild_is_needed = nob_needs_rebuild1(output_path, stage1_laye_sources[i]);

            if (rebuild_is_needed < 0) {
                char* msg = nob_temp_sprintf("Couldn't detected if file changed: %s", stage1_laye_sources[i]);
                nob_log(NOB_WARNING, msg);
            } else if (!rebuild_is_needed) {
                processes[i] = NOB_INVALID_PROC;
                continue;
            }
        }

        compiled_anything = true;

        Nob_Cmd cmd = {0};

        nob_cmd_append(&cmd, CC);
        cflags(&cmd);
        nob_cmd_append(&cmd, "-c");

        nob_cmd_append(&cmd, "-o", output_path);

        nob_cmd_append(&cmd, stage1_laye_sources[i]);

        processes[i] = nob_cmd_run_async(cmd);
        if (processes[i] == NOB_INVALID_PROC) {
            nob_log(NOB_ERROR, "Command is invalid");
            exit(1);
        }
    }

    if (compiled_anything) {
        nob_log(NOB_INFO, "Waiting for object files to finish compiling...");
        for (int i = 0; i < stage1_laye_sources_count; i++) {
            if (processes[i] == NOB_INVALID_PROC) continue;

            Nob_Proc_Result result = nob_proc_wait_result(processes[i]);
            if (result.exit_code != 0) exit(1);
        }
    }

    return compiled_anything;
}

static void build_stage1_laye_executable() {
    bool compiled_anything = build_stage1_laye_object_files(false);
    if (!compiled_anything) {
        return;
    }

    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd, CC);
    cflags(&cmd);
    nob_cmd_append(&cmd, "-o", BUILD_DIR "/laye1");

    for (int i = 0; i < stage1_laye_sources_count; i++) {
        char* output_path = to_object_file_path(stage1_laye_sources[i]);
        nob_cmd_append(&cmd, output_path);
    }

    nob_cmd_run_sync(cmd);
}

static void build_stage2_laye_executable() {
    Nob_File_Paths source_files = {0};
    nob_read_entire_dir_recursively("./src", &source_files);

    const char* stage2_output_path = BUILD_DIR "/laye";
    int64_t rebuild_is_needed = nob_needs_rebuild(stage2_output_path, source_files.items, source_files.count);
    if (rebuild_is_needed < 0) {
        nob_log(NOB_WARNING, "Couldn't detected if source files changed. Forcing rebuild");
    } else if (!rebuild_is_needed) {
        return;
    }

    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd, "./out/laye1");
    layeflags(&cmd);
    nob_cmd_append(&cmd, "-o", stage2_output_path);

    for (int i = 0; i < source_files.count; i++) {
        nob_cmd_append(&cmd, source_files.items[i]);
    }

    nob_cmd_run_sync(cmd);
}

static void build_exec_test_runner() {
    if (0 == nob_needs_rebuild1(BUILD_DIR "/exec_test_runner", "./stage1/src/exec_test_runner.c")) return;

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, CC);
    nob_cmd_append(&cmd, "-o", BUILD_DIR "/exec_test_runner");
    cflags(&cmd);
    nob_cmd_append(&cmd, "./stage1/src/exec_test_runner.c");
    nob_cmd_run_sync(cmd);
}

static void build_parse_fuzzer() {
    Nob_Cmd cmd = {0};
    char fuzzer_path[] = "./fuzz/parse_fuzzer.c";
    char* object_path = to_object_file_path(fuzzer_path);

    // Compile `parse_fuzzer.c` to object file
    nob_cmd_append(&cmd, CC);
    nob_cmd_append(&cmd, "-c");
    nob_cmd_append(&cmd, "-o", object_path);
    cflags(&cmd);
    nob_cmd_append(&cmd, fuzzer_path);
    nob_cmd_run_sync(cmd);

    // Link with `-fsanitize=fuzzer` to executable
    Nob_Cmd cmd_link = {0};
    nob_cmd_append(&cmd_link, CC);
    nob_cmd_append(&cmd_link, "-o", BUILD_DIR "/parse_fuzzer");
    nob_cmd_append(&cmd_link, "-fsanitize=fuzzer");
    cflags(&cmd_link);

    for (int i = 0; i < stage1_laye_sources_count_without_main; i++) {
        nob_cmd_append(&cmd_link, stage1_laye_sources[i]);
    }

    nob_cmd_append(&cmd_link, object_path);

    nob_cmd_run_sync(cmd_link);
}

static void build_stage1_laye_driver() {
    build_stage1_laye_executable();
}

static void build_stage2_laye_driver() {
    build_stage1_laye_driver();
    build_stage2_laye_executable();
}

static void run_fchk(bool rebuild) {
    build_stage1_laye_driver();

    Nob_Cmd cmd = {0};

    if (!nob_file_exists("test-out")) {
        nob_cmd_append(&cmd, "cmake", "-S", ".", "-B", "test-out", "-DBUILD_TESTING=ON");
        nob_cmd_run_sync(cmd);

        cmd.count = 0;
        nob_cmd_append(&cmd, "cmake", "--build", "test-out");
        nob_cmd_run_sync(cmd);
    } else if (rebuild) {
        cmd.count = 0;
        nob_cmd_append(&cmd, "cmake", "--build", "test-out");
        nob_cmd_run_sync(cmd);
    }

    cmd.count = 0;
    nob_cmd_append(&cmd, "ctest", "--test-dir", "test-out", "-j`nproc`", "--progress");
    nob_cmd_run_sync(cmd);
}

#define NOB_HELP_TEXT_USAGE "Usage: nob %s [--help] [<args>]\n"

#define NOB_HELP_TEXT_BUILD \
    "    --stage1             Build only the stage1 compiler\n"

#define NOB_HELP_TEXT_RUN \
    "    --stage2             Runs the stage2 compiler instead\n"

#define NOB_HELP_TEXT_INSTALL                                                                              \
    "    --stage2             Install the stage2 compiler (not currently supported)\n"                     \
    "    --bin-prefix         Set the install prefix for the binary files.\n"                              \
    "                         This path will be suffixed with a `bin` directory.\n"                        \
    "    --lib-prefix         Set the install prefix for the library files.\n"                             \
    "                         This path will be suffixed with a `lib` directory.\n"                        \
    "    --prefix             Set the install prefix for both the binary and library files.\n"             \
    "                         This path will be suffixed with a `bin` or `lib` directory.\n"               \
    "                         --prefix is identical to passing both a --bin-prefix and a --lib-prefix.\n"  \
    "    --print-dirs         Prints the resulting installation directories, then exits.\n"                \
    "                         When no explicit prefixes are passed, the default install directories for\n" \
    "                         this platform will be printed.\n"

#define NOB_HELP_TEXT_TEST \
    "    --stage2             Build the test suite for the stage2 compiler (not currently supported)\n"

#define NOB_HELP_TEXT_FUZZ \
    "    --stage2             Build the fuzzer for the stage2 compiler (not currently supported)\n"

#define NOB_HELP_TEXT                                                                                       \
    "\nCommands:\n\n"                                                                                       \
    "build                    Used to build the Laye tools in this project.\n"                              \
    "                         If no command is provided, this is the default.\n" NOB_HELP_TEXT_BUILD        \
    "\n"                                                                                                    \
    "run                      Same as `build`, but also runs the resulting executable with the arguments\n" \
    "                         provided after `--`.\n"                                                       \
    "                         By default this runs the stage1 compiler.\n" NOB_HELP_TEXT_RUN                \
    "\n"                                                                                                    \
    "install                  Installs the requested build artifacts.\n"                                    \
    "                         By default, this will install the stage1 compiler.\n" NOB_HELP_TEXT_INSTALL   \
    "\n"                                                                                                    \
    "test                     Runs the automated test suite.\n"                                             \
    "                         By default, tests are run against the stage1 compiler.\n" NOB_HELP_TEXT_TEST  \
    "\n"                                                                                                    \
    "fuzz                     Runs the fuzzer. The fuzzer currently only runs through parsing.\n"           \
    "                         By default, fuzzing is run against the stage1 compiler.\n" NOB_HELP_TEXT_FUZZ \
    ""

static int nob_help(const char* command) {
    fprintf(stderr, NOB_HELP_TEXT_USAGE, command == NULL ? "[<command>]" : command);
    if (command == NULL) {
        fprintf(stderr, "%s\n", NOB_HELP_TEXT);
    } else if (0 == strcmp("build", command)) {
        fprintf(stderr, "%s\n", NOB_HELP_TEXT_BUILD);
    } else if (0 == strcmp("run", command)) {
        fprintf(stderr, "%s\n", NOB_HELP_TEXT_RUN);
    } else if (0 == strcmp("install", command)) {
        fprintf(stderr, "%s\n", NOB_HELP_TEXT_INSTALL);
    } else if (0 == strcmp("test", command)) {
        fprintf(stderr, "%s\n", NOB_HELP_TEXT_TEST);
    } else if (0 == strcmp("fuzz", command)) {
        fprintf(stderr, "%s\n", NOB_HELP_TEXT_FUZZ);
    } else {
        fprintf(stderr, "unknown command\n");
        return 1;
    }

    return 0;
}

#define NOB_SHARED_ARG_HANDLED   -2
#define NOB_SHARED_ARG_UNHANDLED -1

static int nob_shared_args(const char* command, int* argc, char*** argv) {
    if (*argc == 0) {
        return NOB_SHARED_ARG_UNHANDLED;
    }

    int argc_cached = *argc;
    char** argv_cached = *argv;

    const char* arg = nob_shift_args(argc, argv);
    if (0 == strcmp("--help", arg)) {
        return nob_help(command);
    }

    *argc = argc_cached;
    *argv = argv_cached;

    return NOB_SHARED_ARG_UNHANDLED;
}

typedef enum nob_build_stage {
    NOB_BUILD_STAGE1,
    NOB_BUILD_STAGE2,
} nob_build_stage;

static int nob_build(bool explicit, int argc, char** argv) {
    nob_build_stage stage = NOB_BUILD_STAGE2;

    while (argc > 0) {
        int shared = nob_shared_args(explicit ? "build" : NULL, &argc, &argv);
        if (shared >= 0) {
            return shared;
        } else if (shared == NOB_SHARED_ARG_HANDLED) {
            continue;
        }

        const char* arg = nob_shift_args(&argc, &argv);
        if (0 == strcmp("--stage1", arg)) {
            stage = NOB_BUILD_STAGE1;
        } else {
            fprintf(stderr, "Unknown argument '%s'.\n", arg);
            nob_help(explicit ? "build" : NULL);
        }
    }

    if (stage >= NOB_BUILD_STAGE1) {
        build_stage1_laye_driver();
        build_exec_test_runner();
    }

    if (stage >= NOB_BUILD_STAGE2) {
        build_stage2_laye_driver();
    }

    return 0;
}

static int nob_run(int argc, char** argv) {
    nob_build_stage stage = NOB_BUILD_STAGE1;

    while (argc > 0) {
        int shared = nob_shared_args("run", &argc, &argv);
        if (shared >= 0) {
            return shared;
        } else if (shared == NOB_SHARED_ARG_HANDLED) {
            continue;
        }

        const char* arg = nob_shift_args(&argc, &argv);
        if (0 == strcmp("--", arg)) {
            break;
        } else if (0 == strcmp("--stage2", arg)) {
            stage = NOB_BUILD_STAGE2;
        } else {
            fprintf(stderr, "Unknown argument '%s'.\n", arg);
            nob_help("run");
        }
    }

    if (stage >= NOB_BUILD_STAGE1) {
        build_stage1_laye_driver();
        build_exec_test_runner();
    }

    if (stage >= NOB_BUILD_STAGE2) {
        build_stage2_laye_driver();
    }

    Nob_Cmd cmd = {0};
    if (stage == NOB_BUILD_STAGE1) {
        nob_cmd_append(&cmd, BUILD_DIR "/laye1");
    } else if (stage == NOB_BUILD_STAGE2) {
        nob_cmd_append(&cmd, BUILD_DIR "/laye");
    } else {
        fprintf(stderr, "unknown stage selected somehow: %d\n", stage);
        return 1;
    }

    nob_da_append_many(&cmd, argv, argc);
    nob_cmd_run_sync(cmd);

    return 0;
}

static int nob_install(int argc, char** argv) {
#if defined(NOB_WINDOWS)
    const char* binary_prefix = "C:\\Program Files\\Laye\\bin";
    const char* library_prefix = "C:\\Program Files\\Laye\\lib";
#elif defined(NOB_UNIX)
    const char* binary_prefix = "/usr/bin";
    const char* library_prefix = "/usr/lib";
#endif

    nob_build_stage stage = NOB_BUILD_STAGE1;

    bool has_provided_bin_dir = false;
    bool has_provided_lib_dir = false;

    bool print_dirs = false;

    while (argc > 0) {
        int shared = nob_shared_args("install", &argc, &argv);
        if (shared >= 0) {
            return shared;
        } else if (shared == NOB_SHARED_ARG_HANDLED) {
            continue;
        }

        const char* arg = nob_shift_args(&argc, &argv);
        if (0 == strcmp("--stage2", arg)) {
            stage = NOB_BUILD_STAGE2;
        } else if (0 == strcmp("--print-dirs", arg)) {
            print_dirs = true;
        } else if (0 == strcmp("--prefix", arg)) {
            if (argc == 0) {
                fprintf(stderr, "Option `--prefix` requires the install prefix path as an argument.\n");
                return 1;
            }

            if (has_provided_bin_dir) {
                fprintf(stderr, "A binary install prefix has already been specified before `--prefix`.\n");
                return 1;
            }

            if (has_provided_lib_dir) {
                fprintf(stderr, "A library install prefix has already been specified before `--prefix`.\n");
                return 1;
            }

            const char* prefix = nob_shift_args(&argc, &argv);

            binary_prefix = path_contact(prefix, "bin");
            library_prefix = path_contact(prefix, "lib");

            has_provided_bin_dir = true;
            has_provided_lib_dir = true;
        } else if (0 == strcmp("--bin-prefix", arg)) {
            if (argc == 0) {
                fprintf(stderr, "Option `--bin-prefix` requires the binary install prefix path as an argument.\n");
                return 1;
            }

            if (has_provided_bin_dir) {
                fprintf(stderr, "A binary directory install prefix has already been specified before `--bin-prefix`.\n");
                return 1;
            }

            const char* prefix = nob_shift_args(&argc, &argv);
            binary_prefix = prefix;
            has_provided_bin_dir = true;
        } else if (0 == strcmp("--lib-prefix", arg)) {
            if (argc == 0) {
                fprintf(stderr, "Option `--lib-prefix` requires the library install prefix path as an argument.\n");
                return 1;
            }

            if (has_provided_bin_dir) {
                fprintf(stderr, "A library directory install prefix has already been specified before `--lib-prefix`.\n");
                return 1;
            }

            const char* prefix = nob_shift_args(&argc, &argv);
            has_provided_lib_dir = true;
            library_prefix = prefix;
        } else {
            fprintf(stderr, "Unknown argument '%s'.\n", arg);
            nob_help("install");
        }
    }

    if (!nob_mkdir_if_not_exists(binary_prefix)) {
        return 1;
    }

    if (!nob_mkdir_if_not_exists(library_prefix)) {
        return 1;
    }

#if defined(NOB_WINDOWS)
    const char* bin_path = path_contact(binary_prefix, "layec.exe");
#elif defined(NOB_UNIX)
    const char* bin_path = path_contact(binary_prefix, "layec");
#endif
    const char* lib_path = path_contact(library_prefix, "laye");

    if (print_dirs) {
        fprintf(stderr, "  laye binary install directory:  %s\n", bin_path);
        fprintf(stderr, "  laye library install directory: %s\n", lib_path);
        return 0;
    }

    const char* binary_to_install = NULL;
    if (stage == NOB_BUILD_STAGE1) {
        binary_to_install = BUILD_DIR "/laye1";
    }

    if (stage >= NOB_BUILD_STAGE1) {
        build_stage1_laye_driver();
        build_exec_test_runner();
    }

    if (stage >= NOB_BUILD_STAGE2) {
        build_stage2_laye_driver();
    }

    nob_copy_file(binary_to_install, bin_path);
    nob_copy_directory_recursively("./lib/laye", lib_path);

    return 0;
}

static int nob_test(int argc, char** argv) {
    nob_build_stage stage = NOB_BUILD_STAGE1;

    while (argc > 0) {
        int shared = nob_shared_args("test", &argc, &argv);
        if (shared >= 0) {
            return shared;
        } else if (shared == NOB_SHARED_ARG_HANDLED) {
            continue;
        }

        const char* arg = nob_shift_args(&argc, &argv);
        if (0 == strcmp("--stage2", arg)) {
            stage = NOB_BUILD_STAGE2;
        } else {
            fprintf(stderr, "Unknown argument '%s'.\n", arg);
            nob_help("test");
        }
    }

    // TODO(local): need some way to specify which stage, as it's currently only going to run against stage1
    // since that's all we know how to build it with
    build_stage1_laye_driver();
    build_exec_test_runner();

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, BUILD_DIR "/exec_test_runner");
    if (!nob_cmd_run_sync(cmd)) {
        return 1;
    }

    run_fchk(false);
    return 0;
}

static int nob_fuzz(int argc, char** argv) {
    nob_build_stage stage = NOB_BUILD_STAGE1;

    while (argc > 0) {
        int shared = nob_shared_args("fuzz", &argc, &argv);
        if (shared >= 0) {
            return shared;
        } else if (shared == NOB_SHARED_ARG_HANDLED) {
            continue;
        }

        const char* arg = nob_shift_args(&argc, &argv);
        if (0 == strcmp("--stage2", arg)) {
            stage = NOB_BUILD_STAGE2;
        } else {
            fprintf(stderr, "Unknown argument '%s'.\n", arg);
            nob_help("fuzz");
        }
    }

    if (stage >= NOB_BUILD_STAGE1) {
        build_stage1_laye_driver();
        build_exec_test_runner();
    }

    if (stage >= NOB_BUILD_STAGE2) {
        build_stage2_laye_driver();
    }

    // TODO(local): need some way to specify which stage, as it's currently only going to run against stage1
    // since that's all we know how to build it with
    build_parse_fuzzer();

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, BUILD_DIR "/parse_fuzzer");
    nob_cmd_append(&cmd, "./fuzz/corpus");
    nob_cmd_run_sync(cmd);

    return 0;
}

int main(int argc, char** argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    const char* program = nob_shift_args(&argc, &argv);
    nob_mkdir_if_not_exists(BUILD_DIR);
    nob_mkdir_if_not_exists(OBJECT_DIR);

    if (argc > 0) {
        const char* maybe_command = argv[0];
        if (0 == strcmp("build", maybe_command)) {
            nob_shift_args(&argc, &argv);
            return nob_build(true, argc, argv);
        } else if (0 == strcmp("run", maybe_command)) {
            nob_shift_args(&argc, &argv);
            return nob_run(argc, argv);
        } else if (0 == strcmp("install", maybe_command)) {
            nob_shift_args(&argc, &argv);
            return nob_install(argc, argv);
        } else if (0 == strcmp("test", maybe_command)) {
            nob_shift_args(&argc, &argv);
            return nob_test(argc, argv);
        } else if (0 == strcmp("fuzz", maybe_command)) {
            nob_shift_args(&argc, &argv);
            return nob_fuzz(argc, argv);
        }
    }

    return nob_build(false, argc, argv);
}
