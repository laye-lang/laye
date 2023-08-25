#include "layec/vector.h"

void layec_vector_maybe_expand(void** vector_ref, long long element_size, long long required_count)
{
    struct layec_vector_header* header = vector_get_header(*vector_ref);
    if (!*vector_ref)
    {
        long long initial_capacity = 32;
        void* new_data = malloc((sizeof *header) + (unsigned long long)(initial_capacity * element_size));
        header = new_data;

        header->capacity = initial_capacity;
        header->count = 0;

    }
    else if (required_count >= header->capacity)
    {
        while (required_count >= header->capacity)
            header->capacity *= 2;
        header = realloc(header, (sizeof *header) + (unsigned long long)(header->capacity * element_size));
    }
    
    *vector_ref = (void*)(header + 1);
}
