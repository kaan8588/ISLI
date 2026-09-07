#ifndef ISLI_TABLE_HPP
#define ISLI_TABLE_HPP

#include "common.hpp"
#include "value.hpp"

struct ObjString;

#define TABLE_TOMBSTONE reinterpret_cast<Value*>(static_cast<uintptr_t>(1))

struct Entry {
    ObjString* key = nullptr;
    Value* valuePtr = nullptr;
};

struct Table {
    int count    = 0;
    int capacity = 0;
    Entry* entries = nullptr;

    void init();
    void free();
    bool get(ObjString* key, Value* value);
    Value* getPtr(ObjString* key);
    bool set(ObjString* key, Value value);
    bool del(ObjString* key);
    void addAll(Table* to);

private:
    static Entry* findEntry(Entry* entries, int capacity, ObjString* key);
    void adjustCapacity(int capacity);
};

#endif // ISLI_TABLE_HPP
