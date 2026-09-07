#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

#include "memory.hpp"
#include "object.hpp"
#include "table.hpp"
#include "value.hpp"
#include "vm.hpp"

static_assert(sizeof(Obj) == 16, "Obj must stay 16 bytes so array/buffer JIT offsets remain valid");
static_assert(offsetof(ObjArray, count) == 16, "ObjArray::count offset must be 16");
static_assert(offsetof(ObjArray, values) == 24, "ObjArray::values offset must be 24");
static_assert(offsetof(ObjBuffer, count) == 16, "ObjBuffer::count offset must be 16");
static_assert(offsetof(ObjBuffer, data) == 24, "ObjBuffer::data offset must be 24");
static_assert(offsetof(ObjClosure, upvalues) == 24, "ObjClosure::upvalues offset must be 24");
static_assert(offsetof(ObjUpvalue, location) == 16, "ObjUpvalue::location offset must be 16");

static inline bool isRefCountedType(ObjType type) {
    return type == OBJ_BUFFER || type == OBJ_ARRAY || type == OBJ_CLOSURE;
}

static Obj* allocateObject(size_t size, ObjType type) {
    auto* object = static_cast<Obj*>(reallocate(nullptr, 0, size));
    object->type = type;
    object->next = nullptr;
    if (isRefCountedType(type)) {
        object->refCount.store(1, std::memory_order_relaxed);
    } else {
        object->refCount.store(RC_IMMORTAL, std::memory_order_relaxed);
    }

    if (type == OBJ_FUNCTION || type == OBJ_NATIVE || type == OBJ_STRUCT) {
        object->next = vm.unownedObjects;
        vm.unownedObjects = object;
    }

    return object;
}

void dropObject(Obj* object) {
    if (object == nullptr) return;
    int32_t n = object->refCount.load(std::memory_order_relaxed);
    if (n <= RC_IMMORTAL / 2) return;
    n = object->refCount.fetch_sub(1, std::memory_order_acq_rel);
    if (n == 1) {
        freeObject(object);
    }
}

Obj* cloneObject(Obj* object) {
    if (object == nullptr) return nullptr;
    for (;;) {
        int32_t n = object->refCount.load(std::memory_order_acquire);
        if (n <= RC_IMMORTAL / 2) return object;
        if (n <= 0) return nullptr;
        if (object->refCount.compare_exchange_weak(
                n, n + 1, std::memory_order_relaxed, std::memory_order_acquire)) {
            return object;
        }
    }
}

// ── Factory: ObjClosure ───────────────────────────────────────────
ObjClosure* newClosure(ObjFunction* function) {
    auto** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
    for (int i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = nullptr;
    }

    void* mem = allocateObject(sizeof(ObjClosure), OBJ_CLOSURE);
    auto* closure = new (mem) ObjClosure();
    closure->type = OBJ_CLOSURE;
    closure->function = function;
    closure->upvalues     = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}

// ── Factory: ObjFunction ──────────────────────────────────────────
ObjFunction* newFunction() {
    void* mem = allocateObject(sizeof(ObjFunction), OBJ_FUNCTION);
    auto* function = new (mem) ObjFunction();
    function->type = OBJ_FUNCTION;
    function->arity        = 0;
    function->upvalueCount = 0;
    function->name         = nullptr;
    function->chunk.init();
    return function;
}

// ── Factory: ObjStruct ────────────────────────────────────────────
ObjStruct* newStruct(ObjString* name) {
    void* mem = allocateObject(sizeof(ObjStruct), OBJ_STRUCT);
    auto* structType = new (mem) ObjStruct();
    structType->type = OBJ_STRUCT;
    structType->name = name;
    return structType;
}

