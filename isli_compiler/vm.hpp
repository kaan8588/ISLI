#ifndef ISLI_VM_HPP
#define ISLI_VM_HPP

#include "object.hpp"
#include "table.hpp"
#include "value.hpp"
#include "jit.hpp"

#define FRAMES_MAX 4096
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

struct CallFrame {
    ObjClosure* closure = nullptr;
    uint8_t* ip = nullptr;
    Value* slots = nullptr;
};

struct VM {
    CallFrame frames[FRAMES_MAX];
    int frameCount = 0;

    Value stack[STACK_MAX];
    Value* stackTop = nullptr;
    Table globals;
    Table* globalsPtr = nullptr;
    ObjUpvalue* openUpvalues = nullptr;
    Obj* unownedObjects = nullptr; // Linked list of unowned objects (functions, structs, natives)

    // Threading locks (member level)
    isli::SharedMutex globalsLock;
    isli::SharedMutex* globalsLockPtr = nullptr;

    void init();
    void free();
    void push(Value value);
    Value pop();
    void defineNative(const char* name, NativeFn function);
};

enum InterpretResult {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
};

// The single global VM instance
extern VM vm;

InterpretResult interpret(const char* source);
InterpretResult interpretVM(VM* v, const char* source);

enum class TaskPriority {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2,
    SIMT_CRITICAL = 3
};

struct Task {
    ObjClosure* closure = nullptr;
    TaskPriority priority = TaskPriority::NORMAL;
    uint64_t sequence = 0;
    
    // SIMT batch parameters
    bool isSIMT = false;
    int dimCount = 1;
    int dimX = 1;
    int dimY = 1;
    int startIdx = 0;
    int count = 0;

    // General asynchronous task parameters
    int argCount = 0;
    Value args[4];

    bool operator<(const Task& other) const {
        if (priority != other.priority) {
            return static_cast<int>(priority) < static_cast<int>(other.priority);
        }
        return sequence > other.sequence; // FIFO tie breaker for equal priority
    }
};

void resetStack(VM* v);
void runtimeError(VM* v, const char* format, ...);
void isliMarkJobFailed(const char* format, ...);
Value jitCallClosure(VM* v, ObjClosure* cl, int argCount, Value* args);

// Push/pop on arbitrary VM (for worker threads)
inline void pushVM(VM* targetVM, Value value) {
    *targetVM->stackTop = value;
    targetVM->stackTop++;
}

inline Value popVM(VM* targetVM) {
    targetVM->stackTop--;
    return *targetVM->stackTop;
}

#endif // ISLI_VM_HPP
