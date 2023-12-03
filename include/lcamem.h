#ifndef LCAMEM_H
#define LCAMEM_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

typedef void* (*lca_allocator_function)(void* user_data, size_t count, void* ptr);

typedef struct lca_allocator {
    void* user_data;
    lca_allocator_function allocator_function;
} lca_allocator;

typedef struct lca_arena lca_arena;

extern lca_allocator default_allocator;
extern lca_allocator temp_allocator;

void* lca_allocate(lca_allocator allocator, size_t n);
void lca_deallocate(lca_allocator allocator, void* ptr);
void* lca_reallocate(lca_allocator allocator, void* ptr, size_t n);

void lca_temp_allocator_init(lca_allocator allocator, int64_t block_size);
void lca_temp_allocator_clear(void);

char* lca_temp_sprintf(const char* format, ...);
char* lca_temp_vsprintf(const char* format, va_list v);

lca_arena* lca_arena_create(lca_allocator allocator, size_t block_size);
void* lca_arena_push(lca_arena* arena, size_t size);
void lca_arena_clear(lca_arena* arena);

#ifdef LCA_MEM_IMPLEMENTATION

#    include <assert.h>
#    include <stdio.h>
#    include <stdlib.h>
#    include <string.h>

typedef struct lca_arena_block {
    void* memory;
    int64_t allocated;
    int64_t capacity;
} lca_arena_block;

struct lca_arena {
    lca_allocator allocator;
    lca_arena_block* blocks;
    int64_t block_size;
    int64_t current_block_index;
    int64_t blocks_count;
    int64_t blocks_capacity;
};

void* lca_default_allocator_function(void* user_data, size_t count, void* ptr);
void* lca_temp_allocator_function(void* user_data, size_t count, void* ptr);

lca_allocator default_allocator = {
    .allocator_function = lca_default_allocator_function
};

lca_allocator temp_allocator;

void* lca_allocate(lca_allocator allocator, size_t n) {
    return allocator.allocator_function(allocator.user_data, n, NULL);
}

void lca_deallocate(lca_allocator allocator, void* n) {
    allocator.allocator_function(allocator.user_data, 0, n);
}

void* lca_reallocate(lca_allocator allocator, void* ptr, size_t n) {
    return allocator.allocator_function(allocator.user_data, n, ptr);
}

void* lca_default_allocator_function(void* user_data, size_t count, void* ptr) {
    if (count == 0) {
        free(ptr);
        return NULL;
    } else if (ptr == NULL)
        return calloc(1, count);
    else return realloc(ptr, count);
}

void arena_ensure_block_initialized(lca_arena* arena, lca_arena_block* block) {
    if (block->memory != NULL) return;
    block->capacity = arena->block_size;
    block->memory = lca_allocate(arena->allocator, block->capacity);
    memset(block->memory, 0, block->capacity);
}

void* lca_temp_allocator_function(void* user_data, size_t count, void* ptr) {
    lca_arena* temp_arena = temp_allocator.user_data;
    assert(temp_arena != NULL && "Where did the arena go? did you init it?");
    assert(ptr == NULL && "Cannot reallocate temp arena memory");
    return lca_arena_push(temp_arena, count);
}

void lca_temp_allocator_init(lca_allocator allocator, int64_t block_size) {
    assert(temp_allocator.user_data == NULL);
    temp_allocator = (lca_allocator){
        .user_data = lca_allocate(allocator, sizeof(struct lca_arena)),
        .allocator_function = lca_temp_allocator_function,
    };

    lca_arena* temp_arena = temp_allocator.user_data;
    assert(temp_arena != NULL);

    *temp_arena = (struct lca_arena){
        .allocator = allocator,
        .block_size = block_size,
    };

    temp_arena->blocks_capacity = 16;
    temp_arena->blocks = lca_allocate(allocator, (size_t)temp_arena->blocks_capacity * sizeof *temp_arena->blocks);
    assert(temp_arena->blocks != NULL);
    memset(temp_arena->blocks, 0, (size_t)temp_arena->blocks_capacity * sizeof *temp_arena->blocks);

    arena_ensure_block_initialized(temp_arena, &temp_arena->blocks[0]);
}

void lca_temp_allocator_clear(void) {
    lca_arena* temp_arena = temp_allocator.user_data;
    lca_arena_clear(temp_arena);
}

char* lca_temp_sprintf(const char* format, ...) {
    va_list v;
    va_start(v, format);
    char* result = lca_temp_vsprintf(format, v);
    va_end(v);
    return result;
}

char* lca_temp_vsprintf(const char* format, va_list v) {
    va_list v1;
    va_copy(v1, v);
    int n = vsnprintf(NULL, 0, format, v1);
    va_end(v1);

    char* result = lca_allocate(temp_allocator, (size_t)n + 1);
    vsnprintf(result, n + 1, format, v);

    return result;
}

lca_arena* lca_arena_create(lca_allocator allocator, size_t block_size) {
    lca_arena* arena = lca_allocate(allocator, block_size);
    *arena = (lca_arena){
        .allocator = allocator,
        .block_size = block_size
    };

    arena->blocks_capacity = 16;
    arena->blocks = lca_allocate(allocator, (size_t)arena->blocks_capacity * sizeof *arena->blocks);
    assert(arena->blocks != NULL);
    memset(arena->blocks, 0, (size_t)arena->blocks_capacity * sizeof *arena->blocks);

    arena_ensure_block_initialized(arena, &arena->blocks[0]);

    return arena;
}

void* lca_arena_push(lca_arena* arena, size_t count) {
    assert(count <= (size_t)arena->block_size && "Requested more memory than a temp arena block can hold :(");

    lca_arena_block* block = &arena->blocks[arena->current_block_index];
    if (block->capacity - block->allocated > (int64_t)count) {
        arena->current_block_index++;
        if (arena->current_block_index >= arena->blocks_capacity) {
            int64_t new_capacity = arena->blocks_capacity * 2;
            arena->blocks = lca_reallocate(arena->allocator, arena->blocks, arena->blocks_capacity);
            memset(arena->blocks + arena->blocks_capacity, 0, (size_t)(new_capacity - arena->blocks_capacity) * sizeof *arena->blocks);
            arena->blocks_capacity = new_capacity;
        }

        block = &arena->blocks[arena->current_block_index];
        arena_ensure_block_initialized(arena, block);
        assert(block->allocated == 0);
    }

    void* result = block->memory + block->allocated;
    block->allocated += count;
    memset(result, 0, count);

    return result;
}

void lca_arena_clear(lca_arena* arena) {
    for (int64_t b = 0; b < arena->blocks_count; b++) {
        lca_arena_block* block = &arena->blocks[b];
        block->allocated = 0;
        memset(block->memory, 0, block->capacity);
    }

    arena->current_block_index = 0;
}

#endif // LCA_MEM_IMPLEMENTATION

#endif // !LCAMEM_H
