/*
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2023 Local Atticus
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

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
        case LAYEC_EVAL_STRING: return string_view_equals(a.string_value, b.string_value);
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
