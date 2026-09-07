#include <algorithm>
#include <cctype>
#include <cmath>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <random>
#include <thread>
#include <vector>
#include <limits>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif
#include "common.hpp"
#include "compiler.hpp"
#include "debug.hpp"
#include "object.hpp"
#include "memory.hpp"
#include "cpu_features.hpp"
#include "vm.hpp"

// ── Global VM ──────────────────────────────────────────────────────
VM vm;
static thread_local VM* tl_currentVM = nullptr;
static thread_local struct ParallelJob* tl_currentJob = nullptr;

static InterpretResult run(VM* v, int stopFrameCount = 0);

static inline bool stringsEqual(ObjString* a, ObjString* b) {
    if (a == b) return true;
    if (a == nullptr || b == nullptr) return false;
    return a->length == b->length && a->hash == b->hash &&
           std::memcmp(a->chars, b->chars, a->length) == 0;
}

// ── Property / Receiver Resolution Helper (D.R.Y) ──────────────────
static inline Value* resolveReceiver(Value* stackSlot, Value& receiver) {
    Value* receiverPtr = stackSlot;
    while (true) {
        if (IS_POINTER(receiver)) {
            receiverPtr = AS_POINTER(receiver);
            receiver = *receiverPtr;
        } else {
            break;
        }
    }
    return receiverPtr;
}

// ── Native: clock ─────────────────────────────────────────────────
static Value clockNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    auto now = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(now.time_since_epoch()).count();
    return NUMBER_VAL(seconds);
}

// ── Native: cpu_info ──────────────────────────────────────────────
static Value cpuInfoNative(int argCount, Value* args) {
    (void)argCount; (void)args;
    const CpuFeatures& f = cpuFeatures();
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "%s | AVX2: %s | FMA: %s | AVX-512: %s | AVX10: %s | Cores: %d | VecWidth: %d-bit",
        f.brand,
        f.avx2 ? "yes" : "no",
        f.fma ? "yes" : "no",
        f.avx512f ? "yes" : "no",
        f.avx10 ? "yes" : "no",
        f.logicalCores,
        f.preferredVectorBytes * 8);
    return OBJ_VAL(copyString(buf, static_cast<int>(std::strlen(buf))));
}

// ── Native: len / sv_len (O(1) Collection Length) ─────────────────
static Value lenNative(int argCount, Value* args) {
    if (argCount != 1) {
        return NUMBER_VAL(0);
    }
    if (IS_STRING(args[0])) {
        return NUMBER_VAL(AS_STRING(args[0])->length);
    }
    if (IS_ARRAY(args[0])) {
        return NUMBER_VAL(AS_ARRAY(args[0])->count);
    }
    if (IS_BUFFER(args[0])) {
        return NUMBER_VAL(AS_BUFFER(args[0])->count);
    }
    if (IS_STACK(args[0])) {
        return NUMBER_VAL(AS_STACK(args[0])->data.size());
    }
    if (IS_LIST(args[0])) {
        return NUMBER_VAL(AS_LIST(args[0])->data.size());
    }
    if (IS_HEAP(args[0])) {
        return NUMBER_VAL(AS_HEAP(args[0])->data.size());
    }
    if (IS_RBTREE(args[0])) {
        return NUMBER_VAL(AS_RBTREE(args[0])->map.size());
    }
    if (IS_BTREE(args[0])) {
        return NUMBER_VAL(AS_BTREE(args[0])->count);
    }
    return NUMBER_VAL(0);
}

// ── Native: sv_take_left (O(1) Zero-Copy Take Left) ───────────────
static Value svTakeLeftNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_NUMBER(args[1])) {
        return NIL_VAL;
    }
    ObjString* str = AS_STRING(args[0]);
    int n = static_cast<int>(AS_NUMBER(args[1]));
    if (n < 0) n = 0;
    if (n > str->length) n = str->length;
    return OBJ_VAL(sliceString(str, 0, n));
}

// ── Native: sv_take_right (O(1) Zero-Copy Take Right) ─────────────
static Value svTakeRightNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_NUMBER(args[1])) {
        return NIL_VAL;
    }
    ObjString* str = AS_STRING(args[0]);
    int n = static_cast<int>(AS_NUMBER(args[1]));
    if (n < 0) n = 0;
    if (n > str->length) n = str->length;
    return OBJ_VAL(sliceString(str, str->length - n, n));
}

// ── Native: sv_chop_left / sv_chop_right (D.R.Y. Helper) ──────────
static inline Value svChopHelper(int argCount, Value* args, bool isLeft) {
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_NUMBER(args[1])) {
        return NIL_VAL;
    }
    ObjString* str = AS_STRING(args[0]);
    int n = static_cast<int>(AS_NUMBER(args[1]));
    if (n < 0) n = 0;
    if (n >= str->length) {
        return OBJ_VAL(sliceString(str, isLeft ? str->length : 0, 0));
    }
    int offset = isLeft ? n : 0;
    int len = str->length - n;
    return OBJ_VAL(sliceString(str, offset, len));
}

// ── Native: sv_chop_left / sdrop_left (O(1) Zero-Copy Left Chop) ──
static Value svChopLeftNative(int argCount, Value* args) {
    return svChopHelper(argCount, args, true);
}

// ── Native: sv_chop_right / sdrop_right (O(1) Zero-Copy Right Chop)
static Value svChopRightNative(int argCount, Value* args) {
    return svChopHelper(argCount, args, false);
}

// ── Native: sv_trim_left / sv_trim_right / sv_trim ─────────────────
static inline void getTrimBounds(ObjString* str, bool trimLeft, bool trimRight, int& outOffset, int& outLength) {
    int offset = 0;
    if (trimLeft) {
        while (offset < str->length && std::isspace(static_cast<unsigned char>(str->chars[offset]))) {
            offset++;
        }
    }
    int end = str->length;
    if (trimRight) {
        while (end > offset && std::isspace(static_cast<unsigned char>(str->chars[end - 1]))) {
            end--;
        }
    }
    outOffset = offset;
    outLength = end - offset;
}

static Value svTrimLeftNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    ObjString* str = AS_STRING(args[0]);
    int offset = 0, len = 0;
    getTrimBounds(str, true, false, offset, len);
    return OBJ_VAL(sliceString(str, offset, len));
}

static Value svTrimRightNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    ObjString* str = AS_STRING(args[0]);
    int offset = 0, len = 0;
    getTrimBounds(str, false, true, offset, len);
    return OBJ_VAL(sliceString(str, offset, len));
}

static Value svTrimNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    ObjString* str = AS_STRING(args[0]);
    int offset = 0, len = 0;
    getTrimBounds(str, true, true, offset, len);
    return OBJ_VAL(sliceString(str, offset, len));
}

// ── Native: sv_sub (Zero-Copy Substring Slice) ────────────────────
static Value svSubNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_STRING(args[0]) || !IS_NUMBER(args[1])) {
        return NIL_VAL;
    }
    ObjString* str = AS_STRING(args[0]);
    int start = static_cast<int>(AS_NUMBER(args[1]));
    int len = str->length - start;
    if (argCount >= 3 && IS_NUMBER(args[2])) {
        len = static_cast<int>(AS_NUMBER(args[2]));
    }
    return OBJ_VAL(sliceString(str, start, len));
}

// ── Native: sv_find (Find Substring Index) ────────────────────────
static Value svFindNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        return NUMBER_VAL(-1);
    }
    ObjString* haystack = AS_STRING(args[0]);
    ObjString* needle = AS_STRING(args[1]);
    if (needle->length == 0) return NUMBER_VAL(0);
    if (needle->length > haystack->length) return NUMBER_VAL(-1);

    for (int i = 0; i <= haystack->length - needle->length; i++) {
        if (std::memcmp(haystack->chars + i, needle->chars, needle->length) == 0) {
            return NUMBER_VAL(i);
        }
    }
    return NUMBER_VAL(-1);
}

// ── Native: sv_split_left (Zero-Copy Part Before Delimiter) ───────
static Value svSplitLeftNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        return NIL_VAL;
    }
    ObjString* str = AS_STRING(args[0]);
    ObjString* delim = AS_STRING(args[1]);
    if (delim->length == 0 || delim->length > str->length) {
        return OBJ_VAL(sliceString(str, 0, str->length));
    }
    for (int i = 0; i <= str->length - delim->length; i++) {
        if (std::memcmp(str->chars + i, delim->chars, delim->length) == 0) {
            return OBJ_VAL(sliceString(str, 0, i));
        }
    }
    return OBJ_VAL(sliceString(str, 0, str->length));
}

// ── Native: sv_split_right (Zero-Copy Part After Delimiter) ────────
static Value svSplitRightNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        return NIL_VAL;
    }
    ObjString* str = AS_STRING(args[0]);
    ObjString* delim = AS_STRING(args[1]);
    if (delim->length == 0 || delim->length > str->length) {
        return OBJ_VAL(sliceString(str, str->length, 0));
    }
    for (int i = 0; i <= str->length - delim->length; i++) {
        if (std::memcmp(str->chars + i, delim->chars, delim->length) == 0) {
            int start = i + delim->length;
            return OBJ_VAL(sliceString(str, start, str->length - start));
        }
    }
    return OBJ_VAL(sliceString(str, str->length, 0));
}


// ── Native: array ─────────────────────────────────────────────────
static Value arrayNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    int size = static_cast<int>(AS_NUMBER(args[0]));
    if (size < 0) size = 0;
    Value initVal = NUMBER_VAL(0);
    if (argCount >= 2) {
        initVal = args[1];
    }
    ObjArray* arr = newArray(size, initVal);
    return OBJ_VAL(arr);
}

// ── Native: copy / clone ──────────────────────────────────────────
static Value copyNative(int argCount, Value* args) {
    if (argCount != 1) return NIL_VAL;
    if (IS_ARRAY(args[0])) {
        ObjArray* src = AS_ARRAY(args[0]);
        ObjArray* dst = newArray(src->count, NIL_VAL);
        int i = 0;
        for (; i + 4 <= src->count; i += 4) {
            if (!IS_NUMBER(src->values[i]) || !IS_NUMBER(src->values[i+1]) || 
                !IS_NUMBER(src->values[i+2]) || !IS_NUMBER(src->values[i+3])) {
                break;
            }
            __m256d data = _mm256_loadu_pd(reinterpret_cast<const double*>(&src->values[i]));
            _mm256_storeu_pd(reinterpret_cast<double*>(&dst->values[i]), data);
        }
        for (; i < src->count; i++) {
            dst->values[i] = cloneValue(src->values[i]);
        }
        return OBJ_VAL(dst);
    }
    if (IS_STACK(args[0])) {
        ObjStack* src = AS_STACK(args[0]);
        ObjStack* dst = newStack();
        for (Value v : src->data) {
            dst->data.push_back(cloneValue(v));
        }
        return OBJ_VAL(dst);
    }
    if (IS_LIST(args[0])) {
        ObjList* src = AS_LIST(args[0]);
        ObjList* dst = newList();
        for (Value v : src->data) {
            dst->data.push_back(cloneValue(v));
        }
        return OBJ_VAL(dst);
    }
    if (IS_HEAP(args[0])) {
        ObjHeap* src = AS_HEAP(args[0]);
        ObjHeap* dst = newHeap(src->isMinHeap);
        for (Value v : src->data) {
            dst->data.push_back(cloneValue(v));
        }
        return OBJ_VAL(dst);
    }
    if (IS_RBTREE(args[0])) {
        ObjRBTree* src = AS_RBTREE(args[0]);
        ObjRBTree* dst = newRBTree();
        for (auto& pair : src->map) {
            dst->map[cloneValue(pair.first)] = cloneValue(pair.second);
        }
        return OBJ_VAL(dst);
    }
    if (IS_BTREE(args[0])) {
        return OBJ_VAL(copyBTree(AS_BTREE(args[0])));
    }
    return cloneValue(args[0]);
}

// ── Native: Typed Buffer Constructors & Utilities ─────────────────
template <BufferElemType T>
static Value bufNewNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) {
        runtimeError(&vm, "Buffer size must be a number.");
        return NIL_VAL;
    }
    double n = AS_NUMBER(args[0]);
    if (n < 0 || n > 2147483647.0) {
        runtimeError(&vm, "Buffer size out of range.");
        return NIL_VAL;
    }
    double init = (argCount >= 2 && IS_NUMBER(args[1])) ? AS_NUMBER(args[1]) : 0.0;
    return OBJ_VAL(newBuffer(T, static_cast<int>(n), init));
}

static Value bufTypeNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_BUFFER(args[0])) return NIL_VAL;
    ObjBuffer* b = AS_BUFFER(args[0]);
    switch (b->elemType) {
        case BUF_F32: return OBJ_VAL(copyString("f32", 3));
        case BUF_F64: return OBJ_VAL(copyString("f64", 3));
        case BUF_I32: return OBJ_VAL(copyString("i32", 3));
        case BUF_U8:  return OBJ_VAL(copyString("u8", 2));
    }
    return NIL_VAL;
}

static Value bufFromArrayNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_ARRAY(args[0])) {
        runtimeError(&vm, "First argument must be an array.");
        return NIL_VAL;
    }
    ObjArray* arr = AS_ARRAY(args[0]);
    BufferElemType elemType = BUF_F64;
    if (argCount >= 2 && IS_STRING(args[1])) {
        ObjString* s = AS_STRING(args[1]);
        if (s->length == 3 && std::memcmp(s->chars, "f32", 3) == 0) elemType = BUF_F32;
        else if (s->length == 3 && std::memcmp(s->chars, "f64", 3) == 0) elemType = BUF_F64;
        else if (s->length == 3 && std::memcmp(s->chars, "i32", 3) == 0) elemType = BUF_I32;
        else if (s->length == 2 && std::memcmp(s->chars, "u8", 2) == 0) elemType = BUF_U8;
    }
    ObjBuffer* buf = newBuffer(elemType, arr->count, 0.0);
    for (int i = 0; i < arr->count; i++) {
        double v = IS_NUMBER(arr->values[i]) ? AS_NUMBER(arr->values[i]) : 0.0;
        bufferStore(buf, i, v);
    }
    return OBJ_VAL(buf);
}

static Value bufToArrayNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_BUFFER(args[0])) {
        runtimeError(&vm, "Argument must be a buffer.");
        return NIL_VAL;
    }
    ObjBuffer* buf = AS_BUFFER(args[0]);
    ObjArray* arr = newArray(buf->count, NUMBER_VAL(0.0));
    for (int i = 0; i < buf->count; i++) {
        arr->values[i] = NUMBER_VAL(bufferLoad(buf, i));
    }
    return OBJ_VAL(arr);
}

