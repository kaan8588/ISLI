#ifndef ISLI_CHUNK_HPP
#define ISLI_CHUNK_HPP

#include "common.hpp"
#include "value.hpp"

enum OpCode : uint8_t {
    OP_CONSTANT,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_POP,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_DEFINE_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_GET_PROPERTY,
    OP_SET_PROPERTY,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MODULO,
    OP_NOT,
    OP_NEGATE,
    OP_PRINT,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,
    OP_CALL,
    OP_CLOSURE,
    OP_CLOSE_UPVALUE,
    OP_RETURN,
    OP_STRUCT,
    OP_DISPATCH,
    OP_DISPATCH2,
    OP_GET_LOCAL_PTR,
    OP_GET_GLOBAL_PTR,
    OP_GET_PROPERTY_PTR,
    OP_GET_UPVALUE_PTR,
    OP_DEREF,
    OP_SET_DEREF,
    OP_ANONYMOUS_STRUCT,
    OP_GET_INDEX,
    OP_SET_INDEX,
    OP_GET_INDEX_PTR,
    OP_BUILD_ARRAY,
    OP_SIMD_ARRAY_COPY,
    OP_SIMD_ARRAY_FILL,
    // Quickened / Specialized Opcodes (Zero-overhead fast paths)
    OP_ADD_NUM,
    OP_SUBTRACT_NUM,
    OP_MULTIPLY_NUM,
    OP_DIVIDE_NUM,
    OP_MODULO_NUM,
    OP_GET_INDEX_NUM,
    OP_SET_INDEX_NUM,
    OP_GET_INDEX_BUF,
    OP_SET_INDEX_BUF,
    // Superinstructions: combined increment + condition + loop
    OP_LOOP_INCR_LESS,
    OP_LOOP_INCR_LEQ,
    OP__COUNT
};

struct Chunk {
    int count    = 0;
    int capacity = 0;
    uint8_t* code = nullptr;
    int* lines    = nullptr;
    ValueArray constants;
    Value** globalCache = nullptr;
    int globalCacheCapacity = 0;

    void init();
    void free();
    void write(uint8_t byte, int line);
    int  addConstant(Value value);
};

#endif // ISLI_CHUNK_HPP
