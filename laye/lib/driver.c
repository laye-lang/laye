#include "laye.h"

int laye_main(laye_args args) {
    int result = 0;
    
    fprintf(stderr, "Hello, %s!\n", args.program_name);

defer:
    return 0;
}

#define LAYE_ARGS_LOG(Message) do { if (logger) { logger(Message); } } while (0)

laye_args_parse_result laye_args_parse(laye_args* args, int argc, char** argv, laye_args_parse_logger logger) {
    if (argc == 0) return LAYE_ARGS_NO_ARGS;

    args->program_name = argv[0];
    lca_shift_args(&argc, &argv);

    return LAYE_ARGS_OK;
}

laye_args_parse_result laye_compat_args_parse(laye_args* args, int argc, char** argv, laye_args_parse_logger logger) {
    if (argc == 0) return LAYE_ARGS_NO_ARGS;

    args->program_name = argv[0];
    lca_shift_args(&argc, &argv);

    return LAYE_ARGS_OK;
}

void laye_args_parse_logger_default(char* message) {
    fprintf(stderr, "%s\n", message);
}
