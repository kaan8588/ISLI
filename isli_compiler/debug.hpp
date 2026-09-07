#ifndef ISLI_DEBUG_HPP
#define ISLI_DEBUG_HPP

#include "chunk.hpp"

void disassembleChunk(Chunk* chunk, const char* name);
int  disassembleInstruction(Chunk* chunk, int offset);

#endif // ISLI_DEBUG_HPP
