#ifndef ISLI_OBJECT_HPP
#define ISLI_OBJECT_HPP

#include <atomic>
#include <climits>
#include <new>
#include <vector>
#include <list>
#include <map>
#include "common.hpp"
#include "chunk.hpp"
#include "table.hpp"
#include "value.hpp"

// ── Type tag enum ──────────────────────────────────────────────────
enum ObjType {
    OBJ_CLOSURE,
    OBJ_FUNCTION,
    OBJ_INSTANCE,
    OBJ_NATIVE,
    OBJ_STRING,
    OBJ_STRING_BUFFER,
    OBJ_STRUCT,
    OBJ_UPVALUE,
    OBJ_ARRAY,
    OBJ_STACK,
    OBJ_LIST,
    OBJ_HEAP,
    OBJ_RBTREE,
    OBJ_BTREE,
    OBJ_BUFFER,
};

// ── Base object — NO virtual functions ─────────────────────────────
static constexpr int32_t RC_IMMORTAL = INT32_MIN / 2;

struct Obj {
    ObjType type;
    std::atomic<int32_t> refCount{RC_IMMORTAL};
    Obj* next = nullptr;

    // Custom allocator: all Obj (and derived) go through slabAlloc
    void* operator new(size_t size);
    void* operator new(size_t, void* ptr) noexcept { return ptr; }
    void  operator delete(void* p, size_t size);
    void  operator delete(void*, void*) noexcept {}
};

// ── Convenience macros (kept for compatibility in VM dispatch) ─────
#define OBJ_TYPE(value)        (AS_OBJ(value)->type)

#define IS_CLOSURE(value)      isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value)     isObjType(value, OBJ_FUNCTION)
#define IS_INSTANCE(value)     isObjType(value, OBJ_INSTANCE)
#define IS_NATIVE(value)       isObjType(value, OBJ_NATIVE)
#define IS_STRING(value)       isObjType(value, OBJ_STRING)
#define IS_STRUCT(value)       isObjType(value, OBJ_STRUCT)
#define IS_UPVALUE(value)      isObjType(value, OBJ_UPVALUE)
#define IS_ARRAY(value)        isObjType(value, OBJ_ARRAY)
#define IS_BUFFER(value)       isObjType(value, OBJ_BUFFER)
#define IS_STACK(value)        isObjType(value, OBJ_STACK)
#define IS_LIST(value)         isObjType(value, OBJ_LIST)
#define IS_HEAP(value)         isObjType(value, OBJ_HEAP)
#define IS_RBTREE(value)       isObjType(value, OBJ_RBTREE)
#define IS_BTREE(value)        isObjType(value, OBJ_BTREE)

#define AS_CLOSURE(value)      (static_cast<ObjClosure*>(AS_OBJ(value)))
#define AS_FUNCTION(value)     (static_cast<ObjFunction*>(AS_OBJ(value)))
#define AS_INSTANCE(value)     (static_cast<ObjInstance*>(AS_OBJ(value)))
#define AS_NATIVE(value)       (static_cast<ObjNative*>(AS_OBJ(value))->function)
#define AS_STRING(value)       (static_cast<ObjString*>(AS_OBJ(value)))
#define AS_CSTRING(value)      (static_cast<ObjString*>(AS_OBJ(value))->chars)
#define AS_STRUCT(value)       (static_cast<ObjStruct*>(AS_OBJ(value)))
#define AS_UPVALUE(value)      (static_cast<ObjUpvalue*>(AS_OBJ(value)))
#define AS_ARRAY(value)        (static_cast<ObjArray*>(AS_OBJ(value)))
#define AS_BUFFER(value)       (static_cast<ObjBuffer*>(AS_OBJ(value)))
#define AS_STACK(value)        (static_cast<ObjStack*>(AS_OBJ(value)))
#define AS_LIST(value)         (static_cast<ObjList*>(AS_OBJ(value)))
#define AS_HEAP(value)         (static_cast<ObjHeap*>(AS_OBJ(value)))
#define AS_RBTREE(value)       (static_cast<ObjRBTree*>(AS_OBJ(value)))
#define AS_BTREE(value)        (static_cast<ObjBTree*>(AS_OBJ(value)))

#define INSTANCE_DENSE_MAX 8

// ── Derived object types (inheritance, no vtable) ─────────────────

struct ObjFunction : public Obj {
    int arity        = 0;
    int upvalueCount = 0;
    Chunk chunk;
    ObjString* name  = nullptr;
    ObjString** paramTypes = nullptr;
    void* jitNative  = nullptr;
    int hotness      = 0;
    bool isQueued    = false;
    bool jitDeclined = false;
};

using NativeFn = Value (*)(int argCount, Value* args);

struct ObjNative : public Obj {
    NativeFn function = nullptr;
};

// ── Ref-counted backing storage for strings ───────────────────────
struct ObjStringBuffer : public Obj {
    int capacity = 0;
    char* chars  = nullptr;
};

// ── String View (Zero-copy Slice) ─────────────────────────────────
struct ObjString : public Obj {
    ObjStringBuffer* buffer = nullptr;
    const char* chars       = nullptr;
    int length              = 0;
    uint32_t hash           = 0;
};

struct ObjUpvalue : public Obj {
    Value* location = nullptr;
    Value closed;
    ObjUpvalue* next = nullptr;
};

struct ObjClosure : public Obj {
    ObjFunction* function   = nullptr;
    ObjUpvalue** upvalues   = nullptr;
    int upvalueCount        = 0;
};

