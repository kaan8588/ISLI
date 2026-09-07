#ifndef ISLI_JIT_HPP
#define ISLI_JIT_HPP

#include <cstdint>
#include <cstddef>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include "common.hpp"
#include "value.hpp"

// Forward declarations
struct VM;
struct ObjFunction;
struct ObjClosure;

// Function pointer signature for JIT compiled native functions:
// Returns uint64_t bits in RAX, takes:
// RCX = VM* v
// RDX = Value* slots
// R8  = ObjClosure* closure
typedef uint64_t (*JitNativeFn)(VM* v, Value* slots, ObjClosure* closure);

namespace isli {
namespace jit {

// ── x86_64 Registers ──────────────────────────────────────────────
enum X64Reg : uint8_t {
    RAX = 0, RCX = 1, RDX = 2, RBX = 3,
    RSP = 4, RBP = 5, RSI = 6, RDI = 7,
    R8  = 8, R9  = 9, R10 = 10, R11 = 11,
    R12 = 12, R13 = 13, R14 = 14, R15 = 15
};

enum X64Xmm : uint8_t {
    XMM0 = 0, XMM1 = 1, XMM2 = 2, XMM3 = 3,
    XMM4 = 4, XMM5 = 5, XMM6 = 6, XMM7 = 7,
    XMM8 = 8, XMM9 = 9, XMM10 = 10, XMM11 = 11,
    XMM12 = 12, XMM13 = 13, XMM14 = 14, XMM15 = 15
};

enum X64Cond : uint8_t {
    COND_O = 0x0,  COND_NO = 0x1,
    COND_B = 0x2,  COND_AE = 0x3,
    COND_E = 0x4,  COND_NE = 0x5,
    COND_BE = 0x6, COND_A = 0x7,
    COND_S = 0x8,  COND_NS = 0x9,
    COND_P = 0xA,  COND_NP = 0xB,
    COND_L = 0xC,  COND_GE = 0xD,
    COND_LE = 0xE, COND_G = 0xF
};

// ── Executable Memory Allocator (W^X Compliant) ────────────────────
class ExecutableMemoryPool {
public:
    static void* allocate(size_t size);
    static void free(void* ptr, size_t size);
    static void flushCache(void* ptr, size_t size);
};

// ── Low-Level x86_64 Machine Code Assembler / Emitter ─────────────
class X64Assembler {
public:
    std::vector<uint8_t> buffer;

    X64Assembler() { buffer.reserve(4096); }

    size_t getOffset() const { return buffer.size(); }

    void emit8(uint8_t byte) { buffer.push_back(byte); }
    void emit16(uint16_t val) {
        emit8(val & 0xFF);
        emit8((val >> 8) & 0xFF);
    }
    void emit32(uint32_t val) {
        emit8(val & 0xFF);
        emit8((val >> 8) & 0xFF);
        emit8((val >> 16) & 0xFF);
        emit8((val >> 24) & 0xFF);
    }
    void emit64(uint64_t val) {
        emit32(static_cast<uint32_t>(val & 0xFFFFFFFF));
        emit32(static_cast<uint32_t>((val >> 32) & 0xFFFFFFFF));
    }

    void patch32(size_t offset, uint32_t val) {
        buffer[offset]     = val & 0xFF;
        buffer[offset + 1] = (val >> 8) & 0xFF;
        buffer[offset + 2] = (val >> 16) & 0xFF;
        buffer[offset + 3] = (val >> 24) & 0xFF;
    }

    // ── Instruction Emitters ──────────────────
    void push(X64Reg reg);
    void pop(X64Reg reg);

    void mov_reg_imm64(X64Reg reg, uint64_t imm);
    void mov_reg_reg(X64Reg dst, X64Reg src);
    void mov_reg_mem(X64Reg dst, X64Reg base, int32_t disp);
    void mov_mem_reg(X64Reg base, int32_t disp, X64Reg src);
    void lea_reg_mem(X64Reg dst, X64Reg base, int32_t disp);

    void mov_mem_imm32(X64Reg base, int32_t disp, uint32_t imm);
    void mov_reg_imm32(X64Reg reg, uint32_t imm);

    void add_reg_reg(X64Reg dst, X64Reg src);
    void sub_reg_reg(X64Reg dst, X64Reg src);
    void xor_reg_reg(X64Reg dst, X64Reg src);
    void and_reg_reg(X64Reg dst, X64Reg src);
    void or_reg_reg(X64Reg dst, X64Reg src);
    void add_rsp_imm32(int32_t imm);
    void sub_rsp_imm32(int32_t imm);
    void cmp_reg_reg(X64Reg a, X64Reg b);
    void cmp_reg_imm32(X64Reg reg, int32_t imm);

    // SSE2 Floating Point (double precision 64-bit)
    void movsd_xmm_mem(X64Xmm dst, X64Reg base, int32_t disp);
    void movsd_mem_xmm(X64Reg base, int32_t disp, X64Xmm src);
    void movsd_xmm_xmm(X64Xmm dst, X64Xmm src);
    void addsd_xmm_xmm(X64Xmm dst, X64Xmm src);
    void subsd_xmm_xmm(X64Xmm dst, X64Xmm src);
    void mulsd_xmm_xmm(X64Xmm dst, X64Xmm src);
    void divsd_xmm_xmm(X64Xmm dst, X64Xmm src);
    void comisd_xmm_xmm(X64Xmm dst, X64Xmm src);
    void ucomisd_xmm_xmm(X64Xmm dst, X64Xmm src);
    void sqrtsd_xmm_xmm(X64Xmm dst, X64Xmm src);

    // Flow Control
    void ret();
    void call_reg(X64Reg reg);
    void call_rel32(size_t targetOffset);
    size_t jmp_label();
    size_t jcc_label(X64Cond cond);
    void patch_jump(size_t labelOffset);
    void jmp_back(size_t targetOffset);

    // Helper
    void nop();
};

// ── Bytecode to x86_64 JIT Compiler ───────────────────────────────
class Compiler {
public:
    static JitNativeFn compileFunction(ObjFunction* function);
};

// ── Concurrent / Background JIT Manager ───────────────────────────
class Manager {
private:
    std::queue<ObjFunction*> queue;
    std::mutex lock;
    std::condition_variable cv;
    std::thread workerThread;
    std::atomic<bool> isRunning{false};

    void workerLoop();

public:
    static Manager& instance() {
        static Manager mgr;
        return mgr;
    }

    void init();
    void shutdown();
    void requestCompilation(ObjFunction* function);
};

// Threshold for background JIT invocation
static constexpr int JIT_HOT_THRESHOLD = 20;

} // namespace jit
} // namespace isli

#endif // ISLI_JIT_HPP
