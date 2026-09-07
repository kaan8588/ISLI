#include "jit.hpp"
#include "object.hpp"
#include "vm.hpp"
#include <iostream>
#include <cstring>
#include <cmath>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace isli {
namespace jit {

// ── Executable Memory Allocator ────────────────────────────────────
void* ExecutableMemoryPool::allocate(size_t size) {
    if (size == 0) return nullptr;
#if defined(_WIN32) || defined(_WIN64)
    void* ptr = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    return ptr;
#else
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (ptr == MAP_FAILED) return nullptr;
    return ptr;
#endif
}

void ExecutableMemoryPool::free(void* ptr, size_t size) {
    if (!ptr || size == 0) return;
#if defined(_WIN32) || defined(_WIN64)
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, size);
#endif
}

void ExecutableMemoryPool::flushCache(void* ptr, size_t size) {
    if (!ptr || size == 0) return;
#if defined(_WIN32) || defined(_WIN64)
    FlushInstructionCache(GetCurrentProcess(), ptr, size);
#else
    __builtin___clear_cache(static_cast<char*>(ptr), static_cast<char*>(ptr) + size);
#endif
}

// ── x86_64 Machine Code Assembler Implementation ───────────────────
void X64Assembler::push(X64Reg reg) {
    if (reg >= 8) {
        emit8(0x41);
        emit8(0x50 + (reg - 8));
    } else {
        emit8(0x50 + reg);
    }
}

void X64Assembler::pop(X64Reg reg) {
    if (reg >= 8) {
        emit8(0x41);
        emit8(0x58 + (reg - 8));
    } else {
        emit8(0x58 + reg);
    }
}

void X64Assembler::mov_reg_imm64(X64Reg reg, uint64_t imm) {
    emit8(0x48 | ((reg >= 8) ? 1 : 0));
    emit8(0xB8 + (reg & 7));
    emit64(imm);
}

void X64Assembler::mov_reg_imm32(X64Reg reg, uint32_t imm) {
    if (reg >= 8) emit8(0x41);
    emit8(0xB8 + (reg & 7));
    emit32(imm);
}

void X64Assembler::mov_reg_reg(X64Reg dst, X64Reg src) {
    emit8(0x48 | ((src >= 8) ? 4 : 0) | ((dst >= 8) ? 1 : 0));
    emit8(0x89);
    emit8(0xC0 | ((src & 7) << 3) | (dst & 7));
}

void X64Assembler::mov_reg_mem(X64Reg dst, X64Reg base, int32_t disp) {
    emit8(0x48 | ((dst >= 8) ? 4 : 0) | ((base >= 8) ? 1 : 0));
    emit8(0x8B);
    
    if (base == RSP || base == R12) {
        if (disp >= -128 && disp <= 127) {
            emit8(0x44 | ((dst & 7) << 3));
            emit8(0x24);
            emit8(disp & 0xFF);
        } else {
            emit8(0x84 | ((dst & 7) << 3));
            emit8(0x24);
            emit32(disp);
        }
    } else {
        if (disp == 0 && (base & 7) != 5) {
            emit8(0x00 | ((dst & 7) << 3) | (base & 7));
        } else if (disp >= -128 && disp <= 127) {
            emit8(0x40 | ((dst & 7) << 3) | (base & 7));
            emit8(disp & 0xFF);
        } else {
            emit8(0x80 | ((dst & 7) << 3) | (base & 7));
            emit32(disp);
        }
    }
}

void X64Assembler::mov_mem_reg(X64Reg base, int32_t disp, X64Reg src) {
    emit8(0x48 | ((src >= 8) ? 4 : 0) | ((base >= 8) ? 1 : 0));
    emit8(0x89);
    
    if (base == RSP || base == R12) {
        if (disp >= -128 && disp <= 127) {
            emit8(0x44 | ((src & 7) << 3));
            emit8(0x24);
            emit8(disp & 0xFF);
        } else {
            emit8(0x84 | ((src & 7) << 3));
            emit8(0x24);
            emit32(disp);
        }
    } else {
        if (disp == 0 && (base & 7) != 5) {
            emit8(0x00 | ((src & 7) << 3) | (base & 7));
        } else if (disp >= -128 && disp <= 127) {
            emit8(0x40 | ((src & 7) << 3) | (base & 7));
            emit8(disp & 0xFF);
        } else {
            emit8(0x80 | ((src & 7) << 3) | (base & 7));
            emit32(disp);
        }
    }
}

void X64Assembler::lea_reg_mem(X64Reg dst, X64Reg base, int32_t disp) {
    emit8(0x48 | ((dst >= 8) ? 4 : 0) | ((base >= 8) ? 1 : 0));
    emit8(0x8D);
    
    if (base == RSP || base == R12) {
        if (disp >= -128 && disp <= 127) {
            emit8(0x44 | ((dst & 7) << 3));
            emit8(0x24);
            emit8(disp & 0xFF);
        } else {
            emit8(0x84 | ((dst & 7) << 3));
            emit8(0x24);
            emit32(disp);
        }
    } else {
        if (disp == 0 && (base & 7) != 5) {
            emit8(0x00 | ((dst & 7) << 3) | (base & 7));
        } else if (disp >= -128 && disp <= 127) {
            emit8(0x40 | ((dst & 7) << 3) | (base & 7));
            emit8(disp & 0xFF);
        } else {
            emit8(0x80 | ((dst & 7) << 3) | (base & 7));
            emit32(disp);
        }
    }
}

void X64Assembler::mov_mem_imm32(X64Reg base, int32_t disp, uint32_t imm) {
    if (base >= 8) emit8(0x41);
    emit8(0xC7);
    if (disp == 0 && (base & 7) != 5) {
        emit8(0x00 | (base & 7));
    } else if (disp >= -128 && disp <= 127) {
        emit8(0x40 | (base & 7));
        emit8(disp & 0xFF);
    } else {
        emit8(0x80 | (base & 7));
        emit32(disp);
    }
    emit32(imm);
}

void X64Assembler::add_reg_reg(X64Reg dst, X64Reg src) {
    emit8(0x48 | ((src >= 8) ? 4 : 0) | ((dst >= 8) ? 1 : 0));
    emit8(0x01);
    emit8(0xC0 | ((src & 7) << 3) | (dst & 7));
}

void X64Assembler::sub_reg_reg(X64Reg dst, X64Reg src) {
    emit8(0x48 | ((src >= 8) ? 4 : 0) | ((dst >= 8) ? 1 : 0));
    emit8(0x29);
    emit8(0xC0 | ((src & 7) << 3) | (dst & 7));
}

void X64Assembler::xor_reg_reg(X64Reg dst, X64Reg src) {
    emit8(0x48 | ((src >= 8) ? 4 : 0) | ((dst >= 8) ? 1 : 0));
    emit8(0x31);
    emit8(0xC0 | ((src & 7) << 3) | (dst & 7));
}

void X64Assembler::and_reg_reg(X64Reg dst, X64Reg src) {
    emit8(0x48 | ((src >= 8) ? 4 : 0) | ((dst >= 8) ? 1 : 0));
    emit8(0x21);
    emit8(0xC0 | ((src & 7) << 3) | (dst & 7));
}

void X64Assembler::or_reg_reg(X64Reg dst, X64Reg src) {
    emit8(0x48 | ((src >= 8) ? 4 : 0) | ((dst >= 8) ? 1 : 0));
    emit8(0x09);
    emit8(0xC0 | ((src & 7) << 3) | (dst & 7));
}

void X64Assembler::add_rsp_imm32(int32_t imm) {
    emit8(0x48);
    emit8(0x81);
    emit8(0xC4);
    emit32(imm);
}

void X64Assembler::sub_rsp_imm32(int32_t imm) {
    emit8(0x48);
    emit8(0x81);
    emit8(0xEC);
    emit32(imm);
}

void X64Assembler::cmp_reg_reg(X64Reg a, X64Reg b) {
    emit8(0x48 | ((b >= 8) ? 4 : 0) | ((a >= 8) ? 1 : 0));
    emit8(0x39);
    emit8(0xC0 | ((b & 7) << 3) | (a & 7));
}

void X64Assembler::cmp_reg_imm32(X64Reg reg, int32_t imm) {
    emit8(0x48 | ((reg >= 8) ? 1 : 0));
    emit8(0x81);
    emit8(0xF8 | (reg & 7));
    emit32(imm);
}

