#ifndef LAYEC_VECTOR_H
#define LAYEC_VECTOR_H

#include <stdlib.h>

/// Header data for a light-weight implelentation of typed vectors.
struct layec_vector_header
{
    long long capacity;
    long long count;
};

void layec_vector_maybe_expand(void** vector_ref, long long element_size, long long required_count);

#define vector(T) T*
#define vector_get_header(V) ((struct layec_vector_header*)(V) - 1)
#define vector_count(V) ((V) ? vector_get_header(V)->count : 0)
#define vector_push(V, E) do { layec_vector_maybe_expand((void**)&(V), (long long)sizeof *(V), vector_count(V) + 1); (V)[vector_count(V)] = E; vector_get_header(V)->count++; } while (0)
#define vector_pop(V) do { if (vector_get_header(V)->count) vector_get_header(V)->count--; } while (0)
#define vector_back(V) (&(V)[vector_count(V) - 1])
#define vector_free(V) do { if (V) { free(vector_get_header(V)); (V) = NULL; } } while (0)

#endif // LAYEC_VECTOR_H
