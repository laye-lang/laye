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
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

// TODO(local): remove this, very soonly
//#include "laye.h"

#include "lyir.h"

void layec_type_destroy(lyir_type* type);
void layec_value_destroy(lyir_value* value);

lyir_target_info* lyir_default_target;
lyir_target_info* lyir_x86_64_linux;
lyir_target_info* lyir_x86_64_windows;

void lyir_init_targets(lca_allocator allocator) {
    lyir_x86_64_linux = lca_allocate(allocator, sizeof(lyir_target_info));
    lyir_x86_64_windows = lca_allocate(allocator, sizeof(lyir_target_info));

    assert(lyir_x86_64_linux != NULL);
    *lyir_x86_64_linux = (lyir_target_info) {
        .ffi = {
            .size_of_bool = 8,
            .size_of_char = 8,
            .size_of_short = 16,
            .size_of_int = 32,
            .size_of_long = 64,
            .size_of_long_long = 64,
            .size_of_float = 32,
            .size_of_double = 64,

            .align_of_bool = 8,
            .align_of_char = 8,
            .align_of_short = 16,
            .align_of_int = 32,
            .align_of_long = 64,
            .align_of_long_long = 64,
            .align_of_float = 32,
            .align_of_double = 64,

            .char_is_signed = true,
        },

        .size_of_pointer = 64,
        .align_of_pointer = 64,
    };

    assert(lyir_x86_64_windows != NULL);
    memcpy(lyir_x86_64_windows, lyir_x86_64_linux, sizeof(lyir_target_info));
    lyir_x86_64_windows->ffi.size_of_long = 32;
    lyir_x86_64_windows->ffi.align_of_long = 32;

    lyir_default_target = lyir_x86_64_linux;

    assert(lyir_default_target != NULL);
}

lyir_context* lyir_context_create(lca_allocator allocator) {
    lyir_context* context = lca_allocate(allocator, sizeof *context);
    assert(context != NULL);

    context->allocator = allocator;

    context->target = lyir_default_target;
    assert(context->target != NULL);

    context->max_interned_string_size = 1024 * 1024;

    context->string_arena = lca_arena_create(allocator, context->max_interned_string_size);
    assert(context->string_arena != NULL);

    context->type_arena = lca_arena_create(allocator, 1024 * 1024);
    assert(context->type_arena != NULL);

    return context;
}

void lyir_context_destroy(lyir_context* context) {
    if (context == NULL) return;

    lca_allocator allocator = context->allocator;

    for (int64_t i = 0, count = lca_da_count(context->sources); i < count; i++) {
        lyir_source* source = &context->sources[i];
        lca_string_destroy(&source->name);
        lca_string_destroy(&source->text);
    }

    lca_da_free(context->sources);
    lca_da_free(context->library_directories);
    lca_da_free(context->link_libraries);

    lca_arena_destroy(context->string_arena);
    lca_da_free(context->_interned_strings);

    for (int64_t i = 0, count = lca_da_count(context->allocated_strings); i < count; i++) {
        lca_string* string = &context->allocated_strings[i];
        lca_string_destroy(string);
    }

    for (int64_t i = 0; i < lca_da_count(context->ir_modules); i++) {
        lyir_module_destroy(context->ir_modules[i]);
        context->ir_modules[i] = NULL;
    }

    lca_da_free(context->allocated_strings);
    lca_da_free(context->ir_modules);

    for (int64_t i = 0, count = lca_da_count(context->_all_depgraphs); i < count; i++) {
        lyir_dependency_graph_destroy(context->_all_depgraphs[i]);
    }

    lca_da_free(context->_all_depgraphs);

    for (int64_t i = 0, count = lca_da_count(context->_all_types); i < count; i++) {
        layec_type_destroy(context->_all_types[i]);
    }
    
    lca_da_free(context->types.int_types);
    lca_da_free(context->_all_types);
    lca_da_free(context->_all_struct_types);
    lca_arena_destroy(context->type_arena);

    for (int64_t i = 0, count = lca_da_count(context->_all_values); i < count; i++) {
        layec_value_destroy(context->_all_values[i]);
        lca_deallocate(allocator, context->_all_values[i]);
    }

    lca_da_free(context->_all_values);

    *context = (lyir_context){0};
    lca_deallocate(allocator, context);
}

