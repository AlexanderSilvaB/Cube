#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "mempool.h"
#include "strings.h"
#include "vm.h"

static bool splitString(int argCount)
{
    if (argCount != 2)
    {
        runtimeError("split() takes 2 arguments (%d given)", argCount);
        return false;
    }

    if (!IS_STRING(peek(0)))
    {
        runtimeError("Argument passed to split() must be a string");
        return false;
    }

    DISABLE_GC;

    char *delimiter = AS_CSTRING(pop());
    char *string = AS_CSTRING(pop());
    char *tmp = mp_malloc(strlen(string) + 1);
    char *tmpFree = tmp;
    strcpy(tmp, string);

    char *token;

    ObjList *list = initList();

    do
    {
        token = strstr(tmp, delimiter);
        if (token)
            *token = '\0';

        ObjString *str = copyString(tmp, strlen(tmp));
        writeValueArray(&list->values, OBJ_VAL(str));
        tmp = token + strlen(delimiter);
    } while (token != NULL);

    mp_free(tmpFree);
    push(OBJ_VAL(list));

    RESTORE_GC;

    return true;
}

Value stringSplit(Value orig, Value del)
{

    if (!IS_STRING(orig))
    {
        printf("Argument passed to split() must be a string\n");
        return NULL_VAL;
    }

    if (!IS_STRING(del))
    {
        printf("Argument passed to split() must be a string\n");
        return NULL_VAL;
    }

    char *delimiter = AS_CSTRING(del);
    char *string = AS_CSTRING(orig);
    char *tmp = mp_malloc(strlen(string) + 1);
    char *tmpFree = tmp;
    strcpy(tmp, string);

    char *token;

    ObjList *list = initList();

    do
    {
        token = strstr(tmp, delimiter);
        if (token)
            *token = '\0';

        ObjString *str = copyString(tmp, strlen(tmp));
        writeValueArray(&list->values, OBJ_VAL(str));
        tmp = token + strlen(delimiter);
    } while (token != NULL);

    mp_free(tmpFree);
    return OBJ_VAL(list);
}

static bool containsString(int argCount)
{
    if (argCount != 2)
    {
        runtimeError("contains() takes 2 arguments (%d given)", argCount);
        return false;
    }

    if (!IS_STRING(peek(0)))
    {
        runtimeError("Argument passed to contains() must be a string");
        return false;
    }

    char *delimiter = AS_CSTRING(pop());
    char *string = AS_CSTRING(pop());
    char *tmp = mp_malloc(strlen(string) + 1);
    char *tmpFree = tmp;
    strcpy(tmp, string);

    if (!strstr(tmp, delimiter))
    {
        push(FALSE_VAL);
        mp_free(tmp);
        return true;
    }

    push(TRUE_VAL);
    mp_free(tmpFree);
    return true;
}

bool stringContains(Value strinvV, Value delimiterV, Value *result)
{
    char *delimiter = AS_CSTRING(delimiterV);
    char *string = AS_CSTRING(strinvV);
    char *tmp = mp_malloc(strlen(string) + 1);
    char *tmpFree = tmp;
    strcpy(tmp, string);

    if (!strstr(tmp, delimiter))
    {
        *result = FALSE_VAL;
        mp_free(tmp);
        return true;
    }

    *result = TRUE_VAL;
    mp_free(tmpFree);
    return true;
}

static bool findString(int argCount)
{
    if (argCount < 2 || argCount > 3)
    {
        runtimeError("find() takes either 2 or 3 arguments (%d given)", argCount);
        return false;
    }

    int index = 1;

    if (argCount == 3)
    {
        if (!IS_NUMBER(peek(0)))
        {
            runtimeError("Index passed to find() must be a number");
            return false;
        }

        index = AS_NUMBER(pop());
    }

    if (!IS_STRING(peek(0)))
    {
        runtimeError("Substring passed to find() must be a string");
        return false;
    }

    char *substr = AS_CSTRING(pop());
    char *string = AS_CSTRING(pop());
    char *tmp = mp_malloc(strlen(string) + 1);
    char *tmpFree = tmp;
    strcpy(tmp, string);

    int position = 0;

    for (int i = 0; i < index; ++i)
    {
        char *result = strstr(tmp, substr);
        if (!result)
        {
            position = -1;
            break;
        }

        position += (result - tmp) + (i * strlen(substr));
        tmp = result + strlen(substr);
    }

    push(NUMBER_VAL(position));
    mp_free(tmpFree);
    return true;
}

