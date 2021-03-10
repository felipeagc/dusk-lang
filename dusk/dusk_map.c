#include "dusk_internal.h"

static void _duskMapGrow(DuskMap *map)
{
    uint64_t old_size = map->size;
    DuskMapSlot *old_slots = map->slots;

    map->size = old_size * 2;
    map->slots = (DuskMapSlot*)duskAllocate(map->allocator, sizeof(*map->slots) * map->size);
    memset(map->slots, 0, sizeof(*map->slots) * map->size);

    for (uint64_t i = 0; i < old_size; i++)
    {
        if (old_slots[i].hash != 0)
        {
            duskMapSet(map, old_slots[i].key, old_slots[i].value);
        }
    }

    duskFree(map->allocator, map->slots);
}

DuskMap *duskMapCreate(DuskAllocator *allocator, size_t size)
{
    DuskMap *map = duskAllocate(allocator, sizeof(DuskMap));
    map->allocator = allocator;
    map->size = size;

    map->size -= 1;
    map->size |= map->size >> 1;
    map->size |= map->size >> 2;
    map->size |= map->size >> 4;
    map->size |= map->size >> 8;
    map->size |= map->size >> 16;
    map->size |= map->size >> 32;
    map->size += 1;

    map->slots = (DuskMapSlot*)duskAllocate(map->allocator, sizeof(*map->slots) * map->size);
    memset(map->slots, 0, sizeof(*map->slots) * map->size);

    return map;
}

void duskMapDestroy(DuskMap *map)
{
    duskFree(map->allocator, map->slots);
    duskFree(map->allocator, map);
}

void duskMapSet(DuskMap *map, const char *key, void *value)
{
    uint64_t hash = duskStringMapHash(key);
    uint64_t i = hash & (map->size - 1);
    uint64_t iters = 0;

    while ((map->slots[i].hash != hash || strcmp(map->slots[i].key, key) != 0) &&
           map->slots[i].hash != 0 && iters < map->size)
    {
        i = (i + 1) & (map->size - 1);
        iters++;
    }

    if (iters >= map->size)
    {
        _duskMapGrow(map);
        return duskMapSet(map, key, value);
    }

    map->slots[i].key = key;
    map->slots[i].value = value;
    map->slots[i].hash = hash;
}

bool duskMapGet(DuskMap *map, const char *key, void **value_ptr)
{
    uint64_t hash = duskStringMapHash(key);
    uint64_t i = hash & (map->size - 1);
    uint64_t iters = 0;

    while ((map->slots[i].hash != hash || strcmp(map->slots[i].key, key) != 0) &&
           map->slots[i].hash != 0 && iters < map->size)
    {
        i = (i + 1) & (map->size - 1);
        iters++;
    }

    if (iters >= map->size)
    {
        return false;
    }

    if (map->slots[i].hash != 0)
    {
        if (value_ptr) *value_ptr = map->slots[i].value;
        return true;
    }

    return false;
}

void duskMapRemove(DuskMap *map, const char *key)
{
    uint64_t hash = duskStringMapHash(key);
    uint64_t i = hash & (map->size - 1);
    uint64_t iters = 0;

    while ((map->slots[i].hash != hash || strcmp(map->slots[i].key, key) != 0) &&
           map->slots[i].hash != 0 && iters < map->size)
    {
        i = (i + 1) & (map->size - 1);
        iters++;
    }

    if (iters >= map->size)
    {
        return;
    }

    map->slots[i].hash = 0;
    map->slots[i].key = NULL;
}
