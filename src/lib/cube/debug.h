#ifndef _CUBE_DEBUG_H_
#define _CUBE_DEBUG_H_

#include "chunk.h"

void disassembleChunk(Chunk* chunk, const char* name);
int disassembleInstruction(Chunk* chunk, int offset);

#endif
