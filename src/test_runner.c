#include <assert.h>
#include <stdio.h>

#define NOB_IMPLEMENTATION
#include "nob.h"

#include "ansi.h"

#define LCA_DA_IMPLEMENTATION
#include "lcads.h"

#define LAYEC_PATH "./out/layec"

#define FCHK_DIR  "./fchk"
#define FCHK_OUT  FCHK_DIR "/out"
#define FCHK_PATH FCHK_OUT "/fchk"

typedef struct test_info {
    const char* test_name;
    const char* test_kind;
} test_info;

typedef struct test_state {
    bool create_exec_test_output;
    int test_count;
    dynarr(test_info) failed_tests;
} test_state;

static bool ensure_fchk_installed(void);
static bool cstring_ends_with(const char* s, const char* ending);
static bool run_fchk_test(const char* test_file_path);
static bool run_exec_test(const char* test_file_path, bool create_output);
static void run_tests_in_directory(test_state* state, const char* test_directory, const char* extension);

int main(int argc, char** argv) {
    if (!ensure_fchk_installed()) {
        return 1;
    }

    const char* program = nob_shift_args(&argc, &argv);

    test_state state = {0};
    state.create_exec_test_output = argc > 0 && 0 == strcmp("create_output", nob_shift_args(&argc, &argv));
    run_tests_in_directory(&state, "./test/laye", ".laye");

    int64_t num_tests_failed = arr_count(state.failed_tests);
    if (num_tests_failed == 0) {
        fprintf(
            stderr,
            "\n%s100%% of tests passed%s out of %d.\n",
            ANSI_COLOR_GREEN,
            ANSI_COLOR_RESET,
            state.test_count
        );
    } else {
        int percent_success = (state.test_count - num_tests_failed) * 100 / state.test_count;
        fprintf(
            stderr,
            "\n%d%% of tests passed, %s%d tests failed%s out of %d.\n",
            percent_success,
            ANSI_COLOR_RED,
            (int)num_tests_failed,
            ANSI_COLOR_RESET,
            state.test_count
        );

        fprintf(stderr, "\nThe following tests FAILED:\n");
        for (int64_t i = 0; i < num_tests_failed; i++) {
            fprintf(stderr, "\t%s%s (%s)%s\n", ANSI_COLOR_RED, state.failed_tests[i].test_name, state.failed_tests[i].test_kind, ANSI_COLOR_RESET);
        }
    }

    arr_free(state.failed_tests);
    nob_temp_reset();

    return 0;
}

static void run_tests_in_directory(test_state* state, const char* test_directory, const char* extension) {
    Nob_File_Paths test_file_paths = {0};
    if (!nob_read_entire_dir(test_directory, &test_file_paths)) {
        nob_log(NOB_ERROR, "Failed to enumerate test directory");
        return;
    }

    for (size_t i = 0; i < test_file_paths.count; i++) {
        const char* test_file_path = test_file_paths.items[i];
        if (!cstring_ends_with(test_file_path, extension)) {
            continue;
        }

        const char* test_full_name = nob_temp_sprintf("%s/%s", test_directory, test_file_path);

        if (state->create_exec_test_output) {
            bool exec_test_success = run_exec_test(test_full_name, true);
            state->test_count++;
            if (!exec_test_success) {
                arr_push(state->failed_tests, ((test_info){ .test_name = test_full_name, .test_kind = "Execution" }));
            }
        } else {
            bool fchk_test_success = run_fchk_test(test_full_name);
            state->test_count++;
            if (!fchk_test_success) {
                arr_push(state->failed_tests, ((test_info){ .test_name = test_full_name, .test_kind = "FCHK" }));
            }

            bool exec_test_success = run_exec_test(test_full_name, false);
            state->test_count++;
            if (!exec_test_success) {
                arr_push(state->failed_tests, ((test_info){ .test_name = test_full_name, .test_kind = "Execution" }));
            }
        }
    }

    nob_da_free(test_file_paths);
}

