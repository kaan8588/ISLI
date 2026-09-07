#include <bit>
#include <cstdlib>
#include <cstring>
#include <mutex>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

#include "compiler.hpp"
#include "memory.hpp"
#include "vm.hpp"

// ── Slab Allocator (Thread-Local Lock-Free Pool) ───────────────────

struct SlabNode {
    SlabNode* next;
};

struct ArenaNode {
    ArenaNode* next;
};

struct GlobalSlabPool {
    ArenaNode* arenas = nullptr;
};

static constexpr size_t slabSizes[SLAB_CLASSES] = {16, 32, 64, 128, 256, 512};
static GlobalSlabPool global_slab;
static std::mutex global_slab_mutex;

struct alignas(64) ThreadSlabCache {
    SlabNode* freeLists[SLAB_CLASSES] = {};
    uint8_t* arenaCurrent = nullptr;
    size_t arenaRemaining = 0;
};
static thread_local ThreadSlabCache tl_slab;

static inline int getSlabClass(size_t size) {
    if (size > 512) return -1;
    if (size <= 16) return 0;
    return std::bit_width(size - 1) - 4;
}

void* slabAlloc(size_t size) {
    if (size == 0) return nullptr;
    int sc = getSlabClass(size);
    if (sc < 0) {
        void* ptr = std::malloc(size);
        if (ptr == nullptr) std::exit(1);
        return ptr;
    }

    size_t blockSize = slabSizes[sc];
    if (tl_slab.freeLists[sc] != nullptr) {
        SlabNode* node = tl_slab.freeLists[sc];
        tl_slab.freeLists[sc] = node->next;
        return static_cast<void*>(node);
    }

    if (tl_slab.arenaRemaining < blockSize) {
        size_t chunkBytes = 65536;
        std::lock_guard<std::mutex> lock(global_slab_mutex);
        void* raw = std::malloc(chunkBytes);
        if (raw == nullptr) std::exit(1);
        auto* node = static_cast<ArenaNode*>(raw);
        node->next = global_slab.arenas;
        global_slab.arenas = node;

        constexpr size_t headerAligned = (sizeof(ArenaNode) + 15) & ~static_cast<size_t>(15);
        tl_slab.arenaCurrent = static_cast<uint8_t*>(raw) + headerAligned;
        tl_slab.arenaRemaining = chunkBytes - headerAligned;
    }

    void* ptr = static_cast<void*>(tl_slab.arenaCurrent);
    tl_slab.arenaCurrent += blockSize;
    tl_slab.arenaRemaining -= blockSize;
    return ptr;
}

void freeSlabs() {
    std::lock_guard<std::mutex> lock(global_slab_mutex);
    ArenaNode* node = global_slab.arenas;
    while (node != nullptr) {
        ArenaNode* next = node->next;
        std::free(node);
        node = next;
    }
    global_slab.arenas = nullptr;
    for (int i = 0; i < SLAB_CLASSES; i++) {
        tl_slab.freeLists[i] = nullptr;
    }
    tl_slab.arenaCurrent = nullptr;
    tl_slab.arenaRemaining = 0;
}

void slabFree(void* pointer, size_t size) {
    if (pointer == nullptr || size == 0) return;
    int sc = getSlabClass(size);
    if (sc < 0) {
        std::free(pointer);
        return;
    }

    auto* node = static_cast<SlabNode*>(pointer);
    node->next = tl_slab.freeLists[sc];
    tl_slab.freeLists[sc] = node;
}

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    if (newSize == 0) {
        slabFree(pointer, oldSize);
        return nullptr;
    }

    if (oldSize == 0) {
        return slabAlloc(newSize);
    }

    int oldSc = getSlabClass(oldSize);
    int newSc = getSlabClass(newSize);
    if (oldSc >= 0 && oldSc == newSc) {
        return pointer;
    }

    void* result = slabAlloc(newSize);
    size_t copySize = oldSize < newSize ? oldSize : newSize;
    std::memcpy(result, pointer, copySize);
    slabFree(pointer, oldSize);
    return result;
}

void* alignedAlloc64(size_t bytes) {
    if (bytes == 0) bytes = 64;
#if defined(_WIN32) || defined(_WIN64)
    return _aligned_malloc(bytes, 64);
#else
    void* p = nullptr;
    if (posix_memalign(&p, 64, bytes) != 0) return nullptr;
    return p;
#endif
}

