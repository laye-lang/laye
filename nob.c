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

#include <libgen.h>

#define BUILD_DIR "./out"
#define OBJECT_DIR "o"

typedef struct args {
    const char* build_project;
    bool no_asan;
} args_t;

static args_t args;

static const char *stage1_laye_headers_dir = "./stage1/include/";

static const char *stage1_laye_sources[] = {
    "./stage1/src/layec_shared.c",
    "./stage1/src/layec_context.c",
    "./stage1/src/layec_depgraph.c",
    "./stage1/src/layec_ir.c",
    "./stage1/src/irpass/validate.c",
    "./stage1/src/layec_llvm.c",
    "./stage1/src/laye/laye_data.c",
    "./stage1/src/laye/laye_debug.c",
    "./stage1/src/laye/laye_parser.c",
    "./stage1/src/laye/laye_sema.c",
    "./stage1/src/laye/laye_irgen.c",
    // Main file always has to be the last
    "./stage1/src/layec.c"
};

static const char* stage2_laye_sources[] = {
    "./src/main.laye",
};

static int64_t stage1_laye_sources_count = sizeof(stage1_laye_sources) / sizeof(stage1_laye_sources[0]);
static int64_t stage1_laye_sources_count_without_main = (sizeof(stage1_laye_sources) / sizeof(stage1_laye_sources[0])) - 1;
static int64_t stage2_laye_sources_count = sizeof(stage2_laye_sources) / sizeof(stage2_laye_sources[0]);

static void cflags(Nob_Cmd* cmd) {
    nob_cmd_append(cmd, "-I", stage1_laye_headers_dir);
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

static void layeflags(Nob_Cmd* cmd) {
    nob_cmd_append(cmd, "-I", "./lib/liblaye");
}

static char *to_object_file_path(const char *filepath) {
    char *filename = basename((char *) filepath);
    char *objectfile_path = nob_temp_sprintf(BUILD_DIR"/"OBJECT_DIR"/%s.o", filename);

    return objectfile_path;
}

static void build_stage1_laye_object_files(bool complete_rebuild) {
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
        char *output_path = to_object_file_path(stage1_laye_sources[0]);

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
        char *output_path = to_object_file_path(stage1_laye_sources[i]);

        if (!complete_rebuild) {
            int64_t rebuild_is_needed = nob_needs_rebuild1(output_path, stage1_laye_sources[i]);

            if (rebuild_is_needed < 0) {
                char *msg = nob_temp_sprintf("Couldn't detected if file changed: %s", stage1_laye_sources[i]);
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
}

static void build_stage1_laye_executable() {
    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd, CC);
    cflags(&cmd);
    nob_cmd_append(&cmd, "-o", BUILD_DIR"/laye1");

    for (int i = 0; i < stage1_laye_sources_count; i++) {
        char *output_path = to_object_file_path(stage1_laye_sources[i]);
        nob_cmd_append(&cmd, output_path);
    }
    
    nob_cmd_run_sync(cmd);
}

static void build_stage2_laye_executable() {
    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd, "./out/laye1");
    layeflags(&cmd);
    nob_cmd_append(&cmd, "-o", BUILD_DIR"/laye");

    for (int i = 0; i < stage2_laye_sources_count; i++) {
        nob_cmd_append(&cmd, stage2_laye_sources[i]);
    }
    
    nob_cmd_run_sync(cmd);
}

static void build_exec_test_runner() {
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, CC);
    nob_cmd_append(&cmd, "-o", BUILD_DIR"/exec_test_runner");
    cflags(&cmd);
    nob_cmd_append(&cmd, "./stage1/src/exec_test_runner.c");
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

    for (int i = 0; i < stage1_laye_sources_count_without_main; i++) {
        nob_cmd_append(&cmd_link, stage1_laye_sources[i]);
    }

    nob_cmd_append(&cmd_link, object_path);

    nob_cmd_run_sync(cmd_link);
}

static void build_stage1_laye_driver() {
    build_stage1_laye_object_files(false);
    build_stage1_laye_executable();
}

static void build_stage2_laye_driver() {
    build_stage1_laye_driver();
    build_stage2_laye_executable();
}

static void build_all() {
    build_stage2_laye_driver();
    build_exec_test_runner();
    build_parse_fuzzer();
}

static void install() {
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
                if (0 == strcmp("laye1", what_to_build)) {
                    build_stage1_laye_driver();
                } else if (0 == strcmp("laye", what_to_build)) {
                    build_stage2_laye_driver();
                } else {
                    fprintf(stderr, "Invalid project specified to build.\n");
                    return 1;
                }
            } else {
                build_all();
            }
        } else if (0 == strcmp("run", command)) {
            build_stage1_laye_object_files(false);
            build_stage1_laye_executable();

            Nob_Cmd cmd = {0};
            nob_cmd_append(&cmd, BUILD_DIR"/laye1");
            nob_da_append_many(&cmd, argv, argc);
            nob_cmd_run_sync(cmd);
        } else if (0 == strcmp("install", command)) {
            fprintf(stderr, "todo\n");
            return 1;

            if (argc == 0) {
                fprintf(stderr, "install command expects an install directory as its only argument.\n");
                return 1;
            }

            const char* bin_prefix = nob_shift_args(&argc, &argv);
            const char* lib_prefix = nob_shift_args(&argc, &argv);

            if (!nob_mkdir_if_not_exists(bin_prefix)) return 1;

            if (!nob_mkdir_if_not_exists(lib_prefix)) return 1;
            if (!nob_mkdir_if_not_exists(nob_temp_sprintf("%s/lib", lib_prefix))) return 1;

            build_stage1_laye_driver();

            nob_copy_file(BUILD_DIR"/laye1", nob_temp_sprintf("%s/laye1", bin_prefix));
            nob_copy_directory_recursively("./liblaye", nob_temp_sprintf("%s/lib/liblaye", lib_prefix));
        } else if (0 == strcmp("fuzz", command)) {
            build_stage1_laye_object_files(false);
            build_parse_fuzzer();

            Nob_Cmd cmd = {0};
            nob_cmd_append(&cmd, BUILD_DIR"/parse_fuzzer");
            nob_cmd_append(&cmd, "./fuzz/corpus");
            nob_cmd_run_sync(cmd);
        } else if (0 == strcmp("test-exec", command)) {
            build_stage1_laye_driver();
            build_exec_test_runner();

            Nob_Cmd cmd = {0};
            nob_cmd_append(&cmd, BUILD_DIR"/exec_test_runner");
            nob_cmd_run_sync(cmd);
        } else if (0 == strcmp("test-fchk", command)) {
            run_fchk(false);
        } else if (0 == strcmp("test-fchk-build", command)) {
            run_fchk(true);
        } else if (0 == strcmp("test", command)) {
            build_stage1_laye_driver();
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
