#ifndef LAYEC_UTIL_H
#define LAYEC_UTIL_H

typedef struct layec_string_view layec_string_view;

/// Simple string wrapper which does not own its data and does not need to be nul terminated.
/// A view does not own its data. It is simply a window into existing string data.
struct layec_string_view
{
    const char* data;
    long long length;
};

/// Header data for a light-weight implelentation of typed vectors.
struct layec_vector_header
{
    long long capacity;
    long long count;
};

/// Create a string view from a pointer and a length.
/// String views do not own their data, so we do not like them modifying it.
layec_string_view layec_string_view_create(const char* data, long long length);

void layec_vector_maybe_expand(void** vector_ref, long long element_size, long long required_count);

#define vector(T) T*
#define vector_get_header(V) ((struct layec_vector_header*)(V) - 1)
#define vector_count(V) ((V) ? vector_get_header(V)->count : 0)
#define vector_push(V, E) do { layec_vector_maybe_expand((void**)&(V), (long long)sizeof *(V), vector_count(V) + 1); (V)[vector_count(V)] = E; vector_get_header(V)->count++; } while (0)

#endif // LAYEC_UTIL_H
