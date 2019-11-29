#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ_LIST(type, objectType) \
	(type *)allocateObject(sizeof(type), objectType, true)

#define ALLOCATE_OBJ(type, objectType) \
	(type *)allocateObject(sizeof(type), objectType, false)

static Obj *allocateObject(size_t size, ObjType type, bool isList)
{
	Obj *object = (Obj *)reallocate(NULL, 0, size);
	object->type = type;
	object->isDark = false;

	if (!isList)
	{
		object->next = vm.objects;
		vm.objects = object;
	}
	else
	{
		object->next = vm.listObjects;
		vm.listObjects = object;
	}

#ifdef DEBUG_TRACE_GC
	printf("%p allocate %ld for %d\n", object, size, type);
#endif

	return object;
}

ObjBoundMethod *newBoundMethod(Value receiver, ObjClosure *method)
{
	ObjBoundMethod *bound = ALLOCATE_OBJ(ObjBoundMethod,
										 OBJ_BOUND_METHOD);

	bound->receiver = receiver;
	bound->method = method;
	return bound;
}

ObjClass *newClass(ObjString *name)
{
	ObjClass *klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
	klass->name = name;
	initTable(&klass->methods);
	initTable(&klass->fields);
	return klass;
}

ObjClosure *newClosure(ObjFunction *function)
{
	ObjUpvalue **upvalues = ALLOCATE(ObjUpvalue *, function->upvalueCount);
	for (int i = 0; i < function->upvalueCount; i++)
	{
		upvalues[i] = NULL;
	}

	ObjClosure *closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
	closure->function = function;
	closure->upvalues = upvalues;
	closure->upvalueCount = function->upvalueCount;
	return closure;
}

ObjFunction *newFunction()
{
	ObjFunction *function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);

	function->arity = 0;
	function->upvalueCount = 0;
	function->name = NULL;
	function->staticMethod = false;
	initChunk(&function->chunk);
	return function;
}

ObjInstance *newInstance(ObjClass *klass)
{
	ObjInstance *instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
	instance->klass = klass;
	initTable(&instance->fields);
	tableAddAll(&klass->fields, &instance->fields);
	return instance;
}

ObjNative *newNative(NativeFn function)
{
	ObjNative *native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
	native->function = function;
	return native;
}

static ObjString *allocateString(char *chars, int length,
								 uint32_t hash)
{
	ObjString *string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
	string->length = length;
	string->chars = chars;
	string->hash = hash;
	push(OBJ_VAL(string));
	tableSet(&vm.strings, string, NONE_VAL);
	pop();
	return string;
}

ObjList *initList()
{
	ObjList *list = ALLOCATE_OBJ(ObjList, OBJ_LIST);
	initValueArray(&list->values);
	return list;
}

ObjDict *initDict()
{
	ObjDict *dict = initDictValues(8);
	return dict;
}

ObjFile *initFile()
{
	ObjFile *file = ALLOCATE_OBJ(ObjFile, OBJ_FILE);
	file->isOpen = false;
	return file;
}

static uint32_t hashString(const char *key, int length)
{
	uint32_t hash = 2166136261u;

	for (int i = 0; i < length; i++)
	{
		hash ^= key[i];
		hash *= 16777619;
	}

	return hash;
}

ObjString *takeString(char *chars, int length)
{
	uint32_t hash = hashString(chars, length);
	ObjString *interned = tableFindString(&vm.strings, chars, length,
										  hash);
	if (interned != NULL)
	{
		FREE_ARRAY(char, chars, length + 1);
		return interned;
	}

	return allocateString(chars, length, hash);
}

ObjString *copyString(const char *chars, int length)
{
	uint32_t hash = hashString(chars, length);
	ObjString *interned = tableFindString(&vm.strings, chars, length,
										  hash);
	if (interned != NULL)
		return interned;
	char *heapChars = ALLOCATE(char, length + 1);
	memcpy(heapChars, chars, length);
	heapChars[length] = '\0';

	return allocateString(heapChars, length, hash);
}

ObjUpvalue *newUpvalue(Value *slot)
{
	ObjUpvalue *upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
	upvalue->closed = NONE_VAL;
	upvalue->location = slot;
	upvalue->next = NULL;
	return upvalue;
}

static void printFunction(ObjFunction *function)
{
	if (function->name == NULL)
	{
		printf("<script>");
		return;
	}
	printf("<fn %s>", function->name->chars);
}