static bool replaceString(int argCount)
{
    if (argCount != 3)
    {
        runtimeError("replace() takes 3 arguments (%d given)", argCount);
        return false;
    }

    if (!IS_STRING(peek(0)))
    {
        runtimeError("Argument passed to replace() must be a string");
        return false;
    }

    if (!IS_STRING(peek(1)))
    {
        runtimeError("Argument passed to replace() must be a string");
        return false;
    }

    char *replace = AS_CSTRING(pop());
    char *to_replace = AS_CSTRING(pop());
    Value stringValue = pop();
    char *string = AS_CSTRING(stringValue);

    int count = 0;
    size_t len = strlen(to_replace);

    // Make a copy of the string so we do not modify the original
    char *tmp = mp_malloc(strlen(string) + 1);
    char *tmpFree = tmp;
    char *tmp1 = mp_malloc(strlen(string) + 1);
    char *tmp1Free = tmp1;
    strcpy(tmp, string);
    strcpy(tmp1, string);

    while ((tmp = strstr(tmp, to_replace)) != NULL)
    {
        count++;
        tmp += len;
    }

    mp_free(tmpFree);

    if (count == 0)
    {
        push(stringValue);
        mp_free(tmp1Free); // We're exiting early so remember to free
        return true;
    }

    int length = strlen(tmp1) - count * (len - strlen(replace)) + 1;
    char *pos;
    char *newStr = mp_malloc(sizeof(char) * length);

    for (int i = 0; i < count; ++i)
    {
        pos = strstr(tmp1, to_replace);
        if (pos != NULL)
            *pos = '\0';

        if (i == 0)
            snprintf(newStr, length, "%s", tmp1);
        else
            strncat(newStr, tmp1, strlen(tmp1));

        strncat(newStr, replace, strlen(replace));
        tmp1 = pos + len;
    }

    strncat(newStr, tmp1, strlen(tmp1));
    ObjString *newString = copyString(newStr, length - 1);
    mp_free(newStr);
    mp_free(tmp1Free);
    push(OBJ_VAL(newString));
    return true;
}

static bool substrString(int argCount)
{
    if (argCount != 3)
    {
        runtimeError("substr() takes 3 argument (%d  given)", argCount);
        return false;
    }

    int length = AS_NUMBER(pop());
    int start = AS_NUMBER(pop());

    ObjString *string = AS_STRING(pop());

    if (start >= string->length)
    {
        runtimeError("start index out of bounds", start);
        return false;
    }

    if (start < 0)
        start = string->length + start;

    if (length < 0)
    {
        length = string->length + length;
    }

    if (start + length > string->length)
    {
        length = string->length - start;
    }

    char *temp = mp_malloc(sizeof(char) * (length + 1));
    strncpy(temp, string->chars + start, length);
    temp[length] = '\0';

    push(STRING_VAL(temp));
    mp_free(temp);
    return true;
}

static bool fromString(int argCount)
{
    if (argCount != 2)
    {
        runtimeError("from() takes 2 argument (%d  given)", argCount);
        return false;
    }

    int start = AS_NUMBER(pop());

    ObjString *string = AS_STRING(pop());

    if (start > string->length)
    {
        runtimeError("start index out of bounds", start);
        return false;
    }

    if (start < 0)
        start = string->length + start;

    int length = string->length - start;

    char *temp = mp_malloc(sizeof(char) * (length + 1));
    strncpy(temp, string->chars + start, length);
    temp[length] = '\0';

    push(STRING_VAL(temp));
    mp_free(temp);
    return true;
}