static Value atomicAddNative(int argCount, Value* args) {
    auto fail = [](const char* msg) -> Value {
        isliMarkJobFailed("%s", msg);
        return NIL_VAL;
    };
    if (argCount != 3 || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2])) {
        return fail("atomic_add expects (buffer, index, delta).");
    }
    double delta = AS_NUMBER(args[2]);
    double idxD = AS_NUMBER(args[1]);
    if (idxD < 0.0 || idxD != idxD) {
        return fail("atomic_add index out of bounds.");
    }
    int idx = static_cast<int>(idxD);

    if (!IS_BUFFER(args[0])) {
        return fail("atomic_add expects i32/f32/f64 buffer.");
    }
    ObjBuffer* b = AS_BUFFER(args[0]);
    if (idx < 0 || idx >= b->count) {
        return fail("atomic_add index out of bounds.");
    }
    if (b->elemType == BUF_I32) {
        int32_t* p = static_cast<int32_t*>(b->data) + idx;
        int32_t addend = static_cast<int32_t>(delta);
        int32_t old = __atomic_fetch_add(p, addend, __ATOMIC_ACQ_REL);
        return NUMBER_VAL(static_cast<double>(old + addend));
    }
    if (b->elemType == BUF_F64) {
        uint64_t* p = reinterpret_cast<uint64_t*>(static_cast<double*>(b->data) + idx);
        uint64_t oldBits = __atomic_load_n(p, __ATOMIC_RELAXED);
        for (;;) {
            double oldVal;
            std::memcpy(&oldVal, &oldBits, sizeof(double));
            double newVal = oldVal + delta;
            uint64_t newBits;
            std::memcpy(&newBits, &newVal, sizeof(double));
            if (__atomic_compare_exchange_n(p, &oldBits, newBits, true, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
                return NUMBER_VAL(newVal);
            }
        }
    }
    if (b->elemType == BUF_F32) {
        uint32_t* p = reinterpret_cast<uint32_t*>(static_cast<float*>(b->data) + idx);
        uint32_t oldBits = __atomic_load_n(p, __ATOMIC_RELAXED);
        for (;;) {
            float oldVal;
            std::memcpy(&oldVal, &oldBits, sizeof(float));
            float newVal = oldVal + static_cast<float>(delta);
            uint32_t newBits;
            std::memcpy(&newBits, &newVal, sizeof(float));
            if (__atomic_compare_exchange_n(p, &oldBits, newBits, true, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
                return NUMBER_VAL(static_cast<double>(newVal));
            }
        }
    }
    return fail("atomic_add expects i32/f32/f64 buffer.");
}

// ── Native: simd_copy(dst, src, [start, end]) ─────────────────────
static Value simdCopyNative(int argCount, Value* args) {
    if (argCount < 2) return NIL_VAL;
    if (IS_BUFFER(args[0]) && IS_BUFFER(args[1])) {
        ObjBuffer* dst = AS_BUFFER(args[0]);
        ObjBuffer* src = AS_BUFFER(args[1]);
        int start = 0;
        int end = dst->count < src->count ? dst->count : src->count;
        if (argCount >= 3 && IS_NUMBER(args[2])) start = static_cast<int>(AS_NUMBER(args[2]));
        if (argCount >= 4 && IS_NUMBER(args[3])) end = static_cast<int>(AS_NUMBER(args[3]));

        if (start < 0) start = 0;
        if (end > dst->count) end = dst->count;
        if (end > src->count) end = src->count;

        if (start < end) {
            if (dst->elemType == src->elemType) {
                size_t es = bufferElemSize(dst->elemType);
                std::memcpy(static_cast<char*>(dst->data) + start * es,
                            static_cast<const char*>(src->data) + start * es,
                            (end - start) * es);
            } else {
                for (int i = start; i < end; i++) {
                    bufferStore(dst, i, bufferLoad(src, i));
                }
            }
        }
        return NIL_VAL;
    }

    if (!IS_ARRAY(args[0]) || !IS_ARRAY(args[1])) return NIL_VAL;
    ObjArray* dst = AS_ARRAY(args[0]);
    ObjArray* src = AS_ARRAY(args[1]);
    int start = 0;
    int end = dst->count < src->count ? dst->count : src->count;
    if (argCount >= 3 && IS_NUMBER(args[2])) start = static_cast<int>(AS_NUMBER(args[2]));
    if (argCount >= 4 && IS_NUMBER(args[3])) end = static_cast<int>(AS_NUMBER(args[3]));

    if (start < 0) start = 0;
    if (end > dst->count) end = dst->count;
    if (end > src->count) end = src->count;

    if (start < end) {
        int i = start;
        for (; i + 4 <= end; i += 4) {
            if (!IS_NUMBER(src->values[i]) || !IS_NUMBER(src->values[i+1]) || 
                !IS_NUMBER(src->values[i+2]) || !IS_NUMBER(src->values[i+3])) {
                break;
            }
            __m256d data = _mm256_loadu_pd(reinterpret_cast<const double*>(&src->values[i]));
            _mm256_storeu_pd(reinterpret_cast<double*>(&dst->values[i]), data);
        }
        for (; i < end; i++) {
            dst->values[i] = cloneValue(src->values[i]);
        }
    }
    return NIL_VAL;
}

// ── Native: simd_fill(dst, value, [start, end]) ───────────────────
static Value simdFillNative(int argCount, Value* args) {
    if (argCount < 2) return NIL_VAL;
    if (IS_BUFFER(args[0])) {
        ObjBuffer* dst = AS_BUFFER(args[0]);
        double v = IS_NUMBER(args[1]) ? AS_NUMBER(args[1]) : 0.0;
        int start = 0;
        int end = dst->count;
        if (argCount >= 3 && IS_NUMBER(args[2])) start = static_cast<int>(AS_NUMBER(args[2]));
        if (argCount >= 4 && IS_NUMBER(args[3])) end = static_cast<int>(AS_NUMBER(args[3]));

        if (start < 0) start = 0;
        if (end > dst->count) end = dst->count;

        if (start < end) {
            if (dst->elemType == BUF_F64) {
                __m256d data = _mm256_set1_pd(v);
                double* ptr = static_cast<double*>(dst->data);
                int i = start;
                for (; i + 4 <= end; i += 4) {
                    _mm256_storeu_pd(ptr + i, data);
                }
                for (; i < end; i++) ptr[i] = v;
            } else if (dst->elemType == BUF_F32) {
                __m256 data = _mm256_set1_ps(static_cast<float>(v));
                float* ptr = static_cast<float*>(dst->data);
                int i = start;
                for (; i + 8 <= end; i += 8) {
                    _mm256_storeu_ps(ptr + i, data);
                }
                for (; i < end; i++) ptr[i] = static_cast<float>(v);
            } else {
                for (int i = start; i < end; i++) {
                    bufferStore(dst, i, v);
                }
            }
        }
        return NIL_VAL;
    }

    if (!IS_ARRAY(args[0])) return NIL_VAL;
    ObjArray* dst = AS_ARRAY(args[0]);
    Value val = args[1];
    int start = 0;
    int end = dst->count;
    if (argCount >= 3 && IS_NUMBER(args[2])) start = static_cast<int>(AS_NUMBER(args[2]));
    if (argCount >= 4 && IS_NUMBER(args[3])) end = static_cast<int>(AS_NUMBER(args[3]));

    if (start < 0) start = 0;
    if (end > dst->count) end = dst->count;

    if (start < end) {
        if (IS_NUMBER(val)) {
            __m256d data = _mm256_set1_pd(AS_NUMBER(val));
            int i = start;
            for (; i + 4 <= end; i += 4) {
                _mm256_storeu_pd(reinterpret_cast<double*>(&dst->values[i]), data);
            }
            for (; i < end; i++) {
                dst->values[i] = val;
            }
        } else {
            for (int i = start; i < end; i++) {
                dst->values[i] = cloneValue(val);
            }
        }
    }
    return NIL_VAL;
}

// ── Generic SIMD Math Engine ──────────────────────────────────────
template <typename AvxOp, typename ScalarOp>
static Value simdBinaryOp(int argCount, Value* args, AvxOp avxOp, ScalarOp scalarOp) {
    if (argCount == 2 && IS_BUFFER(args[0])) {
        ObjBuffer* a = AS_BUFFER(args[0]);
        if (IS_BUFFER(args[1])) {
            ObjBuffer* b = AS_BUFFER(args[1]);
            int count = std::min(a->count, b->count);
            if (a->elemType == BUF_F64 && b->elemType == BUF_F64) {
                double* pa = static_cast<double*>(a->data);
                const double* pb = static_cast<const double*>(b->data);
                int i = 0;
                for (; i + 4 <= count; i += 4) {
                    __m256d va = _mm256_loadu_pd(pa + i);
                    __m256d vb = _mm256_loadu_pd(pb + i);
                    _mm256_storeu_pd(pa + i, avxOp(va, vb));
                }
                for (; i < count; i++) pa[i] = scalarOp(pa[i], pb[i]);
            } else {
                for (int i = 0; i < count; i++) {
                    bufferStore(a, i, scalarOp(bufferLoad(a, i), bufferLoad(b, i)));
                }
            }
            return cloneValue(args[0]);
        } else if (IS_NUMBER(args[1])) {
            double s = AS_NUMBER(args[1]);
            if (a->elemType == BUF_F64) {
                double* pa = static_cast<double*>(a->data);
                __m256d vb = _mm256_set1_pd(s);
                int i = 0;
                for (; i + 4 <= a->count; i += 4) {
                    __m256d va = _mm256_loadu_pd(pa + i);
                    _mm256_storeu_pd(pa + i, avxOp(va, vb));
                }
                for (; i < a->count; i++) pa[i] = scalarOp(pa[i], s);
            } else {
                for (int i = 0; i < a->count; i++) {
                    bufferStore(a, i, scalarOp(bufferLoad(a, i), s));
                }
            }
            return cloneValue(args[0]);
        }
    } else if (argCount >= 3 && IS_BUFFER(args[0])) {
        ObjBuffer* dst = AS_BUFFER(args[0]);
        if (IS_BUFFER(args[1]) && IS_BUFFER(args[2])) {
            ObjBuffer* a = AS_BUFFER(args[1]);
            ObjBuffer* b = AS_BUFFER(args[2]);
            int count = std::min({dst->count, a->count, b->count});
            if (dst->elemType == BUF_F64 && a->elemType == BUF_F64 && b->elemType == BUF_F64) {
                double* pd = static_cast<double*>(dst->data);
                const double* pa = static_cast<const double*>(a->data);
                const double* pb = static_cast<const double*>(b->data);
                int i = 0;
                for (; i + 4 <= count; i += 4) {
                    __m256d va = _mm256_loadu_pd(pa + i);
                    __m256d vb = _mm256_loadu_pd(pb + i);
                    _mm256_storeu_pd(pd + i, avxOp(va, vb));
                }
                for (; i < count; i++) pd[i] = scalarOp(pa[i], pb[i]);
            } else {
                for (int i = 0; i < count; i++) {
                    bufferStore(dst, i, scalarOp(bufferLoad(a, i), bufferLoad(b, i)));
                }
            }
            return cloneValue(args[0]);
        } else if (IS_BUFFER(args[1]) && IS_NUMBER(args[2])) {
            ObjBuffer* a = AS_BUFFER(args[1]);
            double s = AS_NUMBER(args[2]);
            int count = std::min(dst->count, a->count);
            if (dst->elemType == BUF_F64 && a->elemType == BUF_F64) {
                double* pd = static_cast<double*>(dst->data);
                const double* pa = static_cast<const double*>(a->data);
                __m256d vb = _mm256_set1_pd(s);
                int i = 0;
                for (; i + 4 <= count; i += 4) {
                    __m256d va = _mm256_loadu_pd(pa + i);
                    _mm256_storeu_pd(pd + i, avxOp(va, vb));
                }
                for (; i < count; i++) pd[i] = scalarOp(pa[i], s);
            } else {
                for (int i = 0; i < count; i++) {
                    bufferStore(dst, i, scalarOp(bufferLoad(a, i), s));
                }
            }
            return cloneValue(args[0]);
        } else if (IS_NUMBER(args[1]) && IS_BUFFER(args[2])) {
            double s = AS_NUMBER(args[1]);
            ObjBuffer* b = AS_BUFFER(args[2]);
            int count = std::min(dst->count, b->count);
            if (dst->elemType == BUF_F64 && b->elemType == BUF_F64) {
                double* pd = static_cast<double*>(dst->data);
                const double* pb = static_cast<const double*>(b->data);
                __m256d va = _mm256_set1_pd(s);
                int i = 0;
                for (; i + 4 <= count; i += 4) {
                    __m256d vb = _mm256_loadu_pd(pb + i);
                    _mm256_storeu_pd(pd + i, avxOp(va, vb));
                }
                for (; i < count; i++) pd[i] = scalarOp(s, pb[i]);
            } else {
                for (int i = 0; i < count; i++) {
                    bufferStore(dst, i, scalarOp(s, bufferLoad(b, i)));
                }
            }
            return cloneValue(args[0]);
        }
    }

    if (argCount == 2) {
        if (!IS_ARRAY(args[0])) return NIL_VAL;
        ObjArray* a = AS_ARRAY(args[0]);
        if (IS_ARRAY(args[1])) {
            ObjArray* b = AS_ARRAY(args[1]);
            int count = std::min(a->count, b->count);
            int i = 0;
            for (; i + 4 <= count; i += 4) {
                if (!IS_NUMBER(a->values[i]) || !IS_NUMBER(a->values[i+1]) || !IS_NUMBER(a->values[i+2]) || !IS_NUMBER(a->values[i+3]) ||
                    !IS_NUMBER(b->values[i]) || !IS_NUMBER(b->values[i+1]) || !IS_NUMBER(b->values[i+2]) || !IS_NUMBER(b->values[i+3])) break;
                __m256d va = _mm256_loadu_pd(reinterpret_cast<const double*>(&a->values[i]));
                __m256d vb = _mm256_loadu_pd(reinterpret_cast<const double*>(&b->values[i]));
                __m256d vres = avxOp(va, vb);
                _mm256_storeu_pd(reinterpret_cast<double*>(&a->values[i]), vres);
            }
            for (; i < count; i++) {
                if (IS_NUMBER(a->values[i]) && IS_NUMBER(b->values[i])) {
                    a->values[i] = NUMBER_VAL(scalarOp(AS_NUMBER(a->values[i]), AS_NUMBER(b->values[i])));
                }
            }
            return cloneValue(args[0]);
        } else if (IS_NUMBER(args[1])) {
            double s = AS_NUMBER(args[1]);
            __m256d vb = _mm256_set1_pd(s);
            int i = 0;
            for (; i + 4 <= a->count; i += 4) {
                __m256d va = _mm256_loadu_pd(reinterpret_cast<const double*>(&a->values[i]));
                __m256d vres = avxOp(va, vb);
                _mm256_storeu_pd(reinterpret_cast<double*>(&a->values[i]), vres);
            }
            for (; i < a->count; i++) {
                if (IS_NUMBER(a->values[i])) {
                    a->values[i] = NUMBER_VAL(scalarOp(AS_NUMBER(a->values[i]), s));
                }
            }
            return cloneValue(args[0]);
        }
        return NIL_VAL;
    } else if (argCount >= 3) {
        if (!IS_ARRAY(args[0])) return NIL_VAL;
        ObjArray* dst = AS_ARRAY(args[0]);
        if (IS_ARRAY(args[1]) && IS_ARRAY(args[2])) {
            ObjArray* a = AS_ARRAY(args[1]);
            ObjArray* b = AS_ARRAY(args[2]);
            int count = std::min({dst->count, a->count, b->count});
            int i = 0;
            for (; i + 4 <= count; i += 4) {
                __m256d va = _mm256_loadu_pd(reinterpret_cast<const double*>(&a->values[i]));
                __m256d vb = _mm256_loadu_pd(reinterpret_cast<const double*>(&b->values[i]));
                __m256d vres = avxOp(va, vb);
                _mm256_storeu_pd(reinterpret_cast<double*>(&dst->values[i]), vres);
            }
            for (; i < count; i++) {
                if (IS_NUMBER(a->values[i]) && IS_NUMBER(b->values[i])) {
                    dst->values[i] = NUMBER_VAL(scalarOp(AS_NUMBER(a->values[i]), AS_NUMBER(b->values[i])));
                }
            }
            return cloneValue(args[0]);
        } else if (IS_ARRAY(args[1]) && IS_NUMBER(args[2])) {
            ObjArray* a = AS_ARRAY(args[1]);
            double s = AS_NUMBER(args[2]);
            int count = std::min(dst->count, a->count);
            __m256d vb = _mm256_set1_pd(s);
            int i = 0;
            for (; i + 4 <= count; i += 4) {
                __m256d va = _mm256_loadu_pd(reinterpret_cast<const double*>(&a->values[i]));
                __m256d vres = avxOp(va, vb);
                _mm256_storeu_pd(reinterpret_cast<double*>(&dst->values[i]), vres);
            }
            for (; i < count; i++) {
                if (IS_NUMBER(a->values[i])) {
                    dst->values[i] = NUMBER_VAL(scalarOp(AS_NUMBER(a->values[i]), s));
                }
            }
            return cloneValue(args[0]);
        } else if (IS_NUMBER(args[1]) && IS_ARRAY(args[2])) {
            double s = AS_NUMBER(args[1]);
            ObjArray* b = AS_ARRAY(args[2]);
            int count = std::min(dst->count, b->count);
            __m256d va = _mm256_set1_pd(s);
            int i = 0;
            for (; i + 4 <= count; i += 4) {
                __m256d vb = _mm256_loadu_pd(reinterpret_cast<const double*>(&b->values[i]));
                __m256d vres = avxOp(va, vb);
                _mm256_storeu_pd(reinterpret_cast<double*>(&dst->values[i]), vres);
            }
            for (; i < count; i++) {
                if (IS_NUMBER(b->values[i])) {
                    dst->values[i] = NUMBER_VAL(scalarOp(s, AS_NUMBER(b->values[i])));
                }
            }
            return cloneValue(args[0]);
        }
    }
    return NIL_VAL;
}

// ── Native: simd_add ──────────────────────────────────────────────
static Value simdAddNative(int argCount, Value* args) {
    return simdBinaryOp(argCount, args,
        [](const __m256d& a, const __m256d& b) { return _mm256_add_pd(a, b); },
        [](double a, double b) { return a + b; });
}

// ── Native: simd_sub ──────────────────────────────────────────────
static Value simdSubNative(int argCount, Value* args) {
    return simdBinaryOp(argCount, args,
        [](const __m256d& a, const __m256d& b) { return _mm256_sub_pd(a, b); },
        [](double a, double b) { return a - b; });
}

// ── Native: simd_mul ──────────────────────────────────────────────
static Value simdMulNative(int argCount, Value* args) {
    return simdBinaryOp(argCount, args,
        [](const __m256d& a, const __m256d& b) { return _mm256_mul_pd(a, b); },
        [](double a, double b) { return a * b; });
}

// ── Native: simd_div ──────────────────────────────────────────────
static Value simdDivNative(int argCount, Value* args) {
    return simdBinaryOp(argCount, args,
        [](const __m256d& a, const __m256d& b) { return _mm256_div_pd(a, b); },
        [](double a, double b) { return a / b; });
}

// ── Native: simd_fma(dst, a, b, c) -> dst = a * b + c ─────────────
static Value simdFmaNative(int argCount, Value* args) {
    if (argCount < 4) return NIL_VAL;

    if (IS_BUFFER(args[0]) && IS_BUFFER(args[1])) {
        ObjBuffer* dst = AS_BUFFER(args[0]);
        ObjBuffer* a = AS_BUFFER(args[1]);
        if (IS_BUFFER(args[2]) && IS_BUFFER(args[3])) {
            ObjBuffer* b = AS_BUFFER(args[2]);
            ObjBuffer* c = AS_BUFFER(args[3]);
            int count = std::min({dst->count, a->count, b->count, c->count});
            if (dst->elemType == BUF_F64 && a->elemType == BUF_F64 && b->elemType == BUF_F64 && c->elemType == BUF_F64) {
                double* pd = static_cast<double*>(dst->data);
                const double* pa = static_cast<const double*>(a->data);
                const double* pb = static_cast<const double*>(b->data);
                const double* pc = static_cast<const double*>(c->data);
                int i = 0;
                for (; i + 4 <= count; i += 4) {
                    __m256d va = _mm256_loadu_pd(pa + i);
                    __m256d vb = _mm256_loadu_pd(pb + i);
                    __m256d vc = _mm256_loadu_pd(pc + i);
                    _mm256_storeu_pd(pd + i, _mm256_fmadd_pd(va, vb, vc));
                }
                for (; i < count; i++) pd[i] = pa[i] * pb[i] + pc[i];
            } else {
                for (int i = 0; i < count; i++) {
                    bufferStore(dst, i, bufferLoad(a, i) * bufferLoad(b, i) + bufferLoad(c, i));
                }
            }
            return cloneValue(args[0]);
        } else if (IS_NUMBER(args[2]) && IS_NUMBER(args[3])) {
            double sb = AS_NUMBER(args[2]);
            double sc = AS_NUMBER(args[3]);
            int count = std::min(dst->count, a->count);
            if (dst->elemType == BUF_F64 && a->elemType == BUF_F64) {
                double* pd = static_cast<double*>(dst->data);
                const double* pa = static_cast<const double*>(a->data);
                __m256d vb = _mm256_set1_pd(sb);
                __m256d vc = _mm256_set1_pd(sc);
                int i = 0;
                for (; i + 4 <= count; i += 4) {
                    __m256d va = _mm256_loadu_pd(pa + i);
                    _mm256_storeu_pd(pd + i, _mm256_fmadd_pd(va, vb, vc));
                }
                for (; i < count; i++) pd[i] = pa[i] * sb + sc;
            } else {
                for (int i = 0; i < count; i++) {
                    bufferStore(dst, i, bufferLoad(a, i) * sb + sc);
                }
            }
            return cloneValue(args[0]);
        }
    }

    if (!IS_ARRAY(args[0]) || !IS_ARRAY(args[1])) return NIL_VAL;
    ObjArray* dst = AS_ARRAY(args[0]);
    ObjArray* a = AS_ARRAY(args[1]);
    
    if (IS_ARRAY(args[2]) && IS_ARRAY(args[3])) {
        ObjArray* b = AS_ARRAY(args[2]);
        ObjArray* c = AS_ARRAY(args[3]);
        int count = std::min({dst->count, a->count, b->count, c->count});
        int i = 0;
        for (; i + 4 <= count; i += 4) {
            __m256d va = _mm256_loadu_pd(reinterpret_cast<const double*>(&a->values[i]));
            __m256d vb = _mm256_loadu_pd(reinterpret_cast<const double*>(&b->values[i]));
            __m256d vc = _mm256_loadu_pd(reinterpret_cast<const double*>(&c->values[i]));
            __m256d vres = _mm256_fmadd_pd(va, vb, vc);
            _mm256_storeu_pd(reinterpret_cast<double*>(&dst->values[i]), vres);
        }
        for (; i < count; i++) {
            if (IS_NUMBER(a->values[i]) && IS_NUMBER(b->values[i]) && IS_NUMBER(c->values[i])) {
                dst->values[i] = NUMBER_VAL(AS_NUMBER(a->values[i]) * AS_NUMBER(b->values[i]) + AS_NUMBER(c->values[i]));
            }
        }
        return cloneValue(args[0]);
    } else if (IS_NUMBER(args[2]) && IS_NUMBER(args[3])) {
        double sb = AS_NUMBER(args[2]);
        double sc = AS_NUMBER(args[3]);
        int count = std::min(dst->count, a->count);
        __m256d vb = _mm256_set1_pd(sb);
        __m256d vc = _mm256_set1_pd(sc);
        int i = 0;
        for (; i + 4 <= count; i += 4) {
            __m256d va = _mm256_loadu_pd(reinterpret_cast<const double*>(&a->values[i]));
            __m256d vres = _mm256_fmadd_pd(va, vb, vc);
            _mm256_storeu_pd(reinterpret_cast<double*>(&dst->values[i]), vres);
        }
        for (; i < count; i++) {
            if (IS_NUMBER(a->values[i])) {
                dst->values[i] = NUMBER_VAL(AS_NUMBER(a->values[i]) * sb + sc);
            }
        }
        return cloneValue(args[0]);
    }
    return NIL_VAL;
}

// ── Native: simd_sum(arr, [start, end]) ───────────────────────────
static Value simdSumNative(int argCount, Value* args) {
    if (argCount < 1) return NUMBER_VAL(0);

    if (IS_BUFFER(args[0])) {
        ObjBuffer* buf = AS_BUFFER(args[0]);
        int start = 0;
        int end = buf->count;
        if (argCount >= 2 && IS_NUMBER(args[1])) start = static_cast<int>(AS_NUMBER(args[1]));
        if (argCount >= 3 && IS_NUMBER(args[2])) end = static_cast<int>(AS_NUMBER(args[2]));
        if (start < 0) start = 0;
        if (end > buf->count) end = buf->count;
        if (start >= end) return NUMBER_VAL(0);

        if (buf->elemType == BUF_F64) {
            __m256d vsum = _mm256_setzero_pd();
            const double* ptr = static_cast<const double*>(buf->data);
            int i = start;
            for (; i + 4 <= end; i += 4) {
                __m256d va = _mm256_loadu_pd(ptr + i);
                vsum = _mm256_add_pd(vsum, va);
            }
            double temp[4];
            _mm256_storeu_pd(temp, vsum);
            double total = temp[0] + temp[1] + temp[2] + temp[3];
            for (; i < end; i++) total += ptr[i];
            return NUMBER_VAL(total);
        } else if (buf->elemType == BUF_F32) {
            __m256 vsum = _mm256_setzero_ps();
            const float* ptr = static_cast<const float*>(buf->data);
            int i = start;
            for (; i + 8 <= end; i += 8) {
                __m256 va = _mm256_loadu_ps(ptr + i);
                vsum = _mm256_add_ps(vsum, va);
            }
            float temp[8];
            _mm256_storeu_ps(temp, vsum);
            double total = 0;
            for (int j = 0; j < 8; j++) total += temp[j];
            for (; i < end; i++) total += ptr[i];
            return NUMBER_VAL(total);
        } else {
            double total = 0;
            for (int i = start; i < end; i++) total += bufferLoad(buf, i);
            return NUMBER_VAL(total);
        }
    }

    if (!IS_ARRAY(args[0])) return NUMBER_VAL(0);
    ObjArray* arr = AS_ARRAY(args[0]);
    int start = 0;
    int end = arr->count;
    if (argCount >= 2 && IS_NUMBER(args[1])) start = static_cast<int>(AS_NUMBER(args[1]));
    if (argCount >= 3 && IS_NUMBER(args[2])) end = static_cast<int>(AS_NUMBER(args[2]));
    if (start < 0) start = 0;
    if (end > arr->count) end = arr->count;
    if (start >= end) return NUMBER_VAL(0);

    __m256d vsum = _mm256_setzero_pd();
    int i = start;
    for (; i + 4 <= end; i += 4) {
        __m256d va = _mm256_loadu_pd(reinterpret_cast<const double*>(&arr->values[i]));
        vsum = _mm256_add_pd(vsum, va);
    }
    double temp[4];
    _mm256_storeu_pd(temp, vsum);
    double total = temp[0] + temp[1] + temp[2] + temp[3];
    for (; i < end; i++) {
        if (IS_NUMBER(arr->values[i])) total += AS_NUMBER(arr->values[i]);
    }
    return NUMBER_VAL(total);
}

// ── Native: simd_dot(a, b) ────────────────────────────────────────
static Value simdDotNative(int argCount, Value* args) {
    if (argCount < 2) return NUMBER_VAL(0);

    if (IS_BUFFER(args[0]) && IS_BUFFER(args[1])) {
        ObjBuffer* a = AS_BUFFER(args[0]);
        ObjBuffer* b = AS_BUFFER(args[1]);
        int count = std::min(a->count, b->count);
        if (a->elemType == BUF_F64 && b->elemType == BUF_F64) {
            __m256d vdot = _mm256_setzero_pd();
            const double* pa = static_cast<const double*>(a->data);
            const double* pb = static_cast<const double*>(b->data);
            int i = 0;
            for (; i + 4 <= count; i += 4) {
                __m256d va = _mm256_loadu_pd(pa + i);
                __m256d vb = _mm256_loadu_pd(pb + i);
                vdot = _mm256_fmadd_pd(va, vb, vdot);
            }
            double temp[4];
            _mm256_storeu_pd(temp, vdot);
            double total = temp[0] + temp[1] + temp[2] + temp[3];
            for (; i < count; i++) total += pa[i] * pb[i];
            return NUMBER_VAL(total);
        } else if (a->elemType == BUF_F32 && b->elemType == BUF_F32) {
            __m256 vdot = _mm256_setzero_ps();
            const float* pa = static_cast<const float*>(a->data);
            const float* pb = static_cast<const float*>(b->data);
            int i = 0;
            for (; i + 8 <= count; i += 8) {
                __m256 va = _mm256_loadu_ps(pa + i);
                __m256 vb = _mm256_loadu_ps(pb + i);
                vdot = _mm256_fmadd_ps(va, vb, vdot);
            }
            float temp[8];
            _mm256_storeu_ps(temp, vdot);
            double total = 0;
            for (int j = 0; j < 8; j++) total += temp[j];
            for (; i < count; i++) total += static_cast<double>(pa[i]) * static_cast<double>(pb[i]);
            return NUMBER_VAL(total);
        } else {
            double total = 0;
            for (int i = 0; i < count; i++) total += bufferLoad(a, i) * bufferLoad(b, i);
            return NUMBER_VAL(total);
        }
    }

    if (!IS_ARRAY(args[0]) || !IS_ARRAY(args[1])) return NUMBER_VAL(0);
    ObjArray* a = AS_ARRAY(args[0]);
    ObjArray* b = AS_ARRAY(args[1]);
    int count = std::min(a->count, b->count);
    
    __m256d vdot = _mm256_setzero_pd();
    int i = 0;
    for (; i + 4 <= count; i += 4) {
        __m256d va = _mm256_loadu_pd(reinterpret_cast<const double*>(&a->values[i]));
        __m256d vb = _mm256_loadu_pd(reinterpret_cast<const double*>(&b->values[i]));
        vdot = _mm256_fmadd_pd(va, vb, vdot);
    }
    double temp[4];
    _mm256_storeu_pd(temp, vdot);
    double total = temp[0] + temp[1] + temp[2] + temp[3];
    for (; i < count; i++) {
        if (IS_NUMBER(a->values[i]) && IS_NUMBER(b->values[i])) {
            total += AS_NUMBER(a->values[i]) * AS_NUMBER(b->values[i]);
        }
    }
    return NUMBER_VAL(total);
}

// ── Native: push / arr_push / stack_push ──────────────────────────
static Value pushNative(int argCount, Value* args) {
    if (argCount != 2) return NIL_VAL;
    if (IS_ARRAY(args[0])) {
        ObjArray* arr = AS_ARRAY(args[0]);
        arr->lock.lock();
        arrayPush(arr, cloneValue(args[1]));
        arr->lock.unlock();
        return NIL_VAL;
    }
    if (IS_STACK(args[0])) {
        ObjStack* st = AS_STACK(args[0]);
        st->lock.lock();
        st->data.push_back(cloneValue(args[1]));
        st->lock.unlock();
        return NIL_VAL;
    }
    return NIL_VAL;
}

// ── Native: Stack ─────────────────────────────────────────────────
static Value stackNative(int argCount, Value* args) {
    (void)argCount; (void)args;
    return OBJ_VAL(newStack());
}

static Value popNative(int argCount, Value* args) {
    if (argCount != 1) return NIL_VAL;
    if (IS_STACK(args[0])) {
        ObjStack* st = AS_STACK(args[0]);
        st->lock.lock();
        if (st->data.empty()) {
            st->lock.unlock();
            return NIL_VAL;
        }
        Value val = st->data.back();
        st->data.pop_back();
        st->lock.unlock();
        return val;
    }
    return NIL_VAL;
}

static Value peekNative(int argCount, Value* args) {
    if (argCount != 1) return NIL_VAL;
    if (IS_STACK(args[0])) {
        ObjStack* st = AS_STACK(args[0]);
        st->lock.lock();
        if (st->data.empty()) {
            st->lock.unlock();
            return NIL_VAL;
        }
        Value val = cloneValue(st->data.back());
        st->lock.unlock();
        return val;
    }
    return NIL_VAL;
}

// ── Native: Linked List ───────────────────────────────────────────
static Value listNative(int argCount, Value* args) {
    (void)argCount; (void)args;
    return OBJ_VAL(newList());
}

static inline Value listPushHelper(int argCount, Value* args, bool isFront) {
    if (argCount != 2 || !IS_LIST(args[0])) return NIL_VAL;
    ObjList* li = AS_LIST(args[0]);
    li->lock.lock();
    if (isFront) li->data.push_front(cloneValue(args[1]));
    else li->data.push_back(cloneValue(args[1]));
    li->lock.unlock();
    return NIL_VAL;
}

static Value listPushBackNative(int argCount, Value* args) {
    return listPushHelper(argCount, args, false);
}

static Value listPushFrontNative(int argCount, Value* args) {
    return listPushHelper(argCount, args, true);
}

static inline Value listPopHelper(int argCount, Value* args, bool isFront) {
    if (argCount != 1 || !IS_LIST(args[0])) return NIL_VAL;
    ObjList* li = AS_LIST(args[0]);
    li->lock.lock();
    if (li->data.empty()) {
        li->lock.unlock();
        return NIL_VAL;
    }
    Value val = isFront ? li->data.front() : li->data.back();
    if (isFront) li->data.pop_front();
    else li->data.pop_back();
    li->lock.unlock();
    return val;
}

static Value listPopBackNative(int argCount, Value* args) {
    return listPopHelper(argCount, args, false);
}

static Value listPopFrontNative(int argCount, Value* args) {
    return listPopHelper(argCount, args, true);
}

static inline Value listPeekHelper(int argCount, Value* args, bool isFront) {
    if (argCount != 1 || !IS_LIST(args[0])) return NIL_VAL;
    ObjList* li = AS_LIST(args[0]);
    li->lock.lock();
    if (li->data.empty()) {
        li->lock.unlock();
        return NIL_VAL;
    }
    Value val = cloneValue(isFront ? li->data.front() : li->data.back());
    li->lock.unlock();
    return val;
}

static Value listPeekFrontNative(int argCount, Value* args) {
    return listPeekHelper(argCount, args, true);
}

static Value listPeekBackNative(int argCount, Value* args) {
    return listPeekHelper(argCount, args, false);
}

// ── Native: Heap (Min-Heap & Max-Heap) ─────────────────────────────
static Value minHeapNative(int argCount, Value* args) {
    (void)argCount; (void)args;
    return OBJ_VAL(newHeap(true));
}

static Value maxHeapNative(int argCount, Value* args) {
    (void)argCount; (void)args;
    return OBJ_VAL(newHeap(false));
}

static Value heapNative(int argCount, Value* args) {
    bool isMin = true;
    if (argCount >= 1 && IS_STRING(args[0])) {
        ObjString* s = AS_STRING(args[0]);
        if (s->length == 3 && std::memcmp(s->chars, "max", 3) == 0) {
            isMin = false;
        }
    }
    return OBJ_VAL(newHeap(isMin));
}

static Value heapPushNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_HEAP(args[0])) return NIL_VAL;
    ObjHeap* hp = AS_HEAP(args[0]);
    hp->lock.lock();
    hp->data.push_back(cloneValue(args[1]));
    if (hp->isMinHeap) {
        std::push_heap(hp->data.begin(), hp->data.end(), [](const Value& a, const Value& b) {
            ValueLess less;
            return less(b, a);
        });
    } else {
        std::push_heap(hp->data.begin(), hp->data.end(), ValueLess());
    }
    hp->lock.unlock();
    return NIL_VAL;
}