// ── SSE2 Instructions ──────────────────────────────────────────────
void X64Assembler::movsd_xmm_mem(X64Xmm dst, X64Reg base, int32_t disp) {
    emit8(0xF2);
    if (dst >= 8 || base >= 8) {
        emit8(0x40 | ((dst >= 8) ? 4 : 0) | ((base >= 8) ? 1 : 0));
    }
    emit8(0x0F);
    emit8(0x10);
    
    if (base == RSP || base == R12) {
        if (disp >= -128 && disp <= 127) {
            emit8(0x44 | ((dst & 7) << 3));
            emit8(0x24);
            emit8(disp & 0xFF);
        } else {
            emit8(0x84 | ((dst & 7) << 3));
            emit8(0x24);
            emit32(disp);
        }
    } else {
        if (disp == 0 && (base & 7) != 5) {
            emit8(0x00 | ((dst & 7) << 3) | (base & 7));
        } else if (disp >= -128 && disp <= 127) {
            emit8(0x40 | ((dst & 7) << 3) | (base & 7));
            emit8(disp & 0xFF);
        } else {
            emit8(0x80 | ((dst & 7) << 3) | (base & 7));
            emit32(disp);
        }
    }
}

void X64Assembler::movsd_mem_xmm(X64Reg base, int32_t disp, X64Xmm src) {
    emit8(0xF2);
    if (src >= 8 || base >= 8) {
        emit8(0x40 | ((src >= 8) ? 4 : 0) | ((base >= 8) ? 1 : 0));
    }
    emit8(0x0F);
    emit8(0x11);
    
    if (base == RSP || base == R12) {
        if (disp >= -128 && disp <= 127) {
            emit8(0x44 | ((src & 7) << 3));
            emit8(0x24);
            emit8(disp & 0xFF);
        } else {
            emit8(0x84 | ((src & 7) << 3));
            emit8(0x24);
            emit32(disp);
        }
    } else {
        if (disp == 0 && (base & 7) != 5) {
            emit8(0x00 | ((src & 7) << 3) | (base & 7));
        } else if (disp >= -128 && disp <= 127) {
            emit8(0x40 | ((src & 7) << 3) | (base & 7));
            emit8(disp & 0xFF);
        } else {
            emit8(0x80 | ((src & 7) << 3) | (base & 7));
            emit32(disp);
        }
    }
}

void X64Assembler::movsd_xmm_xmm(X64Xmm dst, X64Xmm src) {
    emit8(0xF2);
    if (dst >= 8 || src >= 8) {
        emit8(0x40 | ((dst >= 8) ? 4 : 0) | ((src >= 8) ? 1 : 0));
    }
    emit8(0x0F);
    emit8(0x10);
    emit8(0xC0 | ((dst & 7) << 3) | (src & 7));
}

void X64Assembler::addsd_xmm_xmm(X64Xmm dst, X64Xmm src) {
    emit8(0xF2);
    if (dst >= 8 || src >= 8) {
        emit8(0x40 | ((dst >= 8) ? 4 : 0) | ((src >= 8) ? 1 : 0));
    }
    emit8(0x0F);
    emit8(0x58);
    emit8(0xC0 | ((dst & 7) << 3) | (src & 7));
}

void X64Assembler::subsd_xmm_xmm(X64Xmm dst, X64Xmm src) {
    emit8(0xF2);
    if (dst >= 8 || src >= 8) {
        emit8(0x40 | ((dst >= 8) ? 4 : 0) | ((src >= 8) ? 1 : 0));
    }
    emit8(0x0F);
    emit8(0x5C);
    emit8(0xC0 | ((dst & 7) << 3) | (src & 7));
}

void X64Assembler::mulsd_xmm_xmm(X64Xmm dst, X64Xmm src) {
    emit8(0xF2);
    if (dst >= 8 || src >= 8) {
        emit8(0x40 | ((dst >= 8) ? 4 : 0) | ((src >= 8) ? 1 : 0));
    }
    emit8(0x0F);
    emit8(0x59);
    emit8(0xC0 | ((dst & 7) << 3) | (src & 7));
}

void X64Assembler::divsd_xmm_xmm(X64Xmm dst, X64Xmm src) {
    emit8(0xF2);
    if (dst >= 8 || src >= 8) {
        emit8(0x40 | ((dst >= 8) ? 4 : 0) | ((src >= 8) ? 1 : 0));
    }
    emit8(0x0F);
    emit8(0x5E);
    emit8(0xC0 | ((dst & 7) << 3) | (src & 7));
}

void X64Assembler::comisd_xmm_xmm(X64Xmm dst, X64Xmm src) {
    emit8(0x66);
    if (dst >= 8 || src >= 8) {
        emit8(0x40 | ((dst >= 8) ? 4 : 0) | ((src >= 8) ? 1 : 0));
    }
    emit8(0x0F);
    emit8(0x2F);
    emit8(0xC0 | ((dst & 7) << 3) | (src & 7));
}

void X64Assembler::ucomisd_xmm_xmm(X64Xmm dst, X64Xmm src) {
    emit8(0x66);
    if (dst >= 8 || src >= 8) {
        emit8(0x40 | ((dst >= 8) ? 4 : 0) | ((src >= 8) ? 1 : 0));
    }
    emit8(0x0F);
    emit8(0x2E);
    emit8(0xC0 | ((dst & 7) << 3) | (src & 7));
}

void X64Assembler::sqrtsd_xmm_xmm(X64Xmm dst, X64Xmm src) {
    emit8(0xF2);
    if (dst >= 8 || src >= 8) {
        emit8(0x40 | ((dst >= 8) ? 4 : 0) | ((src >= 8) ? 1 : 0));
    }
    emit8(0x0F);
    emit8(0x51);
    emit8(0xC0 | ((dst & 7) << 3) | (src & 7));
}

void X64Assembler::ret() {
    emit8(0xC3);
}

void X64Assembler::call_reg(X64Reg reg) {
    if (reg >= 8) emit8(0x41);
    emit8(0xFF);
    emit8(0xD0 + (reg & 7));
}

void X64Assembler::call_rel32(size_t targetOffset) {
    emit8(0xE8);
    size_t cur = getOffset() + 4;
    int32_t rel = static_cast<int32_t>(targetOffset - cur);
    emit32(static_cast<uint32_t>(rel));
}

size_t X64Assembler::jmp_label() {
    emit8(0xE9);
    size_t offset = getOffset();
    emit32(0);
    return offset;
}

size_t X64Assembler::jcc_label(X64Cond cond) {
    emit8(0x0F);
    emit8(0x80 + cond);
    size_t offset = getOffset();
    emit32(0);
    return offset;
}

void X64Assembler::patch_jump(size_t labelOffset) {
    size_t cur = getOffset();
    int32_t rel = static_cast<int32_t>(cur - (labelOffset + 4));
    patch32(labelOffset, static_cast<uint32_t>(rel));
}

void X64Assembler::jmp_back(size_t targetOffset) {
    emit8(0xE9);
    size_t cur = getOffset() + 4;
    int32_t rel = static_cast<int32_t>(targetOffset - cur);
    emit32(static_cast<uint32_t>(rel));
}

void X64Assembler::nop() {
    emit8(0x90);
}