// ── Factory: ObjInstance ──────────────────────────────────────────
ObjInstance* newInstance(ObjStruct* structType) {
    void* mem = allocateObject(sizeof(ObjInstance), OBJ_INSTANCE);
    auto* instance = new (mem) ObjInstance();
    instance->type = OBJ_INSTANCE;
    instance->structType = structType;
    new (&instance->lock) isli::SpinLock();
    instance->denseCount = 0;
    for (int i = 0; i < INSTANCE_DENSE_MAX; i++) {
        instance->denseFields[i].name = nullptr;
        instance->denseFields[i].value = NIL_VAL;
    }
    instance->fields.init();
    return instance;
}

// ── Factory: ObjNative ────────────────────────────────────────────
ObjNative* newNative(NativeFn function) {
    void* mem = allocateObject(sizeof(ObjNative), OBJ_NATIVE);
    auto* native = new (mem) ObjNative();
    native->type = OBJ_NATIVE;
    native->function = function;
    return native;
}

// ── String & String View Helpers ──────────────────────────────────
static uint32_t hashString(const char* key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= static_cast<uint8_t>(key[i]);
        hash *= 16777619;
    }
    return hash;
}

ObjStringBuffer* newStringBuffer(char* chars, int capacity) {
    void* mem = allocateObject(sizeof(ObjStringBuffer), OBJ_STRING_BUFFER);
    auto* buffer = new (mem) ObjStringBuffer();
    buffer->type = OBJ_STRING_BUFFER;
    buffer->capacity = capacity;
    buffer->chars = chars;
    return buffer;
}

static ObjString* allocateString(ObjStringBuffer* buffer, const char* chars, int length, uint32_t hash) {
    void* mem = allocateObject(sizeof(ObjString), OBJ_STRING);
    auto* string = new (mem) ObjString();
    string->type = OBJ_STRING;
    string->buffer = buffer;
    string->chars  = chars;
    string->length = length;
    string->hash   = hash;
    return string;
}

ObjString* takeString(char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjStringBuffer* buffer = newStringBuffer(chars, length + 1);
    return allocateString(buffer, chars, length, hash);
}

ObjString* copyString(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    char* heapChars = static_cast<char*>(std::malloc(length + 1));
    if (heapChars == nullptr) std::exit(1);
    std::memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    ObjStringBuffer* buffer = newStringBuffer(heapChars, length + 1);
    return allocateString(buffer, heapChars, length, hash);
}

ObjString* sliceString(ObjString* original, int offset, int length) {
    if (original == nullptr) return nullptr;
    if (offset < 0) offset = 0;
    if (offset > original->length) offset = original->length;
    if (length < 0) length = 0;
    if (offset + length > original->length) length = original->length - offset;

    const char* sliceChars = original->chars + offset;
    uint32_t hash = hashString(sliceChars, length);

    return allocateString(original->buffer, sliceChars, length, hash);
}

// ── Factory: ObjUpvalue ───────────────────────────────────────────
ObjUpvalue* newUpvalue(Value* slot) {
    void* mem = allocateObject(sizeof(ObjUpvalue), OBJ_UPVALUE);
    auto* upvalue = new (mem) ObjUpvalue();
    upvalue->type = OBJ_UPVALUE;
    upvalue->closed   = NIL_VAL;
    upvalue->location = slot;
    upvalue->next     = nullptr;
    return upvalue;
}

// ── Factory: ObjArray ─────────────────────────────────────────────
ObjArray* newArray(int count, Value initialValue) {
    void* mem = allocateObject(sizeof(ObjArray), OBJ_ARRAY);
    auto* arr = new (mem) ObjArray();
    arr->type = OBJ_ARRAY;
    new (&arr->lock) isli::SpinLock();
    arr->count = count;
    arr->capacity = count > 0 ? count : 8;
    arr->values = ALLOCATE(Value, arr->capacity);
    if (IS_NUMBER(initialValue)) {
        // SIMD AVX2 fast path for numeric arrays
        __m256d data = _mm256_set1_pd(AS_NUMBER(initialValue));
        int i = 0;
        for (; i + 4 <= count; i += 4) {
            _mm256_storeu_pd(reinterpret_cast<double*>(&arr->values[i]), data);
        }
        for (; i < count; i++) {
            arr->values[i] = initialValue;
        }
    } else if (IS_OBJ(initialValue)) {
        for (int i = 0; i < count; i++) {
            arr->values[i] = cloneValue(initialValue);
        }
    } else {
        // NIL, BOOL — plain copy
        for (int i = 0; i < count; i++) {
            arr->values[i] = initialValue;
        }
    }
    return arr;
}

