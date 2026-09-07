#include "isli_api.hpp"
#include "vm.hpp"
#include "compiler.hpp"
#include "object.hpp"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>

struct IsliState {
    VM vm;
    std::string stringBuffer;
};

static Value* indexToSlot(IsliState* L, int index) {
    if (L == nullptr) return nullptr;
    int top = static_cast<int>(L->vm.stackTop - L->vm.stack);
    if (index > 0 && index <= top) {
        return L->vm.stack + (index - 1);
    } else if (index < 0 && (top + index) >= 0) {
        return L->vm.stackTop + index;
    }
    return nullptr;
}

extern "C" {

IsliState* isli_open(void) {
    IsliState* L = new IsliState();
    L->vm.init();
    return L;
}

void isli_close(IsliState* L) {
    if (L == nullptr) return;
    L->vm.free();
    delete L;
}

bool isli_dostring(IsliState* L, const char* source) {
    if (L == nullptr || source == nullptr) return false;
    InterpretResult res = interpretVM(&L->vm, source);
    return res == INTERPRET_OK;
}

bool isli_dofile(IsliState* L, const char* filepath) {
    if (L == nullptr || filepath == nullptr) return false;
    FILE* f = std::fopen(filepath, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    if (size < 0) {
        std::fclose(f);
        return false;
    }
    std::rewind(f);
    char* buf = static_cast<char*>(std::malloc(size + 1));
    if (!buf) {
        std::fclose(f);
        return false;
    }
    size_t read = std::fread(buf, 1, size, f);
    buf[read] = '\0';
    std::fclose(f);
    bool ok = isli_dostring(L, buf);
    std::free(buf);
    return ok;
}

int isli_gettop(IsliState* L) {
    if (L == nullptr) return 0;
    return static_cast<int>(L->vm.stackTop - L->vm.stack);
}

void isli_settop(IsliState* L, int index) {
    if (L == nullptr) return;
    if (index == 0) {
        while (L->vm.stackTop > L->vm.stack) {
            dropValue(popVM(&L->vm));
        }
    } else if (index > 0) {
        int target = index;
        int current = isli_gettop(L);
        while (current > target) {
            dropValue(popVM(&L->vm));
            current--;
        }
        while (current < target) {
            pushVM(&L->vm, NIL_VAL);
            current++;
        }
    } else if (index < 0) {
        int target = isli_gettop(L) + index + 1;
        isli_settop(L, target);
    }
}

void isli_pop(IsliState* L, int n) {
    if (L == nullptr) return;
    for (int i = 0; i < n && L->vm.stackTop > L->vm.stack; i++) {
        dropValue(popVM(&L->vm));
    }
}

void isli_pushnil(IsliState* L) {
    if (L == nullptr) return;
    pushVM(&L->vm, NIL_VAL);
}

void isli_pushboolean(IsliState* L, bool b) {
    if (L == nullptr) return;
    pushVM(&L->vm, BOOL_VAL(b));
}

void isli_pushnumber(IsliState* L, double n) {
    if (L == nullptr) return;
    pushVM(&L->vm, NUMBER_VAL(n));
}

void isli_pushstring(IsliState* L, const char* s) {
    if (L == nullptr || s == nullptr) return;
    ObjString* str = copyString(s, static_cast<int>(std::strlen(s)));
    pushVM(&L->vm, OBJ_VAL(str));
}

bool isli_isnil(IsliState* L, int index) {
    Value* slot = indexToSlot(L, index);
    return slot != nullptr && IS_NIL(*slot);
}

bool isli_isboolean(IsliState* L, int index) {
    Value* slot = indexToSlot(L, index);
    return slot != nullptr && IS_BOOL(*slot);
}

bool isli_isnumber(IsliState* L, int index) {
    Value* slot = indexToSlot(L, index);
    return slot != nullptr && IS_NUMBER(*slot);
}

bool isli_isstring(IsliState* L, int index) {
    Value* slot = indexToSlot(L, index);
    return slot != nullptr && IS_STRING(*slot);
}

bool isli_isarray(IsliState* L, int index) {
    Value* slot = indexToSlot(L, index);
    return slot != nullptr && IS_ARRAY(*slot);
}

bool isli_toboolean(IsliState* L, int index) {
    Value* slot = indexToSlot(L, index);
    if (!slot) return false;
    if (IS_BOOL(*slot)) return AS_BOOL(*slot);
    return !IS_NIL(*slot);
}

double isli_tonumber(IsliState* L, int index) {
    Value* slot = indexToSlot(L, index);
    if (!slot || !IS_NUMBER(*slot)) return 0.0;
    return AS_NUMBER(*slot);
}

const char* isli_tostring(IsliState* L, int index) {
    size_t dummy = 0;
    return isli_tolstring(L, index, &dummy);
}

const char* isli_tolstring(IsliState* L, int index, size_t* len) {
    if (L == nullptr) {
        if (len) *len = 0;
        return nullptr;
    }
    Value* slot = indexToSlot(L, index);
    if (!slot || !IS_STRING(*slot)) {
        if (len) *len = 0;
        return nullptr;
    }
    ObjString* str = AS_STRING(*slot);
    if (len) *len = static_cast<size_t>(str->length);
    if (str->chars[str->length] == '\0') {
        return str->chars;
    }
    L->stringBuffer.assign(str->chars, str->length);
    return L->stringBuffer.c_str();
}

int isli_stringlen(IsliState* L, int index) {
    Value* slot = indexToSlot(L, index);
    if (!slot || !IS_STRING(*slot)) return 0;
    return AS_STRING(*slot)->length;
}

bool isli_getglobal(IsliState* L, const char* name) {
    if (L == nullptr || name == nullptr) return false;
    ObjString* str = copyString(name, static_cast<int>(std::strlen(name)));
    Value val;
    L->vm.globalsLock.lock_shared();
    bool found = L->vm.globals.get(str, &val);
    if (found && IS_OBJ(val)) val = cloneValue(val);
    L->vm.globalsLock.unlock_shared();
    dropObject(reinterpret_cast<Obj*>(str));
    if (found) {
        pushVM(&L->vm, val);
        return true;
    }
    return false;
}

void isli_setglobal(IsliState* L, const char* name) {
    if (L == nullptr || name == nullptr || L->vm.stackTop <= L->vm.stack) return;
    Value val = popVM(&L->vm);
    ObjString* str = copyString(name, static_cast<int>(std::strlen(name)));
    L->vm.globalsLock.lock();
    L->vm.globals.set(str, val);
    L->vm.globalsLock.unlock();
    dropObject(reinterpret_cast<Obj*>(str));
}

void isli_pushcfunction(IsliState* L, IsliNativeFn fn) {
    if (L == nullptr || fn == nullptr) return;
    ObjNative* native = newNative(fn);
    pushVM(&L->vm, OBJ_VAL(native));
}

void isli_setcfunction(IsliState* L, const char* name, IsliNativeFn fn) {
    if (L == nullptr || name == nullptr || fn == nullptr) return;
    L->vm.globalsLock.lock();
    L->vm.defineNative(name, fn);
    L->vm.globalsLock.unlock();
}

}
