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

bool layec_string_view_ends_with_cstring(layec_string_view s, const char* end)
{
    unsigned long long end_len = strlen(end);
    if (s.length < (long long)end_len)
        return false;
    
    return 0 == strncmp(s.data + s.length - end_len, end, end_len);
}