void arrayPush(ObjArray* array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(Value, array->values, oldCapacity, array->capacity);
    }
    array->values[array->count++] = value;
}

// ── Factory: ObjBuffer ────────────────────────────────────────────
ObjBuffer* newBuffer(BufferElemType type, int count, double initialValue) {
    if (count < 0) count = 0;
    void* mem = allocateObject(sizeof(ObjBuffer), OBJ_BUFFER);
    auto* buf = new (mem) ObjBuffer();
    buf->type = OBJ_BUFFER;
    new (&buf->lock) isli::SpinLock();
    buf->count = count;
    buf->elemType = type;
    buf->elemShift = (type == BUF_F64) ? 3 : (type == BUF_U8 ? 0 : 2);
    buf->allocKind = 0;

    size_t bytes = static_cast<size_t>(count) << buf->elemShift;
    buf->byteCapacity = (bytes + 63) & ~static_cast<size_t>(63);
#if defined(_WIN32) || defined(_WIN64)
    if (buf->byteCapacity >= 1048576) {
        buf->data = VirtualAlloc(nullptr, buf->byteCapacity, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        buf->allocKind = 1;
    } else {
        buf->data = alignedAlloc64(buf->byteCapacity);
        buf->allocKind = 0;
    }
#else
    buf->data = alignedAlloc64(buf->byteCapacity);
    buf->allocKind = 0;
#endif
    if (buf->data == nullptr) {
        fprintf(stderr, "Out of memory allocating buffer.\n");
        std::exit(1);
    }
    if (initialValue == 0.0) {
        std::memset(buf->data, 0, buf->byteCapacity);
    } else {
        for (int i = 0; i < count; i++) {
            bufferStore(buf, i, initialValue);
        }
    }
    return buf;
}

// ── ValueLess implementation ──────────────────────────────────────
bool ValueLess::operator()(const Value& a, const Value& b) const {
    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        return AS_NUMBER(a) < AS_NUMBER(b);
    }
    if (IS_STRING(a) && IS_STRING(b)) {
        ObjString* sa = AS_STRING(a);
        ObjString* sb = AS_STRING(b);
        int minLen = sa->length < sb->length ? sa->length : sb->length;
        int cmp = std::memcmp(sa->chars, sb->chars, minLen);
        if (cmp != 0) return cmp < 0;
        return sa->length < sb->length;
    }
#ifdef NAN_BOXING
    return a.bits < b.bits;
#else
    if (a.type != b.type) return a.type < b.type;
    return a.as.number < b.as.number;
#endif
}

// ── B-Tree Implementation ─────────────────────────────────────────
void btreeFreeNodes(BTreeNode* node) {
    if (node == nullptr) return;
    for (auto* child : node->children) {
        btreeFreeNodes(child);
    }
    for (size_t i = 0; i < node->keys.size(); i++) {
        dropValue(node->keys[i]);
        dropValue(node->values[i]);
    }
    delete node;
}

BTreeNode* btreeCopyNodes(BTreeNode* node) {
    if (node == nullptr) return nullptr;
    auto* copyNode = new BTreeNode();
    copyNode->isLeaf = node->isLeaf;
    copyNode->keys.reserve(node->keys.size());
    copyNode->values.reserve(node->values.size());
    for (size_t i = 0; i < node->keys.size(); i++) {
        copyNode->keys.push_back(cloneValue(node->keys[i]));
        copyNode->values.push_back(cloneValue(node->values[i]));
    }
    copyNode->children.reserve(node->children.size());
    for (auto* child : node->children) {
        copyNode->children.push_back(btreeCopyNodes(child));
    }
    return copyNode;
}