// ── C++ Helper Functions Called from JIT Machine Code ──────────────
extern "C" {

uint64_t jit_helper_call(VM* v, uint64_t calleeBits, int argCount, Value* args) {
    Value callee(calleeBits);
    if (IS_CLOSURE(callee)) {
        ObjClosure* cl = AS_CLOSURE(callee);
        ObjFunction* fn = cl->function;

        if (fn->jitNative == nullptr && !fn->jitDeclined && fn->name != nullptr) {
            JitNativeFn compiledFn = isli::jit::Compiler::compileFunction(fn);
            if (compiledFn != nullptr) {
                __atomic_store_n(&fn->jitNative, reinterpret_cast<void*>(compiledFn), __ATOMIC_RELEASE);
            }
        }

        if (fn->jitNative != nullptr && fn->arity == argCount) {
            JitNativeFn nativeFn = reinterpret_cast<JitNativeFn>(fn->jitNative);
            return nativeFn(v, args - 1, cl);
        }
        return jitCallClosure(v, cl, argCount, args).bits;
    }
    if (IS_NATIVE(callee)) {
        NativeFn native = AS_NATIVE(callee);
        Value res = native(argCount, args);
        return res.bits;
    }
    if (IS_STRUCT(callee)) {
        ObjStruct* structType = AS_STRUCT(callee);
        Value res = OBJ_VAL(newInstance(structType));
        return res.bits;
    }
    return NIL_VAL.bits;
}

void jit_helper_print(uint64_t valBits) {
    Value val(valBits);
    printValue(val);
    printf("\n");
    fflush(stdout);
}

uint64_t jit_helper_get_global_cached(VM* v, Chunk* chunk, uint32_t constIdx, ObjString* name) {
    if (chunk != nullptr && constIdx < static_cast<uint32_t>(chunk->globalCacheCapacity)) {
        Value* ptr = chunk->globalCache[constIdx];
        if (__builtin_expect(ptr != nullptr, 1)) {
            return ptr->bits;
        }
    }
    Table* globals = v->globalsPtr ? v->globalsPtr : &v->globals;
    isli::SharedMutex* lock = v->globalsLockPtr ? v->globalsLockPtr : &v->globalsLock;
    lock->lock_shared();
    Value* ptr = globals->getPtr(name);
    lock->unlock_shared();
    if (ptr != nullptr) {
        if (chunk != nullptr && constIdx < static_cast<uint32_t>(chunk->globalCacheCapacity)) {
            chunk->globalCache[constIdx] = ptr;
        }
        return ptr->bits;
    }
    return NIL_VAL.bits;
}

uint64_t jit_helper_get_global(VM* v, ObjString* name) {
    Value val = NIL_VAL;
    Table* globals = v->globalsPtr ? v->globalsPtr : &v->globals;
    isli::SharedMutex* lock = v->globalsLockPtr ? v->globalsLockPtr : &v->globalsLock;
    lock->lock_shared();
    globals->get(name, &val);
    lock->unlock_shared();
    return val.bits;
}

void jit_helper_set_global(VM* v, ObjString* name, uint64_t valBits) {
    Value val(valBits);
    Table* globals = v->globalsPtr ? v->globalsPtr : &v->globals;
    isli::SharedMutex* lock = v->globalsLockPtr ? v->globalsLockPtr : &v->globalsLock;
    lock->lock();
    globals->set(name, val);
    lock->unlock();
}

uint64_t jit_helper_add(VM* v, uint64_t aBits, uint64_t bBits) {
    (void)v;
    Value a(aBits);
    Value b(bBits);
    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        return NUMBER_VAL(AS_NUMBER(a) + AS_NUMBER(b)).bits;
    }
    if (IS_STRING(a) && IS_STRING(b)) {
        ObjString* sa = AS_STRING(a);
        ObjString* sb = AS_STRING(b);
        int length = sa->length + sb->length;
        char* chars = static_cast<char*>(std::malloc(length + 1));
        if (!chars) std::exit(1);
        std::memcpy(chars, sa->chars, sa->length);
        std::memcpy(chars + sa->length, sb->chars, sb->length);
        chars[length] = '\0';
        ObjString* res = takeString(chars, length);
        return OBJ_VAL(res).bits;
    }
    isliMarkJobFailed( "Operands must be two numbers or two strings.");
    return NIL_VAL.bits;
}

uint64_t jit_helper_sub(VM* v, uint64_t aBits, uint64_t bBits) {
    (void)v;
    Value a(aBits);
    Value b(bBits);
    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        return NUMBER_VAL(AS_NUMBER(a) - AS_NUMBER(b)).bits;
    }
    isliMarkJobFailed( "Operands must be numbers.");
    return NIL_VAL.bits;
}

uint64_t jit_helper_mul(VM* v, uint64_t aBits, uint64_t bBits) {
    (void)v;
    Value a(aBits);
    Value b(bBits);
    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        return NUMBER_VAL(AS_NUMBER(a) * AS_NUMBER(b)).bits;
    }
    isliMarkJobFailed( "Operands must be numbers.");
    return NIL_VAL.bits;
}

uint64_t jit_helper_div(VM* v, uint64_t aBits, uint64_t bBits) {
    (void)v;
    Value a(aBits);
    Value b(bBits);
    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        return NUMBER_VAL(AS_NUMBER(a) / AS_NUMBER(b)).bits;
    }
    isliMarkJobFailed( "Operands must be numbers.");
    return NIL_VAL.bits;
}

uint64_t jit_helper_mod(VM* v, uint64_t aBits, uint64_t bBits) {
    (void)v;
    Value a(aBits);
    Value b(bBits);
    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        double divisor = AS_NUMBER(b);
        if (divisor == 0.0) {
            isliMarkJobFailed( "Division by zero in modulo.");
            return NIL_VAL.bits;
        }
        return NUMBER_VAL(std::fmod(AS_NUMBER(a), divisor)).bits;
    }
    isliMarkJobFailed( "Operands must be numbers.");
    return NIL_VAL.bits;
}

uint64_t jit_helper_negate(VM* v, uint64_t aBits) {
    (void)v;
    Value a(aBits);
    if (IS_NUMBER(a)) {
        return NUMBER_VAL(-AS_NUMBER(a)).bits;
    }
    isliMarkJobFailed( "Operand must be a number.");
    return NIL_VAL.bits;
}

uint64_t jit_helper_equal(uint64_t aBits, uint64_t bBits) {
    Value a(aBits);
    Value b(bBits);
    return BOOL_VAL(valuesEqual(a, b)).bits;
}

uint64_t jit_helper_cmp_less(VM* v, uint64_t aBits, uint64_t bBits) {
    (void)v;
    Value a(aBits);
    Value b(bBits);
    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        return BOOL_VAL(AS_NUMBER(a) < AS_NUMBER(b)).bits;
    }
    if (IS_STRING(a) && IS_STRING(b)) {
        ObjString* sa = AS_STRING(a);
        ObjString* sb = AS_STRING(b);
        int minLen = std::min(sa->length, sb->length);
        int cmp = std::memcmp(sa->chars, sb->chars, minLen);
        if (cmp != 0) return BOOL_VAL(cmp < 0).bits;
        return BOOL_VAL(sa->length < sb->length).bits;
    }
    isliMarkJobFailed( "Operands must be numbers or strings.");
    return BOOL_VAL(false).bits;
}

uint64_t jit_helper_cmp_greater(VM* v, uint64_t aBits, uint64_t bBits) {
    (void)v;
    Value a(aBits);
    Value b(bBits);
    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        return BOOL_VAL(AS_NUMBER(a) > AS_NUMBER(b)).bits;
    }
    if (IS_STRING(a) && IS_STRING(b)) {
        ObjString* sa = AS_STRING(a);
        ObjString* sb = AS_STRING(b);
        int minLen = std::min(sa->length, sb->length);
        int cmp = std::memcmp(sa->chars, sb->chars, minLen);
        if (cmp != 0) return BOOL_VAL(cmp > 0).bits;
        return BOOL_VAL(sa->length > sb->length).bits;
    }
    isliMarkJobFailed( "Operands must be numbers or strings.");
    return BOOL_VAL(false).bits;
}

uint64_t jit_helper_get_index(VM* v, uint64_t arrayValBits, uint64_t indexValBits) {
    (void)v;
    Value arrayVal(arrayValBits);
    Value indexVal(indexValBits);
    if (__builtin_expect(IS_ARRAY(arrayVal) && IS_NUMBER(indexVal), 1)) {
        ObjArray* arr = AS_ARRAY(arrayVal);
        int idx = static_cast<int>(AS_NUMBER(indexVal));
        if (__builtin_expect(idx >= 0 && idx < arr->count, 1)) {
            Value elem = arr->values[idx];
            if (IS_NUMBER(elem)) return elem.bits;
            arr->lock.lock();
            uint64_t bits = cloneValue(arr->values[idx]).bits;
            arr->lock.unlock();
            return bits;
        }
    }
    if (__builtin_expect(IS_BUFFER(arrayVal) && IS_NUMBER(indexVal), 1)) {
        ObjBuffer* buf = AS_BUFFER(arrayVal);
        int idx = static_cast<int>(AS_NUMBER(indexVal));
        if (__builtin_expect(static_cast<unsigned>(idx) < static_cast<unsigned>(buf->count), 1)) {
            return NUMBER_VAL(bufferLoad(buf, idx)).bits;
        }
    }
    if (IS_POINTER(arrayVal)) {
        arrayVal = *AS_POINTER(arrayVal);
        if (IS_ARRAY(arrayVal) && IS_NUMBER(indexVal)) {
            ObjArray* arr = AS_ARRAY(arrayVal);
            int idx = static_cast<int>(AS_NUMBER(indexVal));
            if (idx >= 0 && idx < arr->count) {
                Value elem = arr->values[idx];
                if (IS_NUMBER(elem)) return elem.bits;
                arr->lock.lock();
                uint64_t bits = cloneValue(arr->values[idx]).bits;
                arr->lock.unlock();
                return bits;
            }
        }
        if (IS_BUFFER(arrayVal) && IS_NUMBER(indexVal)) {
            ObjBuffer* buf = AS_BUFFER(arrayVal);
            int idx = static_cast<int>(AS_NUMBER(indexVal));
            if (static_cast<unsigned>(idx) < static_cast<unsigned>(buf->count)) {
                return NUMBER_VAL(bufferLoad(buf, idx)).bits;
            }
        }
    }
    if (IS_BUFFER(arrayVal)) {
        if (!IS_NUMBER(indexVal)) {
            isliMarkJobFailed( "Buffer index must be a number.");
            return NIL_VAL.bits;
        }
        ObjBuffer* buf = AS_BUFFER(arrayVal);
        int idx = static_cast<int>(AS_NUMBER(indexVal));
        isliMarkJobFailed( "Buffer index %d out of bounds (length %d).", idx, buf->count);
        return NIL_VAL.bits;
    }
    if (!IS_ARRAY(arrayVal)) {
        isliMarkJobFailed( "Cannot index a non-array value.");
        return NIL_VAL.bits;
    }
    if (!IS_NUMBER(indexVal)) {
        isliMarkJobFailed( "Array index must be a number.");
        return NIL_VAL.bits;
    }
    ObjArray* arr = AS_ARRAY(arrayVal);
    int idx = static_cast<int>(AS_NUMBER(indexVal));
    isliMarkJobFailed("Array index %d out of bounds (length %d).", idx, arr->count);
    return NIL_VAL.bits;
}

