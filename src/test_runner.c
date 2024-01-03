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

typedef struct test_state {
    int test_count;
    dynarr(const char*) failed_tests;
} test_state;

static bool ensure_fchk_installed(void);
static bool cstring_ends_with(const char* s, const char* ending);
static bool run_test(const char* test_file_path);
static void run_tests_in_directory(test_state* state, const char* test_directory, const char* extension);

int main(int argc, char** argv) {
    if (!ensure_fchk_installed()) {
        return 1;
    }

    test_state state = {0};
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
            fprintf(stderr, "\t%s%s (Failed)%s\n", ANSI_COLOR_RED, state.failed_tests[i], ANSI_COLOR_RESET);
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
        state->test_count++;

        bool test_success = run_test(test_full_name);
        if (!test_success) {
            arr_push(state->failed_tests, test_full_name);
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

static bool run_test(const char* test_file_path) {
    nob_log(NOB_INFO, "-- Running test for \"%s\"", test_file_path);

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