static void btreeSplitChild(BTreeNode* parent, int index, BTreeNode* fullChild, int degree) {
    auto* newSibling = new BTreeNode();
    newSibling->isLeaf = fullChild->isLeaf;

    int medianIdx = degree - 1;

    for (size_t j = medianIdx + 1; j < fullChild->keys.size(); j++) {
        newSibling->keys.push_back(fullChild->keys[j]);
        newSibling->values.push_back(fullChild->values[j]);
    }

    if (!fullChild->isLeaf) {
        for (size_t j = degree; j < fullChild->children.size(); j++) {
            newSibling->children.push_back(fullChild->children[j]);
        }
        fullChild->children.resize(degree);
    }

    Value medianKey = fullChild->keys[medianIdx];
    Value medianVal = fullChild->values[medianIdx];

    fullChild->keys.resize(medianIdx);
    fullChild->values.resize(medianIdx);

    parent->keys.insert(parent->keys.begin() + index, medianKey);
    parent->values.insert(parent->values.begin() + index, medianVal);
    parent->children.insert(parent->children.begin() + index + 1, newSibling);
}

static void btreeInsertNonFull(BTreeNode* node, Value key, Value value, int degree) {
    ValueLess less;
    int i = static_cast<int>(node->keys.size()) - 1;

    if (node->isLeaf) {
        while (i >= 0 && less(key, node->keys[i])) {
            i--;
        }
        if (i >= 0 && !less(key, node->keys[i]) && !less(node->keys[i], key)) {
            dropValue(node->values[i]);
            node->values[i] = value;
            dropValue(key);
            return;
        }
        node->keys.insert(node->keys.begin() + i + 1, key);
        node->values.insert(node->values.begin() + i + 1, value);
    } else {
        while (i >= 0 && less(key, node->keys[i])) {
            i--;
        }
        if (i >= 0 && !less(key, node->keys[i]) && !less(node->keys[i], key)) {
            dropValue(node->values[i]);
            node->values[i] = value;
            dropValue(key);
            return;
        }
        i++;
        if (static_cast<int>(node->children[i]->keys.size()) == 2 * degree - 1) {
            btreeSplitChild(node, i, node->children[i], degree);
            if (less(node->keys[i], key)) {
                i++;
            }
        }
        btreeInsertNonFull(node->children[i], key, value, degree);
    }
}

void btreeInsert(ObjBTree* tree, Value key, Value value) {
    tree->lock.lock();
    if (tree->root == nullptr) {
        tree->root = new BTreeNode();
        tree->root->keys.push_back(cloneValue(key));
        tree->root->values.push_back(cloneValue(value));
        tree->count = 1;
        tree->lock.unlock();
        return;
    }

    if (static_cast<int>(tree->root->keys.size()) == 2 * tree->degree - 1) {
        auto* newRoot = new BTreeNode();
        newRoot->isLeaf = false;
        newRoot->children.push_back(tree->root);
        btreeSplitChild(newRoot, 0, tree->root, tree->degree);
        tree->root = newRoot;
    }
    btreeInsertNonFull(tree->root, cloneValue(key), cloneValue(value), tree->degree);
    tree->count++;
    tree->lock.unlock();
}

static bool btreeSearchNode(BTreeNode* node, Value key, Value* outValue) {
    if (node == nullptr) return false;
    ValueLess less;
    int i = 0;
    while (i < static_cast<int>(node->keys.size()) && less(node->keys[i], key)) {
        i++;
    }
    if (i < static_cast<int>(node->keys.size()) && !less(key, node->keys[i]) && !less(node->keys[i], key)) {
        if (outValue != nullptr) *outValue = cloneValue(node->values[i]);
        return true;
    }
    if (node->isLeaf) return false;
    return btreeSearchNode(node->children[i], key, outValue);
}