void jit_helper_set_index(VM* v, uint64_t arrayValBits, uint64_t indexValBits, uint64_t valBits) {
    (void)v;
    Value arrayVal(arrayValBits);
    Value indexVal(indexValBits);
    Value val(valBits);
    if (__builtin_expect(IS_ARRAY(arrayVal) && IS_NUMBER(indexVal), 1)) {
        ObjArray* arr = AS_ARRAY(arrayVal);
        int idx = static_cast<int>(AS_NUMBER(indexVal));
        if (__builtin_expect(idx >= 0 && idx < arr->count, 1)) {
            if (IS_NUMBER(val) && IS_NUMBER(arr->values[idx])) {
                arr->values[idx] = val;
            } else {
                arr->lock.lock();
                Value oldElem = arr->values[idx];
                arr->values[idx] = cloneValue(val);
                arr->lock.unlock();
                dropValue(oldElem);
            }
            return;
        }
    }
    if (__builtin_expect(IS_BUFFER(arrayVal) && IS_NUMBER(indexVal) && IS_NUMBER(val), 1)) {
        ObjBuffer* buf = AS_BUFFER(arrayVal);
        int idx = static_cast<int>(AS_NUMBER(indexVal));
        if (__builtin_expect(static_cast<unsigned>(idx) < static_cast<unsigned>(buf->count), 1)) {
            bufferStore(buf, idx, AS_NUMBER(val));
            return;
        }
    }
    if (IS_POINTER(arrayVal)) {
        arrayVal = *AS_POINTER(arrayVal);
        if (IS_ARRAY(arrayVal) && IS_NUMBER(indexVal)) {
            ObjArray* arr = AS_ARRAY(arrayVal);
            int idx = static_cast<int>(AS_NUMBER(indexVal));
            if (idx >= 0 && idx < arr->count) {
                if (IS_NUMBER(val) && IS_NUMBER(arr->values[idx])) {
                    arr->values[idx] = val;
                } else {
                    arr->lock.lock();
                    Value oldElem = arr->values[idx];
                    arr->values[idx] = cloneValue(val);
                    arr->lock.unlock();
                    dropValue(oldElem);
                }
                return;
            }
        }
        if (IS_BUFFER(arrayVal) && IS_NUMBER(indexVal) && IS_NUMBER(val)) {
            ObjBuffer* buf = AS_BUFFER(arrayVal);
            int idx = static_cast<int>(AS_NUMBER(indexVal));
            if (static_cast<unsigned>(idx) < static_cast<unsigned>(buf->count)) {
                bufferStore(buf, idx, AS_NUMBER(val));
                return;
            }
        }
    }
    if (IS_BUFFER(arrayVal)) {
        if (!IS_NUMBER(val)) {
            isliMarkJobFailed( "Buffer elements must be numbers.");
            return;
        }
        if (!IS_NUMBER(indexVal)) {
            isliMarkJobFailed( "Buffer index must be a number.");
            return;
        }
        ObjBuffer* buf = AS_BUFFER(arrayVal);
        int idx = static_cast<int>(AS_NUMBER(indexVal));
        isliMarkJobFailed( "Buffer index %d out of bounds (length %d).", idx, buf->count);
        return;
    }
    if (!IS_ARRAY(arrayVal)) {
        isliMarkJobFailed( "Cannot index a non-array value.");
        return;
    }
    if (!IS_NUMBER(indexVal)) {
        isliMarkJobFailed( "Array index must be a number.");
        return;
    }
    ObjArray* arr = AS_ARRAY(arrayVal);
    int idx = static_cast<int>(AS_NUMBER(indexVal));
    isliMarkJobFailed( "Array index %d out of bounds (length %d).", idx, arr->count);
}

uint64_t jit_helper_clone_value(uint64_t bits) {
    return cloneValue(Value(bits)).bits;
}

void jit_helper_drop_value(uint64_t bits) {
    dropValue(Value(bits));
}

void jit_helper_set_local(uint64_t newBits, uint64_t* slot, int isParam) {
    Value nw(newBits);
    if (IS_OBJ(nw)) nw = cloneValue(nw);
    Value old{Value{*slot}};
    *slot = nw.bits;
    if (!isParam) dropValue(old);
}

uint64_t jit_helper_return_and_drop(uint64_t* top, uint64_t* end) {
    Value ret = NIL_VAL;
    if (top < end) ret = Value(*top);
    if (IS_OBJ(ret)) ret = cloneValue(ret);
    for (uint64_t* p = top; p < end; p++) dropValue(Value(*p));
    return ret.bits;
}

uint64_t jit_helper_get_index_buf(VM* v, uint64_t bufBits, uint64_t idxBits) {
    Value bufVal(bufBits);
    Value indexVal(idxBits);
    if (__builtin_expect(IS_BUFFER(bufVal) && IS_NUMBER(indexVal), 1)) {
        ObjBuffer* buf = AS_BUFFER(bufVal);
        int idx = static_cast<int>(AS_NUMBER(indexVal));
        if (__builtin_expect(static_cast<unsigned>(idx) < static_cast<unsigned>(buf->count), 1)) {
            return NUMBER_VAL(bufferLoad(buf, idx)).bits;
        }
    }
    return jit_helper_get_index(v, bufBits, idxBits);
}

void jit_helper_set_index_buf(VM* v, uint64_t bufBits, uint64_t idxBits, uint64_t valBits) {
    Value bufVal(bufBits);
    Value indexVal(idxBits);
    Value val(valBits);
    if (__builtin_expect(IS_BUFFER(bufVal) && IS_NUMBER(indexVal) && IS_NUMBER(val), 1)) {
        ObjBuffer* buf = AS_BUFFER(bufVal);
        int idx = static_cast<int>(AS_NUMBER(indexVal));
        if (__builtin_expect(static_cast<unsigned>(idx) < static_cast<unsigned>(buf->count), 1)) {
            bufferStore(buf, idx, AS_NUMBER(val));
            return;
        }
    }
    jit_helper_set_index(v, bufBits, idxBits, valBits);
}

} // extern "C"

