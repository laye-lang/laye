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
