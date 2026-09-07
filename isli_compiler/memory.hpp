#ifndef ISLI_MEMORY_HPP
#define ISLI_MEMORY_HPP

#include "common.hpp"
#include "object.hpp"

#define SLAB_CLASSES 6
#define SLAB_MAX_SIZE 512

#define ALLOCATE(type, count) \
    static_cast<type*>(reallocate(nullptr, 0, sizeof(type) * (count)))

#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

#define GROW_ARRAY(type, pointer, oldCount, newCount) \
    static_cast<type*>(reallocate(pointer, sizeof(type) * (oldCount), \
        sizeof(type) * (newCount)))

#define FREE_ARRAY(type, pointer, oldCount) \
    reallocate(pointer, sizeof(type) * (oldCount), 0)

void* slabAlloc(size_t size);
void  slabFree(void* pointer, size_t size);
void* alignedAlloc64(size_t bytes);
void  alignedFree64(void* pointer);
void* reallocate(void* pointer, size_t oldSize, size_t newSize);
void  freeObject(Obj* object);
void  freeSlabs();

#endif // ISLI_MEMORY_HPP