static bool lowerString(int argCount)
{
    if (argCount != 1)
    {
        runtimeError("lower() takes 1 argument (%d  given)", argCount);
        return false;
    }

    ObjString *string = AS_STRING(pop());

    char *temp = mp_malloc(sizeof(char) * (string->length + 1));

    for (int i = 0; string->chars[i]; i++)
        temp[i] = tolower(string->chars[i]);

    temp[string->length] = '\0';

    push(OBJ_VAL(copyString(temp, string->length)));
    mp_free(temp);
    return true;
}

static bool upperString(int argCount)
{
    if (argCount != 1)
    {
        runtimeError("upper() takes 1 argument (%d  given)", argCount);
        return false;
    }

    ObjString *string = AS_STRING(pop());

    char *temp = mp_malloc(sizeof(char) * (string->length + 1));

    for (int i = 0; string->chars[i]; i++)
        temp[i] = toupper(string->chars[i]);

    temp[string->length] = '\0';

    push(OBJ_VAL(copyString(temp, string->length)));
    mp_free(temp);
    return true;
}

static bool startsWithString(int argCount)
{
    if (argCount != 2)
    {
        runtimeError("startsWith() takes 2 arguments (%d  given)", argCount);
        return false;
    }

    if (!IS_STRING(peek(0)))
    {
        runtimeError("Argument passed to startsWith() must be a string");
        return false;
    }

    ObjString *start = AS_STRING(pop());
    char *string = AS_CSTRING(pop());

    push(BOOL_VAL(strncmp(string, start->chars, start->length) == 0));
    return true;
}

static bool endsWithString(int argCount)
{
    if (argCount != 2)
    {
        runtimeError("endsWith() takes 2 arguments (%d  given)", argCount);
        return false;
    }

    if (!IS_STRING(peek(0)))
    {
        runtimeError("Argument passed to endsWith() must be a string");
        return false;
    }

    ObjString *suffix = AS_STRING(pop());
    ObjString *string = AS_STRING(pop());

    if (string->length < suffix->length)
    {
        push(FALSE_VAL);
        return true;
    }

    push(BOOL_VAL(strcmp(string->chars + (string->length - suffix->length), suffix->chars) == 0));
    return true;
}

bool stringEndsWith(const char *string, const char *suffix)
{
    int stringLen = strlen(string);
    int suffixLen = strlen(suffix);
    if (stringLen < suffixLen)
        return false;

    return strcmp(string + (stringLen - suffixLen), suffix) == 0;
}

static bool leftStripString(int argCount)
{
    if (argCount != 1)
    {
        runtimeError("leftStrip() takes 1 argument (%d  given)", argCount);
        return false;
    }
    ObjString *string = AS_STRING(pop());

    bool charSeen = false;
    int i, count = 0;

    char *temp = mp_malloc(sizeof(char) * (string->length + 1));

    for (i = 0; i < string->length; ++i)
    {
        if (!charSeen && isspace(string->chars[i]))
        {
            count++;
            continue;
        }
        temp[i - count] = string->chars[i];
        charSeen = true;
    }
    temp[i - count] = '\0';
    push(OBJ_VAL(copyString(temp, strlen(temp))));
    mp_free(temp);
    return true;
}

static bool rightStripString(int argCount)
{
    if (argCount != 1)
    {
        runtimeError("rightStrip() takes 1 argument (%d  given)", argCount);
        return false;
    }
    ObjString *string = AS_STRING(pop());

    int length;

    char *temp = mp_malloc(sizeof(char) * (string->length + 1));

    for (length = string->length - 1; length > 0; --length)
        if (!isspace(string->chars[length]))
            break;

    strncpy(temp, string->chars, length + 1);
    temp[length + 1] = '\0';
    push(OBJ_VAL(copyString(temp, strlen(temp))));
    mp_free(temp);
    return true;
}

