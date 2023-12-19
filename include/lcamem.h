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
void lca_temp_allocator_dump(void);

char* lca_temp_sprintf(const char* format, ...);
char* lca_temp_vsprintf(const char* format, va_list v);

lca_arena* lca_arena_create(lca_allocator allocator, size_t block_size);
void lca_arena_destroy(lca_arena* arena);
void* lca_arena_push(lca_arena* arena, size_t size);
void lca_arena_clear(lca_arena* arena);
void lca_arena_dump(lca_arena* arena);

#ifdef LCA_MEM_IMPLEMENTATION

#    include "lcads.h"

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
    dynarr(lca_arena_block) blocks;
    int64_t block_size;
};

void* lca_default_allocator_function(void* user_data, size_t count, void* ptr);
void* lca_temp_allocator_function(void* user_data, size_t count, void* ptr);

lca_allocator default_allocator = {
    .allocator_function = lca_default_allocator_function
};

lca_allocator temp_allocator = {};

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
    } else if (ptr == NULL) {
        void* data = calloc(1, count);
        assert(data != NULL);
        return data;
    } else {
        void* data = realloc(ptr, count);
        assert(data != NULL);
        return data;
    }
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
        .user_data = lca_arena_create(allocator, block_size),
        .allocator_function = lca_temp_allocator_function,
    };

    assert(temp_allocator.user_data != NULL);
}

void lca_temp_allocator_clear(void) {
    lca_arena* temp_arena = temp_allocator.user_data;
    lca_arena_clear(temp_arena);
}

void lca_temp_allocator_dump(void) {
    lca_arena* temp_arena = temp_allocator.user_data;
    lca_arena_dump(temp_arena);
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

lca_arena_block lca_arena_block_create(lca_arena* arena) {
    return (lca_arena_block){
        .memory = lca_allocate(arena->allocator, arena->block_size),
        .capacity = arena->block_size,
    };
}

lca_arena* lca_arena_create(lca_allocator allocator, size_t block_size) {
    lca_arena* arena = lca_allocate(allocator, block_size);
    *arena = (lca_arena){
        .allocator = allocator,
        .block_size = block_size
    };

    lca_arena_block first_block = lca_arena_block_create(arena);
    lca_da_push(arena->blocks, first_block);

    return arena;
}

void lca_arena_destroy(lca_arena* arena) {
    if (arena == NULL) return;

    lca_allocator allocator = arena->allocator;

    for (int64_t i = 0, count = arr_count(arena->blocks); i < count; i++) {
        lca_arena_block* block = &arena->blocks[i];
        lca_deallocate(allocator, block->memory);
        *block = (lca_arena_block){};
    }

    arr_free(arena->blocks);

    *arena = (lca_arena){};
    lca_deallocate(allocator, arena);
}

void* lca_arena_push(lca_arena* arena, size_t count) {
    assert(count <= (size_t)arena->block_size && "Requested more memory than a temp arena block can hold :(");

    lca_arena_block* block = &arena->blocks[lca_da_count(arena->blocks) - 1];
    if (block->capacity - block->allocated < (int64_t)count) {
        lca_arena_block new_block = lca_arena_block_create(arena);
        lca_da_push(arena->blocks, new_block);
        block = &arena->blocks[lca_da_count(arena->blocks) - 1];
    }

    void* result = block->memory + block->allocated;
    block->allocated += count;
    memset(result, 0, count);

    return result;
}

void lca_arena_clear(lca_arena* arena) {
    for (int64_t i = 0, count = lca_da_count(arena->blocks); i < count; i++) {
        lca_arena_block* block = &arena->blocks[i];
        memset(block->memory, 0, block->capacity);
        lca_deallocate(arena->allocator, block->memory);
    }

    lca_da_free(arena->blocks);
    arena->blocks = NULL;

    lca_arena_block first_block = lca_arena_block_create(arena);
    lca_da_push(arena->blocks, first_block);
}

void lca_arena_dump(lca_arena* arena) {
    fprintf(stderr, "<Memory Arena %p>\n", (void*)arena);
    fprintf(stderr, "  Block Count: %ld\n", lca_da_count(arena->blocks));
    fprintf(stderr, "  Block Size: %ld\n", arena->block_size);
    fprintf(stderr, "  Block Storage: %p\n", (void*)arena->blocks);
    fprintf(stderr, "  Allocator:\n");
    fprintf(stderr, "    User Data: %p\n", (void*)arena->allocator.user_data);
    fprintf(stderr, "    Function: %p\n", (void*)arena->allocator.allocator_function);
    fprintf(stderr, "  Blocks:\n");

    for (int64_t i = 0, count = lca_da_count(arena->blocks); i < count; i++) {
        fprintf(stderr, "    %ld:\n", i);
        lca_arena_block block = arena->blocks[i];

        fprintf(stderr, "      Memory: %p\n", (void*)block.memory);
        fprintf(stderr, "      Allocated: %ld\n", block.allocated);
        fprintf(stderr, "      Capacity: %ld\n", block.capacity);
    }
}

#endif // LCA_MEM_IMPLEMENTATION

#endif // !LCAMEM_H
