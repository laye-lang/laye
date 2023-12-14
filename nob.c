#define NOB_REBUILD_URSELF(binary_path, source_path) "cc", "-o", binary_path, source_path
#define NOB_IMPLEMENTATION
#include "nob.h"

int main(int argc, char** argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    nob_mkdir_if_not_exists("./out");

    Nob_Cmd driver_cmd = {};
    nob_cmd_append(&driver_cmd, "clang");
    nob_cmd_append(&driver_cmd, "-o", "./out/layec");
    nob_cmd_append(&driver_cmd, "-I", "include");
    nob_cmd_append(&driver_cmd, "-std=c23");
    nob_cmd_append(&driver_cmd, "-ggdb");
    nob_cmd_append(&driver_cmd, "-fsanitize=address");
    nob_cmd_append(&driver_cmd, "./lib/layec_shared.c");
    nob_cmd_append(&driver_cmd, "./lib/layec_context.c");
    nob_cmd_append(&driver_cmd, "./lib/laye_data.c");
    nob_cmd_append(&driver_cmd, "./lib/laye_parser.c");
    nob_cmd_append(&driver_cmd, "./src/layec.c");
    nob_cmd_run_sync(driver_cmd);

    return 0;
}