static bool stripString(int argCount)
{
    if (argCount != 1)
    {
        runtimeError("strip() takes 1 argument (%d  given)", argCount);
        return false;
    }

    leftStripString(1);
    rightStripString(1);
    return true;
}

static bool formatString(int argCount)
{
    if (argCount == 1)
    {
        runtimeError("format() takes at least 2 arguments (%d given)", argCount);
        return false;
    }

    int length = 0;
    char **replace_strings = mp_malloc((argCount - 1) * sizeof(char *));

    for (int j = argCount - 2; j >= 0; --j)
    {
        Value value = pop();
        if (!IS_STRING(value))
            replace_strings[j] = valueToString(value, false);
        else
        {
            ObjString *strObj = AS_STRING(value);
            char *str = mp_malloc(strObj->length + 1);
            snprintf(str, strObj->length + 1, "%s", strObj->chars);
            replace_strings[j] = str;
        }

        length += strlen(replace_strings[j]);
    }

    char *string = AS_CSTRING(pop());

    char *tmp = mp_malloc(strlen(string) + 1);
    char *tmpFree = tmp;
    strcpy(tmp, string);

    int count = 0;
    while ((tmp = strstr(tmp, "{}")))
    {
        count++;
        tmp++;
    }

    mp_free(tmpFree);

    if (count != argCount - 1)
    {
        runtimeError("format() placeholders do not match arguments");

        for (int i = 0; i < argCount - 1; ++i)
            mp_free(replace_strings[i]);

        mp_free(replace_strings);

        return false;
    }

    int fullLength = strlen(string) - count * 2 + length + 1;
    char *pos;
    char *tmp1 = mp_malloc(strlen(string) + 1);
    char *tmp1Free = tmp1;
    strcpy(tmp1, string);
    char *newStr = mp_malloc(sizeof(char) * fullLength);

    for (int i = 0; i < argCount - 1; ++i)
    {
        pos = strstr(tmp1, "{}");
        if (pos != NULL)
            *pos = '\0';

        if (i == 0)
            snprintf(newStr, fullLength, "%s", tmp1);
        else
            strncat(newStr, tmp1, strlen(tmp1));

        strncat(newStr, replace_strings[i], strlen(replace_strings[i]));
        tmp1 = pos + 2;

        mp_free(replace_strings[i]);
    }

    mp_free(replace_strings);

    strncat(newStr, tmp1, strlen(tmp1));
    ObjString *newString = copyString(newStr, fullLength - 1);
    push(OBJ_VAL(newString));

    mp_free(newStr);
    mp_free(tmp1Free);

    return true;
}

bool stringMethods(char *method, int argCount)
{
    if (strcmp(method, "split") == 0)
        return splitString(argCount);
    else if (strcmp(method, "contains") == 0)
        return containsString(argCount);
    else if (strcmp(method, "find") == 0)
        return findString(argCount);
    else if (strcmp(method, "replace") == 0)
        return replaceString(argCount);
    else if (strcmp(method, "lower") == 0)
        return lowerString(argCount);
    else if (strcmp(method, "upper") == 0)
        return upperString(argCount);
    else if (strcmp(method, "startsWith") == 0)
        return startsWithString(argCount);
    else if (strcmp(method, "endsWith") == 0)
        return endsWithString(argCount);
    else if (strcmp(method, "leftStrip") == 0)
        return leftStripString(argCount);
    else if (strcmp(method, "rightStrip") == 0)
        return rightStripString(argCount);
    else if (strcmp(method, "strip") == 0)
        return stripString(argCount);
    else if (strcmp(method, "format") == 0)
        return formatString(argCount);
    else if (strcmp(method, "from") == 0)
        return fromString(argCount);
    else if (strcmp(method, "substr") == 0)
        return substrString(argCount);

    runtimeError("String has no method %s()", method);
    return false;
}