static int read_file_to_string(lca_allocator allocator, lca_string file_path, lca_string* out_contents) {
    assert(out_contents != NULL);
    const char* file_path_cstr = lca_string_as_cstring(file_path);
    assert(file_path_cstr != NULL);
    *out_contents = lca_file_read(allocator, file_path_cstr);
    return 0;
}

lyir_sourceid lyir_context_get_or_add_source_from_file(lyir_context* context, lca_string_view file_path) {
    assert(context != NULL);

    lyir_sourceid sourceid = 0;
    for (; sourceid < lca_da_count(context->sources); sourceid++) {
        if (lca_string_view_equals(lca_string_as_view(context->sources[sourceid].name), file_path))
            return sourceid;
    }

    lca_string file_path_owned = lca_string_view_to_string(context->allocator, file_path);
    lca_string text = {0};
    
    int error_code = read_file_to_string(context->allocator, file_path_owned, &text);
    if (error_code != 0) {
        //const char* error_string = strerror(error_code);
        //fprintf(stderr, "Error when opening source file \"%.*s\": %s\n", LCA_STR_EXPAND(file_path), error_string);

        lca_string_destroy(&file_path_owned);
        return -1;
    }

    return lyir_context_get_or_add_source_from_string(context, file_path_owned, text);
}

lyir_sourceid lyir_context_get_or_add_source_from_string(lyir_context* context, lca_string name, lca_string source_text) {
    assert(context != NULL);

    lyir_sourceid sourceid = lca_da_count(context->sources);
    lyir_source source = {
        .name = name,
        .text = source_text,
    };

    lca_da_push(context->sources, source);
    return sourceid;
}

lyir_source lyir_context_get_source(lyir_context* context, lyir_sourceid sourceid) {
    assert(context != NULL);
    assert(sourceid >= 0 && sourceid < lca_da_count(context->sources));
    return context->sources[sourceid];
}

bool lyir_context_get_location_info(lyir_context* context, lyir_location location, lca_string_view* out_name, int64_t* out_line, int64_t* out_column) {
    assert(context != NULL);

    if (location.offset < 0) return false;

    lyir_source source = lyir_context_get_source(context, location.sourceid);
    if (out_name != NULL) *out_name = lca_string_as_view(source.name);

    if (location.offset >= source.text.count) return false;
    if (location.offset + location.length > source.text.count) return false;

    int64_t last_line_start_offset = 0;
    int64_t line_number = 1;

    char lastc = 0;
    for (int64_t i = 0; i <= location.offset; i++) {
        if (lastc == '\n') {
            last_line_start_offset = i;
            line_number++;
        }

        lastc = source.text.data[i];
    }

    if (out_line != NULL) *out_line = line_number;
    if (out_column != NULL) *out_column = 1 + (location.offset - last_line_start_offset);

    return true;
}

void lyir_context_print_location_info(lyir_context* context, lyir_location location, lyir_status status, FILE* stream, bool use_color) {
    assert(context != NULL);

    lyir_source source = lyir_context_get_source(context, location.sourceid);
    lca_string_view name = lca_string_as_view(source.name);

    const char* col = "";
    const char* status_string = "";

    switch (status) {
        default: break;
        case LYIR_INFO: col = COL(CYAN); status_string = "Info:"; break;
        case LYIR_NOTE: col = COL(BRIGHT_GREEN); status_string = "Note:"; break;
        case LYIR_WARN: col = COL(YELLOW); status_string = "Warning:"; break;
        case LYIR_ERROR: col = COL(RED); status_string = "Error:"; break;
        case LYIR_FATAL: col = COL(BRIGHT_RED); status_string = "Fatal:"; break;
        case LYIR_ICE: col = COL(MAGENTA); status_string = "Internal Compiler Exception:"; break;
    }

    fprintf(stream, "%.*s", LCA_STR_EXPAND(name));

    if (context->use_byte_positions_in_diagnostics) {
        fprintf(stream, "[%ld]", location.offset);
    } else {
        int64_t line = 0, column = 0;
        if (lyir_context_get_location_info(context, location, &name, &line, &column)) {
            fprintf(stream, "(%ld, %ld)", line, column);
        } else {
            fprintf(stream, "(0, 0)");
        }
    }

    fprintf(stream, ": %s%s%s", col, status_string, COL(RESET));
}

