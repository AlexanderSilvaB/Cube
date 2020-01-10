#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "object.h"
#include "memory.h"
#include "value.h"
#include "vm.h"

void initValueArray(ValueArray *array)
{
  array->values = NULL;
  array->capacity = 0;
  array->count = 0;
}

void writeValueArray(ValueArray *array, Value value)
{
  if (array->capacity < array->count + 1)
  {
    int oldCapacity = array->capacity;
    array->capacity = GROW_CAPACITY(oldCapacity);
    array->values = GROW_ARRAY(array->values, Value,
                               oldCapacity, array->capacity);
    for(int i = oldCapacity; i < array->capacity; i++)
      array->values[i] = NONE_VAL;
  }

  array->values[array->count] = value;
  array->count++;
}

void freeValueArray(ValueArray *array)
{
  FREE_ARRAY(Value, array->values, array->capacity);
  initValueArray(array);
}

static uint32_t hash(char *str)
{
  uint32_t hash = 5381;
  int c;

  while ((c = *str++))
    hash = ((hash << 5) + hash) + c;

  return hash;
}

void insertDict(ObjDict *dict, char *key, Value value)
{
  if (dict->count * 100 / dict->capacity >= 60)
    resizeDict(dict, true);

  uint32_t hashValue = hash(key);
  int index = hashValue % dict->capacity;
  char *key_m = ALLOCATE(char, strlen(key) + 1);

  if (!key_m)
  {
    printf("ERROR!");
    return;
  }

  strcpy(key_m, key);

  dictItem *item = ALLOCATE(dictItem, sizeof(dictItem));

  if (!item)
  {
    printf("ERROR!");
    return;
  }

  item->key = key_m;
  item->item = value;
  item->deleted = false;
  item->hash = hashValue;

  while (dict->items[index] && !dict->items[index]->deleted &&
         strcmp(dict->items[index]->key, key) != 0)
  {
    index++;
    if (index == dict->capacity)
      index = 0;
  }

  if (dict->items[index])
  {
    free(dict->items[index]->key);
    free(dict->items[index]);
    dict->count--;
  }

  dict->items[index] = item;
  dict->count++;
}

void resizeDict(ObjDict *dict, bool grow)
{
  int newSize;

  if (grow)
    newSize = dict->capacity << 1; // Grow by a factor of 2
  else
    newSize = dict->capacity >> 1; // Shrink by a factor of 2

  dictItem **items = calloc(newSize, sizeof(*dict->items));

  for (int j = 0; j < dict->capacity; ++j)
  {
    if (!dict->items[j])
      continue;
    if (dict->items[j]->deleted)
      continue;

    int index = dict->items[j]->hash % newSize;

    while (items[index])
      index = (index + 1) % newSize;

    items[index] = dict->items[j];
  }

  // Free deleted values
  for (int j = 0; j < dict->capacity; ++j)
  {
    if (!dict->items[j])
      continue;
    if (dict->items[j]->deleted)
      freeDictValue(dict->items[j]);
  }

  free(dict->items);

  dict->capacity = newSize;
  dict->items = items;
}

Value searchDict(ObjDict *dict, char *key)
{
  int index = hash(key) % dict->capacity;

  if (!dict->items[index])
    return NONE_VAL;

  while (index < dict->capacity && dict->items[index] &&
         !dict->items[index]->deleted &&
         strcmp(dict->items[index]->key, key) != 0)
  {
    index++;
    if (index == dict->capacity)
    {
      index = 0;
    }
  }

  if (dict->items[index] && !dict->items[index]->deleted)
  {
    return dict->items[index]->item;
  }

  return NONE_VAL;
}

// Calling function needs to free memory
char *valueToString(Value value, bool literal)
{
  if (IS_BOOL(value))
  {
    char *str = AS_BOOL(value) ? "true" : "false";
    char *boolString = malloc(sizeof(char) * (strlen(str) + 1));
    snprintf(boolString, strlen(str) + 1, "%s", str);
    return boolString;
  }
  else if (IS_NONE(value))
  {
    char *nilString = malloc(sizeof(char) * 5);
    snprintf(nilString, 5, "%s", "none");
    return nilString;
  }
  else if (IS_NUMBER(value))
  {
    double number = AS_NUMBER(value);
    int numberStringLength = snprintf(NULL, 0, "%.15g", number) + 1;
    char *numberString = malloc(sizeof(char) * numberStringLength);
    snprintf(numberString, numberStringLength, "%.15g", number);
    int l = strlen(numberString);
    for(int i = 0; i < l; i++)
    {
      if(numberString[i] == ',')
        numberString[i] = '.';
    }
    return numberString;
  }
  else if (IS_OBJ(value))
  {
    return objectToString(value, literal);
  }

  char *unknown = malloc(sizeof(char) * 8);
  snprintf(unknown, 8, "%s", "unknown");
  return unknown;
}