// ── JIT Compiler: Bytecode -> Machine Code Emitter ─────────────────
JitNativeFn Compiler::compileFunction(ObjFunction* function) {
    static std::mutex compileMu;
    std::lock_guard<std::mutex> lk(compileMu);
    if (!function || function->chunk.count == 0) {
        if (function) function->jitDeclined = true;
        return nullptr;
    }
    if (function->jitNative != nullptr) {
        return reinterpret_cast<JitNativeFn>(function->jitNative);
    }
    if (function->jitDeclined) return nullptr;
    
    Chunk* chunk = &function->chunk;
    int maxLocalSlot = function->arity;
    int maxCallArgs = 0;
    
    // Quick pre-pass: Ensure all opcodes in the chunk are supported and compute frame layout
    for (int i = 0; i < chunk->count; ) {
        uint8_t op = chunk->code[i++];
        switch (op) {
            case OP_CONSTANT: i++; break;
            case OP_NIL:
            case OP_TRUE:
            case OP_FALSE:
            case OP_POP: break;
            case OP_GET_LOCAL:
            case OP_SET_LOCAL:
            case OP_GET_LOCAL_PTR: {
                uint8_t slot = chunk->code[i++];
                if (slot > maxLocalSlot) maxLocalSlot = slot;
                break;
            }
            case OP_GET_UPVALUE:
            case OP_SET_UPVALUE:
            case OP_GET_GLOBAL:
            case OP_SET_GLOBAL:
            case OP_DEFINE_GLOBAL: i++; break;
            case OP_ADD:
            case OP_ADD_NUM:
            case OP_SUBTRACT:
            case OP_SUBTRACT_NUM:
            case OP_MULTIPLY:
            case OP_MULTIPLY_NUM:
            case OP_DIVIDE:
            case OP_DIVIDE_NUM:
            case OP_MODULO:
            case OP_MODULO_NUM:
            case OP_NOT:
            case OP_NEGATE:
            case OP_LESS:
            case OP_GREATER:
            case OP_EQUAL:
            case OP_PRINT:
            case OP_GET_INDEX:
            case OP_GET_INDEX_NUM:
            case OP_GET_INDEX_BUF:
            case OP_SET_INDEX:
            case OP_SET_INDEX_NUM:
            case OP_SET_INDEX_BUF:
            case OP_RETURN: break;
            case OP_JUMP:
            case OP_JUMP_IF_FALSE:
            case OP_LOOP: i += 2; break;
            case OP_LOOP_INCR_LESS:
            case OP_LOOP_INCR_LEQ: {
                uint8_t s1 = chunk->code[i++];
                uint8_t s2 = chunk->code[i++];
                if (s1 > maxLocalSlot) maxLocalSlot = s1;
                if (s2 > maxLocalSlot) maxLocalSlot = s2;
                i += 2;
                break;
            }
            case OP_CALL: {
                uint8_t aCount = chunk->code[i++];
                if (aCount > maxCallArgs) maxCallArgs = aCount;
                break;
            }
            default:
                function->jitDeclined = true;
                return nullptr;
        }
    }
    
    if (maxLocalSlot >= 64 || maxCallArgs > 16) {
        function->jitDeclined = true;
        return nullptr;
    }
    
    int numLocals = maxLocalSlot + 1;
    int outSlotsCount = std::max(maxCallArgs + 1, 4);
    int maxEvalSlots = 64;
    
    int rawFrameSize = (numLocals * 8) + (maxEvalSlots * 8) + (outSlotsCount * 8) + 32;
    int alignedFrame = (rawFrameSize + 15) & ~15;
    int totalFrameSize = alignedFrame + 8; // (Entry: 8 mod 16) - 48 (6 pushes) = 8 mod 16. (8 - 8) = 0 mod 16
    
    X64Assembler as;
    
    // Standard x86-64 Stack Frame:
    // [RBP]                  = Saved R15
    // [RBP + 8]              = Saved R14
    // [RBP + 16]             = Saved R13
    // [RBP + 24]             = Saved R12
    // [RBP + 32]             = Saved RBX
    // [RBP + 40]             = Saved Old RBP
    // [RBP + 48]             = Return Address
    // [RBP - 8 .. -numLocals*8] = Local variables: local[i] = [RBP - 8 - i*8]
    // Evaluation stack grows downward from [RBP - numLocals*8]
    // [RSP + 32 .. RSP + 32 + outSlotsCount*8] = Outgoing call slots: slots[i] = [RSP + 32 + i*8]
    // [RSP .. RSP + 31]      = 32-byte Shadow Space for Windows x64 ABI calls
    
    as.push(RBP);
    as.push(RBX);
    as.push(R12);
    as.push(R13);
    as.push(R14);
    as.push(R15);
    as.mov_reg_reg(RBP, RSP);
    as.sub_rsp_imm32(totalFrameSize);
    
    as.mov_reg_reg(R12, RCX); // R12 = VM* v
    as.mov_reg_reg(R13, RDX); // R13 = Value* slots
    as.mov_reg_reg(R14, R8);  // R14 = ObjClosure* closure
    
    // Copy incoming parameters from slots into local variables frame.
    // Locals share the downward eval stack (same as the interpreter): a
    // declaration initializer is a push that occupies the next local slot.
    int paramCount = function->arity + 1;
    for (int p = 0; p < paramCount; p++) {
        as.mov_reg_mem(RAX, R13, p * 8);
        as.mov_mem_reg(RBP, -8 - p * 8, RAX);
    }
    if (numLocals > paramCount) {
        as.mov_reg_imm64(RAX, NIL_VAL.bits);
        for (int p = paramCount; p < numLocals; p++) {
            as.mov_mem_reg(RBP, -8 - p * 8, RAX);
        }
    }

    // R15 points at the last occupied slot (local[paramCount-1]) so the next
    // push (R15 -= 8; store) fills local[paramCount] — matching VM stackTop.
    as.mov_reg_reg(R15, RBP);
    as.mov_reg_imm64(RAX, static_cast<uint64_t>(paramCount) * 8);
    as.sub_reg_reg(R15, RAX);
    
    std::vector<size_t> byteToAsm(chunk->count + 1, 0);
    std::vector<std::pair<size_t, int>> pendingJumps;
    
    int ip = 0;
    while (ip < chunk->count) {
        byteToAsm[ip] = as.getOffset();
        uint8_t op = chunk->code[ip++];
        
        switch (op) {
            case OP_CONSTANT: {
                uint8_t constIdx = chunk->code[ip++];
                Value val = chunk->constants.values[constIdx];
                as.mov_reg_imm64(RAX, val.bits);
                as.mov_reg_imm64(RBX, 8);
                as.sub_reg_reg(R15, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                break;
            }
            case OP_NIL: {
                as.mov_reg_imm64(RAX, NIL_VAL.bits);
                as.mov_reg_imm64(RBX, 8);
                as.sub_reg_reg(R15, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                break;
            }
            case OP_TRUE: {
                as.mov_reg_imm64(RAX, TRUE_VAL.bits);
                as.mov_reg_imm64(RBX, 8);
                as.sub_reg_reg(R15, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                break;
            }
            case OP_FALSE: {
                as.mov_reg_imm64(RAX, FALSE_VAL.bits);
                as.mov_reg_imm64(RBX, 8);
                as.sub_reg_reg(R15, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                break;
            }
            case OP_POP: {
                as.mov_reg_mem(RCX, R15, 0);
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_drop_value));
                as.call_reg(RAX);
                as.mov_reg_imm64(RBX, 8);
                as.add_reg_reg(R15, RBX);
                break;
            }
            case OP_GET_LOCAL: {
                uint8_t slot = chunk->code[ip++];
                as.mov_reg_mem(RAX, RBP, -8 - slot * 8);
                as.mov_reg_reg(RCX, RAX);
                as.mov_reg_imm64(RBX, QNAN | SIGN_BIT);
                as.and_reg_reg(RCX, RBX);
                as.cmp_reg_reg(RCX, RBX);
                size_t lbl_not_obj = as.jcc_label(COND_NE);
                as.mov_reg_mem(RCX, RBP, -8 - slot * 8);
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_clone_value));
                as.call_reg(RAX);
                as.patch_jump(lbl_not_obj);
                as.mov_reg_imm64(RBX, 8);
                as.sub_reg_reg(R15, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = chunk->code[ip++];
                as.mov_reg_mem(RCX, R15, 0);
                as.lea_reg_mem(RDX, RBP, -8 - slot * 8);
                as.mov_reg_imm32(R8, slot < paramCount ? 1 : 0);
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_set_local));
                as.call_reg(RAX);
                break;
            }
            case OP_GET_LOCAL_PTR: {
                uint8_t slot = chunk->code[ip++];
                as.lea_reg_mem(RAX, RBP, -8 - slot * 8);
                as.mov_reg_imm64(RBX, PTR_MASK);
                as.and_reg_reg(RAX, RBX);
                as.mov_reg_imm64(RBX, QNAN | TAG_POINTER);
                as.or_reg_reg(RAX, RBX);
                
                as.mov_reg_imm64(RBX, 8);
                as.sub_reg_reg(R15, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                break;
            }
            case OP_GET_UPVALUE: {
                uint8_t slot = chunk->code[ip++];
                as.mov_reg_mem(RAX, R14, static_cast<int32_t>(offsetof(ObjClosure, upvalues)));
                as.mov_reg_mem(RAX, RAX, slot * 8);
                as.mov_reg_mem(RAX, RAX, static_cast<int32_t>(offsetof(ObjUpvalue, location)));
                as.mov_reg_mem(RAX, RAX, 0);

                as.mov_reg_reg(RCX, RAX);
                as.mov_reg_imm64(RBX, QNAN | SIGN_BIT);
                as.and_reg_reg(RCX, RBX);
                as.cmp_reg_reg(RCX, RBX);
                size_t lbl_uv_num = as.jcc_label(COND_NE);
                as.mov_reg_reg(RCX, RAX);
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_clone_value));
                as.call_reg(RAX);
                as.patch_jump(lbl_uv_num);

                as.mov_reg_imm64(RBX, 8);
                as.sub_reg_reg(R15, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                break;
            }
            case OP_SET_UPVALUE: {
                uint8_t slot = chunk->code[ip++];
                as.mov_reg_mem(RCX, R15, 0);
                as.mov_reg_mem(RDX, R14, static_cast<int32_t>(offsetof(ObjClosure, upvalues)));
                as.mov_reg_mem(RDX, RDX, slot * 8);
                as.mov_reg_mem(RDX, RDX, static_cast<int32_t>(offsetof(ObjUpvalue, location)));
                as.mov_reg_imm32(R8, 0);
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_set_local));
                as.call_reg(RAX);
                break;
            }
            case OP_GET_GLOBAL: {
                uint8_t constIdx = chunk->code[ip++];
                ObjString* name = AS_STRING(chunk->constants.values[constIdx]);
                if (function->name != nullptr && strcmp(function->name->chars, name->chars) == 0) {
                    // Fast self-reference: load local[0] (current closure)
                    as.mov_reg_mem(RAX, RBP, -8);
                } else {
                    as.mov_reg_reg(RCX, R12); // VM* v
                    as.mov_reg_imm64(RDX, reinterpret_cast<uint64_t>(chunk));
                    as.mov_reg_imm32(R8, constIdx);
                    as.mov_reg_imm64(R9, reinterpret_cast<uint64_t>(name));
                    as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_get_global_cached));
                    as.call_reg(RAX);
                }
                
                as.mov_reg_imm64(RBX, 8);
                as.sub_reg_reg(R15, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                break;
            }
            case OP_SET_GLOBAL:
            case OP_DEFINE_GLOBAL: {
                uint8_t constIdx = chunk->code[ip++];
                ObjString* name = AS_STRING(chunk->constants.values[constIdx]);
                as.mov_reg_reg(RCX, R12); // VM* v
                as.mov_reg_imm64(RDX, reinterpret_cast<uint64_t>(name));
                as.mov_reg_mem(R8, R15, 0); // peek value
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_set_global));
                as.call_reg(RAX);
                break;
            }
            case OP_ADD:
            case OP_ADD_NUM: {
                as.mov_reg_mem(RAX, R15, 8); // a
                as.mov_reg_mem(RBX, R15, 0); // b
                as.mov_reg_imm64(RCX, QNAN);
                
                as.mov_reg_reg(RDX, RAX);
                as.and_reg_reg(RDX, RCX);
                as.cmp_reg_reg(RDX, RCX);
                size_t lbl_not_num = as.jcc_label(COND_E);
                
                as.mov_reg_reg(RDX, RBX);
                as.and_reg_reg(RDX, RCX);
                as.cmp_reg_reg(RDX, RCX);
                size_t lbl_b_not_num = as.jcc_label(COND_E);
                
                as.movsd_xmm_mem(XMM1, R15, 0);
                as.movsd_xmm_mem(XMM0, R15, 8);
                as.addsd_xmm_xmm(XMM0, XMM1);
                as.mov_reg_imm64(RBX, 8);
                as.add_reg_reg(R15, RBX);
                as.movsd_mem_xmm(R15, 0, XMM0);
                size_t lbl_done = as.jmp_label();
                
                as.patch_jump(lbl_not_num);
                as.patch_jump(lbl_b_not_num);
                as.mov_reg_reg(RCX, R12);    // VM* v
                as.mov_reg_mem(RDX, R15, 8); // a
                as.mov_reg_mem(R8, R15, 0);  // b
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_add));
                as.call_reg(RAX);
                as.mov_reg_imm64(RBX, 8);
                as.add_reg_reg(R15, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                as.patch_jump(lbl_done);
                break;
            }
            case OP_SUBTRACT:
            case OP_SUBTRACT_NUM: {
                as.mov_reg_mem(RAX, R15, 8); // a
                as.mov_reg_mem(RBX, R15, 0); // b
                as.mov_reg_imm64(RCX, QNAN);
                
                as.mov_reg_reg(RDX, RAX);
                as.and_reg_reg(RDX, RCX);
                as.cmp_reg_reg(RDX, RCX);
                size_t lbl_not_num = as.jcc_label(COND_E);
                
                as.mov_reg_reg(RDX, RBX);
                as.and_reg_reg(RDX, RCX);
                as.cmp_reg_reg(RDX, RCX);
                size_t lbl_b_not_num = as.jcc_label(COND_E);
                
                as.movsd_xmm_mem(XMM1, R15, 0);
                as.movsd_xmm_mem(XMM0, R15, 8);
                as.subsd_xmm_xmm(XMM0, XMM1);
                as.mov_reg_imm64(RBX, 8);
                as.add_reg_reg(R15, RBX);
                as.movsd_mem_xmm(R15, 0, XMM0);
                size_t lbl_done = as.jmp_label();
                
                as.patch_jump(lbl_not_num);
                as.patch_jump(lbl_b_not_num);
                as.mov_reg_reg(RCX, R12);    // VM* v
                as.mov_reg_mem(RDX, R15, 8); // a
                as.mov_reg_mem(R8, R15, 0);  // b
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_sub));
                as.call_reg(RAX);
                as.mov_reg_imm64(RBX, 8);
                as.add_reg_reg(R15, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                as.patch_jump(lbl_done);
                break;
            }
            case OP_MULTIPLY:
            case OP_MULTIPLY_NUM: {
                as.mov_reg_mem(RAX, R15, 8); // a
                as.mov_reg_mem(RBX, R15, 0); // b
                as.mov_reg_imm64(RCX, QNAN);
                
                as.mov_reg_reg(RDX, RAX);
                as.and_reg_reg(RDX, RCX);
                as.cmp_reg_reg(RDX, RCX);
                size_t lbl_not_num = as.jcc_label(COND_E);
                
                as.mov_reg_reg(RDX, RBX);
                as.and_reg_reg(RDX, RCX);
                as.cmp_reg_reg(RDX, RCX);
                size_t lbl_b_not_num = as.jcc_label(COND_E);
                
                as.movsd_xmm_mem(XMM1, R15, 0);
                as.movsd_xmm_mem(XMM0, R15, 8);
                as.mulsd_xmm_xmm(XMM0, XMM1);
                as.mov_reg_imm64(RBX, 8);
                as.add_reg_reg(R15, RBX);
                as.movsd_mem_xmm(R15, 0, XMM0);
                size_t lbl_done = as.jmp_label();
                
                as.patch_jump(lbl_not_num);
                as.patch_jump(lbl_b_not_num);
                as.mov_reg_reg(RCX, R12);    // VM* v
                as.mov_reg_mem(RDX, R15, 8); // a
                as.mov_reg_mem(R8, R15, 0);  // b
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_mul));
                as.call_reg(RAX);
                as.mov_reg_imm64(RBX, 8);
                as.add_reg_reg(R15, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                as.patch_jump(lbl_done);
                break;
            }
            case OP_DIVIDE:
            case OP_DIVIDE_NUM: {
                as.mov_reg_mem(RAX, R15, 8); // a
                as.mov_reg_mem(RBX, R15, 0); // b
                as.mov_reg_imm64(RCX, QNAN);
                
                as.mov_reg_reg(RDX, RAX);
                as.and_reg_reg(RDX, RCX);
                as.cmp_reg_reg(RDX, RCX);
                size_t lbl_not_num = as.jcc_label(COND_E);
                
                as.mov_reg_reg(RDX, RBX);
                as.and_reg_reg(RDX, RCX);
                as.cmp_reg_reg(RDX, RCX);
                size_t lbl_b_not_num = as.jcc_label(COND_E);
                
                as.movsd_xmm_mem(XMM1, R15, 0);
                as.movsd_xmm_mem(XMM0, R15, 8);
                as.divsd_xmm_xmm(XMM0, XMM1);
                as.mov_reg_imm64(RBX, 8);
                as.add_reg_reg(R15, RBX);
                as.movsd_mem_xmm(R15, 0, XMM0);
                size_t lbl_done = as.jmp_label();
                
                as.patch_jump(lbl_not_num);
                as.patch_jump(lbl_b_not_num);
                as.mov_reg_reg(RCX, R12);    // VM* v
                as.mov_reg_mem(RDX, R15, 8); // a
                as.mov_reg_mem(R8, R15, 0);  // b
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_div));
                as.call_reg(RAX);
                as.mov_reg_imm64(RBX, 8);
                as.add_reg_reg(R15, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                as.patch_jump(lbl_done);
                break;
            }
            case OP_MODULO:
            case OP_MODULO_NUM: {
                as.mov_reg_reg(RCX, R12);    // VM* v
                as.mov_reg_mem(RDX, R15, 8); // a
                as.mov_reg_mem(R8, R15, 0);  // b
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_mod));
                as.call_reg(RAX);
                as.mov_reg_imm64(RBX, 8);
                as.add_reg_reg(R15, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                break;
            }
            case OP_NOT: {
                as.mov_reg_mem(RAX, R15, 0);
                as.mov_reg_imm64(RBX, FALSE_VAL.bits);
                as.cmp_reg_reg(RAX, RBX);
                size_t lbl_is_false = as.jcc_label(COND_E);
                as.mov_reg_imm64(RBX, NIL_VAL.bits);
                as.cmp_reg_reg(RAX, RBX);
                size_t lbl_is_nil = as.jcc_label(COND_E);
                
                // Truthy value -> false
                as.mov_reg_imm64(RAX, FALSE_VAL.bits);
                size_t lbl_end = as.jmp_label();
                
                // Falsey value -> true
                as.patch_jump(lbl_is_false);
                as.patch_jump(lbl_is_nil);
                as.mov_reg_imm64(RAX, TRUE_VAL.bits);
                
                as.patch_jump(lbl_end);
                as.mov_mem_reg(R15, 0, RAX);
                break;
            }
            case OP_NEGATE: {
                as.mov_reg_mem(RAX, R15, 0);
                as.mov_reg_imm64(RBX, QNAN);
                as.mov_reg_reg(RDX, RAX);
                as.and_reg_reg(RDX, RBX);
                as.cmp_reg_reg(RDX, RBX);
                size_t lbl_not_num = as.jcc_label(COND_E);
                
                as.mov_reg_imm64(RBX, 0x8000000000000000ULL);
                as.xor_reg_reg(RAX, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                size_t lbl_done = as.jmp_label();
                
                as.patch_jump(lbl_not_num);
                as.mov_reg_reg(RCX, R12);
                as.mov_reg_mem(RDX, R15, 0);
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_negate));
                as.call_reg(RAX);
                as.mov_mem_reg(R15, 0, RAX);
                as.patch_jump(lbl_done);
                break;
            }
            case OP_LESS: {
                as.mov_reg_mem(RAX, R15, 8);
                as.mov_reg_mem(RBX, R15, 0);
                as.mov_reg_imm64(RCX, QNAN);
                as.mov_reg_reg(RDX, RAX);
                as.and_reg_reg(RDX, RCX);
                as.cmp_reg_reg(RDX, RCX);
                size_t lbl_not_num = as.jcc_label(COND_E);
                as.mov_reg_reg(RDX, RBX);
                as.and_reg_reg(RDX, RCX);
                as.cmp_reg_reg(RDX, RCX);
                size_t lbl_not_num2 = as.jcc_label(COND_E);
                
                as.movsd_xmm_mem(XMM1, R15, 0); // b
                as.movsd_xmm_mem(XMM0, R15, 8); // a
                as.ucomisd_xmm_xmm(XMM0, XMM1);
                
                size_t lbl_true = as.jcc_label(COND_B); // JB -> a < b
                as.mov_reg_imm64(RAX, FALSE_VAL.bits);
                size_t lbl_end = as.jmp_label();
                as.patch_jump(lbl_true);
                as.mov_reg_imm64(RAX, TRUE_VAL.bits);
                as.patch_jump(lbl_end);
                size_t lbl_done = as.jmp_label();
                
                as.patch_jump(lbl_not_num);
                as.patch_jump(lbl_not_num2);
                as.mov_reg_reg(RCX, R12);    // VM* v
                as.mov_reg_mem(RDX, R15, 8); // a
                as.mov_reg_mem(R8, R15, 0);  // b
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_cmp_less));
                as.call_reg(RAX);
                
                as.patch_jump(lbl_done);
                as.mov_reg_imm64(RBX, 8);
                as.add_reg_reg(R15, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                break;
            }
            case OP_GREATER: {
                as.mov_reg_mem(RAX, R15, 8);
                as.mov_reg_mem(RBX, R15, 0);
                as.mov_reg_imm64(RCX, QNAN);
                as.mov_reg_reg(RDX, RAX);
                as.and_reg_reg(RDX, RCX);
                as.cmp_reg_reg(RDX, RCX);
                size_t lbl_not_num = as.jcc_label(COND_E);
                as.mov_reg_reg(RDX, RBX);
                as.and_reg_reg(RDX, RCX);
                as.cmp_reg_reg(RDX, RCX);
                size_t lbl_not_num2 = as.jcc_label(COND_E);
                
                as.movsd_xmm_mem(XMM1, R15, 0); // b
                as.movsd_xmm_mem(XMM0, R15, 8); // a
                as.ucomisd_xmm_xmm(XMM0, XMM1);
                
                size_t lbl_true = as.jcc_label(COND_A); // JA -> a > b
                as.mov_reg_imm64(RAX, FALSE_VAL.bits);
                size_t lbl_end = as.jmp_label();
                as.patch_jump(lbl_true);
                as.mov_reg_imm64(RAX, TRUE_VAL.bits);
                as.patch_jump(lbl_end);
                size_t lbl_done = as.jmp_label();
                
                as.patch_jump(lbl_not_num);
                as.patch_jump(lbl_not_num2);
                as.mov_reg_reg(RCX, R12);    // VM* v
                as.mov_reg_mem(RDX, R15, 8); // a
                as.mov_reg_mem(R8, R15, 0);  // b
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_cmp_greater));
                as.call_reg(RAX);
                
                as.patch_jump(lbl_done);
                as.mov_reg_imm64(RBX, 8);
                as.add_reg_reg(R15, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                break;
            }
            case OP_EQUAL: {
                as.mov_reg_mem(RAX, R15, 8); // a
                as.mov_reg_mem(RBX, R15, 0); // b
                as.cmp_reg_reg(RAX, RBX);
                size_t lbl_diff = as.jcc_label(COND_NE);
                
                // If same bits, check if QNAN (NaN != NaN)
                as.mov_reg_imm64(RCX, QNAN);
                as.mov_reg_reg(RDX, RAX);
                as.and_reg_reg(RDX, RCX);
                as.cmp_reg_reg(RDX, RCX);
                size_t lbl_nan = as.jcc_label(COND_E);
                
                as.mov_reg_imm64(RAX, TRUE_VAL.bits);
                size_t lbl_eq_done = as.jmp_label();
                
                as.patch_jump(lbl_diff);
                as.patch_jump(lbl_nan);
                as.mov_reg_mem(RCX, R15, 8);
                as.mov_reg_mem(RDX, R15, 0);
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_equal));
                as.call_reg(RAX);
                
                as.patch_jump(lbl_eq_done);
                as.mov_reg_imm64(RBX, 8);
                as.add_reg_reg(R15, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                break;
            }
            case OP_PRINT: {
                as.mov_reg_mem(RCX, R15, 0);
                as.mov_reg_imm64(RBX, 8);
                as.add_reg_reg(R15, RBX);
                
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_print));
                as.call_reg(RAX);
                break;
            }
            case OP_GET_INDEX:
            case OP_GET_INDEX_NUM: {
                as.mov_reg_reg(RCX, R12);
                as.mov_reg_mem(RDX, R15, 8);
                as.mov_reg_mem(R8, R15, 0);
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_get_index));
                as.call_reg(RAX);
                as.mov_reg_imm64(RBX, 8);
                as.add_reg_reg(R15, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                break;
            }
            case OP_GET_INDEX_BUF: {
                as.mov_reg_reg(RCX, R12);
                as.mov_reg_mem(RDX, R15, 8);
                as.mov_reg_mem(R8, R15, 0);
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_get_index_buf));
                as.call_reg(RAX);
                as.mov_reg_imm64(RBX, 8);
                as.add_reg_reg(R15, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                break;
            }
            case OP_SET_INDEX:
            case OP_SET_INDEX_NUM: {
                as.mov_reg_reg(RCX, R12);
                as.mov_reg_mem(RDX, R15, 16);
                as.mov_reg_mem(R8, R15, 8);
                as.mov_reg_mem(R9, R15, 0);
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_set_index));
                as.call_reg(RAX);
                as.mov_reg_mem(RAX, R15, 0);
                as.mov_reg_imm64(RBX, 16);
                as.add_reg_reg(R15, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                break;
            }
            case OP_SET_INDEX_BUF: {
                as.mov_reg_reg(RCX, R12);
                as.mov_reg_mem(RDX, R15, 16);
                as.mov_reg_mem(R8, R15, 8);
                as.mov_reg_mem(R9, R15, 0);
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_set_index_buf));
                as.call_reg(RAX);
                as.mov_reg_mem(RAX, R15, 0);
                as.mov_reg_imm64(RBX, 16);
                as.add_reg_reg(R15, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                break;
            }
            case OP_JUMP: {
                uint16_t offset = (static_cast<uint16_t>(chunk->code[ip]) << 8) | chunk->code[ip + 1];
                ip += 2;
                int targetIp = ip + offset;
                size_t jmpOffset = as.jmp_label();
                pendingJumps.push_back({jmpOffset, targetIp});
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = (static_cast<uint16_t>(chunk->code[ip]) << 8) | chunk->code[ip + 1];
                ip += 2;
                int targetIp = ip + offset;
                
                // Direct jump if FALSE_VAL
                as.mov_reg_mem(RAX, R15, 0);
                as.mov_reg_imm64(RBX, FALSE_VAL.bits);
                as.cmp_reg_reg(RAX, RBX);
                size_t jmpFalse = as.jcc_label(COND_E);
                pendingJumps.push_back({jmpFalse, targetIp});
                
                // Direct jump if NIL_VAL
                as.mov_reg_imm64(RBX, NIL_VAL.bits);
                as.cmp_reg_reg(RAX, RBX);
                size_t jmpNil = as.jcc_label(COND_E);
                pendingJumps.push_back({jmpNil, targetIp});
                break;
            }
            case OP_LOOP: {
                uint16_t offset = (static_cast<uint16_t>(chunk->code[ip]) << 8) | chunk->code[ip + 1];
                ip += 2;
                int targetIp = ip - offset;
                if (targetIp >= 0 && targetIp < static_cast<int>(byteToAsm.size()) && byteToAsm[targetIp] > 0) {
                    as.jmp_back(byteToAsm[targetIp]);
                } else {
                    size_t jmpOffset = as.jmp_label();
                    pendingJumps.push_back({jmpOffset, targetIp});
                }
                break;
            }
            case OP_LOOP_INCR_LESS:
            case OP_LOOP_INCR_LEQ: {
                bool isLeq = (op == OP_LOOP_INCR_LEQ);
                uint8_t slot = chunk->code[ip++];
                uint8_t limitSlot = chunk->code[ip++];
                uint16_t offset = (static_cast<uint16_t>(chunk->code[ip]) << 8) | chunk->code[ip + 1];
                ip += 2;
                int targetIp = ip - offset;
                
                // Load local[slot], increment by 1.0
                as.movsd_xmm_mem(XMM0, RBP, -8 - slot * 8);
                as.mov_reg_imm64(RAX, 0x3FF0000000000000ULL); // 1.0 double
                as.mov_mem_reg(RSP, 32, RAX);
                as.movsd_xmm_mem(XMM1, RSP, 32);
                as.addsd_xmm_xmm(XMM0, XMM1);
                as.movsd_mem_xmm(RBP, -8 - slot * 8, XMM0);
                
                // Compare local[slot] with local[limitSlot]
                as.movsd_xmm_mem(XMM1, RBP, -8 - limitSlot * 8);
                as.ucomisd_xmm_xmm(XMM0, XMM1);
                
                // If condition holds, jump back to target loop header
                if (targetIp >= 0 && targetIp < static_cast<int>(byteToAsm.size()) && byteToAsm[targetIp] > 0) {
                    size_t lbl_true = as.jcc_label(isLeq ? COND_BE : COND_B);
                    pendingJumps.push_back({lbl_true, targetIp});
                } else {
                    size_t lbl_true = as.jcc_label(isLeq ? COND_BE : COND_B);
                    pendingJumps.push_back({lbl_true, targetIp});
                }
                break;
            }
            case OP_CALL: {
                uint8_t argCount = chunk->code[ip++];
                
                // Outgoing slots buffer at [RSP + 32]:
                // slots[0] = callee
                as.mov_reg_mem(RAX, R15, argCount * 8);
                as.mov_mem_reg(RSP, 32, RAX);
                
                // slots[1..argCount] = args
                for (int a = 0; a < argCount; a++) {
                    as.mov_reg_mem(RAX, R15, (argCount - 1 - a) * 8);
                    as.mov_mem_reg(RSP, 32 + (a + 1) * 8, RAX);
                }
                
                // Fast path check: Is callee equal to local[0] (self-recursion)?
                as.mov_reg_mem(RAX, RBP, -8);  // local[0]
                as.mov_reg_mem(RBX, RSP, 32);  // callee
                as.cmp_reg_reg(RAX, RBX);
                size_t lbl_not_self = as.jcc_label(COND_NE);
                
                // === Direct Native Self Call (Zero Overhead) ===
                as.mov_reg_reg(RCX, R12);       // RCX = VM* v
                as.lea_reg_mem(RDX, RSP, 32);   // RDX = &slots[0]
                as.mov_reg_reg(R8, R14);        // R8 = ObjClosure* closure
                as.call_rel32(0);               // Direct relative call to offset 0!
                size_t lbl_after_call = as.jmp_label();
                
                // === General Call Fallback ===
                as.patch_jump(lbl_not_self);
                as.mov_reg_reg(RCX, R12);       // VM* v
                as.mov_reg_mem(RDX, RSP, 32);   // callee
                as.mov_reg_imm32(R8, argCount);
                as.lea_reg_mem(R9, RSP, 40);    // pointer to slots[1]
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_call));
                as.call_reg(RAX);
                
                as.patch_jump(lbl_after_call);
                
                // Return value in RAX
                as.mov_reg_imm64(RBX, (argCount + 1) * 8);
                as.add_reg_reg(R15, RBX);
                
                as.mov_reg_imm64(RBX, 8);
                as.sub_reg_reg(R15, RBX);
                as.mov_mem_reg(R15, 0, RAX);
                break;
            }
            case OP_RETURN: {
                as.mov_reg_reg(RCX, R15);
                as.mov_reg_reg(RDX, RBP);
                as.mov_reg_imm64(RAX, static_cast<uint64_t>(paramCount) * 8);
                as.sub_reg_reg(RDX, RAX);
                as.mov_reg_imm64(RAX, reinterpret_cast<uint64_t>(jit_helper_return_and_drop));
                as.call_reg(RAX);

                as.mov_reg_reg(RSP, RBP);
                as.pop(R15);
                as.pop(R14);
                as.pop(R13);
                as.pop(R12);
                as.pop(RBX);
                as.pop(RBP);
                as.ret();
                break;
            }
            default: {
                as.nop();
                break;
            }
        }
    }
    
    for (const auto& pj : pendingJumps) {
        size_t patchOffset = pj.first;
        int targetBytecode = pj.second;
        if (targetBytecode >= 0 && targetBytecode < static_cast<int>(byteToAsm.size())) {
            size_t targetAsm = byteToAsm[targetBytecode];
            int32_t rel = static_cast<int32_t>(targetAsm - (patchOffset + 4));
            as.patch32(patchOffset, static_cast<uint32_t>(rel));
        }
    }
    
    size_t codeSize = as.getOffset();
    if (codeSize == 0) return nullptr;
    
    void* execMem = ExecutableMemoryPool::allocate(codeSize);
    if (!execMem) return nullptr;
    
    std::memcpy(execMem, as.buffer.data(), codeSize);
    ExecutableMemoryPool::flushCache(execMem, codeSize);
    
    return reinterpret_cast<JitNativeFn>(execMem);
}

