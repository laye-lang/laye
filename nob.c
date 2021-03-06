#define NOB_IMPLEMENTATION
#include "include/nob.h"

static void cflags(Nob_Cmd* cmd) {
    nob_cmd_append(cmd, "-I", "include");
    nob_cmd_append(cmd, "-std=c17");
    nob_cmd_append(cmd, "-pedantic");
    nob_cmd_append(cmd, "-pedantic-errors");
    nob_cmd_append(cmd, "-ggdb");
    nob_cmd_append(cmd, "-fsanitize=address");
    nob_cmd_append(cmd, "-D__USE_POSIX");
    nob_cmd_append(cmd, "-D_XOPEN_SOURCE=600");
}

static void layec_sources(Nob_Cmd* cmd) {
    nob_cmd_append(cmd, "./lib/layec_shared.c");
    nob_cmd_append(cmd, "./lib/layec_context.c");
    nob_cmd_append(cmd, "./lib/layec_depgraph.c");
    nob_cmd_append(cmd, "./lib/layec_ir.c");
    nob_cmd_append(cmd, "./lib/irpass/validate.c");
    nob_cmd_append(cmd, "./lib/layec_llvm.c");
    nob_cmd_append(cmd, "./lib/laye/laye_data.c");
    nob_cmd_append(cmd, "./lib/laye/laye_debug.c");
    nob_cmd_append(cmd, "./lib/laye/laye_parser.c");
    nob_cmd_append(cmd, "./lib/laye/laye_sema.c");
    nob_cmd_append(cmd, "./lib/laye/laye_irgen.c");
}

static void build_layec_driver() {
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "clang");
    nob_cmd_append(&cmd, "-o", "./out/layec");
    cflags(&cmd);
    layec_sources(&cmd);
    nob_cmd_append(&cmd, "./src/layec.c");
    nob_cmd_run_sync(cmd);
}

static void build_test_runner() {
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "clang");
    nob_cmd_append(&cmd, "-o", "./out/test_runner");
    cflags(&cmd);
    nob_cmd_append(&cmd, "./src/test_runner.c");
    nob_cmd_run_sync(cmd);
}

static void build_parse_fuzzer() {
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "clang");
    nob_cmd_append(&cmd, "-o", "./out/parse_fuzzer");
    nob_cmd_append(&cmd, "-fsanitize=fuzzer");
    cflags(&cmd);
    layec_sources(&cmd);
    nob_cmd_append(&cmd, "./fuzz/parse_fuzzer.c");
    nob_cmd_run_sync(cmd);
}

static void build_all() {
    build_layec_driver();
    build_test_runner();
    build_parse_fuzzer();
}

int main(int argc, char** argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    const char* program = nob_shift_args(&argc, &argv);
    nob_mkdir_if_not_exists("./out");

    if (argc > 0) {
        const char* command = nob_shift_args(&argc, &argv);
        if (0 == strcmp("build", command)) {
            build_all();
        } else if (0 == strcmp("run", command)) {
            build_layec_driver();

            Nob_Cmd cmd = {0};
            nob_cmd_append(&cmd, "./out/layec");
            nob_da_append_many(&cmd, argv, argc);
            nob_cmd_run_sync(cmd);
        } else if (0 == strcmp("fuzz", command)) {
            build_parse_fuzzer();

            Nob_Cmd cmd = {0};
            nob_cmd_append(&cmd, "./out/parse_fuzzer");
            nob_cmd_append(&cmd, "./fuzz/corpus");
            nob_cmd_run_sync(cmd);
        } else if (0 == strcmp("test", command)) {
            build_layec_driver();
            build_test_runner();

            Nob_Cmd cmd = {0};
            nob_cmd_append(&cmd, "./out/test_runner");
            nob_cmd_run_sync(cmd);
        } else if (0 == strcmp("test_cache", command)) {
            build_layec_driver();
            build_test_runner();

            Nob_Cmd cmd = {0};
            nob_cmd_append(&cmd, "./out/test_runner");
            nob_cmd_append(&cmd, "create_output");
            nob_cmd_run_sync(cmd);
        } else {
            fprintf(stderr, "Unknown nob command: %s", command);
            return 1;
        }
    } else {
        build_all();
    }

    return 0;
}
