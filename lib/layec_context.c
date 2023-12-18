#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

#include "layec.h"

layec_target_info* layec_default_target;
layec_target_info* layec_x86_64_linux;
layec_target_info* layec_x86_64_windows;

void layec_init_targets(lca_allocator allocator) {
    layec_x86_64_linux = lca_allocate(allocator, sizeof(layec_target_info));
    layec_x86_64_windows = lca_allocate(allocator, sizeof(layec_target_info));

    assert(layec_x86_64_linux != NULL);
    *layec_x86_64_linux = (layec_target_info) {
        .c = {
            .size_of_bool = 8,
            .size_of_char = 8,
            .size_of_short = 16,
            .size_of_int = 32,
            .size_of_long = 64,
            .size_of_long_long = 64,

            .align_of_bool = 8,
            .align_of_char = 8,
            .align_of_short = 16,
            .align_of_int = 32,
            .align_of_long = 64,
            .align_of_long_long = 64,

            .char_is_signed = true,
        },

        .laye = {
            .size_of_bool = 8,
            .size_of_int = 64,

            .align_of_bool = 8,
            .align_of_int = 64,
        },

        .size_of_pointer = 64,
        .align_of_pointer = 64,
    };

    assert(layec_x86_64_windows != NULL);
    memcpy(layec_x86_64_windows, layec_x86_64_linux, sizeof(layec_target_info));
    layec_x86_64_windows->c.size_of_long = 32;
    layec_x86_64_windows->c.align_of_long = 32;

    layec_default_target = layec_x86_64_linux;

    assert(layec_default_target != NULL);
}

layec_context* layec_context_create(lca_allocator allocator) {
    layec_context* context = lca_allocate(allocator, sizeof *context);
    assert(context != NULL);
    context->allocator = allocator;
    context->target = layec_default_target;
    context->max_interned_string_size = 1024 * 1024;
    context->string_arena = lca_arena_create(allocator, context->max_interned_string_size);
    context->laye_types.type = laye_node_create_in_context(context, LAYE_NODE_TYPE_TYPE, NULL);
    context->laye_types.type->type = context->laye_types.type;
    context->laye_types.poison = laye_node_create_in_context(context, LAYE_NODE_TYPE_POISON, context->laye_types.type);
    context->laye_types.unknown = laye_node_create_in_context(context, LAYE_NODE_TYPE_UNKNOWN, context->laye_types.type);
    context->laye_types._void = laye_node_create_in_context(context, LAYE_NODE_TYPE_VOID, context->laye_types.type);
    context->laye_types.noreturn = laye_node_create_in_context(context, LAYE_NODE_TYPE_NORETURN, context->laye_types.type);
    context->laye_types._bool = laye_node_create_in_context(context, LAYE_NODE_TYPE_BOOL, context->laye_types.type);
    return context;
}

void layec_context_destroy(layec_context* context) {
    if (context == NULL) return;

    lca_allocator allocator = context->allocator;

    for (int64_t i = 0, count = arr_count(context->sources); i < count; i++) {
        layec_source* source = &context->sources[i];
        string_destroy(&source->name);
        string_destroy(&source->text);
    }

    arr_free(context->sources);
    arr_free(context->include_directories);

    lca_arena_destroy(context->string_arena);
    arr_free(context->_interned_strings);

    for (int64_t i = 0, count = arr_count(context->allocated_strings); i < count; i++) {
        string* string = &context->allocated_strings[i];
        string_destroy(string);
    }

    arr_free(context->allocated_strings);

    lca_deallocate(allocator, context->laye_types.poison);
    lca_deallocate(allocator, context->laye_types.unknown);
    lca_deallocate(allocator, context->laye_types.type);
    lca_deallocate(allocator, context->laye_types._void);
    lca_deallocate(allocator, context->laye_types.noreturn);
    lca_deallocate(allocator, context->laye_types._bool);

    *context = (layec_context){};
    lca_deallocate(allocator, context);
}

static string read_file_to_string(lca_allocator allocator, string file_path) {
    const char* file_path_cstr = string_as_cstring(file_path);
    FILE* stream = fopen(file_path_cstr, "r");
    fseek(stream, 0, SEEK_END);
    int64_t count = ftell(stream);
    fseek(stream, 0, SEEK_SET);
    char* data = lca_allocate(allocator, count + 1);
    fread(data, (size_t)count, 1, stream);
    data[count] = 0;
    fclose(stream);
    return string_from_data(allocator, data, count, count + 1);
}

layec_sourceid layec_context_get_or_add_source_from_file(layec_context* context, string_view file_path) {
    assert(context != NULL);

    layec_sourceid sourceid = 0;
    for (; sourceid < arr_count(context->sources); sourceid++) {
        if (string_view_equals(SV(context->sources[sourceid].name), file_path))
            return sourceid;
    }

    string file_path_owned = string_view_to_string(context->allocator, file_path);
    string text = read_file_to_string(context->allocator, file_path_owned);

    layec_source source = {
        .name = file_path_owned,
        .text = text,
    };

    sourceid = arr_count(context->sources);
    arr_push(context->sources, source);

    return sourceid;
}

