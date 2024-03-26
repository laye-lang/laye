#include <assert.h>
#include <stdio.h>

#define LCA_DA_IMPLEMENTATION
#define LCA_MEM_IMPLEMENTATION
#define LCA_STR_IMPLEMENTATION
#define LCA_PLAT_IMPLEMENTATION
#include "laye.h"
#include "layec.h"

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    lca_temp_allocator_init(default_allocator, 1024 * 1024);
    lyir_init_targets(default_allocator);
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
    int exit_code = 0;

    lyir_context* context = lyir_context_create(default_allocator);
    context->use_color = false;

    string_view name = SV_CONSTANT("<fuzz-input>");
    string_view data = {
        .data = (char*)Data,
        .count = (int64_t)Size,
    };

    lyir_sourceid sourceid = lyir_context_get_or_add_source_from_string(
        context,
        string_view_to_string(default_allocator, name),
        string_view_to_string(default_allocator, data)
    );

    laye_module* module = laye_parse(context, sourceid);
    assert(module != NULL);

    if (context->has_reported_errors) {
        exit_code = 1;
        goto end_fuzz;
    }

    laye_analyse(context);

    if (context->has_reported_errors) {
        exit_code = 1;
        goto end_fuzz;
    }

end_fuzz:;
    lyir_context_destroy(context);
    lca_temp_allocator_clear();

    return exit_code;
}