static Value heapPopNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_HEAP(args[0])) return NIL_VAL;
    ObjHeap* hp = AS_HEAP(args[0]);
    hp->lock.lock();
    if (hp->data.empty()) {
        hp->lock.unlock();
        return NIL_VAL;
    }
    if (hp->isMinHeap) {
        std::pop_heap(hp->data.begin(), hp->data.end(), [](const Value& a, const Value& b) {
            ValueLess less;
            return less(b, a);
        });
    } else {
        std::pop_heap(hp->data.begin(), hp->data.end(), ValueLess());
    }
    Value topVal = hp->data.back();
    hp->data.pop_back();
    hp->lock.unlock();
    return topVal;
}

static Value heapPeekNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_HEAP(args[0])) return NIL_VAL;
    ObjHeap* hp = AS_HEAP(args[0]);
    hp->lock.lock();
    if (hp->data.empty()) {
        hp->lock.unlock();
        return NIL_VAL;
    }
    Value topVal = cloneValue(hp->data.front());
    hp->lock.unlock();
    return topVal;
}

// ── Native: Red-Black Tree (rbtree) ───────────────────────────────
static Value rbTreeNative(int argCount, Value* args) {
    (void)argCount; (void)args;
    return OBJ_VAL(newRBTree());
}

static Value rbInsertNative(int argCount, Value* args) {
    if (argCount != 3 || !IS_RBTREE(args[0])) return NIL_VAL;
    ObjRBTree* rb = AS_RBTREE(args[0]);
    rb->lock.lock();
    auto it = rb->map.find(args[1]);
    if (it != rb->map.end()) {
        dropValue(it->second);
        it->second = cloneValue(args[2]);
    } else {
        rb->map[cloneValue(args[1])] = cloneValue(args[2]);
    }
    rb->lock.unlock();
    return NIL_VAL;
}

static Value rbGetNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_RBTREE(args[0])) return NIL_VAL;
    ObjRBTree* rb = AS_RBTREE(args[0]);
    rb->lock.lock();
    auto it = rb->map.find(args[1]);
    if (it == rb->map.end()) {
        rb->lock.unlock();
        return NIL_VAL;
    }
    Value res = cloneValue(it->second);
    rb->lock.unlock();
    return res;
}

static Value rbHasNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_RBTREE(args[0])) return BOOL_VAL(false);
    ObjRBTree* rb = AS_RBTREE(args[0]);
    rb->lock.lock();
    bool exists = (rb->map.find(args[1]) != rb->map.end());
    rb->lock.unlock();
    return BOOL_VAL(exists);
}