layec_source layec_context_get_source(layec_context* context, layec_sourceid sourceid) {
    assert(context != NULL);
    assert(sourceid >= 0 && sourceid < arr_count(context->sources));
    return context->sources[sourceid];
}

bool layec_context_get_location_info(layec_context* context, layec_location location, string_view* out_name, int64_t* out_line, int64_t* out_column) {
    assert(context != NULL);

    if (location.offset < 0) return false;

    layec_source source = layec_context_get_source(context, location.sourceid);
    if (out_name != NULL) *out_name = string_as_view(source.name);

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

void layec_context_print_location_info(layec_context* context, layec_location location, layec_status status, FILE* stream, bool use_color) {
    assert(context != NULL);

    string_view name = {};
    int64_t line = 0;
    int64_t column = 0;

    if (!layec_context_get_location_info(context, location, &name, &line, &column)) {
        fprintf(stream, "%s<unknown>:0:0%s:", COL(WHITE), COL(RESET));
        return;
    }

    const char* col = "";
    const char* status_string = "";

    switch (status) {
        default: break;
        case LAYEC_INFO: col = COL(CYAN); status_string = " Info:"; break;
        case LAYEC_NOTE: col = COL(BRIGHT_GREEN); status_string = " Note:"; break;
        case LAYEC_WARN: col = COL(YELLOW); status_string = " Warning:"; break;
        case LAYEC_ERROR: col = COL(RED); status_string = " Error:"; break;
        case LAYEC_FATAL: col = COL(BRIGHT_RED); status_string = " Fatal:"; break;
        case LAYEC_ICE: col = COL(MAGENTA); status_string = " Internal Compiler Exception:"; break;
    }

    fprintf(stream, "%s%.*s:%ld:%ld:%s%s", col, STR_EXPAND(name), line, column, status_string, COL(RESET));
}

#define GET_MESSAGE \
    va_list v; \
    va_start(v, format); \
    string message = string_vformat(format, v); \
    va_end(v)

layec_diag layec_info(layec_context* context, layec_location location, const char* format, ...) {
    GET_MESSAGE;
    return (layec_diag) {
        .location = location,
        .status = LAYEC_INFO,
        .message = message
    };
}

layec_diag layec_note(layec_context* context, layec_location location, const char* format, ...) {
    GET_MESSAGE;
    return (layec_diag) {
        .location = location,
        .status = LAYEC_NOTE,
        .message = message
    };
}

layec_diag layec_warn(layec_context* context, layec_location location, const char* format, ...) {
    GET_MESSAGE;
    return (layec_diag) {
        .location = location,
        .status = LAYEC_WARN,
        .message = message
    };
}

layec_diag layec_error(layec_context* context, layec_location location, const char* format, ...) {
    GET_MESSAGE;
    return (layec_diag) {
        .location = location,
        .status = LAYEC_ERROR,
        .message = message
    };
}

void layec_write_diag(layec_context* context, layec_diag diag) {
    layec_context_print_location_info(context, diag.location, diag.status, stderr, context->use_color);
    fprintf(stderr, " %.*s\n", STR_EXPAND(diag.message));
}

void layec_write_info(layec_context* context, layec_location location, const char* format, ...) {
    GET_MESSAGE;
    layec_context_print_location_info(context, location, LAYEC_INFO, stderr, context->use_color);
    fprintf(stderr, " %.*s\n", STR_EXPAND(message));
}

void layec_write_note(layec_context* context, layec_location location, const char* format, ...) {
    GET_MESSAGE;
    layec_context_print_location_info(context, location, LAYEC_NOTE, stderr, context->use_color);
    fprintf(stderr, " %.*s\n", STR_EXPAND(message));
}

void layec_write_warn(layec_context* context, layec_location location, const char* format, ...) {
    GET_MESSAGE;
    layec_context_print_location_info(context, location, LAYEC_WARN, stderr, context->use_color);
    fprintf(stderr, " %.*s\n", STR_EXPAND(message));
}

void layec_write_error(layec_context* context, layec_location location, const char* format, ...) {
    GET_MESSAGE;
    layec_context_print_location_info(context, location, LAYEC_ERROR, stderr, context->use_color);
    fprintf(stderr, " %.*s\n", STR_EXPAND(message));
}

#undef GET_MESSAGE

string layec_context_intern_string_view(layec_context* context, string_view s) {
    if (s.count + 1 > context->max_interned_string_size) {
        string allocated_string = string_view_to_string(context->allocator, s);
        arr_push(context->allocated_strings, allocated_string);
        return allocated_string;
    }

    // TODO(local): these aren't properly interned yet, do that eventually.

    char* arena_string_data = lca_arena_push(context->string_arena, s.count + 1);
    memcpy(arena_string_data, s.data, (size_t)s.count);
    
    string arena_string = string_from_data(context->allocator, arena_string_data, s.count, s.count + 1);

    return arena_string;
}
