#include <assert.h>

#include "layec.h"

const char* layec_value_category_to_cstring(layec_value_category category) {
    switch (category) {
        default: assert(false && "unreachable layec_value_category case");
        case LAYEC_LVALUE: return "LVALUE";
        case LAYEC_RVALUE: return "RVALUE";
    }
}
