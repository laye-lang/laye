#define NOB_REBUILD_URSELF(binary_path, source_path) "cc", "-o", binary_path, source_path
#define NOB_IMPLEMENTATION
#include "include/nob.h"

static void build_layec_driver() {
    Nob_Cmd driver_cmd = {};
    nob_cmd_append(&driver_cmd, "clang");
    nob_cmd_append(&driver_cmd, "-o", "./out/layec");
    nob_cmd_append(&driver_cmd, "-I", "include");
    nob_cmd_append(&driver_cmd, "-std=c23");
    nob_cmd_append(&driver_cmd, "-ggdb");
    nob_cmd_append(&driver_cmd, "-fsanitize=address");
    nob_cmd_append(&driver_cmd, "./lib/layec_shared.c");
    nob_cmd_append(&driver_cmd, "./lib/layec_context.c");
    nob_cmd_append(&driver_cmd, "./lib/layec_depgraph.c");
    nob_cmd_append(&driver_cmd, "./lib/layec_ir.c");
    nob_cmd_append(&driver_cmd, "./lib/irpass/validate.c");
    nob_cmd_append(&driver_cmd, "./lib/layec_llvm.c");
    nob_cmd_append(&driver_cmd, "./lib/laye/laye_data.c");
    nob_cmd_append(&driver_cmd, "./lib/laye/laye_debug.c");
    nob_cmd_append(&driver_cmd, "./lib/laye/laye_parser.c");
    nob_cmd_append(&driver_cmd, "./lib/laye/laye_sema.c");
    nob_cmd_append(&driver_cmd, "./lib/laye/laye_irgen.c");
    nob_cmd_append(&driver_cmd, "./src/layec.c");
    nob_cmd_run_sync(driver_cmd);
}

static void build_test_runner() {
    Nob_Cmd driver_cmd = {};
    nob_cmd_append(&driver_cmd, "clang");
    nob_cmd_append(&driver_cmd, "-o", "./out/test_runner");
    nob_cmd_append(&driver_cmd, "-I", "include");
    nob_cmd_append(&driver_cmd, "-std=c23");
    nob_cmd_append(&driver_cmd, "-ggdb");
    nob_cmd_append(&driver_cmd, "-fsanitize=address");
    nob_cmd_append(&driver_cmd, "./src/test_runner.c");
    nob_cmd_run_sync(driver_cmd);

}

int main(int argc, char** argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    nob_mkdir_if_not_exists("./out");

    build_layec_driver();
    build_test_runner();

    return 0;
}
