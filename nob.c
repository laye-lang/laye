#define NOB_REBUILD_URSELF(binary_path, source_path) "cc", "-o", binary_path, source_path
#define NOB_IMPLEMENTATION
#include "nob.h"

int main(int argc, char** argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    nob_mkdir_if_not_exists("./out");

    Nob_Cmd driver_cmd = {};
    nob_cmd_append(&driver_cmd, "clang", "-o", "./out/layec", "-I", "include", "-std=c23", "-ggdb", "./src/layec_shared.c", "./src/layec_context.c", "./src/laye_data.c", "./src/laye_parser.c", "./src/driver.c");
    nob_cmd_run_sync(driver_cmd);

    return 0;
}
