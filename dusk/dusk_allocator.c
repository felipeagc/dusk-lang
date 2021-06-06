#include "dusk_internal.h"

#include <stdlib.h>
#include <stdio.h>

void *duskAllocate(DuskAllocator *allocator, size_t size)
{
    if (!allocator) return malloc(size);
    return allocator->allocate(allocator, size);
}

void *duskAllocateZeroed(DuskAllocator *allocator, size_t size)
{
    void *data = duskAllocate(allocator, size);
    memset(data, 0, size);
    return data;
}

void *duskReallocate(DuskAllocator *allocator, void *ptr, size_t size)
{
    if (!allocator) return realloc(ptr, size);
    return allocator->reallocate(allocator, ptr, size);
}

void duskFree(DuskAllocator *allocator, void *ptr)
{
    if (!allocator) {
        free(ptr);
        return;
    }
    allocator->free(allocator, ptr);
}

typedef struct DuskArenaChunk {
    struct DuskArenaChunk *prev;
    uint8_t *data;
    size_t offset;
    size_t size;
} DuskArenaChunk;

struct DuskArena {
    DuskAllocator allocator;
    DuskAllocator *parent_allocator;
    DuskArenaChunk *last_chunk;
};

#define ARENA_PTR_SIZE(ptr) *(((uint64_t *)ptr) - 1)

static DuskArenaChunk *
_duskArenaNewChunk(DuskArena *arena, DuskArenaChunk *prev, size_t size)
{
    DuskArenaChunk *chunk =
        (DuskArenaChunk *)duskAllocate(arena->parent_allocator, sizeof(*chunk));
    memset(chunk, 0, sizeof(*chunk));

    chunk->prev = prev;

    chunk->size = size;
    chunk->data = (uint8_t *)duskAllocate(arena->parent_allocator, chunk->size);

    return chunk;
}

static void *_duskArenaAllocate(DuskAllocator *allocator, size_t size)
{
    DuskArena *arena = (DuskArena *)allocator;

    DuskArenaChunk *chunk = arena->last_chunk;

    size_t new_offset = chunk->offset;
    while (new_offset % 16 != 0)
        new_offset++;
    new_offset += 16; // Header
    size_t data_offset = new_offset;
    new_offset += size;

    if (chunk->size <= new_offset) {
        size_t new_chunk_size = chunk->size * 2;
        while (new_chunk_size <= (size + 16))
            new_chunk_size *= 2;
        arena->last_chunk = _duskArenaNewChunk(arena, chunk, new_chunk_size);
        return _duskArenaAllocate(allocator, size);
    }

    uint8_t *ptr = &chunk->data[data_offset];
    ARENA_PTR_SIZE(ptr) = size;

    chunk->offset = new_offset;

    return (void *)ptr;
}

static void *
_duskArenaReallocate(DuskAllocator *allocator, void *ptr, size_t size)
{
    uint64_t old_size = ARENA_PTR_SIZE(ptr);

    void *new_ptr = _duskArenaAllocate(allocator, size);
    memcpy(new_ptr, ptr, old_size);

    return new_ptr;
}

static void _duskArenaFree(DuskAllocator *allocator, void *ptr)
{
    (void)allocator;
    (void)ptr;
}

DuskArena *duskArenaCreate(DuskAllocator *parent_allocator, size_t default_size)
{
    DuskArena *arena =
        (DuskArena *)duskAllocate(parent_allocator, sizeof(*arena));
    memset(arena, 0, sizeof(*arena));

    arena->allocator.allocate = _duskArenaAllocate;
    arena->allocator.reallocate = _duskArenaReallocate;
    arena->allocator.free = _duskArenaFree;

    arena->parent_allocator = parent_allocator;
    arena->last_chunk = _duskArenaNewChunk(arena, NULL, default_size);

    return arena;
}

DuskAllocator *duskArenaGetAllocator(DuskArena *arena)
{
    return &arena->allocator;
}

void duskArenaDestroy(DuskArena *arena)
{
    DuskArenaChunk *chunk = arena->last_chunk;
    while (chunk) {
        duskFree(arena->parent_allocator, chunk->data);
        DuskArenaChunk *chunk_to_free = chunk;
        chunk = chunk->prev;
        duskFree(arena->parent_allocator, chunk_to_free);
    }
    duskFree(arena->parent_allocator, arena);
}

const char *duskStrdup(DuskAllocator *allocator, const char *str)
{
    size_t length = strlen(str);
    char *new_str = (char *)duskAllocate(allocator, length + 1);
    memcpy(new_str, str, length);
    new_str[length] = '\0';
    return new_str;
}

const char *
duskNullTerminate(DuskAllocator *allocator, const char *str, size_t length)
{

    char *new_str = (char *)duskAllocate(allocator, length + 1);
    memcpy(new_str, str, length);
    new_str[length] = '\0';
    return new_str;
}

DUSK_PRINTF_FORMATTING(2, 3)
const char *duskSprintf(DuskAllocator *allocator, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    size_t str_size = vsnprintf(NULL, 0, format, args) + 1;
    va_end(args);

    char *str = (char *)duskAllocate(allocator, str_size);

    va_start(args, format);
    vsnprintf(str, str_size, format, args);
    va_end(args);

    str[str_size - 1] = '\0';

    return str;
}

const char *
duskVsprintf(DuskAllocator *allocator, const char *format, va_list args)
{
    va_list va1;
    va_copy(va1, args);
    size_t str_size = vsnprintf(NULL, 0, format, va1) + 1;
    va_end(va1);

    char *str = (char *)duskAllocate(allocator, str_size);

    vsnprintf(str, str_size, format, args);

    str[str_size - 1] = '\0';

    return str;
}