static Value rbRemoveNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_RBTREE(args[0])) return BOOL_VAL(false);
    ObjRBTree* rb = AS_RBTREE(args[0]);
    rb->lock.lock();
    auto it = rb->map.find(args[1]);
    if (it != rb->map.end()) {
        dropValue(it->first);
        dropValue(it->second);
        rb->map.erase(it);
        rb->lock.unlock();
        return BOOL_VAL(true);
    }
    rb->lock.unlock();
    return BOOL_VAL(false);
}

static Value rbMinNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_RBTREE(args[0])) return NIL_VAL;
    ObjRBTree* rb = AS_RBTREE(args[0]);
    rb->lock.lock();
    if (rb->map.empty()) {
        rb->lock.unlock();
        return NIL_VAL;
    }
    Value minKey = cloneValue(rb->map.begin()->first);
    rb->lock.unlock();
    return minKey;
}

static Value rbMaxNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_RBTREE(args[0])) return NIL_VAL;
    ObjRBTree* rb = AS_RBTREE(args[0]);
    rb->lock.lock();
    if (rb->map.empty()) {
        rb->lock.unlock();
        return NIL_VAL;
    }
    Value maxKey = cloneValue(rb->map.rbegin()->first);
    rb->lock.unlock();
    return maxKey;
}

// ── Native: B-Tree ────────────────────────────────────────────────
static Value bTreeNative(int argCount, Value* args) {
    int degree = 3;
    if (argCount >= 1 && IS_NUMBER(args[0])) {
        degree = static_cast<int>(AS_NUMBER(args[0]));
    }
    return OBJ_VAL(newBTree(degree));
}

static Value bTreeInsertNative(int argCount, Value* args) {
    if (argCount != 3 || !IS_BTREE(args[0])) return NIL_VAL;
    ObjBTree* bt = AS_BTREE(args[0]);
    btreeInsert(bt, args[1], args[2]);
    return NIL_VAL;
}

static Value bTreeGetNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_BTREE(args[0])) return NIL_VAL;
    ObjBTree* bt = AS_BTREE(args[0]);
    Value val;
    if (btreeGet(bt, args[1], &val)) {
        return val;
    }
    return NIL_VAL;
}

static Value bTreeHasNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_BTREE(args[0])) return BOOL_VAL(false);
    ObjBTree* bt = AS_BTREE(args[0]);
    return BOOL_VAL(btreeHas(bt, args[1]));
}

static Value bTreeRemoveNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_BTREE(args[0])) return BOOL_VAL(false);
    ObjBTree* bt = AS_BTREE(args[0]);
    return BOOL_VAL(btreeRemove(bt, args[1]));
}

// ── Native: is_empty ──────────────────────────────────────────────
static Value isEmptyNative(int argCount, Value* args) {
    if (argCount != 1) return BOOL_VAL(true);
    Value v = args[0];
    if (IS_ARRAY(v)) return BOOL_VAL(AS_ARRAY(v)->count == 0);
    if (IS_STACK(v)) return BOOL_VAL(AS_STACK(v)->data.empty());
    if (IS_LIST(v)) return BOOL_VAL(AS_LIST(v)->data.empty());
    if (IS_HEAP(v)) return BOOL_VAL(AS_HEAP(v)->data.empty());
    if (IS_RBTREE(v)) return BOOL_VAL(AS_RBTREE(v)->map.empty());
    if (IS_BTREE(v)) return BOOL_VAL(AS_BTREE(v)->count == 0);
    if (IS_STRING(v)) return BOOL_VAL(AS_STRING(v)->length == 0);
    return BOOL_VAL(true);
}

// ── Native: c_sort (C++ std::sort in-place) ───────────────────────
static Value cSortNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_ARRAY(args[0])) return NIL_VAL;
    ObjArray* arr = AS_ARRAY(args[0]);
    arr->lock.lock();
    std::sort(arr->values, arr->values + arr->count, ValueLess{});
    arr->lock.unlock();
    return cloneValue(OBJ_VAL(arr));
}

// ── Native: floor ─────────────────────────────────────────────────
static Value floorNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    return NUMBER_VAL(std::floor(AS_NUMBER(args[0])));
}

// ── Native: sqrt ──────────────────────────────────────────────────
static Value sqrtNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    return NUMBER_VAL(std::sqrt(AS_NUMBER(args[0])));
}

// ── Native: pow ───────────────────────────────────────────────────
static Value powNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) return NIL_VAL;
    return NUMBER_VAL(std::pow(AS_NUMBER(args[0]), AS_NUMBER(args[1])));
}

// ── Native: abs ───────────────────────────────────────────────────
static Value absNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    return NUMBER_VAL(std::fabs(AS_NUMBER(args[0])));
}

// ── Native: rand / random ─────────────────────────────────────────
static Value randNative(int argCount, Value* args) {
    (void)argCount; (void)args;
    thread_local std::mt19937 rng(static_cast<unsigned int>(std::hash<std::thread::id>{}(std::this_thread::get_id()) ^ static_cast<size_t>(std::time(nullptr))));
    return NUMBER_VAL(static_cast<double>(rng()));
}

static Value randomNative(int argCount, Value* args) {
    thread_local std::mt19937 rng(static_cast<unsigned int>(std::hash<std::thread::id>{}(std::this_thread::get_id()) ^ static_cast<size_t>(std::time(nullptr))));
    if (argCount == 0) {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return NUMBER_VAL(dist(rng));
    }
    if (argCount == 1 && IS_NUMBER(args[0])) {
        double max = AS_NUMBER(args[0]);
        if (max <= 0) return NUMBER_VAL(0);
        std::uniform_int_distribution<int> dist(0, static_cast<int>(max) - 1);
        return NUMBER_VAL(static_cast<double>(dist(rng)));
    }
    return NUMBER_VAL(0);
}

// ── Stack helpers ─────────────────────────────────────────────────
void resetStack(VM* v) {
    v->stackTop = v->stack;
    v->frameCount = 0;
    while (v->openUpvalues != nullptr) {
        ObjUpvalue* upvalue = v->openUpvalues;
        v->openUpvalues = upvalue->next;
        dropObject(reinterpret_cast<Obj*>(upvalue));
    }
}

void runtimeError(VM* v, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (int i = v->frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &v->frames[i];
        ObjFunction* function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
        if (function->name == nullptr) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }
    fflush(stderr);
    resetStack(v);
}

void VM::push(Value value) {
    pushVM(this, value);
}

Value VM::pop() {
    return popVM(this);
}

static Value peek(VM* v, int distance) {
    return v->stackTop[-1 - distance];
}

void VM::defineNative(const char* name, NativeFn function) {
    ObjString* str = copyString(name, static_cast<int>(std::strlen(name)));
    ObjNative* native = newNative(function);
    globals.set(str, OBJ_VAL(native));
}

// ── Call ──────────────────────────────────────────────────────────
static bool call(VM* v, ObjClosure* closure, int argCount) {
    if (argCount == closure->function->arity) {
        if (closure->function->paramTypes != nullptr) {
            for (int i = 0; i < argCount; i++) {
                Value* argPtr = &v->stackTop[-argCount + i];
                ObjString* expectedType = closure->function->paramTypes[i];
                if (expectedType != nullptr && IS_INSTANCE(*argPtr)) {
                    ObjInstance* inst = AS_INSTANCE(*argPtr);
                    if (inst->structType == nullptr) {
                        Value structVal;
                        Table* globals = v->globalsPtr ? v->globalsPtr : &v->globals;
                        isli::SharedMutex* globalsLock = v->globalsLockPtr ? v->globalsLockPtr : &v->globalsLock;
                        globalsLock->lock_shared();
                        bool found = globals->get(expectedType, &structVal);
                        globalsLock->unlock_shared();
                        if (found && IS_STRUCT(structVal)) {
                            ObjInstance* casted = newInstance(AS_STRUCT(structVal));
                            inst->lock.lock();
                            for (int f = 0; f < inst->denseCount; f++) {
                                casted->denseFields[f].name = inst->denseFields[f].name;
                                casted->denseFields[f].value = cloneValue(inst->denseFields[f].value);
                            }
                            casted->denseCount = inst->denseCount;
                            for (int f = 0; f < inst->fields.capacity; f++) {
                                if (inst->fields.entries[f].key != nullptr) {
                                    casted->fields.set(inst->fields.entries[f].key,
                                                       cloneValue(*inst->fields.entries[f].valuePtr));
                                }
                            }
                            inst->lock.unlock();

                            Value oldVal = *argPtr;
                            *argPtr = OBJ_VAL(casted);
                            dropValue(oldVal);
                        }
                    }
                }
            }
        }
    }

    if (argCount != closure->function->arity) {
        runtimeError(v, "Expected %d arguments but got %d.",
                     closure->function->arity, argCount);
        return false;
    }

    if (v->frameCount == FRAMES_MAX) {
        runtimeError(v, "Stack overflow.");
        return false;
    }

    CallFrame* frame = &v->frames[v->frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = v->stackTop - argCount - 1;
    return true;
}

static bool callValue(VM* v, Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_CLOSURE: {
                ObjClosure* closure = AS_CLOSURE(callee);
                ObjFunction* fn = closure->function;

                if (fn->jitNative == nullptr && fn->name != nullptr) {
                    JitNativeFn compiledFn = isli::jit::Compiler::compileFunction(fn);
                    if (compiledFn != nullptr) {
                        __atomic_store_n(&fn->jitNative, reinterpret_cast<void*>(compiledFn), __ATOMIC_RELEASE);
                    }
                }

                if (fn->jitNative != nullptr && fn->arity == argCount) {
                    if (fn->paramTypes != nullptr) {
                        for (int i = 0; i < argCount; i++) {
                            Value* argPtr = &v->stackTop[-argCount + i];
                            ObjString* expectedType = fn->paramTypes[i];
                            if (expectedType != nullptr && IS_INSTANCE(*argPtr)) {
                                ObjInstance* inst = AS_INSTANCE(*argPtr);
                                if (inst->structType == nullptr) {
                                    Value structVal;
                                    Table* globals = v->globalsPtr ? v->globalsPtr : &v->globals;
                                    isli::SharedMutex* globalsLock = v->globalsLockPtr ? v->globalsLockPtr : &v->globalsLock;
                                    globalsLock->lock_shared();
                                    bool found = globals->get(expectedType, &structVal);
                                    globalsLock->unlock_shared();
                                    if (found && IS_STRUCT(structVal)) {
                                        ObjInstance* casted = newInstance(AS_STRUCT(structVal));
                                        inst->lock.lock();
                                        for (int f = 0; f < inst->denseCount; f++) {
                                            casted->denseFields[f].name = inst->denseFields[f].name;
                                            casted->denseFields[f].value = cloneValue(inst->denseFields[f].value);
                                        }
                                        casted->denseCount = inst->denseCount;
                                        for (int f = 0; f < inst->fields.capacity; f++) {
                                            if (inst->fields.entries[f].key != nullptr) {
                                                casted->fields.set(inst->fields.entries[f].key,
                                                                   cloneValue(*inst->fields.entries[f].valuePtr));
                                            }
                                        }
                                        inst->lock.unlock();

                                        Value oldVal = *argPtr;
                                        *argPtr = OBJ_VAL(casted);
                                        dropValue(oldVal);
                                    }
                                }
                            }
                        }
                    }
                    JitNativeFn nativeFn = reinterpret_cast<JitNativeFn>(fn->jitNative);
                    Value* slots = v->stackTop - argCount - 1;
                    slots[0] = callee;
                    uint64_t resBits = nativeFn(v, slots, closure);
                    Value res(resBits);
                    v->stackTop = slots;
                    pushVM(v, res);
                    return true;
                }
                return call(v, closure, argCount);
            }
            case OBJ_NATIVE: {
                NativeFn native = AS_NATIVE(callee);
                Value result = native(argCount, v->stackTop - argCount);
                for (int i = 0; i < argCount; i++) {
                    dropValue(v->stackTop[-1 - i]);
                }
                dropValue(callee);
                v->stackTop -= argCount + 1;
                pushVM(v, result);
                return true;
            }
            case OBJ_STRUCT: {
                ObjStruct* structType = AS_STRUCT(callee);
                if (argCount != 0) {
                    runtimeError(v, "Expected 0 arguments but got %d.", argCount);
                    return false;
                }
                ObjInstance* instance = newInstance(structType);
                Value oldCallee = v->stackTop[-1];
                v->stackTop[-1] = OBJ_VAL(instance);
                dropValue(oldCallee);
                return true;
            }
            default:
                break;
        }
    }
    runtimeError(v, "Can only call functions and structs.");
    return false;
}