char *objectToString(Value value)
{
	Obj *obj = AS_OBJ(value);
	ObjType tp = obj->type;
	switch (OBJ_TYPE(value))
	{
	case OBJ_CLASS:
	{
		ObjClass *klass = AS_CLASS(value);
		char *classString =
			malloc(sizeof(char) * (klass->name->length + 10));
		snprintf(classString, klass->name->length + 9, "<class %s>",
				 klass->name->chars);
		return classString;
	}

	case OBJ_BOUND_METHOD:
	{
		ObjBoundMethod *method = AS_BOUND_METHOD(value);
		char *methodString = malloc(
			sizeof(char) * (method->method->function->name->length + 17));
		char *methodType = method->method->function->staticMethod
							   ? "<static method %s>"
							   : "<bound method %s>";
		snprintf(methodString, method->method->function->name->length + 17,
				 methodType, method->method->function->name->chars);
		return methodString;
	}

	case OBJ_CLOSURE:
	{
		ObjClosure *closure = AS_CLOSURE(value);
		char *closureString = malloc(sizeof(char) * (closure->function->name->length + 8));
		snprintf(closureString, closure->function->name->length + 8,
				 "<func %s>", closure->function->name->chars);
		return closureString;
	}

	case OBJ_FUNCTION:
	{
		ObjFunction *function = AS_FUNCTION(value);
		char *functionString = malloc(sizeof(char) * (function->name->length + 8));
		snprintf(functionString, function->name->length + 8, "<func %s>",
				 function->name->chars);
		return functionString;
	}

	case OBJ_INSTANCE:
	{
		ObjInstance *instance = AS_INSTANCE(value);
		char *instanceString = malloc(sizeof(char) * (instance->klass->name->length + 12));
		snprintf(instanceString, instance->klass->name->length + 12,
				 "<%s instance>", instance->klass->name->chars);
		return instanceString;
	}

	//case OBJ_NATIVE_VOID:
	case OBJ_NATIVE:
	{
		char *nativeString = malloc(sizeof(char) * 15);
		snprintf(nativeString, 14, "%s", "<native func>");
		return nativeString;
	}

	case OBJ_STRING:
	{
		ObjString *stringObj = AS_STRING(value);
		char *string = malloc(sizeof(char) * stringObj->length + 3);
		snprintf(string, stringObj->length + 3, "'%s'", stringObj->chars);
		return string;
	}

	case OBJ_FILE:
	{
		ObjFile *file = AS_FILE(value);
		char *fileString = malloc(sizeof(char) * (strlen(file->path) + 10));
		snprintf(fileString, strlen(file->path) + 9, "<file %s%s>",
				 file->path, file->isOpen ? "*" : "");
		return fileString;
	}

	case OBJ_LIST:
	{
		int size = 50;
		ObjList *list = AS_LIST(value);
		char *listString = calloc(size, sizeof(char));
		snprintf(listString, 2, "%s", "[");

		for (int i = 0; i < list->values.count; ++i)
		{
			char *element = valueToString(list->values.values[i]);

			int elementSize = strlen(element);
			int listStringSize = strlen(listString);

			if (elementSize > (size - listStringSize - 1))
			{
				if (elementSize > size * 2)
					size += elementSize * 2;
				else
					size *= 2;

				char *newB = realloc(listString, sizeof(char) * size);

				if (newB == NULL)
				{
					printf("Unable to allocate memory\n");
					exit(71);
				}

				listString = newB;
			}

			strncat(listString, element, size - listStringSize - 1);

			free(element);

			if (i != list->values.count - 1)
				strncat(listString, ", ", size - listStringSize - 1);
		}

		strncat(listString, "]", size - strlen(listString) - 1);
		return listString;
	}

	case OBJ_DICT:
	{
		int count = 0;
		int size = 50;
		ObjDict *dict = AS_DICT(value);
		char *dictString = malloc(sizeof(char) * size);
		snprintf(dictString, 2, "%s", "{");

		for (int i = 0; i < dict->capacity; ++i)
		{
			dictItem *item = dict->items[i];
			if (!item || item->deleted)
				continue;

			count++;

			int keySize = strlen(item->key);
			int dictStringSize = strlen(dictString);

			if (keySize > (size - dictStringSize - 1))
			{
				if (keySize > size * 2)
					size += keySize * 2;
				else
					size *= 2;

				char *newB = realloc(dictString, sizeof(char) * size);

				if (newB == NULL)
				{
					printf("Unable to allocate memory\n");
					exit(71);
				}

				dictString = newB;
			}

			char *dictKeyString =
				malloc(sizeof(char) * (strlen(item->key) + 5));
			snprintf(dictKeyString, (strlen(item->key) + 5),
					 "\"%s\": ", item->key);

			strncat(dictString, dictKeyString, size - dictStringSize - 1);
			free(dictKeyString);

			char *element = valueToString(item->item);
			int elementSize = strlen(element);

			if (elementSize > (size - dictStringSize - 1))
			{
				if (elementSize > size * 2)
					size += elementSize * 2;
				else
					size *= 2;

				char *newB = realloc(dictString, sizeof(char) * size);

				if (newB == NULL)
				{
					printf("Unable to allocate memory\n");
					exit(71);
				}

				dictString = newB;
			}

			strncat(dictString, element, size - dictStringSize - 1);

			free(element);

			if (count != dict->count)
				strncat(dictString, ", ", size - dictStringSize - 1);
		}

		strncat(dictString, "}", size - strlen(dictString) - 1);
		return dictString;
	}

	case OBJ_UPVALUE:
	{
		char *nativeString = malloc(sizeof(char) * 8);
		snprintf(nativeString, 8, "%s", "upvalue");
		return nativeString;
	}
	}

	char *unknown = malloc(sizeof(char) * 9);
	snprintf(unknown, 8, "%s", "unknown");
	return unknown;
}

