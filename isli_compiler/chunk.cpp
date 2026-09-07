#include "chunk.hpp"
#include "memory.hpp"
#include "vm.hpp"

void Chunk::init() {
    count    = 0;
    capacity = 0;
    code     = nullptr;
    lines    = nullptr;
    constants.init();
    globalCache = nullptr;
    globalCacheCapacity = 0;
}

void Chunk::free() {
    FREE_ARRAY(uint8_t, code, capacity);
    FREE_ARRAY(int, lines, capacity);
    if (globalCache != nullptr) {
        FREE_ARRAY(Value*, globalCache, globalCacheCapacity);
    }
    constants.free();
    init();
}

void Chunk::write(uint8_t byte, int line) {
    if (capacity < count + 1) {
        int oldCapacity = capacity;
        capacity = GROW_CAPACITY(oldCapacity);
        code  = GROW_ARRAY(uint8_t, code, oldCapacity, capacity);
        lines = GROW_ARRAY(int, lines, oldCapacity, capacity);
    }

    code[count]  = byte;
    lines[count] = line;
    count++;
}

int Chunk::addConstant(Value value) {
    for (int i = 0; i < constants.count; i++) {
        if (valuesEqual(constants.values[i], value)) {
            dropValue(value);
            return i;
        }
    }
    constants.write(value);
    if (constants.count > globalCacheCapacity) {
        int oldCap = globalCacheCapacity;
        globalCacheCapacity = constants.capacity;
        globalCache = GROW_ARRAY(Value*, globalCache, oldCap, globalCacheCapacity);
        for (int i = oldCap; i < globalCacheCapacity; i++) {
            globalCache[i] = nullptr;
        }
    }
    return constants.count - 1;
}
