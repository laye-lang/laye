#include "ccly.h"

int ccly_main(ccly_args args) {
    int result = 0;
    
    fprintf(stderr, "Hello, %s!\n", args.program_name);

defer:
    return 0;
}

#define CCLY_ARGS_LOG(Message) do { if (logger) { logger(Message); } } while (0)

ccly_args_parse_result ccly_args_parse(ccly_args* args, int argc, char** argv, ccly_args_parse_logger logger) {
    if (argc == 0) return CCLY_ARGS_NO_ARGS;

    args->program_name = argv[0];
    lca_shift_args(&argc, &argv);

    return CCLY_ARGS_OK;
}

ccly_args_parse_result ccly_compat_args_parse(ccly_args* args, int argc, char** argv, ccly_args_parse_logger logger) {
    if (argc == 0) return CCLY_ARGS_NO_ARGS;

    args->program_name = argv[0];
    lca_shift_args(&argc, &argv);

    return CCLY_ARGS_OK;
}

void ccly_args_parse_logger_default(char* message) {
    fprintf(stderr, "%s\n", message);
}