// ── Background Concurrent JIT Manager ──────────────────────────────
void Manager::init() {
    if (isRunning.load(std::memory_order_relaxed)) return;
    isRunning.store(true, std::memory_order_release);
    workerThread = std::thread(&Manager::workerLoop, this);
}

void Manager::shutdown() {
    if (!isRunning.load(std::memory_order_relaxed)) return;
    {
        std::lock_guard<std::mutex> lk(lock);
        isRunning.store(false, std::memory_order_release);
    }
    cv.notify_all();
    if (workerThread.joinable()) {
        workerThread.join();
    }
}

void Manager::requestCompilation(ObjFunction* function) {
    if (!function || function->name == nullptr || function->jitNative != nullptr || function->isQueued) return;
    
    {
        std::lock_guard<std::mutex> lk(lock);
        if (function->isQueued) return;
        function->isQueued = true;
        queue.push(function);
    }
    cv.notify_one();
}

void Manager::workerLoop() {
    while (isRunning.load(std::memory_order_relaxed)) {
        ObjFunction* fn = nullptr;
        {
            std::unique_lock<std::mutex> lk(lock);
            cv.wait(lk, [this] {
                return !isRunning.load(std::memory_order_relaxed) || !queue.empty();
            });
            
            if (!isRunning.load(std::memory_order_relaxed) && queue.empty()) {
                break;
            }
            
            if (!queue.empty()) {
                fn = queue.front();
                queue.pop();
            }
        }
        
        if (fn != nullptr && fn->jitNative == nullptr) {
            JitNativeFn nativeFn = Compiler::compileFunction(fn);
            if (nativeFn != nullptr) {
                __atomic_store_n(&fn->jitNative, reinterpret_cast<void*>(nativeFn), __ATOMIC_RELEASE);
            }
        }
    }
}

} // namespace jit
} // namespace isli
