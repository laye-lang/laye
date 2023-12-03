#include <stdio.h>

#define LCA_DA_IMPLEMENTATION
#define LCA_MEM_IMPLEMENTATION
#define LCA_STR_IMPLEMENTATION
#include "layec.h"

int main(int argc, char** argv) {
    fprintf(stderr, "layec " LAYEC_VERSION "\n");

    layec_context* context = layec_context_create(default_allocator);
    layec_context_destroy(context);

    return 0;
}
