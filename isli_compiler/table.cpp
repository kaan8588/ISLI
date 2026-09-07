#include <cstdlib>
#include <cstring>

#include "memory.hpp"
#include "table.hpp"
#include "object.hpp"
#include "value.hpp"
#include "vm.hpp"

#define TABLE_MAX_LOAD 0.75

void Table::init() {
    count    = 0;
    capacity = 0;
    entries  = nullptr;
}

void Table::free() {
    FREE_ARRAY(Entry, entries, capacity);
    init();
}

Entry* Table::findEntry(Entry* entries, int capacity, ObjString* key) {
    if (capacity == 0) return nullptr;
    uint32_t index = key->hash & (capacity - 1);
    Entry* tombstone = nullptr;

    for (;;) {
        Entry* entry = &entries[index];
        if (entry->key == nullptr) {
            if (entry->valuePtr == nullptr) {
                return tombstone != nullptr ? tombstone : entry;
            } else {
                if (tombstone == nullptr) tombstone = entry;
            }
        } else if (entry->key == key || 
                   (entry->key->length == key->length && 
                    entry->key->hash == key->hash && 
                    std::memcmp(entry->key->chars, key->chars, key->length) == 0)) {
            return entry;
        }

        index = (index + 1) & (capacity - 1);
    }
}

bool Table::get(ObjString* key, Value* value) {
    if (count == 0) return false;

    Entry* entry = findEntry(entries, capacity, key);
    if (entry->key == nullptr) return false;

    *value = *entry->valuePtr;
    return true;
}

Value* Table::getPtr(ObjString* key) {
    if (count == 0) return nullptr;
    Entry* entry = findEntry(entries, capacity, key);
    if (entry->key == nullptr) return nullptr;
    return entry->valuePtr;
}

void Table::adjustCapacity(int newCapacity) {
    Entry* newEntries = ALLOCATE(Entry, newCapacity);
    for (int i = 0; i < newCapacity; i++) {
        newEntries[i].key      = nullptr;
        newEntries[i].valuePtr = nullptr;
    }

    count = 0;
    for (int i = 0; i < capacity; i++) {
        Entry* entry = &entries[i];
        if (entry->key == nullptr) continue;

        Entry* dest = findEntry(newEntries, newCapacity, entry->key);
        dest->key      = entry->key;
        dest->valuePtr = entry->valuePtr;
        count++;
    }

    FREE_ARRAY(Entry, entries, capacity);
    entries  = newEntries;
    capacity = newCapacity;
}

bool Table::set(ObjString* key, Value value) {
    if ((count + 1) * 4 > capacity * 3) {
        int newCap = GROW_CAPACITY(capacity);
        adjustCapacity(newCap);
    }

    Entry* entry = findEntry(entries, capacity, key);
    bool isNewKey = entry->key == nullptr;
    if (isNewKey && entry->valuePtr == nullptr) count++;

    if (!isNewKey) {
        dropValue(*entry->valuePtr);
    } else {
        entry->key = static_cast<ObjString*>(cloneObject(static_cast<Obj*>(key)));
        entry->valuePtr = static_cast<Value*>(slabAlloc(sizeof(Value)));
    }

    *entry->valuePtr = value;
    return isNewKey;
}

bool Table::del(ObjString* key) {
    if (count == 0) return false;

    Entry* entry = findEntry(entries, capacity, key);
    if (entry->key == nullptr) return false;

    ObjString* oldKey = entry->key;
    Value* oldValPtr  = entry->valuePtr;

    entry->key      = nullptr;
    entry->valuePtr = TABLE_TOMBSTONE;

    dropObject(static_cast<Obj*>(oldKey));
    if (oldValPtr != nullptr && oldValPtr != TABLE_TOMBSTONE) {
        dropValue(*oldValPtr);
        slabFree(oldValPtr, sizeof(Value));
    }
    return true;
}

void Table::addAll(Table* to) {
    for (int i = 0; i < capacity; i++) {
        Entry* entry = &entries[i];
        if (entry->key != nullptr) {
            to->set(entry->key, cloneValue(*entry->valuePtr));
        }
    }
}
