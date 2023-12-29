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
    int num_tests_failed;
} test_state;

static bool ensure_fchk_installed(void);
static bool cstring_ends_with(const char* s, const char* ending);
static bool run_test(const char* test_file_path);

int main(int argc, char** argv) {
    if (!ensure_fchk_installed()) {
        return 1;
    }

    Nob_File_Paths test_file_paths = {};
    if (!nob_read_entire_dir("./test", &test_file_paths)) {
        nob_log(NOB_ERROR, "Failed to enumerate test directory");
        return 1;
    }

    test_state state = {};

    dynarr(const char*) failed_tests = NULL;
    for (size_t i = 0; i < test_file_paths.count; i++) {
        const char* test_file_path = test_file_paths.items[i];
        if (!cstring_ends_with(test_file_path, ".laye")) {
            continue;
        }

        const char* test_full_name = nob_temp_sprintf("./test/%s", test_file_path);
        state.test_count++;

        bool test_success = run_test(test_full_name);
        if (!test_success) {
            arr_push(failed_tests, test_full_name);
            state.num_tests_failed++;
        }
    }

    if (state.num_tests_failed == 0) {
        fprintf(
            stderr,
            "\n%s100%% of tests passed%s out of %d.\n",
            ANSI_COLOR_GREEN,
            ANSI_COLOR_RESET,
            state.test_count
        );
    } else {
        int percent_success = (state.test_count - state.num_tests_failed) * 100 / state.test_count;
        fprintf(
            stderr,
            "\n%d%% of tests passed, %s%d tests failed%s out of %d.\n",
            percent_success,
            ANSI_COLOR_RED,
            state.num_tests_failed,
            ANSI_COLOR_RESET,
            state.test_count
        );

        fprintf(stderr, "\nThe following tests FAILED:\n");
        for (int64_t i = 0, count = arr_count(failed_tests); i < count; i++) {
            fprintf(stderr, "\t%s%s (Failed)%s\n", ANSI_COLOR_RED, failed_tests[i], ANSI_COLOR_RESET);
        }
    }

    arr_free(failed_tests);
    nob_da_free(test_file_paths);
    nob_temp_reset();

    return 0;
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

    Nob_Cmd cmd = {};
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

    Nob_Cmd cmd = {};
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