bool btreeGet(ObjBTree* tree, Value key, Value* outValue) {
    tree->lock.lock();
    bool found = btreeSearchNode(tree->root, key, outValue);
    tree->lock.unlock();
    return found;
}

bool btreeHas(ObjBTree* tree, Value key) {
    tree->lock.lock();
    bool found = btreeSearchNode(tree->root, key, nullptr);
    tree->lock.unlock();
    return found;
}

static void btreeBorrowFromPrev(BTreeNode* node, int idx) {
    BTreeNode* child = node->children[idx];
    BTreeNode* sibling = node->children[idx - 1];

    child->keys.insert(child->keys.begin(), node->keys[idx - 1]);
    child->values.insert(child->values.begin(), node->values[idx - 1]);

    if (!child->isLeaf) {
        child->children.insert(child->children.begin(), sibling->children.back());
        sibling->children.pop_back();
    }

    node->keys[idx - 1] = sibling->keys.back();
    node->values[idx - 1] = sibling->values.back();
    sibling->keys.pop_back();
    sibling->values.pop_back();
}

static void btreeBorrowFromNext(BTreeNode* node, int idx) {
    BTreeNode* child = node->children[idx];
    BTreeNode* sibling = node->children[idx + 1];

    child->keys.push_back(node->keys[idx]);
    child->values.push_back(node->values[idx]);

    if (!child->isLeaf) {
        child->children.push_back(sibling->children.front());
        sibling->children.erase(sibling->children.begin());
    }

    node->keys[idx] = sibling->keys.front();
    node->values[idx] = sibling->values.front();
    sibling->keys.erase(sibling->keys.begin());
    sibling->values.erase(sibling->values.begin());
}

static void btreeMerge(BTreeNode* node, int idx, int degree) {
    (void)degree;
    BTreeNode* child = node->children[idx];
    BTreeNode* sibling = node->children[idx + 1];

    child->keys.push_back(node->keys[idx]);
    child->values.push_back(node->values[idx]);

    for (size_t i = 0; i < sibling->keys.size(); i++) {
        child->keys.push_back(sibling->keys[i]);
        child->values.push_back(sibling->values[i]);
    }
    if (!child->isLeaf) {
        for (size_t i = 0; i < sibling->children.size(); i++) {
            child->children.push_back(sibling->children[i]);
        }
    }

    node->keys.erase(node->keys.begin() + idx);
    node->values.erase(node->values.begin() + idx);
    node->children.erase(node->children.begin() + idx + 1);

    sibling->children.clear();
    delete sibling;
}

