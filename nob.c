#define NOB_REBUILD_URSELF(binary_path, source_path) "cc", "-o", binary_path, source_path
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

static void build_raw_fuzzer() {
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "clang");
    nob_cmd_append(&cmd, "-o", "./out/raw_fuzzer");
    nob_cmd_append(&cmd, "-fsanitize=fuzzer");
    cflags(&cmd);
    layec_sources(&cmd);
    nob_cmd_append(&cmd, "./fuzz/raw_fuzzer.c");
    nob_cmd_run_sync(cmd);
}

int main(int argc, char** argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    nob_mkdir_if_not_exists("./out");

    build_layec_driver();
    build_test_runner();
    build_raw_fuzzer();

    return 0;
}
