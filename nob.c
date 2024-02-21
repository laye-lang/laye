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
#include "include/nob.h"

#include <libgen.h>

#define BUILD_DIR "./out"
#define OBJECT_DIR "o"

typedef struct args {
    const char* build_project;
    bool no_asan;
} args_t;

static args_t args;

static void cflags(Nob_Cmd* cmd) {
    nob_cmd_append(cmd, "-I", "include");
    nob_cmd_append(cmd, "-std=c17");
    nob_cmd_append(cmd, "-pedantic");
    nob_cmd_append(cmd, "-pedantic-errors");
    nob_cmd_append(cmd, "-ggdb");
    nob_cmd_append(cmd, "-Werror=return-type");
    if (!args.no_asan) {
        nob_cmd_append(cmd, "-fsanitize=address");
    }
    nob_cmd_append(cmd, "-D__USE_POSIX");
    nob_cmd_append(cmd, "-D_XOPEN_SOURCE=600");
}

static char *layec_sources[] = {
    "./lib/layec_shared.c",
    "./lib/layec_context.c",
    "./lib/layec_depgraph.c",
    "./lib/layec_ir.c",
    "./lib/irpass/validate.c",
    "./lib/layec_llvm.c",
    "./lib/laye/laye_data.c",
    "./lib/laye/laye_debug.c",
    "./lib/laye/laye_parser.c",
    "./lib/laye/laye_sema.c",
    "./lib/laye/laye_irgen.c",
    // Main file always has to be the last
    "./src/layec.c"
};

static int64_t layec_sources_count = sizeof(layec_sources) / sizeof(layec_sources[0]);
static int64_t layec_sources_count_without_main = (sizeof(layec_sources) / sizeof(layec_sources[0])) - 1;

static char *to_object_file_path(char *filepath) {
    char *filename = basename((char *) filepath);
    char *objectfile_path = nob_temp_sprintf(BUILD_DIR"/"OBJECT_DIR"/%s.o", filename);

    return objectfile_path;
}

static void build_layec_object_files(bool complete_rebuild) {
    Nob_Proc processes[layec_sources_count];
    bool compiled_anything = false;

    for (int i = 0; i < layec_sources_count; i++) {
        char *output_path = to_object_file_path(layec_sources[i]);

        if (!complete_rebuild) {
            int64_t rebuild_is_needed = nob_needs_rebuild1(output_path, layec_sources[i]);

            if (rebuild_is_needed < 0) {
                char *msg = nob_temp_sprintf("Couldn't detected if file changed: %s", layec_sources[i]);
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

        nob_cmd_append(&cmd, layec_sources[i]);

        processes[i] = nob_cmd_run_async(cmd);
        if (processes[i] == NOB_INVALID_PROC) {
            nob_log(NOB_ERROR, "Command is invalid");
            exit(1);
        }
    }

    if (compiled_anything) {
        nob_log(NOB_INFO, "Waiting for object files to finish compiling...");
        for (int i = 0; i < layec_sources_count; i++) {
            if (processes[i] == NOB_INVALID_PROC) continue;

            Nob_Proc_Result result = nob_proc_wait_result(processes[i]);
            if (result.exit_code != 0) exit(1);
        }
    }
}

static void build_layec_executable() {
    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd, CC);
    cflags(&cmd);
    nob_cmd_append(&cmd, "-o", BUILD_DIR"/layec");

    for (int i = 0; i < layec_sources_count; i++) {
        char *output_path = to_object_file_path(layec_sources[i]);
        nob_cmd_append(&cmd, output_path);
    }
    
    nob_cmd_run_sync(cmd);
}

static void build_exec_test_runner() {
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, CC);
    nob_cmd_append(&cmd, "-o", BUILD_DIR"/exec_test_runner");
    cflags(&cmd);
    nob_cmd_append(&cmd, "./src/exec_test_runner.c");
    nob_cmd_run_sync(cmd);
}

static void build_parse_fuzzer() {
    Nob_Cmd cmd = {0};
    char fuzzer_path[] = "./fuzz/parse_fuzzer.c";
    char *object_path = to_object_file_path(fuzzer_path);

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
    nob_cmd_append(&cmd_link, "-o", BUILD_DIR"/parse_fuzzer");
    nob_cmd_append(&cmd_link, "-fsanitize=fuzzer");
    cflags(&cmd_link);

    for (int i = 0; i < layec_sources_count_without_main; i++) {
        nob_cmd_append(&cmd_link, layec_sources[i]);
    }

    nob_cmd_append(&cmd_link, object_path);

    nob_cmd_run_sync(cmd_link);
}

static void build_layec_driver() {
    build_layec_object_files(false);
    build_layec_executable();
}

static void build_all() {
    build_layec_driver();
    build_exec_test_runner();
    build_parse_fuzzer();
}

static void install() {
}

static void run_fchk(bool rebuild) {
    build_layec_driver();

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

static void parse_args(int* argc, char*** argv) {
}

int main(int argc, char** argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    const char* program = nob_shift_args(&argc, &argv);
    nob_mkdir_if_not_exists(BUILD_DIR);
    nob_mkdir_if_not_exists(BUILD_DIR"/"OBJECT_DIR);

    if (argc > 0) {
        const char* command = nob_shift_args(&argc, &argv);
        if (0 == strcmp("build", command)) {
            if (argc > 0) {
                const char* what_to_build = nob_shift_args(&argc, &argv);
                if (0 == strcmp("layec", what_to_build)) {
                    build_layec_driver();
                } else {
                    fprintf(stderr, "Invalid project specified to build.\n");
                    return 1;
                }
            } else {
                build_all();
            }
        } else if (0 == strcmp("run", command)) {
            build_layec_object_files(false);
            build_layec_executable();

            Nob_Cmd cmd = {0};
            nob_cmd_append(&cmd, BUILD_DIR"/layec");
            nob_da_append_many(&cmd, argv, argc);
            nob_cmd_run_sync(cmd);
        } else if (0 == strcmp("install", command)) {
            if (argc == 0) {
                fprintf(stderr, "install command expects an install directory as its only argument.\n");
                return 1;
            }

            const char* install_prefix = nob_shift_args(&argc, &argv);
            build_layec_driver();
            nob_copy_file(BUILD_DIR"/layec", nob_temp_sprintf("%s/layec", install_prefix));
        } else if (0 == strcmp("fuzz", command)) {
            build_layec_object_files(false);
            build_parse_fuzzer();

            Nob_Cmd cmd = {0};
            nob_cmd_append(&cmd, BUILD_DIR"/parse_fuzzer");
            nob_cmd_append(&cmd, "./fuzz/corpus");
            nob_cmd_run_sync(cmd);
        } else if (0 == strcmp("test-exec", command)) {
            build_layec_driver();
            build_exec_test_runner();

            Nob_Cmd cmd = {0};
            nob_cmd_append(&cmd, BUILD_DIR"/exec_test_runner");
            nob_cmd_run_sync(cmd);
        } else if (0 == strcmp("test-fchk", command)) {
            run_fchk(false);
        } else if (0 == strcmp("test-fchk-build", command)) {
            run_fchk(true);
        } else if (0 == strcmp("test", command)) {
            build_layec_driver();
            build_exec_test_runner();

            Nob_Cmd cmd = {0};
            nob_cmd_append(&cmd, BUILD_DIR"/exec_test_runner");
            nob_cmd_run_sync(cmd);

            run_fchk(false);
        } else {
            fprintf(stderr, "Unknown nob command: %s\n", command);
            return 1;
        }
    } else {
        build_all();
    }

    return 0;
}