// ── Upvalue capture / close ───────────────────────────────────────
static ObjUpvalue* captureUpvalue(VM* v, Value* local) {
    ObjUpvalue* prevUpvalue = nullptr;
    ObjUpvalue* upvalue = v->openUpvalues;
    while (upvalue != nullptr && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }
    
    if (upvalue != nullptr && upvalue->location == local) {
        return upvalue;
    }

    ObjUpvalue* createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == nullptr) {
        v->openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

static void closeUpvalues(VM* v, Value* last) {
    while (v->openUpvalues != nullptr && v->openUpvalues->location >= last) {
        ObjUpvalue* upvalue = v->openUpvalues;
        upvalue->closed = cloneValue(*upvalue->location);
        upvalue->location = &upvalue->closed;
        v->openUpvalues = upvalue->next;
        dropObject(reinterpret_cast<Obj*>(upvalue));
    }
}

// ── Helpers ───────────────────────────────────────────────────────
static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate(VM* v) {
    ObjString* b = AS_STRING(peek(v, 0));
    ObjString* a = AS_STRING(peek(v, 1));

    int length = a->length + b->length;
    char* chars = static_cast<char*>(std::malloc(length + 1));
    if (chars == nullptr) std::exit(1);
    std::memcpy(chars, a->chars, a->length);
    std::memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = takeString(chars, length);
    Value bVal = popVM(v);
    Value aVal = popVM(v);
    dropValue(bVal);
    dropValue(aVal);
    pushVM(v, OBJ_VAL(result));
}

// ── Priority Task Scheduler & SIMT Engine ─────────────────────────
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <queue>

struct alignas(64) TaskScheduler {
    static const int MAX_WORKERS = 64;
    std::thread threads[MAX_WORKERS];
    int workerCount = 0;

    std::priority_queue<Task> taskQueue;
    alignas(64) std::atomic<uint64_t> sequenceCounter{0};
    alignas(64) std::atomic<int> activeTasks{0};
    alignas(64) std::atomic<int64_t> remainingSIMTJobs{0};

    alignas(64) std::atomic<void*> currentJob{nullptr};
    alignas(64) std::atomic<uint64_t> jobEpoch{0};

    alignas(64) std::mutex lock;
    std::condition_variable cv_work;
    std::condition_variable cv_simt_done;

    bool isRunning = true;
};

struct alignas(64) WorkerRange {
    std::atomic<uint64_t> packed{0};
    char pad[64 - sizeof(std::atomic<uint64_t>)];

    static uint64_t pack(uint32_t b, uint32_t e) {
        return (static_cast<uint64_t>(e) << 32) | b;
    }
    static uint32_t beginOf(uint64_t p) { return static_cast<uint32_t>(p); }
    static uint32_t endOf(uint64_t p) { return static_cast<uint32_t>(p >> 32); }
};
static_assert(sizeof(WorkerRange) == 64, "WorkerRange must occupy one cache line");

static constexpr int REDUCE_LANE_STRIDE = 8;

struct ParallelJob {
    ObjClosure* closure = nullptr;
    JitNativeFn native = nullptr;
    int dimCount = 1;
    int64_t x = 1, y = 1, z = 1, total = 0;
    int reduceOp = 0;
    int tileFlags = 0;
    int64_t tx = 1, ty = 1;
    int64_t blockSize = 256;
    double* blockPartials = nullptr;
    double* blockLaneAcc = nullptr;
    WorkerRange ranges[ISLI_MAX_WORKERS];
    alignas(64) std::atomic<int64_t> remaining{0};
    alignas(64) std::atomic<int> inFlight{0};
    alignas(64) std::atomic<int> seen{0};
    alignas(64) std::atomic<bool> failed{false};
    int participantCount = 0;
};

static TaskScheduler scheduler;
static ParallelJob g_job;

/**
 * Print a kernel/job error once. Does not reset the host VM stack.
 * Sets job.failed and wakes the dispatch waiter.
 */
/**
 * Kernel/job error: set failed, wake waiter, never resetStack on the host VM.
 * Outside a job, falls back to runtimeError (resets that VM).
 */
void isliMarkJobFailed(const char* format, ...) {
    char buf[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    ParallelJob* job = tl_currentJob;
    if (job != nullptr) {
        bool expected = false;
        bool first = job->failed.compare_exchange_strong(expected, true);
        scheduler.cv_simt_done.notify_all();
        if (!first) return;
        fputs(buf, stderr);
        fputs("\n", stderr);
        fflush(stderr);
        return;
    }
    runtimeError(tl_currentVM ? tl_currentVM : &vm, "%s", buf);
}

static bool jobFinished(const ParallelJob& job) {
    return job.failed.load(std::memory_order_acquire)
        || job.remaining.load(std::memory_order_acquire) == 0;
}

/**
 * Multiply non-negative int64 values. Returns false on overflow (no UB).
 */
static bool mulNonNeg(int64_t a, int64_t b, int64_t* out) {
    if (a == 0 || b == 0) {
        *out = 0;
        return true;
    }
    if (a < 0 || b < 0) return false;
    if (a > INT64_MAX / b) return false;
    *out = a * b;
    return true;
}

struct WorkerInfo {
    int id = 0;
    int logicalCpu = 0;
    int numaNode = 0;
};
static WorkerInfo g_workers[TaskScheduler::MAX_WORKERS];
static thread_local int tl_workerId = -1;
static thread_local uint64_t tl_lastJobEpoch = 0;
static thread_local VM* tl_workerVM = nullptr;

static constexpr int KERNEL_VM_NEST = 4;
static thread_local VM* tl_kernelVMs[KERNEL_VM_NEST] = {};
static thread_local int tl_kernelDepth = 0;

#if defined(_WIN32) || defined(_WIN64)
static void pinCurrentThread(int logicalCpu) {
    GROUP_AFFINITY ga{};
    ga.Group = static_cast<WORD>(logicalCpu / 64);
    ga.Mask = 1ull << (logicalCpu % 64);
    SetThreadGroupAffinity(GetCurrentThread(), &ga, nullptr);
}
#else
static void pinCurrentThread(int logicalCpu) {
    (void)logicalCpu;
}
#endif

static inline void cpuPause() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
    #if defined(_MSC_VER)
        _mm_pause();
    #elif defined(__GNUC__) || defined(__clang__)
        __builtin_ia32_pause();
    #endif
#endif
}

static VM* acquireKernelVM() {
    int d = tl_kernelDepth;
    if (d >= KERNEL_VM_NEST) return nullptr;
    if (tl_kernelVMs[d] == nullptr) {
        tl_kernelVMs[d] = new VM();
        tl_kernelVMs[d]->globalsPtr = &vm.globals;
        tl_kernelVMs[d]->globalsLockPtr = &vm.globalsLock;
    }
    VM* kv = tl_kernelVMs[d];
    resetStack(kv);
    kv->openUpvalues = nullptr;
    kv->frameCount = 0;
    tl_kernelDepth = d + 1;
    return kv;
}

static void releaseKernelVM() {
    if (tl_kernelDepth > 0) tl_kernelDepth--;
}

static inline double reduceOp2(int op, double a, double b) {
    switch (op) {
        case 1: return a + b;
        case 2: return a * b;
        case 3: return a < b ? a : b;
        default: return a > b ? a : b;
    }
}

static inline double reduceIdentity(int op) {
    switch (op) {
        case 1: return 0.0;
        case 2: return 1.0;
        case 3: return std::numeric_limits<double>::infinity();
        default: return -std::numeric_limits<double>::infinity();
    }
}

static inline void accumulateBlock(ParallelJob& job, int64_t i, Value r) {
    if (job.reduceOp == 0) return;
    if (!IS_NUMBER(r)) {
        isliMarkJobFailed("reduce kernel must return a number.");
        return;
    }
    int64_t blk = i / job.blockSize;
    int lane = static_cast<int>(i & 3);
    double* acc = job.blockLaneAcc + blk * REDUCE_LANE_STRIDE;
    acc[lane] = reduceOp2(job.reduceOp, acc[lane], AS_NUMBER(r));
}

static void finishReduce(ParallelJob& job, Value* out) {
    if (job.reduceOp == 0 || out == nullptr) return;
    int64_t blocks = (job.total + job.blockSize - 1) / job.blockSize;
    if (blocks <= 0) {
        *out = NUMBER_VAL(reduceIdentity(job.reduceOp));
        return;
    }
    for (int64_t b = 0; b < blocks; b++) {
        double* a = job.blockLaneAcc + b * REDUCE_LANE_STRIDE;
        job.blockPartials[b] = reduceOp2(job.reduceOp,
            reduceOp2(job.reduceOp, a[0], a[1]),
            reduceOp2(job.reduceOp, a[2], a[3]));
    }
    int64_t n = 1;
    while (n < blocks) n <<= 1;
    std::vector<double> lvl(static_cast<size_t>(n), reduceIdentity(job.reduceOp));
    std::copy(job.blockPartials, job.blockPartials + blocks, lvl.begin());
    for (int64_t w = n; w > 1; w >>= 1) {
        for (int64_t k = 0; k < w / 2; k++) {
            lvl[static_cast<size_t>(k)] = reduceOp2(
                job.reduceOp,
                lvl[static_cast<size_t>(2 * k)],
                lvl[static_cast<size_t>(2 * k + 1)]);
        }
    }
    *out = NUMBER_VAL(lvl[0]);
}

static void mapIndex(const ParallelJob& job, int64_t i, int64_t& ix, int64_t& iy, int64_t& iz, bool& skip) {
    skip = false;
    ix = i; iy = 0; iz = 0;
    if (job.tileFlags != 0 && job.dimCount == 2) {
        int64_t tilesX = (job.x + job.tx - 1) / job.tx;
        int64_t tileArea = job.tx * job.ty;
        int64_t t = i / tileArea;
        int64_t r = i % tileArea;
        int64_t bx = t % tilesX;
        int64_t by = t / tilesX;
        ix = bx * job.tx + (r % job.tx);
        iy = by * job.ty + (r / job.tx);
        if (ix >= job.x || iy >= job.y) skip = true;
        return;
    }
    if (job.dimCount >= 2) {
        ix = i % job.x;
        iy = (i / job.x) % job.y;
        iz = i / (job.x * job.y);
    }
}

static void runKernelRange(VM* wvm, ParallelJob& job, int64_t begin, int64_t end) {
    ObjClosure* cl = job.closure;
    int arity = cl->function->arity;
    Value slots[64];
    slots[0] = OBJ_VAL(cl);
    VM* savedVM = tl_currentVM;
    ParallelJob* savedJob = tl_currentJob;
    tl_currentVM = wvm;
    tl_currentJob = &job;

    for (int64_t i = begin; i < end && !job.failed.load(std::memory_order_relaxed); i++) {
        int64_t ix = 0, iy = 0, iz = 0;
        bool skip = false;
        mapIndex(job, i, ix, iy, iz, skip);
        if (skip) {
            if (job.reduceOp) accumulateBlock(job, i, NUMBER_VAL(reduceIdentity(job.reduceOp)));
            continue;
        }

        slots[1] = NUMBER_VAL(static_cast<double>(job.dimCount == 1 ? i : ix));
        if (arity >= 2) slots[2] = NUMBER_VAL(static_cast<double>(iy));
        if (arity >= 3) slots[3] = NUMBER_VAL(static_cast<double>(iz));
        for (int e = 3; e < arity && e < 63; e++) slots[e + 1] = NIL_VAL;

        uint64_t rbits = NIL_VAL.bits;
        if (job.native != nullptr) {
            rbits = job.native(wvm, slots, cl);
        } else {
            VM* ivm = acquireKernelVM();
            if (ivm == nullptr) {
                bool expected = false;
                job.failed.compare_exchange_strong(expected, true);
                break;
            }
            for (int s = 0; s <= arity; s++) pushVM(ivm, slots[s]);
            bool ok = call(ivm, cl, arity) && run(ivm) == INTERPRET_OK;
            if (ok) {
                rbits = (ivm->stackTop > ivm->stack) ? popVM(ivm).bits : NIL_VAL.bits;
            }
            while (ivm->stackTop > ivm->stack) dropValue(popVM(ivm));
            releaseKernelVM();
            if (!ok) {
                bool expected = false;
                job.failed.compare_exchange_strong(expected, true);
                break;
            }
        }
        if (job.reduceOp) accumulateBlock(job, i, Value(rbits));
    }
    tl_currentJob = savedJob;
    tl_currentVM = savedVM;
}

static int64_t claimOwnBlock(WorkerRange& r) {
    uint64_t p = r.packed.load(std::memory_order_acquire);
    for (;;) {
        uint32_t b = WorkerRange::beginOf(p);
        uint32_t e = WorkerRange::endOf(p);
        if (b >= e) return -1;
        if (r.packed.compare_exchange_weak(p, WorkerRange::pack(b + 1, e),
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            return static_cast<int64_t>(b);
        }
    }
}

static bool stealHalf(WorkerRange& victim, uint32_t& outBegin, uint32_t& outEnd) {
    uint64_t p = victim.packed.load(std::memory_order_acquire);
    for (;;) {
        uint32_t b = WorkerRange::beginOf(p);
        uint32_t e = WorkerRange::endOf(p);
        if (e - b < 2) return false;
        uint32_t mid = b + (e - b) / 2;
        if (victim.packed.compare_exchange_weak(p, WorkerRange::pack(b, mid),
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            outBegin = mid;
            outEnd = e;
            return true;
        }
    }
}

static void workOnJob(VM* wvm, ParallelJob& job, int self) {
    job.inFlight.fetch_add(1, std::memory_order_acq_rel);
    WorkerRange& mine = job.ranges[self];
    for (;;) {
        if (job.failed.load(std::memory_order_relaxed)) {
            scheduler.cv_simt_done.notify_all();
            break;
        }
        int64_t blk = claimOwnBlock(mine);
        if (blk >= 0) {
            int64_t b = blk * job.blockSize;
            int64_t e = std::min(b + job.blockSize, job.total);
            runKernelRange(wvm, job, b, e);
            int64_t prev = job.remaining.fetch_sub(e - b, std::memory_order_acq_rel);
            if (prev - (e - b) == 0) scheduler.cv_simt_done.notify_all();
            continue;
        }
        bool stole = false;
        for (int k = 1; k < job.participantCount && !stole; k++) {
            int victim = (self + k) % job.participantCount;
            uint32_t nb = 0, ne = 0;
            if (stealHalf(job.ranges[victim], nb, ne)) {
                mine.packed.store(WorkerRange::pack(nb, ne), std::memory_order_release);
                stole = true;
            }
        }
        if (!stole) break;
    }
    job.inFlight.fetch_sub(1, std::memory_order_acq_rel);
}

static void workerLoop(int workerId) {
    tl_workerId = workerId;
    if (workerId >= 0 && workerId < scheduler.workerCount) {
        pinCurrentThread(g_workers[workerId].logicalCpu);
    }

    auto workerVMPtr = std::make_unique<VM>();
    VM& workerVM = *workerVMPtr;
    workerVM.globalsPtr = &vm.globals;
    workerVM.globalsLockPtr = &vm.globalsLock;
    tl_workerVM = &workerVM;
    tl_currentVM = &workerVM;

    for (;;) {
        ParallelJob* job = static_cast<ParallelJob*>(scheduler.currentJob.load(std::memory_order_acquire));
        uint64_t epoch = scheduler.jobEpoch.load(std::memory_order_acquire);
        if (job != nullptr && epoch != tl_lastJobEpoch) {
            tl_lastJobEpoch = epoch;
            job->seen.fetch_add(1, std::memory_order_acq_rel);
            workOnJob(&workerVM, *job, workerId);
            continue;
        }

        Task task;
        {
            int spin = 0;
            while (scheduler.isRunning && spin < 200) {
                job = static_cast<ParallelJob*>(scheduler.currentJob.load(std::memory_order_acquire));
                epoch = scheduler.jobEpoch.load(std::memory_order_acquire);
                if (job != nullptr && epoch != tl_lastJobEpoch) break;
                cpuPause();
                spin++;
            }

            std::unique_lock<std::mutex> lk(scheduler.lock);
            scheduler.cv_work.wait(lk, [&] {
                if (!scheduler.isRunning) return true;
                ParallelJob* j = static_cast<ParallelJob*>(scheduler.currentJob.load(std::memory_order_acquire));
                uint64_t e = scheduler.jobEpoch.load(std::memory_order_acquire);
                return j != nullptr && e != tl_lastJobEpoch;
            });

            if (!scheduler.isRunning &&
                scheduler.currentJob.load(std::memory_order_acquire) == nullptr) {
                break;
            }

            job = static_cast<ParallelJob*>(scheduler.currentJob.load(std::memory_order_acquire));
            epoch = scheduler.jobEpoch.load(std::memory_order_acquire);
            if (job != nullptr && epoch != tl_lastJobEpoch) {
                lk.unlock();
                tl_lastJobEpoch = epoch;
                job->seen.fetch_add(1, std::memory_order_acq_rel);
                workOnJob(&workerVM, *job, workerId);
                continue;
            }

            if (scheduler.taskQueue.empty()) continue;
            task = scheduler.taskQueue.top();
            scheduler.taskQueue.pop();
        }

        ObjClosure* cl = task.closure;
        if (cl == nullptr) continue;

        resetStack(&workerVM);
        pushVM(&workerVM, OBJ_VAL(cl));
        for (int a = 0; a < task.argCount; a++) {
            pushVM(&workerVM, task.args[a]);
        }
        if (cl->function->jitNative != nullptr) {
            JitNativeFn nativeFn = reinterpret_cast<JitNativeFn>(cl->function->jitNative);
            nativeFn(&workerVM, workerVM.stack, cl);
        } else {
            cl->function->hotness++;
            if (cl->function->hotness >= isli::jit::JIT_HOT_THRESHOLD) {
                isli::jit::Manager::instance().requestCompilation(cl->function);
            }
            if (call(&workerVM, cl, task.argCount)) {
                run(&workerVM);
            }
        }
        while (workerVM.stackTop > workerVM.stack) {
            dropValue(popVM(&workerVM));
        }
        scheduler.activeTasks.fetch_sub(1, std::memory_order_release);
        dropObject(reinterpret_cast<Obj*>(cl));
        for (int a = 0; a < task.argCount; a++) {
            dropValue(task.args[a]);
        }
    }
}

static void initSIMTEngine() {
    int cores = static_cast<int>(std::thread::hardware_concurrency());
    if (cores < 1) cores = 1;
    if (cores > TaskScheduler::MAX_WORKERS) cores = TaskScheduler::MAX_WORKERS;

    // Leave logical CPU 0 for the main / OS thread; workers occupy 1..N-1.
    // Main still joins parallel jobs as the last participant, so all cores work.
    int workers = (cores > 1) ? (cores - 1) : 0;
    scheduler.workerCount = workers;
    scheduler.isRunning = true;
    scheduler.remainingSIMTJobs.store(0, std::memory_order_relaxed);

    isli::jit::Manager::instance().init();

    const CpuFeatures& cpu = cpuFeatures();
    int totalCores = cpu.logicalCores > 0 ? cpu.logicalCores : cores;
    for (int i = 0; i < scheduler.workerCount; i++) {
        g_workers[i].id = i;
        int coreIndex = (totalCores > 1) ? ((i + 1) % totalCores) : 0;
        g_workers[i].logicalCpu = coreIndex;
#if defined(_WIN32) || defined(_WIN64)
        UCHAR node = 0;
        if (GetNumaProcessorNode(static_cast<UCHAR>(coreIndex), &node)) {
            g_workers[i].numaNode = node;
        }
#endif
        scheduler.threads[i] = std::thread(workerLoop, i);
    }
}

static bool dispatchJob(ObjClosure* closure, int dimCount, int64_t x, int64_t y, int64_t z,
                        int tileFlags, int64_t tx, int64_t ty, int reduceOp, Value* reduceOut) {
    if (!scheduler.isRunning) return true;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (z < 0) z = 0;
    if (tx < 1) tx = 1;
    if (ty < 1) ty = 1;

    int64_t total = 0;
    if (!mulNonNeg(x, y, &total) || !mulNonNeg(total, z, &total)) {
        runtimeError(tl_currentVM ? tl_currentVM : &vm, "dispatch too large.");
        return false;
    }
    if (tileFlags != 0 && dimCount == 2) {
        int64_t tilesX = (x + tx - 1) / tx;
        int64_t tilesY = (y + ty - 1) / ty;
        int64_t t = 0, area = 0;
        if (!mulNonNeg(tilesX, tilesY, &t) || !mulNonNeg(tx, ty, &area)
            || !mulNonNeg(t, area, &total)) {
            runtimeError(tl_currentVM ? tl_currentVM : &vm, "dispatch too large.");
            return false;
        }
    }
    if (total <= 0) {
        if (reduceOut) *reduceOut = NUMBER_VAL(reduceIdentity(reduceOp));
        return true;
    }

    ObjFunction* fn = closure->function;
    if (fn->jitNative == nullptr && !fn->jitDeclined) {
        JitNativeFn compiledFn = isli::jit::Compiler::compileFunction(fn);
        if (compiledFn != nullptr) {
            __atomic_store_n(&fn->jitNative, reinterpret_cast<void*>(compiledFn), __ATOMIC_RELEASE);
        }
    }

    int64_t blockSize = 256;
    int64_t blocks = (total + blockSize - 1) / blockSize;
    if (blocks > 0xFFFFFFFFll) {
        runtimeError(tl_currentVM ? tl_currentVM : &vm, "dispatch too large.");
        return false;
    }

    auto fillJob = [&](ParallelJob& job) {
        job.closure = closure;
        job.native = reinterpret_cast<JitNativeFn>(fn->jitNative);
        job.dimCount = dimCount;
        job.x = x; job.y = y; job.z = z;
        job.total = total;
        job.reduceOp = reduceOp;
        job.tileFlags = tileFlags;
        job.tx = tx; job.ty = ty;
        job.blockSize = blockSize;
        job.blockLaneAcc = nullptr;
        job.blockPartials = nullptr;
        job.failed.store(false, std::memory_order_relaxed);
        job.remaining.store(total, std::memory_order_relaxed);
        job.inFlight.store(0, std::memory_order_relaxed);
        job.seen.store(0, std::memory_order_relaxed);
        job.participantCount = 0;
        if (reduceOp != 0) {
            job.blockLaneAcc = new double[static_cast<size_t>(blocks * REDUCE_LANE_STRIDE)];
            job.blockPartials = new double[static_cast<size_t>(blocks)];
            double id = reduceIdentity(reduceOp);
            for (int64_t i = 0; i < blocks * REDUCE_LANE_STRIDE; i++) job.blockLaneAcc[i] = id;
        }
    };

    VM* hostVM = (tl_workerVM != nullptr) ? tl_workerVM : &vm;
    bool nested = (tl_workerId >= 0)
        || (scheduler.currentJob.load(std::memory_order_acquire) != nullptr);
    if (total < 4096 || nested || scheduler.workerCount <= 0) {
        ParallelJob job;
        fillJob(job);
        runKernelRange(hostVM, job, 0, total);
        if (!job.failed.load(std::memory_order_acquire)) finishReduce(job, reduceOut);
        delete[] job.blockLaneAcc;
        delete[] job.blockPartials;
        return true;
    }

    fillJob(g_job);
    int P = scheduler.workerCount + 1;
    if (P > ISLI_MAX_WORKERS) P = ISLI_MAX_WORKERS;
    g_job.participantCount = P;
    for (int w = 0; w < P; w++) {
        uint32_t bBegin = static_cast<uint32_t>((blocks * static_cast<int64_t>(w)) / P);
        uint32_t bEnd = static_cast<uint32_t>((blocks * static_cast<int64_t>(w + 1)) / P);
        g_job.ranges[w].packed.store(WorkerRange::pack(bBegin, bEnd), std::memory_order_relaxed);
    }
    g_job.remaining.store(total, std::memory_order_release);

    scheduler.jobEpoch.fetch_add(1, std::memory_order_acq_rel);
    scheduler.currentJob.store(&g_job, std::memory_order_release);
    scheduler.cv_work.notify_all();

    workOnJob(&vm, g_job, P - 1);

    uint32_t spins = 0;
    while (!jobFinished(g_job)) {
        if (spins < 25000) {
            cpuPause();
            spins++;
            continue;
        }
        std::unique_lock<std::mutex> lk(scheduler.lock);
        scheduler.cv_simt_done.wait_for(lk, std::chrono::microseconds(200), [&] {
            return jobFinished(g_job);
        });
    }
    while (g_job.inFlight.load(std::memory_order_acquire) != 0) {
        cpuPause();
    }
    while (g_job.seen.load(std::memory_order_acquire) < scheduler.workerCount) {
        cpuPause();
    }
    scheduler.currentJob.store(nullptr, std::memory_order_release);

    if (!g_job.failed.load(std::memory_order_acquire)) finishReduce(g_job, reduceOut);
    delete[] g_job.blockLaneAcc;
    delete[] g_job.blockPartials;
    g_job.blockLaneAcc = nullptr;
    g_job.blockPartials = nullptr;
    return true;
}

static void shutdownSIMTEngine() {
    isli::jit::Manager::instance().shutdown();
    {
        std::lock_guard<std::mutex> lk(scheduler.lock);
        scheduler.isRunning = false;
        scheduler.cv_work.notify_all();
    }
    for (int i = 0; i < scheduler.workerCount; i++) {
        if (scheduler.threads[i].joinable()) {
            scheduler.threads[i].join();
        }
    }
}

// ── VM init / free ────────────────────────────────────────────────
void VM::init() {
    resetStack(this);
    globalsPtr = &globals;
    globalsLockPtr = &globalsLock;
    globals.init();

    defineNative("clock", clockNative);
    defineNative("cpu_info", cpuInfoNative);

    defineNative("len", lenNative);
    defineNative("sv_len", lenNative);
    defineNative("stakel", svTakeLeftNative);
    defineNative("staker", svTakeRightNative);
    defineNative("schopl", svChopLeftNative);
    defineNative("schopr", svChopRightNative);
    defineNative("sdrop_left", svChopLeftNative);
    defineNative("sdrop_right", svChopRightNative);
    defineNative("sv_trim_left", svTrimLeftNative);
    defineNative("sv_trim_right", svTrimRightNative);
    defineNative("strim", svTrimNative);
    defineNative("ssub", svSubNative);
    defineNative("sfind", svFindNative);
    defineNative("stakefl", svSplitLeftNative);
    defineNative("stakefr", svSplitRightNative);
    
    defineNative("array", arrayNative);
    defineNative("f32buf", bufNewNative<BUF_F32>);
    defineNative("f64buf", bufNewNative<BUF_F64>);
    defineNative("i32buf", bufNewNative<BUF_I32>);
    defineNative("u8buf",  bufNewNative<BUF_U8>);
    defineNative("buf_type", bufTypeNative);
    defineNative("buf_from_array", bufFromArrayNative);
    defineNative("buf_to_array", bufToArrayNative);
    defineNative("atomic_add", atomicAddNative);
    defineNative("copy", copyNative);
    defineNative("clone", copyNative);
    defineNative("push", pushNative);
    defineNative("arr_push", pushNative);
    defineNative("stack_push", pushNative);
    defineNative("c_sort", cSortNative);
    defineNative("sort", cSortNative);
    defineNative("simd_copy", simdCopyNative);
    defineNative("simd_fill", simdFillNative);
    defineNative("simd_add", simdAddNative);
    defineNative("simd_sub", simdSubNative);
    defineNative("simd_mul", simdMulNative);
    defineNative("simd_div", simdDivNative);
    defineNative("simd_fma", simdFmaNative);
    defineNative("simd_sum", simdSumNative);
    defineNative("simd_dot", simdDotNative);
    defineNative("floor", floorNative);
    defineNative("sqrt", sqrtNative);
    defineNative("pow", powNative);
    defineNative("abs", absNative);
    defineNative("rand", randNative);
    defineNative("random", randomNative);

    defineNative("stack", stackNative);
    defineNative("pop", popNative);
    defineNative("stack_pop", popNative);
    defineNative("peek", peekNative);
    defineNative("top", peekNative);
    defineNative("stack_peek", peekNative);

    defineNative("list", listNative);
    defineNative("push_back", listPushBackNative);
    defineNative("list_push_back", listPushBackNative);
    defineNative("push_front", listPushFrontNative);
    defineNative("list_push_front", listPushFrontNative);
    defineNative("pop_back", listPopBackNative);
    defineNative("list_pop_back", listPopBackNative);
    defineNative("pop_front", listPopFrontNative);
    defineNative("list_pop_front", listPopFrontNative);
    defineNative("peek_front", listPeekFrontNative);
    defineNative("front", listPeekFrontNative);
    defineNative("peek_back", listPeekBackNative);
    defineNative("back", listPeekBackNative);

    defineNative("min_heap", minHeapNative);
    defineNative("max_heap", maxHeapNative);
    defineNative("heap", heapNative);
    defineNative("heap_push", heapPushNative);
    defineNative("heap_pop", heapPopNative);
    defineNative("heap_peek", heapPeekNative);

    defineNative("rbtree", rbTreeNative);
    defineNative("rb_insert", rbInsertNative);
    defineNative("tree_insert", rbInsertNative);
    defineNative("rb_get", rbGetNative);
    defineNative("tree_get", rbGetNative);
    defineNative("rb_has", rbHasNative);
    defineNative("tree_has", rbHasNative);
    defineNative("rb_remove", rbRemoveNative);
    defineNative("tree_remove", rbRemoveNative);
    defineNative("rb_min", rbMinNative);
    defineNative("rb_max", rbMaxNative);

    defineNative("btree", bTreeNative);
    defineNative("btree_insert", bTreeInsertNative);
    defineNative("btree_get", bTreeGetNative);
    defineNative("btree_has", bTreeHasNative);
    defineNative("btree_remove", bTreeRemoveNative);

    defineNative("is_empty", isEmptyNative);
    
    initSIMTEngine();
}

void VM::free() {
    shutdownSIMTEngine();
    globals.free();
    stackTop = stack;
    unownedObjects = nullptr;
    freeSlabs();
}

// ── run (VM dispatch loop) ────────────────────────────────────────
static InterpretResult run(VM* v, int stopFrameCount) {
    Table* globals = v->globalsPtr ? v->globalsPtr : &v->globals;
    isli::SharedMutex* globalsLock = v->globalsLockPtr;
    CallFrame* frame = &v->frames[v->frameCount - 1];
    tl_currentVM = v;

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() \
    (frame->ip += 2, static_cast<uint16_t>((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() \
    (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op) \
    do { \
        Value bVal = v->stackTop[-1]; \
        Value aVal = v->stackTop[-2]; \
        if (!IS_NUMBER(bVal) || !IS_NUMBER(aVal)) { \
            runtimeError(v, "Operands must be numbers."); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        v->stackTop[-2] = valueType(AS_NUMBER(aVal) op AS_NUMBER(bVal)); \
        v->stackTop--; \
    } while (false)

#if defined(__GNUC__) || defined(__clang__)
#define DIRECT_THREADING
#endif

#ifdef DIRECT_THREADING
#define DISPATCH() goto *dispatchTable[READ_BYTE()]
#define OP_HANDLER(op) op_##op:
#else
#define DISPATCH() break
#define OP_HANDLER(op) case op:
#endif

#ifdef DIRECT_THREADING
    static const void* const dispatchTable[] = {
        &&op_OP_CONSTANT,
        &&op_OP_NIL,
        &&op_OP_TRUE,
        &&op_OP_FALSE,
        &&op_OP_POP,
        &&op_OP_GET_LOCAL,
        &&op_OP_SET_LOCAL,
        &&op_OP_GET_GLOBAL,
        &&op_OP_DEFINE_GLOBAL,
        &&op_OP_SET_GLOBAL,
        &&op_OP_GET_UPVALUE,
        &&op_OP_SET_UPVALUE,
        &&op_OP_GET_PROPERTY,
        &&op_OP_SET_PROPERTY,
        &&op_OP_EQUAL,
        &&op_OP_GREATER,
        &&op_OP_LESS,
        &&op_OP_ADD,
        &&op_OP_SUBTRACT,
        &&op_OP_MULTIPLY,
        &&op_OP_DIVIDE,
        &&op_OP_MODULO,
        &&op_OP_NOT,
        &&op_OP_NEGATE,
        &&op_OP_PRINT,
        &&op_OP_JUMP,
        &&op_OP_JUMP_IF_FALSE,
        &&op_OP_LOOP,
        &&op_OP_CALL,
        &&op_OP_CLOSURE,
        &&op_OP_CLOSE_UPVALUE,
        &&op_OP_RETURN,
        &&op_OP_STRUCT,
        &&op_OP_DISPATCH,
        &&op_OP_DISPATCH2,
        &&op_OP_GET_LOCAL_PTR,
        &&op_OP_GET_GLOBAL_PTR,
        &&op_OP_GET_PROPERTY_PTR,
        &&op_OP_GET_UPVALUE_PTR,
        &&op_OP_DEREF,
        &&op_OP_SET_DEREF,
        &&op_OP_ANONYMOUS_STRUCT,
        &&op_OP_GET_INDEX,
        &&op_OP_SET_INDEX,
        &&op_OP_GET_INDEX_PTR,
        &&op_OP_BUILD_ARRAY,
        &&op_OP_SIMD_ARRAY_COPY,
        &&op_OP_SIMD_ARRAY_FILL,
        &&op_OP_ADD_NUM,
        &&op_OP_SUBTRACT_NUM,
        &&op_OP_MULTIPLY_NUM,
        &&op_OP_DIVIDE_NUM,
        &&op_OP_MODULO_NUM,
        &&op_OP_GET_INDEX_NUM,
        &&op_OP_SET_INDEX_NUM,
        &&op_OP_GET_INDEX_BUF,
        &&op_OP_SET_INDEX_BUF,
        &&op_OP_LOOP_INCR_LESS,
        &&op_OP_LOOP_INCR_LEQ,
    };
    static_assert(sizeof(dispatchTable) / sizeof(void*) == OP__COUNT, "dispatchTable size must match OP__COUNT");
    DISPATCH();
#else
    for (;;) {
        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
#endif

            OP_HANDLER(OP_CONSTANT) {
                Value constant = READ_CONSTANT();
                if (IS_OBJ(constant)) constant = cloneValue(constant);
                *v->stackTop++ = constant;
                DISPATCH();
            }
            OP_HANDLER(OP_NIL)   *v->stackTop++ = NIL_VAL; DISPATCH();
            OP_HANDLER(OP_TRUE)  *v->stackTop++ = BOOL_VAL(true); DISPATCH();
            OP_HANDLER(OP_FALSE) *v->stackTop++ = BOOL_VAL(false); DISPATCH();
            OP_HANDLER(OP_POP) {
                v->stackTop--;
                dropValue(*v->stackTop);
                DISPATCH();
            }
            OP_HANDLER(OP_GET_LOCAL) {
                uint8_t slot = READ_BYTE();
                Value localVal = frame->slots[slot];
                if (__builtin_expect(IS_OBJ(localVal), 0)) localVal = cloneValue(localVal);
                *v->stackTop++ = localVal;
                DISPATCH();
            }
            OP_HANDLER(OP_SET_LOCAL) {
                uint8_t slot = READ_BYTE();
                Value oldVal = frame->slots[slot];
                Value newVal = v->stackTop[-1];
                if (__builtin_expect(IS_OBJ(newVal), 0)) newVal = cloneValue(newVal);
                frame->slots[slot] = newVal;
                if (__builtin_expect(IS_OBJ(oldVal), 0)) dropValue(oldVal);
                DISPATCH();
            }
            OP_HANDLER(OP_GET_GLOBAL) {
                uint8_t constIdx = READ_BYTE();
                Chunk* chunk = &frame->closure->function->chunk;
                Value* ptr = chunk->globalCache[constIdx];
                if (__builtin_expect(ptr != nullptr, 1)) {
                    Value value = *ptr;
                    if (__builtin_expect(IS_OBJ(value), 0)) value = cloneValue(value);
                    *v->stackTop++ = value;
                    DISPATCH();
                }
                ObjString* name = AS_STRING(chunk->constants.values[constIdx]);
                if (globalsLock) globalsLock->lock_shared();
                ptr = globals->getPtr(name);
                if (globalsLock) globalsLock->unlock_shared();
                if (ptr == nullptr) {
                    runtimeError(v, "Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                chunk->globalCache[constIdx] = ptr;
                Value value = *ptr;
                if (__builtin_expect(IS_OBJ(value), 0)) value = cloneValue(value);
                *v->stackTop++ = value;
                DISPATCH();
            }
            OP_HANDLER(OP_DEFINE_GLOBAL) {
                ObjString* name = READ_STRING();
                Value val = *--v->stackTop;
                if (globalsLock) globalsLock->lock();
                globals->set(name, val);
                if (globalsLock) globalsLock->unlock();
                DISPATCH();
            }
            OP_HANDLER(OP_SET_GLOBAL) {
                ObjString* name = READ_STRING();
                Value newVal = peek(v, 0);
                if (IS_OBJ(newVal)) newVal = cloneValue(newVal);
                if (globalsLock) globalsLock->lock();
                bool isNew = globals->set(name, newVal);
                if (isNew) {
                    globals->del(name);
                    if (globalsLock) globalsLock->unlock();
                    runtimeError(v, "Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (globalsLock) globalsLock->unlock();
                DISPATCH();
            }
            OP_HANDLER(OP_ANONYMOUS_STRUCT) {
                uint8_t fieldCount = READ_BYTE();
                ObjInstance* instance = newInstance(nullptr);
                
                for (int i = 0; i < fieldCount; i++) {
                    ObjString* name = READ_STRING();
                    Value value = v->stackTop[-fieldCount + i];
                    if (instance->denseCount < INSTANCE_DENSE_MAX) {
                        instance->denseFields[instance->denseCount].name = name;
                        instance->denseFields[instance->denseCount].value = value;
                        instance->denseCount++;
                    } else {
                        instance->fields.set(name, value);
                    }
                }
                v->stackTop -= fieldCount;
                pushVM(v, OBJ_VAL(instance));
                DISPATCH();
            }
            OP_HANDLER(OP_GET_UPVALUE) {
                uint8_t slot = READ_BYTE();
                pushVM(v, cloneValue(*frame->closure->upvalues[slot]->location));
                DISPATCH();
            }
            OP_HANDLER(OP_SET_UPVALUE) {
                uint8_t slot = READ_BYTE();
                Value oldVal = *frame->closure->upvalues[slot]->location;
                Value newVal = cloneValue(peek(v, 0));
                *frame->closure->upvalues[slot]->location = newVal;
                dropValue(oldVal);
                DISPATCH();
            }
            OP_HANDLER(OP_GET_PROPERTY) {
                Value receiver = peek(v, 0);
                Value* receiverPtr = resolveReceiver(v->stackTop - 1, receiver);

                if (!IS_INSTANCE(*receiverPtr)) {
                    runtimeError(v, "Only struct instances have fields.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjInstance* instance = AS_INSTANCE(*receiverPtr);
                ObjString* name = READ_STRING();

                instance->lock.lock();
                for (int i = 0; i < instance->denseCount; i++) {
                    if (stringsEqual(instance->denseFields[i].name, name)) {
                        Value val = cloneValue(instance->denseFields[i].value);
                        instance->lock.unlock();
                        dropValue(popVM(v));
                        pushVM(v, val);
                        goto prop_get_success;
                    }
                }

                {
                    Value value;
                    if (instance->fields.get(name, &value)) {
                        Value val = cloneValue(value);
                        instance->lock.unlock();
                        dropValue(popVM(v));
                        pushVM(v, val);
                        goto prop_get_success;
                    }
                }
                instance->lock.unlock();

                runtimeError(v, "Undefined field '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            prop_get_success:
                DISPATCH();
            }
            OP_HANDLER(OP_SET_PROPERTY) {
                Value receiver = peek(v, 1);
                Value* receiverPtr = resolveReceiver(v->stackTop - 2, receiver);

                if (!IS_INSTANCE(*receiverPtr)) {
                    runtimeError(v, "Only struct instances have fields.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjInstance* instance = AS_INSTANCE(*receiverPtr);
                ObjString* name = READ_STRING();
                Value value = cloneValue(peek(v, 0));

                instance->lock.lock();
                for (int i = 0; i < instance->denseCount; i++) {
                    if (stringsEqual(instance->denseFields[i].name, name)) {
                        Value oldVal = instance->denseFields[i].value;
                        instance->denseFields[i].value = value;
                        instance->lock.unlock();
                        dropValue(oldVal);
                        goto prop_set_done;
                    }
                }

                if (instance->denseCount < INSTANCE_DENSE_MAX) {
                    instance->denseFields[instance->denseCount].name  = name;
                    instance->denseFields[instance->denseCount].value = value;
                    instance->denseCount++;
                    instance->lock.unlock();
                    goto prop_set_done;
                }

                instance->fields.set(name, value);
                instance->lock.unlock();

            prop_set_done:
                // Stack: [... instance value] → [... value]
                dropValue(v->stackTop[-2]);         // drop instance
                v->stackTop[-2] = v->stackTop[-1];  // slide value down
                v->stackTop--;
                DISPATCH();
            }
            OP_HANDLER(OP_EQUAL) {
                Value bVal = v->stackTop[-1];
                Value aVal = v->stackTop[-2];
                v->stackTop[-2] = BOOL_VAL(valuesEqual(aVal, bVal));
                v->stackTop--;
                if (IS_OBJ(aVal)) dropValue(aVal);
                if (IS_OBJ(bVal)) dropValue(bVal);
                DISPATCH();
            }
            OP_HANDLER(OP_GREATER)  BINARY_OP(BOOL_VAL, >); DISPATCH();
            OP_HANDLER(OP_LESS)     BINARY_OP(BOOL_VAL, <); DISPATCH();
            OP_HANDLER(OP_ADD) {
                Value bVal = v->stackTop[-1];
                Value aVal = v->stackTop[-2];
                if (IS_NUMBER(bVal) && IS_NUMBER(aVal)) {
                    // Quickening: specialize to OP_ADD_NUM
                    frame->ip[-1] = OP_ADD_NUM;
                    v->stackTop[-2] = NUMBER_VAL(AS_NUMBER(aVal) + AS_NUMBER(bVal));
                    v->stackTop--;
                } else if (IS_STRING(bVal) && IS_STRING(aVal)) {
                    concatenate(v);
                } else {
                    runtimeError(v, "Operands must be two numbers or two strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                DISPATCH();
            }
            OP_HANDLER(OP_ADD_NUM) {
                Value bVal = v->stackTop[-1];
                Value aVal = v->stackTop[-2];
                if (__builtin_expect(IS_NUMBER(bVal) && IS_NUMBER(aVal), 1)) {
                    v->stackTop[-2] = NUMBER_VAL(AS_NUMBER(aVal) + AS_NUMBER(bVal));
                    v->stackTop--;
                } else {
                    frame->ip[-1] = OP_ADD;
                    if (IS_STRING(bVal) && IS_STRING(aVal)) {
                        concatenate(v);
                    } else {
                        runtimeError(v, "Operands must be two numbers or two strings.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                }
                DISPATCH();
            }
            OP_HANDLER(OP_SUBTRACT) {
                Value bVal = v->stackTop[-1];
                Value aVal = v->stackTop[-2];
                if (IS_NUMBER(bVal) && IS_NUMBER(aVal)) {
                    frame->ip[-1] = OP_SUBTRACT_NUM;
                    v->stackTop[-2] = NUMBER_VAL(AS_NUMBER(aVal) - AS_NUMBER(bVal));
                    v->stackTop--;
                } else {
                    runtimeError(v, "Operands must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                DISPATCH();
            }
            OP_HANDLER(OP_SUBTRACT_NUM) {
                Value bVal = v->stackTop[-1];
                Value aVal = v->stackTop[-2];
                if (__builtin_expect(IS_NUMBER(bVal) && IS_NUMBER(aVal), 1)) {
                    v->stackTop[-2] = NUMBER_VAL(AS_NUMBER(aVal) - AS_NUMBER(bVal));
                    v->stackTop--;
                } else {
                    frame->ip[-1] = OP_SUBTRACT;
                    runtimeError(v, "Operands must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                DISPATCH();
            }
            OP_HANDLER(OP_MULTIPLY) {
                Value bVal = v->stackTop[-1];
                Value aVal = v->stackTop[-2];
                if (IS_NUMBER(bVal) && IS_NUMBER(aVal)) {
                    frame->ip[-1] = OP_MULTIPLY_NUM;
                    v->stackTop[-2] = NUMBER_VAL(AS_NUMBER(aVal) * AS_NUMBER(bVal));
                    v->stackTop--;
                } else {
                    runtimeError(v, "Operands must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                DISPATCH();
            }
            OP_HANDLER(OP_MULTIPLY_NUM) {
                Value bVal = v->stackTop[-1];
                Value aVal = v->stackTop[-2];
                if (__builtin_expect(IS_NUMBER(bVal) && IS_NUMBER(aVal), 1)) {
                    v->stackTop[-2] = NUMBER_VAL(AS_NUMBER(aVal) * AS_NUMBER(bVal));
                    v->stackTop--;
                } else {
                    frame->ip[-1] = OP_MULTIPLY;
                    runtimeError(v, "Operands must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                DISPATCH();
            }
            OP_HANDLER(OP_DIVIDE) {
                Value bVal = v->stackTop[-1];
                Value aVal = v->stackTop[-2];
                if (IS_NUMBER(bVal) && IS_NUMBER(aVal)) {
                    frame->ip[-1] = OP_DIVIDE_NUM;
                    v->stackTop[-2] = NUMBER_VAL(AS_NUMBER(aVal) / AS_NUMBER(bVal));
                    v->stackTop--;
                } else {
                    runtimeError(v, "Operands must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                DISPATCH();
            }
            OP_HANDLER(OP_DIVIDE_NUM) {
                Value bVal = v->stackTop[-1];
                Value aVal = v->stackTop[-2];
                if (__builtin_expect(IS_NUMBER(bVal) && IS_NUMBER(aVal), 1)) {
                    v->stackTop[-2] = NUMBER_VAL(AS_NUMBER(aVal) / AS_NUMBER(bVal));
                    v->stackTop--;
                } else {
                    frame->ip[-1] = OP_DIVIDE;
                    runtimeError(v, "Operands must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                DISPATCH();
            }
            OP_HANDLER(OP_MODULO) {
                Value bVal = v->stackTop[-1];
                Value aVal = v->stackTop[-2];
                if (!IS_NUMBER(bVal) || !IS_NUMBER(aVal)) {
                    runtimeError(v, "Operands must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                double b = AS_NUMBER(bVal);
                double a = AS_NUMBER(aVal);
                if (b == 0.0) {
                    runtimeError(v, "Division by zero in modulo.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame->ip[-1] = OP_MODULO_NUM;
                v->stackTop[-2] = NUMBER_VAL(std::fmod(a, b));
                v->stackTop--;
                DISPATCH();
            }
            OP_HANDLER(OP_MODULO_NUM) {
                Value bVal = v->stackTop[-1];
                Value aVal = v->stackTop[-2];
                if (__builtin_expect(IS_NUMBER(bVal) && IS_NUMBER(aVal), 1)) {
                    double b = AS_NUMBER(bVal);
                    double a = AS_NUMBER(aVal);
                    if (b == 0.0) {
                        runtimeError(v, "Division by zero in modulo.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    v->stackTop[-2] = NUMBER_VAL(std::fmod(a, b));
                    v->stackTop--;
                } else {
                    frame->ip[-1] = OP_MODULO;
                    goto op_OP_MODULO;
                }
                DISPATCH();
            }
            OP_HANDLER(OP_NOT) {
                v->stackTop[-1] = BOOL_VAL(isFalsey(v->stackTop[-1]));
                DISPATCH();
            }
            OP_HANDLER(OP_NEGATE) {
                if (!IS_NUMBER(v->stackTop[-1])) {
                    runtimeError(v, "Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                v->stackTop[-1] = NUMBER_VAL(-AS_NUMBER(v->stackTop[-1]));
                DISPATCH();
            }
            OP_HANDLER(OP_PRINT) {
                Value val = *--v->stackTop;
                printValue(val);
                printf("\n");
                fflush(stdout);
                if (IS_OBJ(val)) dropValue(val);
                DISPATCH();
            }
            OP_HANDLER(OP_JUMP) {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                DISPATCH();
            }
            OP_HANDLER(OP_JUMP_IF_FALSE) {
                uint16_t offset = READ_SHORT();
                if (isFalsey(peek(v, 0))) frame->ip += offset;
                DISPATCH();
            }
            OP_HANDLER(OP_LOOP) {
                uint16_t offset = READ_SHORT();
                ObjFunction* fn = frame->closure->function;
                fn->hotness++;
                if (fn->hotness == isli::jit::JIT_HOT_THRESHOLD) {
                    isli::jit::Manager::instance().requestCompilation(fn);
                }
                frame->ip -= offset;
                DISPATCH();
            }
            OP_HANDLER(OP_CALL) {
                int argCount = READ_BYTE();
                Value callee = v->stackTop[-1 - argCount];
                int prevFrameCount = v->frameCount;
                if (!callValue(v, callee, argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (v->frameCount > prevFrameCount) {
                    frame = &v->frames[v->frameCount - 1];
                }
                DISPATCH();
            }
            OP_HANDLER(OP_CLOSURE) {
                ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
                ObjClosure* closure = newClosure(function);
                pushVM(v, OBJ_VAL(closure));
                for (int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        closure->upvalues[i] =
                            captureUpvalue(v, frame->slots + index);
                    } else {
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                DISPATCH();
            }
            OP_HANDLER(OP_CLOSE_UPVALUE) {
                closeUpvalues(v, v->stackTop - 1);
                Value popped = popVM(v);
                dropValue(popped);
                DISPATCH();
            }
            OP_HANDLER(OP_RETURN) {
                Value result = *--v->stackTop;
                if (__builtin_expect(v->openUpvalues != nullptr, 0)) {
                    closeUpvalues(v, frame->slots);
                }
                v->frameCount--;
                if (__builtin_expect(v->frameCount == stopFrameCount, 0)) {
                    if (stopFrameCount == 0) {
                        while (v->stackTop > v->stack) {
                            dropValue(*--v->stackTop);
                        }
                        dropValue(result);
                    } else {
                        while (v->stackTop > frame->slots) {
                            dropValue(*--v->stackTop);
                        }
                        *frame->slots = result;
                        v->stackTop = frame->slots + 1;
                    }
                    return INTERPRET_OK;
                }

                while (v->stackTop > frame->slots) {
                    dropValue(*--v->stackTop);
                }

                *frame->slots = result;
                v->stackTop = frame->slots + 1;
                frame = &v->frames[v->frameCount - 1];
                DISPATCH();
            }
            OP_HANDLER(OP_STRUCT) {
                pushVM(v, OBJ_VAL(newStruct(READ_STRING())));
                DISPATCH();
            }
            OP_HANDLER(OP_DISPATCH) {
                uint8_t dimCount = READ_BYTE();
                Value closureVal = popVM(v);

                if (!IS_CLOSURE(closureVal)) {
                    dropValue(closureVal);
                    runtimeError(v, "Can only dispatch closures.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                int64_t dims[3] = {1, 1, 1};
                bool dimError = false;
                for (int i = dimCount - 1; i >= 0; i--) {
                    Value dVal = popVM(v);
                    if (!IS_NUMBER(dVal)) {
                        dimError = true;
                    } else if (i < 3) {
                        int64_t d = static_cast<int64_t>(AS_NUMBER(dVal));
                        dims[i] = (d < 0) ? 0 : d;
                    }
                    dropValue(dVal);
                }

                if (dimError) {
                    dropValue(closureVal);
                    runtimeError(v, "Dispatch dimensions must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                if (!dispatchJob(AS_CLOSURE(closureVal), dimCount, dims[0], dims[1], dims[2],
                            0, 1, 1, 0, nullptr)) {
                    dropValue(closureVal);
                    return INTERPRET_RUNTIME_ERROR;
                }
                dropValue(closureVal);
                DISPATCH();
            }
            OP_HANDLER(OP_DISPATCH2) {
                uint8_t dimCount = READ_BYTE();
                uint8_t tileFlags = READ_BYTE();
                uint8_t reduceOp = READ_BYTE();
                Value closureVal = popVM(v);

                if (!IS_CLOSURE(closureVal)) {
                    dropValue(closureVal);
                    runtimeError(v, "Can only dispatch closures.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                int64_t tx = 1, ty = 1;
                if (tileFlags != 0) {
                    Value tyVal = popVM(v);
                    Value txVal = popVM(v);
                    if (!IS_NUMBER(txVal) || !IS_NUMBER(tyVal)) {
                        dropValue(txVal);
                        dropValue(tyVal);
                        dropValue(closureVal);
                        runtimeError(v, "tile sizes must be numbers.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    tx = static_cast<int64_t>(AS_NUMBER(txVal));
                    ty = static_cast<int64_t>(AS_NUMBER(tyVal));
                    dropValue(txVal);
                    dropValue(tyVal);
                }

                int64_t dims[3] = {1, 1, 1};
                bool dimError = false;
                for (int i = dimCount - 1; i >= 0; i--) {
                    Value dVal = popVM(v);
                    if (!IS_NUMBER(dVal)) {
                        dimError = true;
                    } else if (i < 3) {
                        int64_t d = static_cast<int64_t>(AS_NUMBER(dVal));
                        dims[i] = (d < 0) ? 0 : d;
                    }
                    dropValue(dVal);
                }

                if (dimError) {
                    dropValue(closureVal);
                    runtimeError(v, "Dispatch dimensions must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                Value reduceOut = NIL_VAL;
                if (!dispatchJob(AS_CLOSURE(closureVal), dimCount, dims[0], dims[1], dims[2],
                            tileFlags, tx, ty, reduceOp,
                            reduceOp != 0 ? &reduceOut : nullptr)) {
                    dropValue(closureVal);
                    return INTERPRET_RUNTIME_ERROR;
                }
                dropValue(closureVal);
                if (reduceOp != 0) {
                    pushVM(v, reduceOut);
                }
                DISPATCH();
            }
            OP_HANDLER(OP_GET_LOCAL_PTR) {
                uint8_t slot = READ_BYTE();
                pushVM(v, POINTER_VAL(&frame->slots[slot]));
                DISPATCH();
            }
            OP_HANDLER(OP_GET_GLOBAL_PTR) {
                uint8_t constIdx = READ_BYTE();
                Chunk* chunk = &frame->closure->function->chunk;
                Value* ptr = chunk->globalCache[constIdx];
                if (__builtin_expect(ptr != nullptr, 1)) {
                    pushVM(v, POINTER_VAL(ptr));
                    DISPATCH();
                }
                ObjString* name = AS_STRING(chunk->constants.values[constIdx]);
                if (globalsLock) globalsLock->lock_shared();
                ptr = globals->getPtr(name);
                if (globalsLock) globalsLock->unlock_shared();
                if (ptr == nullptr) {
                    runtimeError(v, "Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                chunk->globalCache[constIdx] = ptr;
                pushVM(v, POINTER_VAL(ptr));
                DISPATCH();
            }
            OP_HANDLER(OP_GET_PROPERTY_PTR) {
                Value receiver = peek(v, 0);
                Value* receiverPtr = resolveReceiver(v->stackTop - 1, receiver);

                if (!IS_INSTANCE(*receiverPtr)) {
                    runtimeError(v, "Only struct instances have fields.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                ObjInstance* instance = AS_INSTANCE(*receiverPtr);
                ObjString* name = READ_STRING();
                Value* fieldPtr = nullptr;
                
                instance->lock.lock();
                for (int i = 0; i < instance->denseCount; i++) {
                    if (stringsEqual(instance->denseFields[i].name, name)) {
                        fieldPtr = &instance->denseFields[i].value;
                        break;
                    }
                }
                if (!fieldPtr) {
                    fieldPtr = instance->fields.getPtr(name); 
                }
                instance->lock.unlock();
                
                if (fieldPtr == nullptr) {
                    runtimeError(v, "Undefined field '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                dropValue(popVM(v));
                pushVM(v, POINTER_VAL(fieldPtr));
                DISPATCH();
            }
            OP_HANDLER(OP_GET_UPVALUE_PTR) {
                uint8_t slot = READ_BYTE();
                pushVM(v, POINTER_VAL(frame->closure->upvalues[slot]->location));
                DISPATCH();
            }
            OP_HANDLER(OP_DEREF) {
                Value val = peek(v, 0);
                Value result;
                if (IS_POINTER(val)) result = cloneValue(*AS_POINTER(val));
                else {
                    runtimeError(v, "Cannot dereference non-pointer.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                dropValue(popVM(v));
                pushVM(v, result);
                DISPATCH();
            }
            OP_HANDLER(OP_SET_DEREF) {
                Value newVal = peek(v, 0);
                Value ptrVal = peek(v, 1);
                Value* target = nullptr;
                if (IS_POINTER(ptrVal)) target = AS_POINTER(ptrVal);
                else {
                    runtimeError(v, "Cannot dereference non-pointer.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                dropValue(*target);
                *target = cloneValue(newVal);
                
                v->stackTop -= 2;
                dropValue(ptrVal);
                pushVM(v, newVal);
                DISPATCH();
            }
            OP_HANDLER(OP_BUILD_ARRAY) {
                uint8_t itemCount = READ_BYTE();
                ObjArray* arr = newArray(itemCount, NIL_VAL);
                for (int i = itemCount - 1; i >= 0; i--) {
                    arr->values[i] = popVM(v);
                }
                pushVM(v, OBJ_VAL(arr));
                DISPATCH();
            }
            OP_HANDLER(OP_GET_INDEX) {
                Value indexVal = v->stackTop[-1];
                Value arrVal = v->stackTop[-2];

                if (!IS_NUMBER(indexVal)) {
                    runtimeError(v, "Array index must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                if (IS_BUFFER(arrVal)) {
                    ObjBuffer* buf = AS_BUFFER(arrVal);
                    frame->ip[-1] = OP_GET_INDEX_BUF;
                    int idx = static_cast<int>(AS_NUMBER(indexVal));
                    if (idx < 0 || idx >= buf->count) {
                        runtimeError(v, "Buffer index %d out of bounds (length %d).", idx, buf->count);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    Value result = NUMBER_VAL(bufferLoad(buf, idx));
                    dropValue(v->stackTop[-2]);
                    v->stackTop[-2] = result;
                    v->stackTop--;
                    DISPATCH();
                }

                ObjArray* arr;
                if (IS_ARRAY(arrVal)) {
                    arr = AS_ARRAY(arrVal);
                    frame->ip[-1] = OP_GET_INDEX_NUM;
                } else {
                    Value* arrPtr = resolveReceiver(v->stackTop - 2, arrVal);
                    if (IS_BUFFER(*arrPtr)) {
                        ObjBuffer* buf = AS_BUFFER(*arrPtr);
                        frame->ip[-1] = OP_GET_INDEX_BUF;
                        int idx = static_cast<int>(AS_NUMBER(indexVal));
                        if (idx < 0 || idx >= buf->count) {
                            runtimeError(v, "Buffer index %d out of bounds (length %d).", idx, buf->count);
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        Value result = NUMBER_VAL(bufferLoad(buf, idx));
                        dropValue(v->stackTop[-2]);
                        v->stackTop[-2] = result;
                        v->stackTop--;
                        DISPATCH();
                    }
                    if (!IS_ARRAY(*arrPtr)) {
                        runtimeError(v, "Cannot index a non-array value.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    arr = AS_ARRAY(*arrPtr);
                }

                int idx = static_cast<int>(AS_NUMBER(indexVal));
                if (idx < 0 || idx >= arr->count) {
                    runtimeError(v, "Array index %d out of bounds (length %d).", idx, arr->count);
                    return INTERPRET_RUNTIME_ERROR;
                }

                Value elem = arr->values[idx];
                Value result;
                if (IS_NUMBER(elem)) {
                    result = elem;
                } else {
                    arr->lock.lock();
                    result = cloneValue(arr->values[idx]);
                    arr->lock.unlock();
                }

                dropValue(v->stackTop[-2]); // drop arr
                v->stackTop[-2] = result;
                v->stackTop--;
                DISPATCH();
            }
            OP_HANDLER(OP_GET_INDEX_NUM) {
                Value indexVal = v->stackTop[-1];
                Value arrVal = v->stackTop[-2];

                if (__builtin_expect(IS_NUMBER(indexVal) && IS_ARRAY(arrVal), 1)) {
                    ObjArray* arr = AS_ARRAY(arrVal);
                    int idx = static_cast<int>(AS_NUMBER(indexVal));
                    if (__builtin_expect(idx >= 0 && idx < arr->count, 1)) {
                        Value elem = arr->values[idx];
                        Value result;
                        if (IS_NUMBER(elem)) {
                            result = elem;
                        } else {
                            arr->lock.lock();
                            result = cloneValue(arr->values[idx]);
                            arr->lock.unlock();
                        }
                        dropValue(v->stackTop[-2]);
                        v->stackTop[-2] = result;
                        v->stackTop--;
                        DISPATCH();
                    } else {
                        runtimeError(v, "Array index %d out of bounds (length %d).", idx, arr->count);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                } else {
                    frame->ip[-1] = OP_GET_INDEX;
                    goto op_OP_GET_INDEX;
                }
            }
            OP_HANDLER(OP_SET_INDEX) {
                Value newVal = v->stackTop[-1];
                Value indexVal = v->stackTop[-2];
                Value arrVal = v->stackTop[-3];

                if (!IS_NUMBER(indexVal)) {
                    runtimeError(v, "Array index must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                if (IS_BUFFER(arrVal)) {
                    if (!IS_NUMBER(newVal)) {
                        runtimeError(v, "Buffer elements must be numbers.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    ObjBuffer* buf = AS_BUFFER(arrVal);
                    frame->ip[-1] = OP_SET_INDEX_BUF;
                    int idx = static_cast<int>(AS_NUMBER(indexVal));
                    if (idx < 0 || idx >= buf->count) {
                        runtimeError(v, "Buffer index %d out of bounds (length %d).", idx, buf->count);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    bufferStore(buf, idx, AS_NUMBER(newVal));
                    dropValue(v->stackTop[-3]);
                    v->stackTop[-3] = newVal;
                    v->stackTop -= 2;
                    DISPATCH();
                }

                ObjArray* arr;
                if (IS_ARRAY(arrVal)) {
                    arr = AS_ARRAY(arrVal);
                    frame->ip[-1] = OP_SET_INDEX_NUM;
                } else {
                    Value* arrPtr = resolveReceiver(v->stackTop - 3, arrVal);
                    if (IS_BUFFER(*arrPtr)) {
                        if (!IS_NUMBER(newVal)) {
                            runtimeError(v, "Buffer elements must be numbers.");
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        ObjBuffer* buf = AS_BUFFER(*arrPtr);
                        frame->ip[-1] = OP_SET_INDEX_BUF;
                        int idx = static_cast<int>(AS_NUMBER(indexVal));
                        if (idx < 0 || idx >= buf->count) {
                            runtimeError(v, "Buffer index %d out of bounds (length %d).", idx, buf->count);
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        bufferStore(buf, idx, AS_NUMBER(newVal));
                        dropValue(v->stackTop[-3]);
                        v->stackTop[-3] = newVal;
                        v->stackTop -= 2;
                        DISPATCH();
                    }
                    if (!IS_ARRAY(*arrPtr)) {
                        runtimeError(v, "Cannot index a non-array value.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    arr = AS_ARRAY(*arrPtr);
                }

                int idx = static_cast<int>(AS_NUMBER(indexVal));
                if (idx < 0 || idx >= arr->count) {
                    runtimeError(v, "Array index %d out of bounds (length %d).", idx, arr->count);
                    return INTERPRET_RUNTIME_ERROR;
                }

                if (IS_NUMBER(newVal) && IS_NUMBER(arr->values[idx])) {
                    arr->values[idx] = newVal;
                } else {
                    arr->lock.lock();
                    Value oldElem = arr->values[idx];
                    arr->values[idx] = cloneValue(newVal);
                    arr->lock.unlock();
                    dropValue(oldElem);
                }

                dropValue(v->stackTop[-3]);        // drop arr
                v->stackTop[-3] = newVal;          // slide newVal down
                v->stackTop -= 2;
                DISPATCH();
            }
            OP_HANDLER(OP_SET_INDEX_NUM) {
                Value newVal = v->stackTop[-1];
                Value indexVal = v->stackTop[-2];
                Value arrVal = v->stackTop[-3];

                if (__builtin_expect(IS_NUMBER(indexVal) && IS_ARRAY(arrVal), 1)) {
                    ObjArray* arr = AS_ARRAY(arrVal);
                    int idx = static_cast<int>(AS_NUMBER(indexVal));
                    if (__builtin_expect(idx >= 0 && idx < arr->count, 1)) {
                        if (IS_NUMBER(newVal) && IS_NUMBER(arr->values[idx])) {
                            arr->values[idx] = newVal;
                        } else {
                            arr->lock.lock();
                            Value oldElem = arr->values[idx];
                            arr->values[idx] = cloneValue(newVal);
                            arr->lock.unlock();
                            dropValue(oldElem);
                        }
                        dropValue(v->stackTop[-3]);
                        v->stackTop[-3] = newVal;
                        v->stackTop -= 2;
                        DISPATCH();
                    } else {
                        runtimeError(v, "Array index %d out of bounds (length %d).", idx, arr->count);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                } else {
                    frame->ip[-1] = OP_SET_INDEX;
                    goto op_OP_SET_INDEX;
                }
            }
            OP_HANDLER(OP_GET_INDEX_BUF) {
                Value indexVal = v->stackTop[-1];
                Value bufVal = v->stackTop[-2];

                if (__builtin_expect(IS_NUMBER(indexVal) && IS_BUFFER(bufVal), 1)) {
                    ObjBuffer* buf = AS_BUFFER(bufVal);
                    int idx = static_cast<int>(AS_NUMBER(indexVal));
                    if (__builtin_expect(static_cast<unsigned>(idx) < static_cast<unsigned>(buf->count), 1)) {
                        Value result = NUMBER_VAL(bufferLoad(buf, idx));
                        dropValue(v->stackTop[-2]);
                        v->stackTop[-2] = result;
                        v->stackTop--;
                        DISPATCH();
                    } else {
                        runtimeError(v, "Buffer index %d out of bounds (length %d).", idx, buf->count);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                } else {
                    frame->ip[-1] = OP_GET_INDEX;
                    goto op_OP_GET_INDEX;
                }
            }
            OP_HANDLER(OP_SET_INDEX_BUF) {
                Value newVal = v->stackTop[-1];
                Value indexVal = v->stackTop[-2];
                Value bufVal = v->stackTop[-3];

                if (__builtin_expect(IS_NUMBER(indexVal) && IS_BUFFER(bufVal) && IS_NUMBER(newVal), 1)) {
                    ObjBuffer* buf = AS_BUFFER(bufVal);
                    int idx = static_cast<int>(AS_NUMBER(indexVal));
                    if (__builtin_expect(static_cast<unsigned>(idx) < static_cast<unsigned>(buf->count), 1)) {
                        bufferStore(buf, idx, AS_NUMBER(newVal));
                        dropValue(v->stackTop[-3]);
                        v->stackTop[-3] = newVal;
                        v->stackTop -= 2;
                        DISPATCH();
                    } else {
                        runtimeError(v, "Buffer index %d out of bounds (length %d).", idx, buf->count);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                } else {
                    frame->ip[-1] = OP_SET_INDEX;
                    goto op_OP_SET_INDEX;
                }
            }
            OP_HANDLER(OP_GET_INDEX_PTR) {
                Value indexVal = peek(v, 0);
                Value arrVal = peek(v, 1);
                Value* arrPtr = resolveReceiver(v->stackTop - 2, arrVal);

                if (!IS_NUMBER(indexVal)) {
                    runtimeError(v, "Array index must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (!IS_ARRAY(*arrPtr)) {
                    runtimeError(v, "Cannot index a non-array value.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjArray* arr = AS_ARRAY(*arrPtr);
                int idx = static_cast<int>(AS_NUMBER(indexVal));
                if (idx < 0 || idx >= arr->count) {
                    runtimeError(v, "Array index %d out of bounds (length %d).", idx, arr->count);
                    return INTERPRET_RUNTIME_ERROR;
                }

                Value* elemPtr = &arr->values[idx];
                dropValue(popVM(v)); // pop index
                dropValue(popVM(v)); // pop array
                pushVM(v, POINTER_VAL(elemPtr));
                DISPATCH();
            }
            OP_HANDLER(OP_SIMD_ARRAY_COPY) {
                Value endVal = popVM(v);
                Value startVal = popVM(v);
                Value srcVal = popVM(v);
                Value destVal = popVM(v);

                if (!IS_NUMBER(endVal) || !IS_NUMBER(startVal)) {
                    runtimeError(v, "SIMD copy range must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (!IS_ARRAY(destVal) || !IS_ARRAY(srcVal)) {
                    runtimeError(v, "SIMD copy operands must be arrays.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjArray* destArr = AS_ARRAY(destVal);
                ObjArray* srcArr = AS_ARRAY(srcVal);
                int start = static_cast<int>(AS_NUMBER(startVal));
                int end = static_cast<int>(AS_NUMBER(endVal));

                if (start < 0) start = 0;
                if (end > destArr->count) end = destArr->count;
                if (end > srcArr->count) end = srcArr->count;

                if (start < end) {
                    int i = start;
                    for (; i + 4 <= end; i += 4) {
                        if (!IS_NUMBER(srcArr->values[i]) || !IS_NUMBER(srcArr->values[i+1]) || 
                            !IS_NUMBER(srcArr->values[i+2]) || !IS_NUMBER(srcArr->values[i+3])) {
                            break;
                        }
                        __m256d data = _mm256_loadu_pd(reinterpret_cast<const double*>(&srcArr->values[i]));
                        _mm256_storeu_pd(reinterpret_cast<double*>(&destArr->values[i]), data);
                    }
                    for (; i < end; i++) {
                        destArr->values[i] = cloneValue(srcArr->values[i]);
                    }
                }

                dropValue(destVal);
                dropValue(srcVal);
                dropValue(startVal);
                dropValue(endVal);
                DISPATCH();
            }
            OP_HANDLER(OP_SIMD_ARRAY_FILL) {
                Value endVal = popVM(v);
                Value startVal = popVM(v);
                Value fillVal = popVM(v);
                Value destVal = popVM(v);

                if (!IS_NUMBER(endVal) || !IS_NUMBER(startVal)) {
                    runtimeError(v, "SIMD fill range must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (!IS_ARRAY(destVal)) {
                    runtimeError(v, "SIMD fill operand must be an array.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjArray* destArr = AS_ARRAY(destVal);
                int start = static_cast<int>(AS_NUMBER(startVal));
                int end = static_cast<int>(AS_NUMBER(endVal));

                if (start < 0) start = 0;
                if (end > destArr->count) end = destArr->count;

                if (start < end) {
                    if (IS_NUMBER(fillVal)) {
                        __m256d data = _mm256_set1_pd(AS_NUMBER(fillVal));
                        int i = start;
                        for (; i + 4 <= end; i += 4) {
                            _mm256_storeu_pd(reinterpret_cast<double*>(&destArr->values[i]), data);
                        }
                        for (; i < end; i++) {
                            destArr->values[i] = fillVal;
                        }
                    } else {
                        for (int i = start; i < end; i++) {
                            destArr->values[i] = cloneValue(fillVal);
                        }
                    }
                }

                dropValue(destVal);
                dropValue(fillVal);
                dropValue(startVal);
                dropValue(endVal);
                DISPATCH();
            }
            OP_HANDLER(OP_LOOP_INCR_LESS) {
                uint8_t iterSlot = READ_BYTE();
                uint8_t limitSlot = READ_BYTE();
                uint16_t offset = READ_SHORT();
                ObjFunction* fn = frame->closure->function;
                fn->hotness++;
                if (fn->hotness == isli::jit::JIT_HOT_THRESHOLD) {
                    isli::jit::Manager::instance().requestCompilation(fn);
                }
                Value* iterPtr = &frame->slots[iterSlot];
                Value* limitPtr = &frame->slots[limitSlot];
                if (__builtin_expect(IS_NUMBER(*iterPtr) && IS_NUMBER(*limitPtr), 1)) {
                    double iterVal = AS_NUMBER(*iterPtr) + 1.0;
                    *iterPtr = NUMBER_VAL(iterVal);
                    if (iterVal < AS_NUMBER(*limitPtr)) {
                        frame->ip -= offset;
                    }
                } else {
                    runtimeError(v, "Loop variable or limit is not a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                DISPATCH();
            }
            OP_HANDLER(OP_LOOP_INCR_LEQ) {
                uint8_t iterSlot = READ_BYTE();
                uint8_t limitSlot = READ_BYTE();
                uint16_t offset = READ_SHORT();
                ObjFunction* fn = frame->closure->function;
                fn->hotness++;
                if (fn->hotness == isli::jit::JIT_HOT_THRESHOLD) {
                    isli::jit::Manager::instance().requestCompilation(fn);
                }
                Value* iterPtr = &frame->slots[iterSlot];
                Value* limitPtr = &frame->slots[limitSlot];
                if (__builtin_expect(IS_NUMBER(*iterPtr) && IS_NUMBER(*limitPtr), 1)) {
                    double iterVal = AS_NUMBER(*iterPtr) + 1.0;
                    *iterPtr = NUMBER_VAL(iterVal);
                    if (iterVal <= AS_NUMBER(*limitPtr)) {
                        frame->ip -= offset;
                    }
                } else {
                    runtimeError(v, "Loop variable or limit is not a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                DISPATCH();
            }

#ifndef DIRECT_THREADING
        }
    }
#endif

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
    return INTERPRET_OK;
}

// ── interpret entry point ─────────────────────────────────────────
InterpretResult interpretVM(VM* v, const char* source) {
    ObjFunction* function = compile(source);
    if (function == nullptr) return INTERPRET_COMPILE_ERROR;

    v->push(OBJ_VAL(function));
    ObjClosure* closure = newClosure(function);
    v->pop();
    v->push(OBJ_VAL(closure));
    call(v, closure, 0);

    return run(v);
}

InterpretResult interpret(const char* source) {
    return interpretVM(&vm, source);
}

Value jitCallClosure(VM* v, ObjClosure* cl, int argCount, Value* args) {
    // Fallback: execute closure via bytecode interpreter
    if (argCount != cl->function->arity) {
        runtimeError(v, "Expected %d arguments but got %d.", cl->function->arity, argCount);
        return NIL_VAL;
    }

    int stopFrameCount = v->frameCount;
    Value* savedSlots = v->stackTop;
    pushVM(v, OBJ_VAL(cl));
    for (int i = 0; i < argCount; i++) {
        pushVM(v, args[i]);
    }

    if (!call(v, cl, argCount)) {
        while (v->stackTop > savedSlots) {
            dropValue(*--v->stackTop);
        }
        return NIL_VAL;
    }

    InterpretResult result = run(v, stopFrameCount);
    if (result != INTERPRET_OK) {
        return NIL_VAL;
    }

    Value retVal = (v->stackTop > savedSlots) ? *savedSlots : NIL_VAL;
    v->stackTop = savedSlots;
    return retVal;
}