struct ObjStruct : public Obj {
    ObjString* name = nullptr;
};

struct DenseField {
    ObjString* name = nullptr;
    Value value;
};

struct ObjInstance : public Obj {
    ObjStruct* structType = nullptr;
    isli::SpinLock lock;
    uint8_t denseCount = 0;
    DenseField denseFields[INSTANCE_DENSE_MAX];
    Table fields;
};

struct ObjArray : public Obj {
    int count    = 0;
    int capacity = 0;
    Value* values = nullptr;
    isli::SpinLock lock;
};

// ── Typed numeric buffer (64-byte aligned, unboxed contiguous storage) ───
enum BufferElemType : uint8_t {
    BUF_F32 = 0,
    BUF_F64 = 1,
    BUF_I32 = 2,
    BUF_U8  = 3
};

struct ObjBuffer : public Obj {
    int count = 0;                     // offset 16 (matches ObjArray::count)
    BufferElemType elemType = BUF_F64; // offset 20
    uint8_t elemShift = 3;             // offset 21: log2(elemSize): f32->2, f64->3, i32->2, u8->0
    uint8_t allocKind = 0;             // offset 22: 0 = aligned, 1 = VirtualAlloc
    uint8_t reserved = 0;              // offset 23
    void* data = nullptr;              // offset 24 (matches ObjArray::values)
    size_t byteCapacity = 0;           // offset 32
    isli::SpinLock lock;               // offset 40
};

inline size_t bufferElemSize(BufferElemType t) {
    return t == BUF_F64 ? 8 : (t == BUF_U8 ? 1 : 4);
}

inline double bufferLoad(const ObjBuffer* b, int i) {
    switch (b->elemType) {
        case BUF_F32: return static_cast<double>(static_cast<const float*>(b->data)[i]);
        case BUF_F64: return static_cast<const double*>(b->data)[i];
        case BUF_I32: return static_cast<double>(static_cast<const int32_t*>(b->data)[i]);
        case BUF_U8:  return static_cast<double>(static_cast<const uint8_t*>(b->data)[i]);
    }
    return 0.0;
}

inline void bufferStore(ObjBuffer* b, int i, double v) {
    switch (b->elemType) {
        case BUF_F32: static_cast<float*>(b->data)[i] = static_cast<float>(v); break;
        case BUF_F64: static_cast<double*>(b->data)[i] = v; break;
        case BUF_I32: {
            int32_t out;
            if (v != v) out = 0;
            else if (v >= 2147483647.0) out = INT32_MAX;
            else if (v <= -2147483648.0) out = INT32_MIN;
            else out = static_cast<int32_t>(v);
            static_cast<int32_t*>(b->data)[i] = out;
            break;
        }
        case BUF_U8: {
            uint8_t out;
            if (v != v || v <= 0.0) out = 0;
            else if (v >= 255.0) out = 255;
            else out = static_cast<uint8_t>(v);
            static_cast<uint8_t*>(b->data)[i] = out;
            break;
        }
    }
}

struct ObjStack : public Obj {
    std::vector<Value> data;
    isli::SpinLock lock;
};

struct ObjList : public Obj {
    std::list<Value> data;
    isli::SpinLock lock;
};

struct ObjHeap : public Obj {
    bool isMinHeap = true;
    std::vector<Value> data;
    isli::SpinLock lock;
};

struct ValueLess {
    bool operator()(const Value& a, const Value& b) const;
};

struct ObjRBTree : public Obj {
    std::map<Value, Value, ValueLess> map;
    isli::SpinLock lock;
};

struct BTreeNode {
    bool isLeaf = true;
    std::vector<Value> keys;
    std::vector<Value> values;
    std::vector<BTreeNode*> children;
};

struct ObjBTree : public Obj {
    int degree = 3;
    int count  = 0;
    BTreeNode* root = nullptr;
    isli::SpinLock lock;
};

// ── Factory functions ─────────────────────────────────────────────
ObjClosure*      newClosure(ObjFunction* function);
ObjFunction*     newFunction();
ObjInstance*     newInstance(ObjStruct* type);
ObjNative*       newNative(NativeFn function);
ObjStringBuffer* newStringBuffer(char* chars, int capacity);
ObjString*       takeString(char* chars, int length);
ObjString*       copyString(const char* chars, int length);
ObjString*       sliceString(ObjString* original, int offset, int length);
ObjStruct*       newStruct(ObjString* name);
ObjUpvalue*      newUpvalue(Value* slot);
ObjArray*        newArray(int count, Value initialValue);
void             arrayPush(ObjArray* array, Value value);
ObjBuffer*       newBuffer(BufferElemType type, int count, double initialValue);
ObjStack*        newStack();
ObjList*         newList();
ObjHeap*         newHeap(bool isMinHeap);
ObjRBTree*       newRBTree();
ObjBTree*        newBTree(int degree);

void             btreeInsert(ObjBTree* tree, Value key, Value value);
bool             btreeGet(ObjBTree* tree, Value key, Value* outValue);
bool             btreeHas(ObjBTree* tree, Value key);
bool             btreeRemove(ObjBTree* tree, Value key);
void             btreeFreeNodes(BTreeNode* node);
BTreeNode*       btreeCopyNodes(BTreeNode* node);
ObjBTree*        copyBTree(ObjBTree* src);

void             printObject(Value value);

// ── Tag check ─────────────────────────────────────────────────────
inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif // ISLI_OBJECT_HPP
