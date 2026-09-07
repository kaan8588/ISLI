#include <cmath>
#include <cstdio>
#include <cstring>

#include "memory.hpp"
#include "object.hpp"
#include "value.hpp"

// ── ValueArray ─────────────────────────────────────────────────────

void ValueArray::init() {
    values   = nullptr;
    capacity = 0;
    count    = 0;
}

void ValueArray::write(Value value) {
    if (capacity < count + 1) {
        int oldCapacity = capacity;
        capacity = GROW_CAPACITY(oldCapacity);
        values = GROW_ARRAY(Value, values, oldCapacity, capacity);
    }

    values[count] = value;
    count++;
}

void ValueArray::free() {
    for (int i = 0; i < count; i++) {
        dropValue(values[i]);
    }
    FREE_ARRAY(Value, values, capacity);
    init();
}

// ── printValue ─────────────────────────────────────────────────────

void printValue(Value value) {
#ifdef NAN_BOXING
    if (IS_BOOL(value)) {
        printf(AS_BOOL(value) ? "true" : "false");
    } else if (IS_NIL(value)) {
        printf("NULL");
    } else if (IS_NUMBER(value)) {
        double num = AS_NUMBER(value);
        if (std::isfinite(num) && std::floor(num) == num && std::fabs(num) < 9007199254740992.0) {
            printf("%lld", static_cast<long long>(num));
        } else {
            printf("%g", num);
        }
    } else if (IS_POINTER(value)) {
        printf("<pointer %p>", static_cast<void*>(AS_POINTER(value)));
    } else if (IS_OBJ(value)) {
        printObject(value);
    }
#else
    switch (value.type) {
        case VAL_BOOL:
            printf(AS_BOOL(value) ? "true" : "false");
            break;
        case VAL_NIL:
            printf("NULL");
            break;
        case VAL_NUMBER: {
            double num = AS_NUMBER(value);
            if (std::isfinite(num) && std::floor(num) == num && std::fabs(num) < 9007199254740992.0) {
                printf("%lld", static_cast<long long>(num));
            } else {
                printf("%g", num);
            }
            break;
        }
        case VAL_POINTER:
            printf("<pointer %p>", static_cast<void*>(AS_POINTER(value)));
            break;
        case VAL_OBJ:
            printObject(value);
            break;
    }
#endif
}

// ── Helper: content-based string equality ─────────────────────────
static bool objsEqual(Obj* a, Obj* b) {
    if (a == b) return true; // Same address → trivially equal
    if (a->type != b->type) return false;
    if (a->type == OBJ_STRING) {
        auto* sa = static_cast<ObjString*>(a);
        auto* sb = static_cast<ObjString*>(b);
        return sa->length == sb->length &&
               sa->hash   == sb->hash   &&
               std::memcmp(sa->chars, sb->chars, sa->length) == 0;
    }
    return false; // Non-string objects: identity comparison already failed
}

// ── valuesEqual ────────────────────────────────────────────────────

bool valuesEqual(Value a, Value b) {
#ifdef NAN_BOXING
    if (a.bits == b.bits) {
        if (IS_NUMBER(a) && std::isnan(AS_NUMBER(a))) return false;
        return true;
    }
    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        return AS_NUMBER(a) == AS_NUMBER(b);
    }
    if (IS_OBJ(a) && IS_OBJ(b)) {
        return objsEqual(AS_OBJ(a), AS_OBJ(b));
    }
    return false;
#else
    if (std::memcmp(&a, &b, sizeof(Value)) == 0) return true;
    if (a.type != b.type) return false;
    switch (a.type) {
        case VAL_BOOL:   return AS_BOOL(a) == AS_BOOL(b);
        case VAL_NIL:    return true;
        case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
        case VAL_OBJ:    return objsEqual(AS_OBJ(a), AS_OBJ(b));
        default:         return false;
    }
#endif
}