static bool btreeRemoveFromNode(BTreeNode* node, Value key, int degree) {
    ValueLess less;
    int idx = 0;
    while (idx < static_cast<int>(node->keys.size()) && less(node->keys[idx], key)) {
        idx++;
    }

    if (idx < static_cast<int>(node->keys.size()) && !less(key, node->keys[idx]) && !less(node->keys[idx], key)) {
        if (node->isLeaf) {
            dropValue(node->keys[idx]);
            dropValue(node->values[idx]);
            node->keys.erase(node->keys.begin() + idx);
            node->values.erase(node->values.begin() + idx);
            return true;
        } else {
            if (static_cast<int>(node->children[idx]->keys.size()) >= degree) {
                BTreeNode* cur = node->children[idx];
                while (!cur->isLeaf) cur = cur->children.back();
                Value predKey = cloneValue(cur->keys.back());
                Value predVal = cloneValue(cur->values.back());
                dropValue(node->keys[idx]);
                dropValue(node->values[idx]);
                node->keys[idx] = predKey;
                node->values[idx] = predVal;
                return btreeRemoveFromNode(node->children[idx], predKey, degree);
            } else if (static_cast<int>(node->children[idx + 1]->keys.size()) >= degree) {
                BTreeNode* cur = node->children[idx + 1];
                while (!cur->isLeaf) cur = cur->children.front();
                Value succKey = cloneValue(cur->keys.front());
                Value succVal = cloneValue(cur->values.front());
                dropValue(node->keys[idx]);
                dropValue(node->values[idx]);
                node->keys[idx] = succKey;
                node->values[idx] = succVal;
                return btreeRemoveFromNode(node->children[idx + 1], succKey, degree);
            } else {
                btreeMerge(node, idx, degree);
                return btreeRemoveFromNode(node->children[idx], key, degree);
            }
        }
    } else {
        if (node->isLeaf) return false;
        bool flag = (idx == static_cast<int>(node->keys.size()));
        if (static_cast<int>(node->children[idx]->keys.size()) < degree) {
            if (idx != 0 && static_cast<int>(node->children[idx - 1]->keys.size()) >= degree) {
                btreeBorrowFromPrev(node, idx);
            } else if (idx != static_cast<int>(node->keys.size()) && static_cast<int>(node->children[idx + 1]->keys.size()) >= degree) {
                btreeBorrowFromNext(node, idx);
            } else {
                if (idx != static_cast<int>(node->keys.size())) {
                    btreeMerge(node, idx, degree);
                } else {
                    btreeMerge(node, idx - 1, degree);
                    idx--;
                }
            }
        }
        if (flag && idx > static_cast<int>(node->keys.size())) {
            return btreeRemoveFromNode(node->children[idx - 1], key, degree);
        } else {
            return btreeRemoveFromNode(node->children[idx], key, degree);
        }
    }
}

bool btreeRemove(ObjBTree* tree, Value key) {
    tree->lock.lock();
    if (tree->root == nullptr) {
        tree->lock.unlock();
        return false;
    }
    bool res = btreeRemoveFromNode(tree->root, key, tree->degree);
    if (tree->root->keys.empty()) {
        BTreeNode* tmp = tree->root;
        if (tree->root->isLeaf) {
            tree->root = nullptr;
        } else {
            tree->root = tree->root->children[0];
            tmp->children.clear();
        }
        delete tmp;
    }
    if (res && tree->count > 0) tree->count--;
    tree->lock.unlock();
    return res;
}

// ── Factories: Stack, List, Heap, RBTree, BTree ───────────────────
ObjStack* newStack() {
    void* mem = allocateObject(sizeof(ObjStack), OBJ_STACK);
    auto* stackObj = new (mem) ObjStack();
    stackObj->type = OBJ_STACK;
    new (&stackObj->data) std::vector<Value>();
    new (&stackObj->lock) isli::SpinLock();
    return stackObj;
}

ObjList* newList() {
    void* mem = allocateObject(sizeof(ObjList), OBJ_LIST);
    auto* listObj = new (mem) ObjList();
    listObj->type = OBJ_LIST;
    new (&listObj->data) std::list<Value>();
    new (&listObj->lock) isli::SpinLock();
    return listObj;
}

ObjHeap* newHeap(bool isMinHeap) {
    void* mem = allocateObject(sizeof(ObjHeap), OBJ_HEAP);
    auto* heapObj = new (mem) ObjHeap();
    heapObj->type = OBJ_HEAP;
    heapObj->isMinHeap = isMinHeap;
    new (&heapObj->data) std::vector<Value>();
    new (&heapObj->lock) isli::SpinLock();
    return heapObj;
}

ObjRBTree* newRBTree() {
    void* mem = allocateObject(sizeof(ObjRBTree), OBJ_RBTREE);
    auto* treeObj = new (mem) ObjRBTree();
    treeObj->type = OBJ_RBTREE;
    new (&treeObj->map) std::map<Value, Value, ValueLess>();
    new (&treeObj->lock) isli::SpinLock();
    return treeObj;
}