char *objectType(Value value)
{
	Obj *obj = AS_OBJ(value);
	ObjType tp = obj->type;
	switch (OBJ_TYPE(value))
	{
	case OBJ_CLASS:
	{
		char *str = malloc(sizeof(char) * 7);
		snprintf(str, 6, "class");
		return str;
	}

	case OBJ_BOUND_METHOD:
	{
		char *str = malloc(sizeof(char) * 8);
		snprintf(str, 7, "method");
		return str;
	}

	case OBJ_CLOSURE:
	{
		char *str = malloc(sizeof(char) * 6);
		snprintf(str, 5, "func");
		return str;
	}

	case OBJ_FUNCTION:
	{
		char *str = malloc(sizeof(char) * 6);
		snprintf(str, 5, "func");
		return str;
	}

	case OBJ_INSTANCE:
	{
		char *str = malloc(sizeof(char) * 10);
		snprintf(str, 9, "instance");
		return str;
	}

	//case OBJ_NATIVE_VOID:
	case OBJ_NATIVE:
	{
		char *str = malloc(sizeof(char) * 8);
		snprintf(str, 7, "native");
		return str;
	}

	case OBJ_STRING:
	{
		char *str = malloc(sizeof(char) * 8);
		snprintf(str, 7, "string");
		return str;
	}

		
	case OBJ_FILE:
	{
		char *str = malloc(sizeof(char) * 6);
		snprintf(str, 5, "file");
		return str;
	}

	case OBJ_LIST:
	{
		char *str = malloc(sizeof(char) * 6);
		snprintf(str, 5, "list");
		return str;
	}

	case OBJ_DICT:
	{
		char *str = malloc(sizeof(char) * 6);
		snprintf(str, 5, "dict");
		return str;
	}

	case OBJ_UPVALUE:
	{
		char *str = malloc(sizeof(char) * 9);
		snprintf(str, 8, "upvalue");
		return str;
	}
	}

	char *unknown = malloc(sizeof(char) * 9);
	snprintf(unknown, 8, "%s", "unknown");
	return unknown;
}

bool dictComparison(Value a, Value b)
{
	ObjDict *dict = AS_DICT(a);
	ObjDict *dictB = AS_DICT(b);

	// Different lengths, not the same
	if (dict->capacity != dictB->capacity)
		return false;

	// Lengths are the same, and dict 1 has 0 length
	// therefore both are empty
	if (dict->count == 0)
		return true;

	for (int i = 0; i < dict->capacity; ++i)
	{
		dictItem *item = dict->items[i];
		dictItem *itemB = dictB->items[i];

		if (!item || !itemB)
		{
			continue;
		}

		if (strcmp(item->key, itemB->key) != 0)
			return false;

		if (!valuesEqual(item->item, itemB->item))
			return false;
	}

	return true;
}

bool listComparison(Value a, Value b)
{
	ObjList *list = AS_LIST(a);
	ObjList *listB = AS_LIST(b);

	if (list->values.count != listB->values.count)
		return false;

	for (int i = 0; i < list->values.count; ++i)
	{
		if (!valuesEqual(list->values.values[i], listB->values.values[i]))
			return false;
	}

	return true;
}

bool objectComparison(Value a, Value b)
{
	if (!IS_OBJ(a) || !IS_OBJ(b))
	{
		return false;
	}
	else if (IS_STRING(a) && IS_STRING(b))
	{
		return strcmp(AS_CSTRING(a), AS_CSTRING(b)) == 0;
	}
	else if (IS_DICT(a) && IS_DICT(b))
	{
		return dictComparison(a, b);
	}
	else if (IS_LIST(a) && IS_LIST(b))
	{
		return listComparison(a, b);
	}
	else
	{
		return a == b;
	}
	return false;
}