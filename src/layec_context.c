#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

#include "layec.h"

layec_context* layec_context_create(lca_allocator allocator) {
    layec_context* context = lca_allocate(allocator, sizeof *context);
    assert(context != NULL);
    context->allocator = allocator;
    return context;
}

void layec_context_destroy(layec_context* context) {
    if (context == NULL) return;

    lca_allocator allocator = context->allocator;

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

sourceid layec_context_get_or_add_source_from_file(layec_context* context, string_view file_path) {
    assert(context != NULL);

    sourceid sourceid = 0;
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

layec_source layec_context_get_source(layec_context* context, sourceid sourceid) {
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
    for (int64_t i = 0; i < location.offset; i++) {
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
