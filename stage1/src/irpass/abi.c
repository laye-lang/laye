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

#define INT_REG_COUNT   (6)
#define FLOAT_REG_COUNT (8)

#include "layec.h"

#include <assert.h>

typedef enum layec_cabi_class {
    LAYEC_CABI_NO_CLASS,
    LAYEC_CABI_INTEGER,
    LAYEC_CABI_SSE,
    LAYEC_CABI_SSEUP,
    // NOTE: no `long double` or `complex long double` for now, ew
    LAYEC_CABI_MEMORY,
} layec_cabi_class;

static void layec_abi_transform(layec_module* module, layec_value* function);

void layec_irpass_fix_abi(layec_module* module) {
    assert(module != NULL);

    for (int64_t i = 0, count = layec_module_function_count(module); i < count; i++) {
        layec_abi_transform(module, layec_module_get_function_at_index(module, i));
    }

    layec_irpass_validate(module);
}

static layec_cabi_class merge(layec_cabi_class accum, layec_cabi_class field) {
    if (accum == field || field == LAYEC_CABI_NO_CLASS) {
        return accum;
    } else if (field == LAYEC_CABI_MEMORY) {
        return LAYEC_CABI_MEMORY;
    } else if (accum == LAYEC_CABI_NO_CLASS) {
        return field;
    } else if (accum == LAYEC_CABI_INTEGER || field == LAYEC_CABI_INTEGER) {
        return LAYEC_CABI_INTEGER;
    } else { // NOTE(local): not handling X87 et al
        return LAYEC_CABI_SSE;
    }
}

static void classify(
    layec_type* param_type,
    int64_t offset_base,
    layec_cabi_class* lo,
    layec_cabi_class* hi
) {
    int64_t bit_width = layec_type_size_in_bits(param_type);
    assert(bit_width >= 0);

    assert(lo != NULL);
    *lo = LAYEC_CABI_NO_CLASS;

    assert(hi != NULL);
    *hi = LAYEC_CABI_NO_CLASS;

    layec_cabi_class* current = offset_base < 64 ? lo : hi;
    *current = LAYEC_CABI_MEMORY;

    if (layec_type_is_void(param_type)) {
        *current = LAYEC_CABI_NO_CLASS;
        return;
    }

    if (layec_type_is_integer(param_type)) {
        assert(bit_width <= 128 && "> i128 is not explicitly supported just yet");

        if (bit_width <= 64) {
            *current = LAYEC_CABI_INTEGER;
        } else if (bit_width <= 128) {
            *lo = LAYEC_CABI_INTEGER;
            *hi = LAYEC_CABI_INTEGER;
        } else {
            assert(false && "unreachable due to previous assert");
        }

        return;
    }

    if (layec_type_is_float(param_type)) {
        assert(bit_width != 80 && "f80 is not explicitly supported just yet (if ever?)");

        if (bit_width == 16 || bit_width == 32 || bit_width == 64) {
            *current = LAYEC_CABI_SSE;
        } else if (bit_width == 128) {
            *lo = LAYEC_CABI_SSE;
            *hi = LAYEC_CABI_SSEUP;
        } else {
            assert(false && "unreachable due to previous assert");
        }

        return;
    }
    
    if (layec_type_is_struct(param_type)) {
        // We need to break up structs into "eightbytes".
        if (bit_width >= 64 * 64) {
            // a struct that is more than eight eightbytes just goes in MEMORY
            assert(false && "todo");
        }

        int64_t field_count = layec_type_struct_member_count(param_type);

        // TODO(local): as soon as you can introduce unaligned fields in Laye structs,
        // this needs to be able to handle them.
        // Unaligned data makes the whole thing MEMORY by default.

        int64_t current_bit_count = 0;
        layec_cabi_class current_class = LAYEC_CABI_NO_CLASS;

        for (int64_t field_index = 0; field_index < field_count; field_index++) {
            layec_type* field_type = layec_type_struct_get_member_at_index(param_type, field_index);
            assert(field_type != NULL);

            int64_t field_type_bit_width = layec_type_size_in_bits(field_type);

            if (current_bit_count < 64 && current_bit_count + field_type_bit_width > 64) {
                assert(false && "I think this means that there's unaligned memory");
            }

            current_bit_count += field_type_bit_width;

            layec_cabi_class lo = LAYEC_CABI_NO_CLASS, hi = LAYEC_CABI_NO_CLASS;
            classify(field_type, 0, &lo, &hi);
            // TODO(local): merge correctly

            // NOTE: if any field is in memory, the whole struct might just go in memory?
            // probably in the most-merger part.
        }

        if (current_bit_count <= 64) {
            // single eightbyte, return the current class
            return current_class;
        }

        assert(false && "todo");
    }
}

static void layec_abi_transform(layec_module* module, layec_value* function) {
    assert(module != NULL);
    assert(function != NULL);

    int64_t int_registers_used = 0;
    int64_t float_registers_used = 0;

    for (int64_t param_index = 0, param_count = layec_function_parameter_count(function); param_index < param_count; param_index++) {
        layec_value* param = layec_function_get_parameter_at_index(function, param_index);
        assert(param != NULL);

        layec_type* param_type = layec_value_get_type(param);
        assert(param_type != NULL);
    }
}
