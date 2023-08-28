#ifndef LAYEC_ARENA_H
#define LAYEC_ARENA_H

#include "layec/vector.h"

typedef struct layec_arena layec_arena;
typedef struct layec_arena_chunk layec_arena_chunk;

struct layec_arena
{
    vector(layec_arena_chunk*) chunks;
    long long chunk_size;
};

struct layec_arena_chunk
{
    void* data;
    long long capacity;
    long long allocated;
};

layec_arena* layec_arena_create(long long chunk_size);
void layec_arena_destroy(layec_arena* arena);
void* layec_arena_push(layec_arena* arena, long long count);

#endif // LAYEC_ARENA_H