Value toBytes(Value value)
{
	ObjBytes *bytes = NULL;
	char c = 0xFF;
	if(IS_NONE(value))
		bytes = copyBytes(&c, 1);
	else if(IS_BOOL(value))
		bytes = copyBytes(&value.as.boolean, sizeof(value.as.boolean));
	else if(IS_NUMBER(value))
		bytes = copyBytes(&value.as.number, sizeof(value.as.number));
	else if(IS_OBJ(value))
  {
    bytes = objectToBytes(value);
  }

	if(bytes == NULL)
		bytes = initBytes();
	return OBJ_VAL(bytes);
}

char *valueType(Value value)
{
  if (IS_BOOL(value))
  {
    char *str = malloc(sizeof(char) * 6);
		snprintf(str, 5, "bool");
		return str;
  }
  else if (IS_NONE(value))
  {
    char *str = malloc(sizeof(char) * 6);
		snprintf(str, 5, "none");
		return str;
  }
  else if (IS_NUMBER(value))
  {
    char *str = malloc(sizeof(char) * 5);
		snprintf(str, 4, "num");
		return str;
  }
  else if (IS_OBJ(value))
  {
    return objectType(value);
  }

  char *unknown = malloc(sizeof(char) * 8);
  snprintf(unknown, 8, "unknown");
  return unknown;
}

void printValue(Value value)
{
  char *output = valueToString(value, true);
  printf("%s", output);
  free(output);
}

bool valuesEqual(Value a, Value b) 
{
#ifdef NAN_TAGGING
  if(IS_OBJ(a) && IS_OBJ(b))
  {
    return objectComparison(a, b);
  }

	return a == b;
#else
	if (a.type != b.type)
		return false;

	switch (a.type) {
		case VAL_BOOL:
			return AS_BOOL(a) == AS_BOOL(b);
		case VAL_NONE:
			return true;
		case VAL_NUMBER:
			return AS_NUMBER(a) == AS_NUMBER(b);
		case VAL_OBJ:
			return objectComparison(a, b);
	}
  
  return false;
#endif
}

Value toBool(Value value)
{
  if (IS_NONE(value))
    return BOOL_VAL(false);
  else if (IS_BOOL(value))
    return BOOL_VAL(AS_BOOL(value));
  else if (IS_NUMBER(value))
  {
    return BOOL_VAL(AS_NUMBER(value) > 0);
  }
  else if (IS_STRING(value))
  {
    char *str = AS_CSTRING(value);
    if (strlen(str) == 0 || strcmp(str, "0") == 0 || strcmp(str, "false") == 0)
      return BOOL_VAL(false);
    else
      return BOOL_VAL(false);
  }
  else if(IS_BYTES(value))
  {
    ObjBytes *bytes = AS_BYTES(value);
    if(bytes->length == 0)
      return FALSE_VAL;
    return bytes->bytes[0] == 0x0 ? FALSE_VAL : TRUE_VAL;
  }
  return BOOL_VAL(true);
}

Value toNumber(Value value)
{
  if (IS_NONE(value))
    return NUMBER_VAL(0);
  else if (IS_BOOL(value))
    return NUMBER_VAL(AS_BOOL(value) ? 1 : 0);
  else if (IS_NUMBER(value))
  {
    return NUMBER_VAL(AS_NUMBER(value));
  }
  else if (IS_STRING(value))
  {
    char *str = AS_CSTRING(value);
    double value = strtod(str, NULL);
    return NUMBER_VAL(value);
  }
  else if(IS_BYTES(value))
  {
    ObjBytes *bytes = AS_BYTES(value);
    char b[sizeof(double)];
    int len = bytes->length > sizeof(double) ? sizeof(double) : bytes->length;
    memcpy(b, bytes->bytes, len);
    double value = *((double*)b);
    return NUMBER_VAL(value);
  }
  return NUMBER_VAL(1);
}

Value toString(Value value)
{
  char *str = valueToString(value, false);
  Value v = STRING_VAL(str);
  free(str);
  return v;
}

Value copyValue(Value value)
{
  if(IS_STRING(value))
  {
    ObjString* oldStr = AS_STRING(value);
    char *str = ALLOCATE(char, oldStr->length + 1);
    strcpy(str, oldStr->chars);
    Value ret = STRING_VAL(str);
    FREE(char, str);
    return ret;
  }
  else if(IS_OBJ(value))
  {
    return copyObject(value);
  }
  return value;
}