#ifndef ISLI_COMMON_HPP
#define ISLI_COMMON_HPP

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <immintrin.h>

// #define DEBUG_PRINT_CODE
//#define DEBUG_LOG_GC
//#define DEBUG_TRACE_DISPATCH
#define NAN_BOXING
#define UINT8_COUNT (UINT8_MAX + 1)
#define CACHE_LINE_SIZE 64
#define ISLI_MAX_WORKERS 64

// ── Alignment ──────────────────────────────────────────────────────
#if defined(__GNUC__) || defined(__clang__)
    #define ALIGN_64 __attribute__((aligned(CACHE_LINE_SIZE)))
#elif defined(_MSC_VER)
    #define ALIGN_64 __declspec(align(64))
#else
    #define ALIGN_64
#endif

// ── Platform-independent threading primitives (C++ std) ────────────
namespace isli {

using Mutex = std::mutex;
using SharedMutex = std::shared_mutex;

inline void mutexInit(Mutex&) { /* std::mutex is ready at construction */ }
inline void mutexLock(Mutex& m) { m.lock(); }
inline void mutexUnlock(Mutex& m) { m.unlock(); }
inline void mutexDestroy(Mutex&) { /* std::mutex cleans up at destruction */ }

using Cond = std::condition_variable;

inline void condInit(Cond&) { /* ready at construction */ }
inline void condWait(Cond& c, Mutex& m) {
    std::unique_lock<std::mutex> lk(m, std::adopt_lock);
    c.wait(lk);
    lk.release(); // ownership stays with caller
}
template <typename Predicate>
inline void condWait(Cond& c, Mutex& m, Predicate pred) {
    std::unique_lock<std::mutex> lk(m, std::adopt_lock);
    c.wait(lk, pred);
    lk.release();
}
inline void condSignal(Cond& c) { c.notify_one(); }
inline void condBroadcast(Cond& c) { c.notify_all(); }
inline void condDestroy(Cond&) { /* cleaned up at destruction */ }

inline void isliSleep(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// SpinLock using std::atomic_flag (lock-free, no volatile)
struct SpinLock {
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

    void lock() {
        while (flag.test_and_set(std::memory_order_acquire)) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
    #if defined(_MSC_VER)
            _mm_pause();
    #elif defined(__GNUC__) || defined(__clang__)
            __builtin_ia32_pause();
    #endif
#endif
        }
    }

    void unlock() {
        flag.clear(std::memory_order_release);
    }
};

} // namespace isli

// ── Grow capacity (same logic) ─────────────────────────────────────
#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

#endif // ISLI_COMMON_HPP
