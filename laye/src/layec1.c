#define LCA_IMPLEMENTATION
#include "lca.h"

#include "laye.h"

int main(int argc, char** argv) {
    laye_args args = {0};
    laye_args_parse_result args_result = laye_args_parse(&args, argc, argv, laye_args_parse_logger_default);
    return laye_main(args);
}
