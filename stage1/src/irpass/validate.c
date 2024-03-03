#include <assert.h>

#include "layec.h"

static void layec_validate_function(layec_value* function);
static void layec_validate_block(layec_value* block);

void layec_irpass_validate(layec_module* module) {
    for (int64_t i = 0, count = layec_module_function_count(module); i < count; i++) {
        layec_value* function = layec_module_get_function_at_index(module, i);
        layec_validate_function(function);
    }
}

static void layec_validate_function(layec_value* function) {
    for (int64_t i = 0, count = layec_function_block_count(function); i < count; i++) {
        layec_value* block = layec_function_get_block_at_index(function, i);
        layec_validate_block(block);
    }
}

static void layec_validate_block(layec_value* block) {
    if (!layec_block_is_terminated(block)) {
        layec_write_error(layec_value_context(block), layec_value_location(block), "Unterminated block in LayeC IR");
    }
}