static bool cstring_ends_with(const char* s, const char* ending) {
    size_t sLength = strlen(s);
    size_t endingLength = strlen(ending);

    if (sLength < endingLength) {
        return false;
    }

    return 0 == strncmp(s + sLength - endingLength, ending, endingLength);
}

static bool run_fchk_test(const char* test_file_path) {
    nob_log(NOB_INFO, "-- Running FCHK test for \"%s\"", test_file_path);

    Nob_Cmd cmd = {0};
    nob_cmd_append(
        &cmd,
        FCHK_PATH,
        test_file_path,
        "-l",
        ".",
        "-D",
        "layec=" LAYEC_PATH,
        "--prefix",
        "//",
        "-P",
        "re",
        "-P",
        "nocap"
    );

    bool success = nob_cmd_run_sync(cmd);
    nob_cmd_free(cmd);

    return success;
}


static bool run_exec_test(const char* test_file_path, bool create_output) {
    nob_log(NOB_INFO, "-- Running execution test for \"%s\"", test_file_path);

    const char* output_file = nob_temp_sprintf("%s.output", test_file_path);
#ifdef _WIN32
    const char* exec_file = nob_temp_sprintf("%s.out.exe", test_file_path);
#else
    const char* exec_file = nob_temp_sprintf("%s.out", test_file_path);
#endif

    Nob_Cmd cmd = {0};
    nob_cmd_append(
        &cmd,
        LAYEC_PATH,
        "-o",
        exec_file,
        test_file_path
    );

    bool compile_success = nob_cmd_run_sync(cmd);
    if (!compile_success) {
        nob_cmd_free(cmd);
        return false;
    }

    cmd.count = 0;
    nob_cmd_append(&cmd, exec_file);

    Nob_Proc_Result exec_result = nob_cmd_run_sync_result(cmd);
    nob_cmd_free(cmd);
    remove(exec_file);

    if (create_output) {
        bool success = true;

        FILE* f = fopen(output_file, "w");
        if (f == NULL) {
            nob_log(NOB_ERROR, "Failed to open test output file %s for writing", output_file);
            success = false;
            goto create_output_exit;
        }

        fprintf(f, "%d\n", exec_result.exit_code);

    create_output_exit:;
        if (f) fclose(f);
        if (!success) remove(output_file);
        return success;
    } else {
        Nob_String_Builder sb = {0};
        if (!nob_read_entire_file(output_file, &sb)) {
            nob_log(NOB_INFO, "Run 'test_runner create_output' to create the executable test output files.");
            nob_sb_free(sb);
            return false;
        }

        nob_sb_append_null(&sb);

        char* contents = sb.items;
        int exit_code = (int)strtol(contents, &contents, 10);

        nob_sb_free(sb);
        return exit_code == exec_result.exit_code;
    }
}

static bool ensure_fchk_installed(void) {
    if (nob_file_exists(FCHK_PATH)) {
        return true;
    }

    Nob_Cmd cmd = {0};
    nob_cmd_append(
        &cmd,
        "git",
        "clone",
        "--depth=1",
        "https://github.com/Sirraide/fchk",
        FCHK_DIR
    );
    if (!nob_cmd_run_sync(cmd)) {
        nob_log(NOB_ERROR, "Failed to download `Sirraide/fchk`.");
        nob_cmd_free(cmd);
        return false;
    }

    cmd.count = 0;
    nob_cmd_append(&cmd, "cmake", "-B", FCHK_OUT, FCHK_DIR);
    if (!nob_cmd_run_sync(cmd)) {
        nob_log(NOB_ERROR, "Failed to configure fchk.");
        nob_cmd_free(cmd);
        return false;
    }

    cmd.count = 0;
    nob_cmd_append(&cmd, "cmake", "--build", FCHK_OUT);
    if (!nob_cmd_run_sync(cmd)) {
        nob_log(NOB_ERROR, "Failed to build fchk.");
        nob_cmd_free(cmd);
        return false;
    }

    nob_cmd_free(cmd);

    if (!nob_file_exists(FCHK_PATH)) {
        nob_log(NOB_ERROR, "fchk not build correctly.");
        return false;
    }

    return true;
}
