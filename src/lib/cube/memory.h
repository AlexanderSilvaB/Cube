#ifndef CLOX_memory_h
#define CLOX_memory_h

#include "object.h"

#define ALLOCATE(type, count) (type *)reallocate(NULL, 0, sizeof(type) * (count))

#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity)*2)

#define GROW_ARRAY(previous, type, oldCount, count)                                                                    \
    (type *)reallocate(previous, sizeof(type) * (oldCount), sizeof(type) * (count))

#define FREE_ARRAY(type, pointer, oldCount) reallocate(pointer, sizeof(type) * (oldCount), 0)

void *reallocate(void *previous, size_t oldSize, size_t newSize);
/*
void markObject(Obj* object);
void markValue(Value value);
void collectGarbage();
*/

void freeObject(Obj *object);
void freeObjects();
void freeDictValue(dictItem *dictItem);
void freeFile(ObjFile *file);
void freeProcess(ObjProcess *process);
void freeNativeFunc(ObjNativeFunc *func);
void freeNativeStruct(ObjNativeStruct *func);
void freeNativeLib(ObjNativeLib *lib);

#endif
