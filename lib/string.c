#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "layec/string.h"

layec_string_view layec_string_view_create(const char* data, long long length)
{
    assert(length >= 0);
    if (length != 0) assert(data);

    return (layec_string_view)
    {
        .data = data,
        .length = length,
    };
}

layec_string_view layec_string_view_slice(layec_string_view s, long long offset, long long length)
{
    if (length < 0) length = s.length - offset;

    if (offset >= s.length) return (layec_string_view){0};
    if (offset < 0) offset = 0;

    if (length < 0) return (layec_string_view){0};
    if (length > s.length - offset) length = s.length - offset;

    return layec_string_view_create(s.data + offset, length);
}

bool layec_string_view_equals(layec_string_view s0, layec_string_view s1)
{
    if (s0.length != s1.length) return false;
    return 0 == strncmp(s0.data, s1.data, (unsigned long long)s0.length);
}

char* layec_string_view_to_cstring(layec_string_view s)
{
    char* result = malloc((unsigned long long)s.length + 1);
    result[s.length] = 0;
    memcpy(result, s.data, (unsigned long long)s.length);
    return result;
}

bool layec_string_view_starts_with_cstring(layec_string_view s, const char* start)
{
    unsigned long long start_len = strlen(start);
    if (s.length < (long long)start_len)
        return false;
    
    return 0 == strncmp(s.data, start, start_len);
}

bool layec_string_view_ends_with_cstring(layec_string_view s, const char* end)
{
    unsigned long long end_len = strlen(end);
    if (s.length < (long long)end_len)
        return false;
    
    return 0 == strncmp(s.data + s.length - end_len, end, end_len);
}

layec_string_view layec_string_view_concat(layec_string_view s0, layec_string_view s1)
{
    layec_string_builder builder = {0};
    layec_string_builder_append_string_view(&builder, s0);
    layec_string_builder_append_string_view(&builder, s1);

    layec_string_view result = layec_string_builder_to_string_view(&builder);
    layec_string_builder_destroy(&builder);

    return result;
}

layec_string_view layec_string_view_path_concat(layec_string_view s0, layec_string_view s1)
{
    bool s0_has_slash = layec_string_view_ends_with_cstring(s0, "/") || layec_string_view_ends_with_cstring(s0, "\\");
    bool s1_has_slash = layec_string_view_starts_with_cstring(s1, "/") || layec_string_view_starts_with_cstring(s1, "\\");

    layec_string_builder builder = {0};
    layec_string_builder_append_string_view(&builder, s0);
    if (s0_has_slash)
    {
        if (!s1_has_slash)
            layec_string_builder_append_string_view(&builder, s1);
        else layec_string_builder_append_string_view(&builder, layec_string_view_slice(s1, 1, -1));
    }
    else
    {
        if (!s1_has_slash)
            layec_string_builder_append_rune(&builder, '/');
        layec_string_builder_append_string_view(&builder, s1);
    }

    layec_string_view result = layec_string_builder_to_string_view(&builder);
    layec_string_builder_destroy(&builder);

    return result;
}

layec_string_view layec_string_view_get_parent_path(layec_string_view s)
{
    for (long long i = s.length - 1; i >= 0; i--)
    {
        if (s.data[i] == '/' || s.data[i] == '\\')
            return layec_string_view_slice(s, 0, i);
    }

    return (layec_string_view){0};
}

void layec_string_builder_destroy(layec_string_builder* sb)
{
    vector_free(sb->data);
}

void layec_string_builder_append_rune(layec_string_builder* sb, int rune)
{
    assert(sb);
    assert(rune >= 0 && rune <= 127);

    vector_push(sb->data, (char)rune);
}

void layec_string_builder_append_string_view(layec_string_builder* sb, layec_string_view s)
{
    for (long long i = 0; i < s.length; i++)
        layec_string_builder_append_rune(sb, s.data[i]);
}

layec_string_view layec_string_builder_to_string_view(layec_string_builder* sb)
{
    assert(sb);
    unsigned long long count = (unsigned long long)vector_count(sb->data);
    char* result = malloc(count + 1);
    result[count] = 0;
    memcpy(result, sb->data, (unsigned long long)count);
    return layec_string_view_create(result, (long long)count);
}

char* layec_string_builder_to_cstring(layec_string_builder* sb)
{
    assert(sb);
    unsigned long long count = (unsigned long long)vector_count(sb->data);
    char* result = malloc(count + 1);
    result[count] = 0;
    memcpy(result, sb->data, (unsigned long long)count);
    return result;
}
