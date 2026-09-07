#ifndef ISLI_API_HPP
#define ISLI_API_HPP

#include "value.hpp"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

typedef struct IsliState IsliState;

// ── VM Lifecycle ──────────────────────────────────────────────────
IsliState*  isli_open(void);
void        isli_close(IsliState* L);

// ── Execution ─────────────────────────────────────────────────────
bool        isli_dostring(IsliState* L, const char* source);
bool        isli_dofile(IsliState* L, const char* filepath);

// ── Stack Management ──────────────────────────────────────────────
int         isli_gettop(IsliState* L);
void        isli_settop(IsliState* L, int index);
void        isli_pop(IsliState* L, int n);

// ── Push to Stack ─────────────────────────────────────────────────
void        isli_pushnil(IsliState* L);
void        isli_pushboolean(IsliState* L, bool b);
void        isli_pushnumber(IsliState* L, double n);
void        isli_pushstring(IsliState* L, const char* s);

// ── Type Inspection (1-based from bottom, negative from top) ──────
bool        isli_isnil(IsliState* L, int index);
bool        isli_isboolean(IsliState* L, int index);
bool        isli_isnumber(IsliState* L, int index);
bool        isli_isstring(IsliState* L, int index);
bool        isli_isarray(IsliState* L, int index);

// ── Value Extraction ──────────────────────────────────────────────
bool        isli_toboolean(IsliState* L, int index);
double      isli_tonumber(IsliState* L, int index);
const char* isli_tostring(IsliState* L, int index);
const char* isli_tolstring(IsliState* L, int index, size_t* len);
int         isli_stringlen(IsliState* L, int index);

// ── Global Variables ──────────────────────────────────────────────
bool        isli_getglobal(IsliState* L, const char* name);
void        isli_setglobal(IsliState* L, const char* name);

// ── Native Functions ──────────────────────────────────────────────
typedef Value (*IsliNativeFn)(int argCount, Value* args);
void        isli_pushcfunction(IsliState* L, IsliNativeFn fn);
void        isli_setcfunction(IsliState* L, const char* name, IsliNativeFn fn);

#ifdef __cplusplus
}
#endif

#endif // ISLI_API_HPP
