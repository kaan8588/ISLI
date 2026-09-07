#ifndef ISLI_VALUE_HPP
#define ISLI_VALUE_HPP

#include <cstring>
#include <cstdio>
#include "common.hpp"

// Forward declarations
struct Obj;
struct ObjString;

// ── NaN-Boxing Value ───────────────────────────────────────────────
#ifdef NAN_BOXING

static constexpr uint64_t SIGN_BIT   = 0x8000000000000000ULL;
static constexpr uint64_t QNAN       = 0x7ffc000000000000ULL;
static constexpr uint64_t PTR_MASK   = 0x0000ffffffffffffULL;

static constexpr uint64_t TAG_NIL     = 1;
static constexpr uint64_t TAG_FALSE   = 2;
static constexpr uint64_t TAG_TRUE    = 3;
static constexpr uint64_t TAG_POINTER = 0x0002000000000000ULL;

struct Value {
    uint64_t bits;

    Value() : bits(QNAN | TAG_NIL) {}
    explicit Value(uint64_t b) : bits(b) {}

    // ── Type checks ───────────────────────────
    inline bool isBool()   const { return (bits | 1) == (QNAN | TAG_TRUE); }
    inline bool isNil()    const { return bits == (QNAN | TAG_NIL); }
    inline bool isNumber() const { return (bits & QNAN) != QNAN; }
    inline bool isObj()    const { return (bits & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT); }
    inline bool isPointer()const { return (bits & (QNAN | SIGN_BIT | TAG_POINTER)) == (QNAN | TAG_POINTER); }

    // ── Unwrappers ────────────────────────────
    inline bool asBool() const { return bits == (QNAN | TAG_TRUE); }

    inline double asNumber() const {
        double num;
        std::memcpy(&num, &bits, sizeof(uint64_t));
        return num;
    }

    inline Obj* asObj() const {
        uintptr_t ptrVal = static_cast<uintptr_t>(bits & PTR_MASK);
        if (ptrVal & 0x0000800000000000ULL) ptrVal |= 0xffff000000000000ULL;
        return reinterpret_cast<Obj*>(ptrVal);
    }

    inline Value* asPointer() const {
        uintptr_t ptrVal = static_cast<uintptr_t>(bits & PTR_MASK);
        if (ptrVal & 0x0000800000000000ULL) ptrVal |= 0xffff000000000000ULL;
        return reinterpret_cast<Value*>(ptrVal);
    }

    // ── Factories ─────────────────────────────
    static inline Value makeBool(bool b) { return Value(b ? (QNAN | TAG_TRUE) : (QNAN | TAG_FALSE)); }
    static inline Value makeFalse()      { return Value(QNAN | TAG_FALSE); }
    static inline Value makeTrue()       { return Value(QNAN | TAG_TRUE); }
    static inline Value makeNil()        { return Value(QNAN | TAG_NIL); }

    static inline Value makeNumber(double num) {
        Value v;
        std::memcpy(&v.bits, &num, sizeof(double));
        if ((v.bits & QNAN) == QNAN) {
            v.bits = 0x7ff8000000000000ULL; // Canonical Quiet NaN (never collides with QNAN tag)
        }
        return v;
    }

    static inline Value makeObj(Obj* obj) {
        return Value(SIGN_BIT | QNAN | (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(obj)) & PTR_MASK));
    }

    static inline Value makePointer(Value* ptr) {
        return Value(QNAN | TAG_POINTER | (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr)) & PTR_MASK));
    }

    // ── Comparison ────────────────────────────
    inline bool operator==(Value other) const { return bits == other.bits; }
    inline bool operator!=(Value other) const { return bits != other.bits; }
};

// ── Backward-compatible macros (used inside VM dispatch loop etc.) ─
#define IS_BOOL(value)    ((value).isBool())
#define IS_NIL(value)     ((value).isNil())
#define IS_NUMBER(value)  ((value).isNumber())
#define IS_OBJ(value)     ((value).isObj())
#define IS_POINTER(value) ((value).isPointer())

#define AS_BOOL(value)    ((value).asBool())
#define AS_NUMBER(value)  ((value).asNumber())
#define AS_OBJ(value)     ((value).asObj())
#define AS_POINTER(value) ((value).asPointer())

#define BOOL_VAL(b)       (Value::makeBool(b))
#define FALSE_VAL         (Value::makeFalse())
#define TRUE_VAL          (Value::makeTrue())
#define NIL_VAL           (Value::makeNil())
#define NUMBER_VAL(num)   (Value::makeNumber(num))
#define OBJ_VAL(obj)      (Value::makeObj(reinterpret_cast<Obj*>(obj)))
#define POINTER_VAL(ptr)  (Value::makePointer(reinterpret_cast<Value*>(ptr)))

#else // ── Tagged-union fallback ────────────────────────────────────

enum ValueType {
    VAL_BOOL,
    VAL_NIL,
    VAL_NUMBER,
    VAL_OBJ,
    VAL_POINTER
};

struct Value {
    ValueType type;
    union {
        bool boolean;
        double number;
        Obj* obj;
        Value* pointer;
    } as;
};

#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_NUMBER)
#define IS_OBJ(value)     ((value).type == VAL_OBJ)
#define IS_POINTER(value) ((value).type == VAL_POINTER)

#define AS_BOOL(value)    ((value).as.boolean)
#define AS_NUMBER(value)  ((value).as.number)
#define AS_OBJ(value)     ((value).as.obj)
#define AS_POINTER(value) ((value).as.pointer)

#define BOOL_VAL(b)       (Value{VAL_BOOL, {.boolean = (b)}})
#define NIL_VAL           (Value{VAL_NIL, {.number = 0}})
#define NUMBER_VAL(num)   (Value{VAL_NUMBER, {.number = (num)}})
#define OBJ_VAL(object)   (Value{VAL_OBJ, {.obj = reinterpret_cast<Obj*>(object)}})
#define POINTER_VAL(ptr)  (Value{VAL_POINTER, {.pointer = reinterpret_cast<Value*>(ptr)}})

#endif // NAN_BOXING

// ── ValueArray (custom dynamic array, no STL) ─────────────────────
struct ValueArray {
    int capacity = 0;
    int count    = 0;
    Value* values = nullptr;

    void init();
    void write(Value value);
    void free();
};

// ── Drop and Clone (Value-level wrappers) ───────────────────────
void dropObject(Obj* object);
Obj* cloneObject(Obj* object);

inline void dropValue(Value value) {
    if (IS_OBJ(value)) dropObject(AS_OBJ(value));
}

inline Value cloneValue(Value value) {
    if (IS_OBJ(value)) {
        if (cloneObject(AS_OBJ(value)) == nullptr) return NIL_VAL;
    }
    return value;
}

bool valuesEqual(Value a, Value b);
void printValue(Value value);

#endif // ISLI_VALUE_HPP
