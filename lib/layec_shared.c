#include <assert.h>

#include "layec.h"

const char* layec_status_to_cstring(layec_status status) {
    switch (status) {
        default: assert(false && "unreachable layec_status case");
        case LAYEC_NO_STATUS: return "NO_STATUS";
        case LAYEC_INFO: return "INFO";
        case LAYEC_NOTE: return "NOTE";
        case LAYEC_WARN: return "WARN";
        case LAYEC_ERROR: return "ERROR";
        case LAYEC_FATAL: return "FATAL";
        case LAYEC_ICE: return "ICE";
    }
}

const char* layec_value_category_to_cstring(layec_value_category category) {
    switch (category) {
        default: assert(false && "unreachable layec_value_category case");
        case LAYEC_LVALUE: return "LVALUE";
        case LAYEC_RVALUE: return "RVALUE";
    }
}

bool layec_evaluated_constant_equals(layec_evaluated_constant a, layec_evaluated_constant b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        default: assert(false); return false;
        case LAYEC_EVAL_NULL: return true;
        case LAYEC_EVAL_VOID: return true;
        case LAYEC_EVAL_BOOL: return a.bool_value == b.bool_value;
        case LAYEC_EVAL_INT: return a.int_value == b.int_value;
        case LAYEC_EVAL_FLOAT: return a.float_value == b.float_value;
        case LAYEC_EVAL_STRING: return string_equals(a.string_value, b.string_value);
    }
}

int layec_get_significant_bits(int64_t value) {
    int bit_width = 8 * sizeof value;
    assert(bit_width == 64);

    if (value < 0) {
        int sig = bit_width - 1;
        while (sig > 0) {
            if (!(value & (1ull << (sig - 1))))
                return sig + 1;
            sig--;
        }
    } else {
        int sig = bit_width - 1;
        while (sig > 0) {
            if (value & (1ull << (sig - 1)))
                return sig;
            sig--;
        }
    }

    return 1;
}
