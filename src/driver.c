#include <assert.h>
#include <stdio.h>

#include "layec/context.h"
#include "layec/source.h"

const char* usage_text = "Usage: layec [options] file...\n";

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        printf("%s", usage_text);
        return 0;
    }

    const char* file_path = argv[1];

    layec_context* context = layec_context_create();
    assert(context > 0);

    int source_id = layec_context_get_or_add_source_buffer_from_file(context, file_path);
    if (source_id <= 0)
        return 1;

    layec_source_buffer source_buffer = layec_context_get_source_buffer(context, source_id);
    assert(source_buffer.name == file_path);
    assert(source_buffer.text);

    printf("%s\n", source_buffer.text);
    return 0;
}