#define GET_MESSAGE \
    va_list v; \
    va_start(v, format); \
    lca_string message = lca_string_vformat(format, v); \
    va_end(v)

lyir_diag lyir_info(lyir_context* context, lyir_location location, const char* format, ...) {
    GET_MESSAGE;
    return (lyir_diag) {
        .location = location,
        .status = LYIR_INFO,
        .message = message
    };
}

lyir_diag lyir_note(lyir_context* context, lyir_location location, const char* format, ...) {
    GET_MESSAGE;
    return (lyir_diag) {
        .location = location,
        .status = LYIR_NOTE,
        .message = message
    };
}

lyir_diag lyir_warn(lyir_context* context, lyir_location location, const char* format, ...) {
    GET_MESSAGE;
    return (lyir_diag) {
        .location = location,
        .status = LYIR_WARN,
        .message = message
    };
}

lyir_diag lyir_error(lyir_context* context, lyir_location location, const char* format, ...) {
    GET_MESSAGE;
    return (lyir_diag) {
        .location = location,
        .status = LYIR_ERROR,
        .message = message
    };
}

lyir_diag lyir_ice(lyir_context* context, lyir_location location, const char* format, ...) {
    GET_MESSAGE;
    return (lyir_diag) {
        .location = location,
        .status = LYIR_ICE,
        .message = message
    };
}

void lyir_write_diag(lyir_context* context, lyir_diag diag) {
    lyir_context_print_location_info(context, diag.location, diag.status, stdout, context->use_color);
    fprintf(stdout, " %.*s\n", LCA_STR_EXPAND(diag.message));
    if (diag.status == LYIR_ERROR || diag.status == LYIR_FATAL || diag.status == LYIR_ICE) {
        context->has_reported_errors = true;
    }
}

void lyir_write_info(lyir_context* context, lyir_location location, const char* format, ...) {
    GET_MESSAGE;
    lyir_context_print_location_info(context, location, LYIR_INFO, stdout, context->use_color);
    fprintf(stdout, " %.*s\n", LCA_STR_EXPAND(message));
}

void lyir_write_note(lyir_context* context, lyir_location location, const char* format, ...) {
    GET_MESSAGE;
    lyir_context_print_location_info(context, location, LYIR_NOTE, stdout, context->use_color);
    fprintf(stdout, " %.*s\n", LCA_STR_EXPAND(message));
}

void lyir_write_warn(lyir_context* context, lyir_location location, const char* format, ...) {
    GET_MESSAGE;
    lyir_context_print_location_info(context, location, LYIR_WARN, stdout, context->use_color);
    fprintf(stdout, " %.*s\n", LCA_STR_EXPAND(message));
}

void lyir_write_error(lyir_context* context, lyir_location location, const char* format, ...) {
    GET_MESSAGE;
    lyir_context_print_location_info(context, location, LYIR_ERROR, stdout, context->use_color);
    fprintf(stdout, " %.*s\n", LCA_STR_EXPAND(message));
    context->has_reported_errors = true;
}

void lyir_write_ice(lyir_context* context, lyir_location location, const char* format, ...) {
    GET_MESSAGE;
    lyir_context_print_location_info(context, location, LYIR_ICE, stdout, context->use_color);
    fprintf(stdout, " %.*s\n", LCA_STR_EXPAND(message));
    context->has_reported_errors = true;
}

#undef GET_MESSAGE

lca_string_view lyir_context_intern_string_view(lyir_context* context, lca_string_view s) {
    if (s.count + 1 > context->max_interned_string_size) {
        lca_string allocated_string = lca_string_view_to_string(context->allocator, s);
        lca_da_push(context->allocated_strings, allocated_string);
        return lca_string_as_view(allocated_string);
    }

    // TODO(local): these aren't properly interned yet, do that eventually.

    char* arena_string_data = lca_arena_push(context->string_arena, s.count + 1);
    memcpy(arena_string_data, s.data, (size_t)s.count);
    
    lca_string arena_string = lca_string_from_data(context->allocator, arena_string_data, s.count, s.count + 1);

    return lca_string_as_view(arena_string);
}
