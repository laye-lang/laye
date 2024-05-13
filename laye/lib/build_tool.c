#include "laye.h"

int laye_build_tool_main(laye_build_tool_args args) {
    int result = 0;
    
    fprintf(stderr, "Hello, %s!\n", args.program_name);

defer:
    return 0;
}

laye_args_parse_result laye_build_tool_args_parse(laye_build_tool_args* args, int argc, char** argv, laye_args_parse_logger logger) {
    if (argc == 0) return LAYE_ARGS_NO_ARGS;

    args->program_name = argv[0];
    lca_shift_args(&argc, &argv);

    return LAYE_ARGS_OK;
}
