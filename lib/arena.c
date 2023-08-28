#include <assert.h>
#include <stdlib.h>

#include "layec/arena.h"

static void layec_arena_add_chunk(layec_arena* arena)
{
    assert(arena);
    assert(arena->chunk_size >= 1024);
    layec_arena_chunk* chunk = calloc(1, sizeof *chunk);
    assert(chunk);
    chunk->capacity = arena->chunk_size;
    chunk->data = calloc((unsigned long long)arena->chunk_size, sizeof(char));
    assert(chunk->data);
    vector_push(arena->chunks, chunk);
    assert(arena->chunks);
}

static void layec_arena_chunk_destroy(layec_arena_chunk* chunk)
{
    assert(chunk);
    free(chunk->data);
    *chunk = (layec_arena_chunk){0};
    free(chunk);
}

static long long layec_arena_align(long long n)
{
    const int align_by = 16;
    if (n < align_by) return align_by;
    return ((n + (align_by - 1)) / align_by) * align_by;
}

layec_arena* layec_arena_create(long long chunk_size)
{
    if (chunk_size < 1024) chunk_size = 1024;
    chunk_size = layec_arena_align(chunk_size);
    layec_arena* arena = calloc(1, sizeof *arena);
    assert(arena);
    arena->chunk_size = chunk_size;
    layec_arena_add_chunk(arena);
    assert(arena->chunks);
    return arena;
}

void layec_arena_destroy(layec_arena* arena)
{
    for (long long i = 0; i < vector_count(arena->chunks); i++)
        layec_arena_chunk_destroy(arena->chunks[i]);

    vector_free(arena->chunks);

    *arena = (layec_arena){0};
    free(arena);
}

void* layec_arena_push(layec_arena* arena, long long count)
{
    assert(arena);
    assert(arena->chunks);

    count = layec_arena_align(count);
    assert(count >= 16);
    assert(count <= arena->chunk_size);

    layec_arena_chunk* chunk = *vector_back(arena->chunks);
    assert(chunk);
    assert(chunk->data);
    assert(chunk->capacity > 0);
    assert(chunk->allocated >= 0);

    if (count > chunk->capacity - chunk->allocated)
    {
        layec_arena_add_chunk(arena);
        chunk = *vector_back(arena->chunks);
        assert(chunk);
        assert(chunk->data);
        assert(chunk->capacity > 0);
        assert(chunk->allocated >= 0);
    }

    void* result_ptr = (char*)chunk->data + chunk->allocated;
    assert(*(char*)result_ptr == 0);

    chunk->allocated += count;
    assert(chunk->allocated <= chunk->capacity);

    return result_ptr;
}