void alignedFree64(void* pointer) {
    if (pointer == nullptr) return;
#if defined(_WIN32) || defined(_WIN64)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}

// ── Obj operator new/delete (uses slabAlloc) ──────────────────────

void* Obj::operator new(size_t size) {
    return reallocate(nullptr, 0, size);
}

void Obj::operator delete(void* p, size_t size) {
    reallocate(p, size, 0);
}

// ── freeObject ────────────────────────────────────────────────────

void freeObject(Obj* object) {
    if (object == nullptr) return;
    switch (object->type) {
        case OBJ_CLOSURE: {
            auto* closure = static_cast<ObjClosure*>(object);
            if (closure->function != nullptr) {
                dropObject(static_cast<Obj*>(closure->function));
            }
            for (int i = 0; i < closure->upvalueCount; i++) {
                if (closure->upvalues[i] != nullptr) {
                    dropObject(static_cast<Obj*>(closure->upvalues[i]));
                }
            }
            FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
            FREE(ObjClosure, object);
            break;
        }
        case OBJ_FUNCTION: {
            auto* function = static_cast<ObjFunction*>(object);
            if (function->name != nullptr) {
                dropObject(static_cast<Obj*>(function->name));
            }
            if (function->paramTypes != nullptr) {
                for (int i = 0; i < function->arity; i++) {
                    if (function->paramTypes[i] != nullptr) {
                        dropObject(static_cast<Obj*>(function->paramTypes[i]));
                    }
                }
                FREE_ARRAY(ObjString*, function->paramTypes, UINT8_COUNT);
            }
            function->chunk.free();
            FREE(ObjFunction, object);
            break;
        }
        case OBJ_INSTANCE: {
            auto* instance = static_cast<ObjInstance*>(object);
            for (int i = 0; i < instance->denseCount; i++) {
                dropValue(instance->denseFields[i].value);
            }
            instance->fields.free();
            FREE(ObjInstance, object);
            break;
        }
        case OBJ_NATIVE:
            FREE(ObjNative, object);
            break;
        case OBJ_STRING: {
            FREE(ObjString, object);
            break;
        }
        case OBJ_STRING_BUFFER: {
            auto* buf = static_cast<ObjStringBuffer*>(object);
            std::free(buf->chars);
            FREE(ObjStringBuffer, buf);
            break;
        }
        case OBJ_STRUCT: {
            auto* structType = static_cast<ObjStruct*>(object);
            if (structType->name != nullptr) {
                dropObject(static_cast<Obj*>(structType->name));
            }
            FREE(ObjStruct, object);
            break;
        }
        case OBJ_UPVALUE: {
            auto* upvalue = static_cast<ObjUpvalue*>(object);
            if (upvalue->location == &upvalue->closed) {
                dropValue(upvalue->closed);
            }
            FREE(ObjUpvalue, object);
            break;
        }

        case OBJ_ARRAY: {
            auto* arr = static_cast<ObjArray*>(object);
            for (int i = 0; i < arr->count; i++) {
                dropValue(arr->values[i]);
            }
            FREE_ARRAY(Value, arr->values, arr->capacity);
            FREE(ObjArray, arr);
            break;
        }

        case OBJ_BUFFER: {
            auto* buf = static_cast<ObjBuffer*>(object);
            if (buf->data != nullptr) {
#if defined(_WIN32) || defined(_WIN64)
                if (buf->allocKind == 1) {
                    VirtualFree(buf->data, 0, MEM_RELEASE);
                } else {
                    alignedFree64(buf->data);
                }
#else
                alignedFree64(buf->data);
#endif
                buf->data = nullptr;
            }
            FREE(ObjBuffer, buf);
            break;
        }

        case OBJ_STACK: {
            auto* st = static_cast<ObjStack*>(object);
            for (Value v : st->data) dropValue(v);
            st->data.~vector();
            FREE(ObjStack, st);
            break;
        }

        case OBJ_LIST: {
            auto* li = static_cast<ObjList*>(object);
            for (Value v : li->data) dropValue(v);
            li->data.~list();
            FREE(ObjList, li);
            break;
        }

        case OBJ_HEAP: {
            auto* hp = static_cast<ObjHeap*>(object);
            for (Value v : hp->data) dropValue(v);
            hp->data.~vector();
            FREE(ObjHeap, hp);
            break;
        }

        case OBJ_RBTREE: {
            auto* rb = static_cast<ObjRBTree*>(object);
            for (auto& pair : rb->map) {
                dropValue(pair.first);
                dropValue(pair.second);
            }
            rb->map.~map();
            FREE(ObjRBTree, rb);
            break;
        }

        case OBJ_BTREE: {
            auto* bt = static_cast<ObjBTree*>(object);
            btreeFreeNodes(bt->root);
            FREE(ObjBTree, bt);
            break;
        }
    }
}
