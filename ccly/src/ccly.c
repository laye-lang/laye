#define LCA_IMPLEMENTATION
#include "lca.h"

#include "ccly.h"

int main(int argc, char** argv) {
    ccly_args args = {0};
    ccly_args_parse_result args_result = ccly_compat_args_parse(&args, argc, argv, ccly_args_parse_logger_default);
    return ccly_main(args);
}
