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

#include "lyir.h"

#include <assert.h>

typedef enum layec_cabi_class {
    LAYEC_CABI_NO_CLASS,
    LAYEC_CABI_INTEGER,
    LAYEC_CABI_SSE,
    LAYEC_CABI_SSEUP,
    LAYEC_CABI_X87,
    LAYEC_CABI_X87UP,
    LAYEC_CABI_COMPLEX_X87,
    LAYEC_CABI_MEMORY,
} layec_cabi_class;

static void layec_abi_transform_function_declaration(lyir_module* module, lyir_value* function);
static void layec_abi_transform_callsites(lyir_module* module, lyir_value* function, lyir_builder* builder);

void lyir_irpass_fix_abi(lyir_module* module) {
    assert(module != NULL);
    lyir_context* context = lyir_module_context(module);
    assert(context != NULL);

    lyir_builder* builder = lyir_builder_create(context);

    for (int64_t i = 0, count = lyir_module_function_count(module); i < count; i++) {
        layec_abi_transform_function_declaration(module, lyir_module_get_function_at_index(module, i));
        layec_abi_transform_callsites(module, lyir_module_get_function_at_index(module, i), builder);
    }

    lyir_builder_destroy(builder);

    lyir_irpass_validate(module);
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

static void post_merge(int64_t bit_width, layec_cabi_class* lo, layec_cabi_class* hi) {
    assert(bit_width >= 0);
    assert(lo != NULL);
    assert(hi != NULL);

    if (*hi == LAYEC_CABI_MEMORY) {
        *lo = LAYEC_CABI_MEMORY;
    }

    if (*hi == LAYEC_CABI_X87UP && *lo != LAYEC_CABI_X87) {
        *lo = LAYEC_CABI_MEMORY;
    }

    if (bit_width > 128 && (*lo != LAYEC_CABI_SSE && *hi != LAYEC_CABI_SSEUP)) {
        *lo = LAYEC_CABI_MEMORY;
    }

    if (*hi == LAYEC_CABI_SSEUP && *lo != LAYEC_CABI_SSE) {
        *lo = LAYEC_CABI_SSE;
    }
}

static void classify(
    lyir_type* param_type,
    int64_t offset_base,
    layec_cabi_class* lo,
    layec_cabi_class* hi
) {
    int64_t bit_width = lyir_type_size_in_bits(param_type);
    assert(bit_width >= 0);

    assert(lo != NULL);
    *lo = LAYEC_CABI_NO_CLASS;

    assert(hi != NULL);
    *hi = LAYEC_CABI_NO_CLASS;

    layec_cabi_class* current = offset_base < 64 ? lo : hi;
    *current = LAYEC_CABI_MEMORY;

    if (lyir_type_is_void(param_type)) {
        *current = LAYEC_CABI_NO_CLASS;
        return;
    }

    if (lyir_type_is_integer(param_type)) {
        if (bit_width <= 64) {
            *current = LAYEC_CABI_INTEGER;
        } else if (bit_width <= 128) {
            *lo = LAYEC_CABI_INTEGER;
            *hi = LAYEC_CABI_INTEGER;
        } // else pass in memory

        return;
    }

    if (lyir_type_is_float(param_type)) {
        if (bit_width == 16 || bit_width == 32 || bit_width == 64) {
            *current = LAYEC_CABI_SSE;
        } else if (bit_width == 128) {
            *lo = LAYEC_CABI_SSE;
            *hi = LAYEC_CABI_SSEUP;
        } else {
            assert(bit_width == 80);
            *lo = LAYEC_CABI_X87;
            *hi = LAYEC_CABI_X87UP;
        }

        return;
    }

    // TODO(local): complex numbers don't exist at the IR level, so for now we just ignore them entirely.
    // If we ever do decide to support complex numbers the way C does, at the IR level, which we'd need to
    // do for ABI compatibility, then that needs to happen. It's very low on our list of priorities, though.
    // NOTE(local): might just need special C front-end handling for that.

    if (lyir_type_is_ptr(param_type)) {
        *current = LAYEC_CABI_INTEGER;
        return;
    }

    if (lyir_type_is_struct(param_type)) {
        // We need to break up structs into "eightbytes".
        if (bit_width >= 8 * 64) {
            // a struct that is more than eight eightbytes just goes in MEMORY
            return;
        }

        *current = LAYEC_CABI_NO_CLASS;

        int64_t field_count = lyir_type_struct_member_count_get(param_type);
        for (int64_t field_index = 0, current_offset = offset_base; field_index < field_count; field_index++) {
            lyir_struct_member field = lyir_type_struct_member_get_at_index(param_type, field_index);
            lyir_type* field_type = field.type;

            int64_t field_offset = current_offset;
            current_offset += lyir_type_size_in_bits(field_type);

            // TODO(local): any place here where bitfields can go? not sure, might need special C handling for stuff like that.
            if (0 != (field_offset % lyir_type_align_in_bits(field_type))) {
                *lo = LAYEC_CABI_MEMORY;
                post_merge(bit_width, lo, hi);
                return;
            }

            layec_cabi_class field_lo, field_hi;
            classify(field_type, field_offset, &field_lo, &field_hi);

            *lo = merge(*lo, field_lo);
            *hi = merge(*hi, field_hi);

            if (*lo == LAYEC_CABI_MEMORY || *hi == LAYEC_CABI_MEMORY)
                break;
        }

        post_merge(bit_width, lo, hi);
    }
}

static void layec_abi_transform_function_declaration(lyir_module* module, lyir_value* function) {
    assert(module != NULL);
    assert(function != NULL);
    lyir_context* context = lyir_module_context(module);
    assert(context != NULL);

    int64_t int_registers_used = 0;
    int64_t float_registers_used = 0;

    for (int64_t param_index = 0, param_count = lyir_value_function_parameter_count_get(function); param_index < param_count; param_index++) {
        lyir_value* param = lyir_value_function_parameter_get_at_index(function, param_index);
        assert(param != NULL);

        lyir_type* param_type = lyir_value_type_get(param);
        assert(param_type != NULL);

        int64_t param_type_bit_width = lyir_type_size_in_bits(param_type);
        assert(param_type_bit_width >= 0);

        layec_cabi_class param_lo, param_hi;
        classify(param_type, 0, &param_lo, &param_hi);

        // TODO(local): use the registers

        if (lyir_type_is_struct(param_type)) {
            if (param_lo == LAYEC_CABI_INTEGER && param_hi == LAYEC_CABI_NO_CLASS) {
                assert(param_type_bit_width <= 64);
                lyir_type* new_type = lyir_int_type(context, param_type_bit_width);
                lyir_value_function_parameter_type_set_at_index(function, param_index, new_type);
            } else if (param_lo == LAYEC_CABI_SSE && param_hi == LAYEC_CABI_NO_CLASS) {
                assert(param_type_bit_width == 16 || param_type_bit_width == 32 || param_type_bit_width == 64);
                lyir_type* new_type = lyir_float_type(context, param_type_bit_width);
                lyir_value_function_parameter_type_set_at_index(function, param_index, new_type);
            }
        }
    }
}

static void layec_abi_transform_call(lyir_module* module, lyir_value* function, lyir_builder* builder, lyir_value* call) {
    assert(module != NULL);
    assert(function != NULL);
    lyir_context* context = lyir_module_context(module);
    assert(context != NULL);
    assert(builder != NULL);
    assert(call != NULL);
    assert(lyir_value_kind_get(call) == LYIR_IR_CALL);

    lyir_builder_position_before(builder, call);

    for (int64_t i = 0; i < lyir_value_call_argument_count_get(call); i++) {
        lyir_value* arg = lyir_value_call_argument_get_at_index(call, i);
        assert(arg != NULL);

        lyir_type* arg_type = lyir_value_type_get(arg);
        assert(arg_type != NULL);

        int64_t arg_type_bit_width = lyir_type_size_in_bits(arg_type);
        assert(arg_type_bit_width >= 0);

        layec_cabi_class arg_lo, arg_hi;
        classify(arg_type, 0, &arg_lo, &arg_hi);

        // TODO(local): use the registers

        if (lyir_type_is_struct(arg_type)) {
            if (arg_lo == LAYEC_CABI_INTEGER && arg_hi == LAYEC_CABI_NO_CLASS) {
                assert(arg_type_bit_width <= 64);
                lyir_type* new_type = lyir_int_type(context, arg_type_bit_width);

                if (lyir_value_kind_get(arg) == LYIR_IR_LOAD) {
                    lyir_value_type_set(arg, new_type);
                } else {
                    fprintf(stderr, "for value kind %s\n", lyir_value_kind_to_cstring(lyir_value_kind_get(arg)));
                    assert(false && "how do do this yes plz");
                }
            } else if (arg_lo == LAYEC_CABI_SSE && arg_hi == LAYEC_CABI_NO_CLASS) {
                assert(arg_type_bit_width == 16 || arg_type_bit_width == 32 || arg_type_bit_width == 64);
                lyir_type* new_type = lyir_float_type(context, arg_type_bit_width);

                if (lyir_value_kind_get(arg) == LYIR_IR_LOAD) {
                    lyir_value_type_set(arg, new_type);
                } else {
                    fprintf(stderr, "for value kind %s\n", lyir_value_kind_to_cstring(lyir_value_kind_get(arg)));
                    assert(false && "how do do this yes plz");
                }
            }
        }
    }
}

static void layec_abi_transform_callsites(lyir_module* module, lyir_value* function, lyir_builder* builder) {
    assert(module != NULL);
    assert(function != NULL);
    lyir_context* context = lyir_module_context(module);
    assert(context != NULL);
    assert(builder != NULL);

    int64_t block_count = lyir_value_function_block_count_get(function);
    for (int64_t block_index = 0; block_index < block_count; block_index++) {
        lyir_value* block = lyir_value_function_block_get_at_index(function, block_index);
        assert(block != NULL);
        assert(lyir_value_is_block(block));

        for (int64_t i = 0; i < lyir_value_block_instruction_count_get(block); i++) {
            lyir_value* inst = lyir_value_block_instruction_get_at_index(block, i);
            assert(inst != NULL);

            switch (lyir_value_kind_get(inst)) {
                default: break;

                case LYIR_IR_CALL: {
                    layec_abi_transform_call(module, function, builder, inst);
                } break;
            }
        }
    }
}
