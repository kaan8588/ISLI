#ifndef ISLI_COMPILER_HPP
#define ISLI_COMPILER_HPP

#include "object.hpp"
#include "vm.hpp"

ObjFunction* compile(const char* source);

#endif // ISLI_COMPILER_HPP