ObjBTree* newBTree(int degree) {
    if (degree < 2) degree = 2;
    void* mem = allocateObject(sizeof(ObjBTree), OBJ_BTREE);
    auto* btreeObj = new (mem) ObjBTree();
    btreeObj->type = OBJ_BTREE;
    btreeObj->degree = degree;
    btreeObj->count = 0;
    btreeObj->root = nullptr;
    new (&btreeObj->lock) isli::SpinLock();
    return btreeObj;
}

ObjBTree* copyBTree(ObjBTree* src) {
    src->lock.lock();
    ObjBTree* dst = newBTree(src->degree);
    dst->count = src->count;
    dst->root = btreeCopyNodes(src->root);
    src->lock.unlock();
    return dst;
}

// ── printObject ───────────────────────────────────────────────────
static void printFunction(ObjFunction* function) {
    if (function->name == nullptr) {
        printf("<script>");
        return;
    }
    printf("<fn %s>", function->name->chars);
}

void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_CLOSURE:
            printFunction(AS_CLOSURE(value)->function);
            break;
        case OBJ_FUNCTION:
            printFunction(AS_FUNCTION(value));
            break;
        case OBJ_INSTANCE:
            if (AS_INSTANCE(value)->structType == nullptr) {
                printf("<anonymous struct>");
            } else {
                printf("<struct %s>", AS_INSTANCE(value)->structType->name->chars);
            }
            break;
        case OBJ_NATIVE:
            printf("<native fn>");
            break;
        case OBJ_STRING: {
            auto* str = AS_STRING(value);
            printf("%.*s", str->length, str->chars);
            break;
        }
        case OBJ_STRING_BUFFER:
            printf("<string buffer>");
            break;
        case OBJ_STRUCT:
            printf("<struct %s>", AS_STRUCT(value)->name->chars);
            break;
        case OBJ_UPVALUE:
            printf("upvalue");
            break;

        case OBJ_ARRAY: {
            auto* arr = AS_ARRAY(value);
            printf("[");
            for (int i = 0; i < arr->count; i++) {
                if (i > 0) printf(", ");
                if (i >= 20 && arr->count > 25) {
                    printf("... (%d more)", arr->count - i);
                    break;
                }
                printValue(arr->values[i]);
            }
            printf("]");
            break;
        }

        case OBJ_BUFFER: {
            auto* buf = AS_BUFFER(value);
            const char* typeName = "f64";
            if (buf->elemType == BUF_F32) typeName = "f32";
            else if (buf->elemType == BUF_I32) typeName = "i32";
            else if (buf->elemType == BUF_U8) typeName = "u8";
            printf("<%sbuf %d>", typeName, buf->count);
            break;
        }

        case OBJ_STACK: {
            auto* st = AS_STACK(value);
            printf("stack([");
            for (size_t i = 0; i < st->data.size(); i++) {
                if (i > 0) printf(", ");
                printValue(st->data[i]);
            }
            printf("])");
            break;
        }

        case OBJ_LIST: {
            auto* li = AS_LIST(value);
            printf("list([");
            size_t idx = 0;
            for (const auto& v : li->data) {
                if (idx > 0) printf(", ");
                printValue(v);
                idx++;
            }
            printf("])");
            break;
        }

        case OBJ_HEAP: {
            auto* hp = AS_HEAP(value);
            printf("%s([", hp->isMinHeap ? "min_heap" : "max_heap");
            for (size_t i = 0; i < hp->data.size(); i++) {
                if (i > 0) printf(", ");
                printValue(hp->data[i]);
            }
            printf("])");
            break;
        }

        case OBJ_RBTREE: {
            auto* rb = AS_RBTREE(value);
            printf("rbtree({");
            size_t idx = 0;
            for (const auto& pair : rb->map) {
                if (idx > 0) printf(", ");
                printValue(pair.first);
                printf(": ");
                printValue(pair.second);
                idx++;
            }
            printf("})");
            break;
        }

        case OBJ_BTREE: {
            auto* bt = AS_BTREE(value);
            printf("btree(count=%d, degree=%d)", bt->count, bt->degree);
            break;
        }
    }
}
