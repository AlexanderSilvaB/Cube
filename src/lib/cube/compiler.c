#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct
{
  Token current;
  Token previous;
  bool hadError;
  bool panicMode;
} Parser;

typedef enum
{
  PREC_NONE,
  PREC_ASSIGNMENT, // =
  PREC_OR,         // or
  PREC_AND,        // and
  PREC_EQUALITY,   // == !=
  PREC_COMPARISON, // < > <= >=
  PREC_TERM,       // + -
  PREC_FACTOR,     // * /
  PREC_UNARY,      // ! -
  PREC_CALL,       // . ()
  PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct
{
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

typedef struct
{
  Token name;
  int depth;
  bool isCaptured;
} Local;

typedef struct
{
  uint8_t index;
  bool isLocal;
} Upvalue;

typedef enum
{
  TYPE_FUNCTION,
  TYPE_INITIALIZER,
  TYPE_METHOD,
  TYPE_SCRIPT
} FunctionType;

typedef struct Compiler
{
  struct Compiler *enclosing;
  ObjFunction *function;
  FunctionType type;

  Local locals[UINT8_COUNT];
  int localCount;
  Upvalue upvalues[UINT8_COUNT];
  int scopeDepth;
} Compiler;

typedef struct ClassCompiler
{
  struct ClassCompiler *enclosing;

  Token name;
  bool hasSuperclass;
} ClassCompiler;

Parser parser;

Compiler *current = NULL;

ClassCompiler *currentClass = NULL;

static Chunk *currentChunk()
{
  return &current->function->chunk;
}

static void errorAt(Token *token, const char *message)
{
  if (parser.panicMode)
    return;
  parser.panicMode = true;

  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF)
  {
    fprintf(stderr, " at end");
  }
  else if (token->type == TOKEN_ERROR)
  {
    // Nothing.
  }
  else
  {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

static void error(const char *message)
{
  errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char *message)
{
  errorAt(&parser.current, message);
}

static void advance()
{
  parser.previous = parser.current;

  for (;;)
  {
    parser.current = scanToken();
    if (parser.current.type != TOKEN_ERROR)
      break;

    errorAtCurrent(parser.current.start);
  }
}

static void consume(TokenType type, const char *message)
{
  if (parser.current.type == type)
  {
    advance();
    return;
  }

  errorAtCurrent(message);
}

static bool check(TokenType type)
{
  return parser.current.type == type;
}

static bool match(TokenType type)
{
  if (!check(type))
    return false;
  advance();
  return true;
}

static void emitByte(uint8_t byte)
{
  writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2)
{
  emitByte(byte1);
  emitByte(byte2);
}

static void emitLoop(int loopStart)
{
  emitByte(OP_LOOP);

  int offset = currentChunk()->count - loopStart + 2;
  if (offset > UINT16_MAX)
    error("Loop body too large.");

  emitByte((offset >> 8) & 0xff);
  emitByte(offset & 0xff);
}

static int emitJump(uint8_t instruction)
{
  emitByte(instruction);
  emitByte(0xff);
  emitByte(0xff);
  return currentChunk()->count - 2;
}

static void emitReturn()
{
  if (current->type == TYPE_INITIALIZER)
  {
    emitBytes(OP_GET_LOCAL, 0);
  }
  else
  {
    emitByte(OP_NONE);
  }

  emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value)
{
  int constant = addConstant(currentChunk(), value);
  if (constant > UINT8_MAX)
  {
    error("Too many constants in one chunk.");
    return 0;
  }

  return (uint8_t)constant;
}

static void emitConstant(Value value)
{
  emitBytes(OP_CONSTANT, makeConstant(value));
}

static void patchJump(int offset)
{
  int jump = currentChunk()->count - offset - 2;

  if (jump > UINT16_MAX)
  {
    error("Too much code to jump over.");
  }

  currentChunk()->code[offset] = (jump >> 8) & 0xff;
  currentChunk()->code[offset + 1] = jump & 0xff;
}

static void initCompiler(Compiler *compiler, FunctionType type)
{
  compiler->enclosing = current;
  compiler->function = NULL;
  compiler->type = type;
  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  compiler->function = newFunction();
  current = compiler;

  if (type != TYPE_SCRIPT)
  {
    current->function->name = copyString(parser.previous.start,
                                         parser.previous.length);
  }

  Local *local = &current->locals[current->localCount++];
  local->depth = 0;
  local->isCaptured = false;
  if (type != TYPE_FUNCTION)
  {
    local->name.start = "this";
    local->name.length = 4;
  }
  else
  {
    local->name.start = "";
    local->name.length = 0;
  }
}

static ObjFunction *endCompiler()
{
  emitReturn();
  ObjFunction *function = current->function;

#ifdef DEBUG_PRINT_CODE
  if (!parser.hadError)
  {
    disassembleChunk(currentChunk(),
                     function->name != NULL ? function->name->chars : "<script>");
  }
#endif

  current = current->enclosing;
  return function;
}

static void beginScope()
{
  current->scopeDepth++;
}

static void endScope()
{
  current->scopeDepth--;

  while (current->localCount > 0 &&
         current->locals[current->localCount - 1].depth >
             current->scopeDepth)
  {
    if (current->locals[current->localCount - 1].isCaptured)
    {
      emitByte(OP_CLOSE_UPVALUE);
    }
    else
    {
      emitByte(OP_POP);
    }
    current->localCount--;
  }
}

static void expression();
static void statement();
static void declaration(bool checkEnd);
static ParseRule *getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static uint8_t identifierConstant(Token *name)
{
  return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token *a, Token *b)
{
  if (a->length != b->length)
    return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler *compiler, Token *name)
{
  for (int i = compiler->localCount - 1; i >= 0; i--)
  {
    Local *local = &compiler->locals[i];
    if (identifiersEqual(name, &local->name))
    {
      if (local->depth == -1)
      {
        error("Cannot read local variable in its own initializer.");
      }
      return i;
    }
  }

  return -1;
}

static int addUpvalue(Compiler *compiler, uint8_t index, bool isLocal)
{
  int upvalueCount = compiler->function->upvalueCount;

  for (int i = 0; i < upvalueCount; i++)
  {
    Upvalue *upvalue = &compiler->upvalues[i];
    if (upvalue->index == index && upvalue->isLocal == isLocal)
    {
      return i;
    }
  }

  if (upvalueCount == UINT8_COUNT)
  {
    error("Too many closure variables in function.");
    return 0;
  }

  compiler->upvalues[upvalueCount].isLocal = isLocal;
  compiler->upvalues[upvalueCount].index = index;
  return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler *compiler, Token *name)
{
  if (compiler->enclosing == NULL)
    return -1;

  int local = resolveLocal(compiler->enclosing, name);
  if (local != -1)
  {
    compiler->enclosing->locals[local].isCaptured = true;
    return addUpvalue(compiler, (uint8_t)local, true);
  }

  int upvalue = resolveUpvalue(compiler->enclosing, name);
  if (upvalue != -1)
  {
    return addUpvalue(compiler, (uint8_t)upvalue, false);
  }

  return -1;
}

static void addLocal(Token name)
{
  if (current->localCount == UINT8_COUNT)
  {
    error("Too many local variables in function.");
    return;
  }

  Local *local = &current->locals[current->localCount++];
  local->name = name;
  local->depth = -1;
  local->isCaptured = false;
}

static void declareVariable()
{
  if (current->scopeDepth == 0)
    return;

  Token *name = &parser.previous;
  for (int i = current->localCount - 1; i >= 0; i--)
  {
    Local *local = &current->locals[i];
    if (local->depth != -1 && local->depth < current->scopeDepth)
    {
      break; // [negative]
    }

    if (identifiersEqual(name, &local->name))
    {
      error("Variable with this name already declared in this scope.");
    }
  }

  addLocal(*name);
}

static void declareNamedVariable(Token *name)
{
  if (current->scopeDepth == 0)
    return;

  for (int i = current->localCount - 1; i >= 0; i--)
  {
    Local *local = &current->locals[i];
    if (local->depth != -1 && local->depth < current->scopeDepth)
    {
      break; // [negative]
    }

    if (identifiersEqual(name, &local->name))
    {
      error("Variable with this name already declared in this scope.");
    }
  }

  addLocal(*name);
}

static uint8_t parseVariable(const char *errorMessage)
{
  consume(TOKEN_IDENTIFIER, errorMessage);

  declareVariable();
  if (current->scopeDepth > 0)
    return 0;

  return identifierConstant(&parser.previous);
}

static void markInitialized()
{
  if (current->scopeDepth == 0)
    return;
  current->locals[current->localCount - 1].depth =
      current->scopeDepth;
}

static void defineVariable(uint8_t global)
{
  if (current->scopeDepth > 0)
  {
    markInitialized();
    return;
  }

  emitBytes(OP_DEFINE_GLOBAL, global);
}

static uint8_t argumentList()
{
  uint8_t argCount = 0;
  if (!check(TOKEN_RIGHT_PAREN))
  {
    do
    {
      expression();

      if (argCount == 255)
      {
        error("Cannot have more than 255 arguments.");
      }
      argCount++;
    } while (match(TOKEN_COMMA));
  }

  consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
  return argCount;
}

static void and_(bool canAssign)
{
  int endJump = emitJump(OP_JUMP_IF_FALSE);

  emitByte(OP_POP);
  parsePrecedence(PREC_AND);

  patchJump(endJump);
}

static void binary(bool canAssign)
{
  TokenType operatorType = parser.previous.type;

  ParseRule *rule = getRule(operatorType);
  parsePrecedence((Precedence)(rule->precedence + 1));

  switch (operatorType)
  {
  case TOKEN_BANG_EQUAL:
    emitBytes(OP_EQUAL, OP_NOT);
    break;
  case TOKEN_EQUAL_EQUAL:
    emitByte(OP_EQUAL);
    break;
  case TOKEN_GREATER:
    emitByte(OP_GREATER);
    break;
  case TOKEN_GREATER_EQUAL:
    emitBytes(OP_LESS, OP_NOT);
    break;
  case TOKEN_LESS:
    emitByte(OP_LESS);
    break;
  case TOKEN_LESS_EQUAL:
    emitBytes(OP_GREATER, OP_NOT);
    break;
  case TOKEN_PLUS:
    emitByte(OP_ADD);
    break;
  case TOKEN_MINUS:
    emitByte(OP_SUBTRACT);
    break;
  case TOKEN_STAR:
    emitByte(OP_MULTIPLY);
    break;
  case TOKEN_SLASH:
    emitByte(OP_DIVIDE);
    break;
  case TOKEN_PERCENT:
    emitByte(OP_MOD);
    break;
  case TOKEN_POW:
    emitByte(OP_POW);
    break;
  default:
    return; // Unreachable.
  }
}

static void call(bool canAssign)
{
  uint8_t argCount = argumentList();
  emitBytes(OP_CALL, argCount);
}

static void dot(bool canAssign)
{
  consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
  uint8_t name = identifierConstant(&parser.previous);

  if (canAssign && match(TOKEN_PLUS_EQUALS))
  {
    emitBytes(OP_GET_PROPERTY_NO_POP, name);
    expression();
    emitByte(OP_ADD);
    emitBytes(OP_SET_PROPERTY, name);
  }
  else if (canAssign && match(TOKEN_MINUS_EQUALS))
  {
    emitBytes(OP_GET_PROPERTY_NO_POP, name);
    expression();
    emitByte(OP_SUBTRACT);
    emitBytes(OP_SET_PROPERTY, name);
  }
  else if (canAssign && match(TOKEN_MULTIPLY_EQUALS))
  {
    emitBytes(OP_GET_PROPERTY_NO_POP, name);
    expression();
    emitByte(OP_MULTIPLY);
    emitBytes(OP_SET_PROPERTY, name);
  }
  else if (canAssign && match(TOKEN_DIVIDE_EQUALS))
  {
    emitBytes(OP_GET_PROPERTY_NO_POP, name);
    expression();
    emitByte(OP_DIVIDE);
    emitBytes(OP_SET_PROPERTY, name);
  }
  else if (canAssign && match(TOKEN_EQUAL))
  {
    expression();
    emitBytes(OP_SET_PROPERTY, name);
  }
  else if (match(TOKEN_LEFT_PAREN))
  {
    uint8_t argCount = argumentList();
    emitBytes(OP_INVOKE, argCount);
    emitByte(name);
  }
  else
  {
    emitBytes(OP_GET_PROPERTY, name);
  }
}

static void literal(bool canAssign)
{
  switch (parser.previous.type)
  {
  case TOKEN_FALSE:
    emitByte(OP_FALSE);
    break;
  case TOKEN_NONE:
    emitByte(OP_NONE);
    break;
  case TOKEN_TRUE:
    emitByte(OP_TRUE);
    break;
  default:
    return; // Unreachable.
  }
}

static void grouping(bool canAssign)
{
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(bool canAssign)
{
  double value = strtod(parser.previous.start, NULL);
  emitConstant(NUMBER_VAL(value));
}

static void or_(bool canAssign)
{
  int elseJump = emitJump(OP_JUMP_IF_FALSE);
  int endJump = emitJump(OP_JUMP);

  patchJump(elseJump);
  emitByte(OP_POP);

  parsePrecedence(PREC_OR);
  patchJump(endJump);
}

static void string(bool canAssign)
{
  emitConstant(OBJ_VAL(copyString(parser.previous.start + 1,
                                  parser.previous.length - 2)));
}

static void list(bool canAssign)
{
  emitByte(OP_NEW_LIST);

  do
  {
    if (check(TOKEN_RIGHT_BRACKET))
      break;

    expression();
    emitByte(OP_ADD_LIST);
  } while (match(TOKEN_COMMA));

  consume(TOKEN_RIGHT_BRACKET, "Expected closing ']'");
}

static void dict(bool canAssign)
{
  emitByte(OP_NEW_DICT);

  do
  {
    if (check(TOKEN_RIGHT_BRACE))
      break;

    parsePrecedence(PREC_UNARY);
    consume(TOKEN_COLON, "Expected ':'");
    expression();
    emitByte(OP_ADD_DICT);
  } while (match(TOKEN_COMMA));

  consume(TOKEN_RIGHT_BRACE, "Expected closing '}'");
}

static void subscript(bool canAssign)
{
  expression();

  TokenType type;
  if (parser.previous.type == TOKEN_NUMBER)
    type = TOKEN_NUMBER;
  else
    type = TOKEN_STRING;

  consume(TOKEN_RIGHT_BRACKET, "Expected closing ']'");

  // Number means its a list subscript
  if (type == TOKEN_NUMBER)
  {
    if (match(TOKEN_EQUAL))
    {
      expression();
      emitByte(OP_SUBSCRIPT_ASSIGN);
    }
    else
      emitByte(OP_SUBSCRIPT);
  }
  else
  {
    // Dict subscript
    if (match(TOKEN_EQUAL))
    {
      expression();
      emitByte(OP_SUBSCRIPT_DICT_ASSIGN);
    }
    else
      emitByte(OP_SUBSCRIPT_DICT);
  }
}

static uint8_t setVariable(Token name, Value value)
{
  uint8_t getOp, setOp;
  int arg = resolveLocal(current, &name);
  if (arg != -1)
  {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  }
  else if ((arg = resolveUpvalue(current, &name)) != -1)
  {
    getOp = OP_GET_UPVALUE;
    setOp = OP_SET_UPVALUE;
  }
  else
  {
    arg = identifierConstant(&name);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }

  emitConstant(value);
  emitBytes(setOp, (uint8_t)arg);
  return (uint8_t)arg;
}

static uint8_t getVariable(Token name)
{
  uint8_t getOp, setOp;
  int arg = resolveLocal(current, &name);
  if (arg != -1)
  {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  }
  else if ((arg = resolveUpvalue(current, &name)) != -1)
  {
    getOp = OP_GET_UPVALUE;
    setOp = OP_SET_UPVALUE;
  }
  else
  {
    arg = identifierConstant(&name);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }

  emitBytes(getOp, (uint8_t)arg);
  return (uint8_t)arg;
}

static void namedVariable(Token name, bool canAssign)
{
  uint8_t getOp, setOp;
  int arg = resolveLocal(current, &name);
  if (arg != -1)
  {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  }
  else if ((arg = resolveUpvalue(current, &name)) != -1)
  {
    getOp = OP_GET_UPVALUE;
    setOp = OP_SET_UPVALUE;
  }
  else
  {
    arg = identifierConstant(&name);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }

  if (canAssign && match(TOKEN_PLUS_EQUALS))
  {
    namedVariable(name, false);
    expression();
    emitByte(OP_ADD);
    emitBytes(setOp, (uint8_t)arg);
  }
  else if (canAssign && match(TOKEN_MINUS_EQUALS))
  {
    namedVariable(name, false);
    expression();
    emitByte(OP_SUBTRACT);
    emitBytes(setOp, (uint8_t)arg);
  }
  else if (canAssign && match(TOKEN_MULTIPLY_EQUALS))
  {
    namedVariable(name, false);
    expression();
    emitByte(OP_MULTIPLY);
    emitBytes(setOp, (uint8_t)arg);
  }
  else if (canAssign && match(TOKEN_DIVIDE_EQUALS))
  {
    namedVariable(name, false);
    expression();
    emitByte(OP_DIVIDE);
    emitBytes(setOp, (uint8_t)arg);
  }
  else if (canAssign && match(TOKEN_EQUAL))
  {
    expression();
    emitBytes(setOp, (uint8_t)arg);
  }
  else
  {
    emitBytes(getOp, (uint8_t)arg);
  }
}

static void variable(bool canAssign)
{
  namedVariable(parser.previous, canAssign);
}

static Token syntheticToken(const char *text)
{
  Token token;
  token.start = text;
  token.length = (int)strlen(text);
  return token;
}

static void beginSyntheticCall(const char *name)
{
  Token tok = syntheticToken(name);
  getVariable(tok);
}

static void endSyntheticCall(uint8_t len)
{
  emitBytes(OP_CALL, len);
}

static void syntheticCall(const char *name, Value value)
{
  beginSyntheticCall(name);
  emitConstant(value);
  endSyntheticCall(1);
}

static void pushSuperclass()
{
  if (currentClass == NULL)
    return;
  namedVariable(syntheticToken("super"), false);
}

static void super_(bool canAssign)
{
  if (currentClass == NULL)
  {
    error("Cannot use 'super' outside of a class.");
  }
  else if (!currentClass->hasSuperclass)
  {
    error("Cannot use 'super' in a class with no superclass.");
  }

  consume(TOKEN_DOT, "Expect '.' after 'super'.");
  consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
  uint8_t name = identifierConstant(&parser.previous);

  namedVariable(syntheticToken("this"), false);

  if (match(TOKEN_LEFT_PAREN))
  {
    uint8_t argCount = argumentList();

    pushSuperclass();
    emitBytes(OP_SUPER, argCount);
    emitByte(name);
  }
  else
  {
    pushSuperclass();
    emitBytes(OP_GET_SUPER, name);
  }
}

static void this_(bool canAssign)
{
  if (currentClass == NULL)
  {
    error("Cannot use 'this' outside of a class.");
  }
  else
  {
    variable(false);
  }
}

static void unary(bool canAssign)
{
  TokenType operatorType = parser.previous.type;

  parsePrecedence(PREC_UNARY);
  switch (operatorType)
  {
  case TOKEN_BANG:
    emitByte(OP_NOT);
    break;
  case TOKEN_MINUS:
    emitByte(OP_NEGATE);
    break;
  default:
    return; // Unreachable.
  }
}

static void block()
{
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
  {
    declaration(true);
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(FunctionType type)
{
  Compiler compiler;
  initCompiler(&compiler, type);
  beginScope(); // [no-end-scope]

  // Compile the parameter list.
  consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
  if (!check(TOKEN_RIGHT_PAREN))
  {
    do
    {
      current->function->arity++;
      if (current->function->arity > 255)
      {
        errorAtCurrent("Cannot have more than 255 parameters.");
      }

      uint8_t paramConstant = parseVariable("Expect parameter name.");
      defineVariable(paramConstant);
    } while (match(TOKEN_COMMA));
  }

  consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");

  // The body.
  consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
  block();

  // Create the function object.
  ObjFunction *function = endCompiler();
  emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

  for (int i = 0; i < function->upvalueCount; i++)
  {
    emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
    emitByte(compiler.upvalues[i].index);
  }
}

static void lambda(bool canAssign)
{
  TokenType operatorType = parser.previous.type;
  markInitialized();
  function(TYPE_FUNCTION);
}

static void expand(bool canAssign)
{
  parsePrecedence(PREC_UNARY);

  if (match(TOKEN_COLON))
  {
    parsePrecedence(PREC_UNARY);
  }
  else
  {
    emitByte(OP_NONE);
  }

  emitByte(OP_EXPAND);
}

static void prefix(bool canAssign)
{
  TokenType operatorType = parser.previous.type;
  Token cur = parser.current;
  consume(TOKEN_IDENTIFIER, "Expected variable");
  namedVariable(parser.previous, true);

  int arg;
  bool instance = false;

  if (match(TOKEN_DOT))
  {
    consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    arg = identifierConstant(&parser.previous);
    emitBytes(OP_GET_PROPERTY_NO_POP, arg);
    instance = true;
  }

  switch (operatorType)
  {
  case TOKEN_INC:
    emitByte(OP_INC);
    break;
  case TOKEN_DEC:
    emitByte(OP_DEC);
    break;
  default:
    return;
  }

  if (instance)
    emitBytes(OP_SET_PROPERTY, arg);
  else
  {
    uint8_t setOp;
    //int arg = resolveLocal(current, &cur, false);
    int arg = resolveLocal(current, &cur);

    if (arg != -1)
      setOp = OP_SET_LOCAL;
    else if ((arg = resolveUpvalue(current, &cur)) != -1)
      setOp = OP_SET_UPVALUE;
    else
    {
      arg = identifierConstant(&cur);
      setOp = OP_SET_GLOBAL;
    }

    emitBytes(setOp, (uint8_t)arg);
  }
}

static void let(bool canAssign)
{
  beginScope();

  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'let'.");

  while (!check(TOKEN_RIGHT_PAREN) && !check(TOKEN_EOF))
  {
    declaration(false);
    if (check(TOKEN_COMMA))
      consume(TOKEN_COMMA, "Expect ',' between 'let' expressions.");
  }

  consume(TOKEN_RIGHT_PAREN, "Expect ')' after 'let' expressions.");

  consume(TOKEN_LEFT_BRACE, "Expect '{' before 'slet' body.");
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
  {
    declaration(true);
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after 'let' body.");
  endScope();
  emitByte(OP_NONE);
}

ParseRule rules[] = {
    {grouping, call, PREC_CALL},     // TOKEN_LEFT_PAREN
    {NULL, NULL, PREC_NONE},         // TOKEN_RIGHT_PAREN
    {dict, NULL, PREC_NONE},         // TOKEN_LEFT_BRACE [big]
    {NULL, NULL, PREC_NONE},         // TOKEN_RIGHT_BRACE
    {list, subscript, PREC_CALL},    // TOKEN_LEFT_BRACKET
    {NULL, NULL, PREC_NONE},         // TOKEN_RIGHT_BRACKET
    {NULL, NULL, PREC_NONE},         // TOKEN_COMMA
    {NULL, dot, PREC_CALL},          // TOKEN_DOT
    {unary, binary, PREC_TERM},      // TOKEN_MINUS
    {NULL, binary, PREC_TERM},       // TOKEN_PLUS
    {prefix, NULL, PREC_NONE},       // TOKEN_INC
    {prefix, NULL, PREC_NONE},       // TOKEN_DEC
    {NULL, NULL, PREC_NONE},         // TOKEN_PLUS_EQUALS
    {NULL, NULL, PREC_NONE},         // TOKEN_MINUS_EQUALS
    {NULL, NULL, PREC_NONE},         // TOKEN_MULTIPLY_EQUALS
    {NULL, NULL, PREC_NONE},         // TOKEN_DIVIDE_EQUALS
    {NULL, NULL, PREC_NONE},         // TOKEN_SEMICOLON
    {NULL, expand, PREC_FACTOR},     // TOKEN_COLON
    {NULL, binary, PREC_FACTOR},     // TOKEN_SLASH
    {NULL, binary, PREC_FACTOR},     // TOKEN_STAR
    {NULL, binary, PREC_FACTOR},     // TOKEN_PERCENT
    {NULL, binary, PREC_FACTOR},     // TOKEN_POW
    {unary, NULL, PREC_NONE},        // TOKEN_BANG
    {NULL, binary, PREC_EQUALITY},   // TOKEN_BANG_EQUAL
    {NULL, NULL, PREC_NONE},         // TOKEN_EQUAL
    {NULL, binary, PREC_EQUALITY},   // TOKEN_EQUAL_EQUAL
    {NULL, binary, PREC_COMPARISON}, // TOKEN_GREATER
    {NULL, binary, PREC_COMPARISON}, // TOKEN_GREATER_EQUAL
    {NULL, binary, PREC_COMPARISON}, // TOKEN_LESS
    {NULL, binary, PREC_COMPARISON}, // TOKEN_LESS_EQUAL
    {variable, NULL, PREC_NONE},     // TOKEN_IDENTIFIER
    {string, NULL, PREC_NONE},       // TOKEN_STRING
    {number, NULL, PREC_NONE},       // TOKEN_NUMBER
    {NULL, and_, PREC_AND},          // TOKEN_AND
    {NULL, NULL, PREC_NONE},         // TOKEN_CLASS
    {NULL, NULL, PREC_NONE},         // TOKEN_ELSE
    {literal, NULL, PREC_NONE},      // TOKEN_FALSE
    {NULL, NULL, PREC_NONE},         // TOKEN_FOR
    {NULL, NULL, PREC_NONE},         // TOKEN_FUNC
    {lambda, NULL, PREC_NONE},       // TOKEN_LAMBDA
    {NULL, NULL, PREC_NONE},         // TOKEN_IF
    {literal, NULL, PREC_NONE},      // TOKEN_NONE
    {NULL, or_, PREC_OR},            // TOKEN_OR
    {NULL, NULL, PREC_NONE},         // TOKEN_RETURN
    {super_, NULL, PREC_NONE},       // TOKEN_SUPER
    {this_, NULL, PREC_NONE},        // TOKEN_THIS
    {literal, NULL, PREC_NONE},      // TOKEN_TRUE
    {NULL, NULL, PREC_NONE},         // TOKEN_VAR
    {NULL, NULL, PREC_NONE},         // TOKEN_WHILE
    {NULL, NULL, PREC_NONE},         // TOKEN_IN
    {NULL, NULL, PREC_NONE},         // TOKEN_IMPORT
    {let, NULL, PREC_NONE},          // TOKEN_LET
    {NULL, NULL, PREC_NONE},         // TOKEN_WITH
    {NULL, NULL, PREC_NONE},         // TOKEN_ERROR
    {NULL, NULL, PREC_NONE},         // TOKEN_EOF
};

static void parsePrecedence(Precedence precedence)
{
  advance();
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == NULL)
  {
    error("Expect expression.");
    return;
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  prefixRule(canAssign);

  while (precedence <= getRule(parser.current.type)->precedence)
  {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule(canAssign);
  }

  if (canAssign && match(TOKEN_EQUAL))
  {
    error("Invalid assignment target.");
  }
}

static ParseRule *getRule(TokenType type)
{
  return &rules[type];
}

static void expression()
{
  parsePrecedence(PREC_ASSIGNMENT);
}

static void method()
{
  consume(TOKEN_FUNC, "Expect a function declaration.");
  consume(TOKEN_IDENTIFIER, "Expect method name.");
  uint8_t constant = identifierConstant(&parser.previous);

  // If the method is named "init", it's an initializer.
  FunctionType type = TYPE_METHOD;
  if (parser.previous.length == 4 &&
      memcmp(parser.previous.start, "init", 4) == 0)
  {
    type = TYPE_INITIALIZER;
  }

  function(type);

  emitBytes(OP_METHOD, constant);
}

static void property()
{
  uint8_t name = parseVariable("Expect variable name.");

  if (match(TOKEN_EQUAL))
  {
    expression();
  }
  else
  {
    emitByte(OP_NONE);
  }
  consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

  emitBytes(OP_PROPERTY, name);
}

static void methodOrProperty()
{
  /*if (check(TOKEN_STATIC))
    {
        method();   
    } 
    else*/
  if (check(TOKEN_FUNC))
  {
    method();
  }
  else if (check(TOKEN_VAR))
  {
    consume(TOKEN_VAR, "Expected a variable declaration.");
    property();
  }
  else
  {
    errorAtCurrent("Only variables and functions allowed inside a class.");
  }
}

static void classDeclaration()
{
  consume(TOKEN_IDENTIFIER, "Expect class name.");
  Token className = parser.previous;
  uint8_t nameConstant = identifierConstant(&parser.previous);
  declareVariable();

  emitBytes(OP_CLASS, nameConstant);
  defineVariable(nameConstant);

  ClassCompiler classCompiler;
  classCompiler.name = parser.previous;
  classCompiler.hasSuperclass = false;
  classCompiler.enclosing = currentClass;
  currentClass = &classCompiler;

  if (match(TOKEN_COLON))
  {
    consume(TOKEN_IDENTIFIER, "Expect superclass name.");

    if (identifiersEqual(&className, &parser.previous))
    {
      error("A class cannot inherit from itself.");
    }

    classCompiler.hasSuperclass = true;

    beginScope();

    // Store the superclass in a local variable named "super".
    variable(false);
    addLocal(syntheticToken("super"));
    defineVariable(0);

    namedVariable(className, false);
    emitByte(OP_INHERIT);
  }

  consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
  {
    namedVariable(className, false);
    methodOrProperty();
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");

  if (classCompiler.hasSuperclass)
  {
    endScope();
  }

  currentClass = currentClass->enclosing;
}

static void funDeclaration()
{
  uint8_t global = parseVariable("Expect function name.");
  markInitialized();
  function(TYPE_FUNCTION);
  defineVariable(global);
}

static void varDeclaration(bool checkEnd)
{
  uint8_t global = parseVariable("Expect variable name.");

  if (match(TOKEN_EQUAL))
  {
    expression();
  }
  else
  {
    emitByte(OP_NONE);
  }
  if (checkEnd)
    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

  defineVariable(global);
}

static void expressionStatement()
{
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  emitByte(OP_POP);
}

static void forStatement()
{
  beginScope();

  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

  bool in = false;
  uint8_t name;
  uint8_t it;
  Token loopVar;

  if (match(TOKEN_SEMICOLON))
  {
    // No initializer.
  }
  else if (match(TOKEN_VAR))
  {
    /*
    name = parseVariable("Expect variable name.");
    if (match(TOKEN_EQUAL))
    {
      expression();
    }
    else
    {
      if (match(TOKEN_IN))
      {
        expression();
        emitConstant(NUMBER_VAL(0));
        emitByte(OP_SUBSCRIPT);

        in = true;
      }
      else
        emitByte(OP_NONE);
    }

    if (!in)
      consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    defineVariable(name);
    if (in)
    {
      loopVar = syntheticToken("loop_variable_i");
      declareNamedVariable(&loopVar);
      if (current->scopeDepth > 0)
        it = 0;
      else
        it = identifierConstant(&loopVar);
      emitConstant(NUMBER_VAL(0));
      defineVariable(it);
    }
    */
    varDeclaration(true);
  }
  else
  {
    expressionStatement();
  }

  int loopStart = currentChunk()->count;

  int exitJump = -1;
  if (!in && !match(TOKEN_SEMICOLON))
  {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

    exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP); // Condition.
  }
  else if (in)
  {
    getVariable(loopVar);
    emitConstant(NUMBER_VAL(2));
    emitByte(OP_LESS);
    exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP); // Condition.
  }

  if (!in && !match(TOKEN_RIGHT_PAREN))
  {
    int bodyJump = emitJump(OP_JUMP);

    int incrementStart = currentChunk()->count;
    expression();
    emitByte(OP_POP);
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

    emitLoop(loopStart);
    loopStart = incrementStart;
    patchJump(bodyJump);
  }
  else if (in)
  {
    int bodyJump = emitJump(OP_JUMP);
    int incrementStart = currentChunk()->count;

    getVariable(loopVar);
    emitConstant(NUMBER_VAL(1));
    emitByte(OP_ADD);

    emitByte(OP_POP);
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

    emitLoop(loopStart);
    loopStart = incrementStart;
    patchJump(bodyJump);
  }

  statement();

  emitLoop(loopStart);

  if (exitJump != -1)
  {
    patchJump(exitJump);
    emitByte(OP_POP); // Condition.
  }

  endScope();
}

static void ifStatement()
{
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition."); // [paren]

  int thenJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();

  int elseJump = emitJump(OP_JUMP);

  patchJump(thenJump);
  emitByte(OP_POP);

  if (match(TOKEN_ELSE))
    statement();
  patchJump(elseJump);
}

static void withStatement()
{
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'with'.");
  expression();
  consume(TOKEN_COMMA, "Expect comma");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after 'with'.");

  beginScope();

  Token file = syntheticToken("file");
  Local *local = &current->locals[current->localCount++];
  local->depth = current->scopeDepth;
  local->isCaptured = false;
  local->name = file;

  emitByte(OP_FILE);

  statement();


  getVariable(file);
  emitBytes(OP_INVOKE, 0);

  Token fn = syntheticToken("close");
  uint8_t name = identifierConstant(&fn);
  emitByte(name);

  endScope();
}

static void returnStatement()
{
  if (current->type == TYPE_SCRIPT)
  {
    error("Cannot return from top-level code.");
  }

  if (match(TOKEN_SEMICOLON))
  {
    emitReturn();
  }
  else
  {
    if (current->type == TYPE_INITIALIZER)
    {
      error("Cannot return a value from an initializer.");
    }

    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
    emitByte(OP_RETURN);
  }
}

static void importStatement()
{
  if (match(TOKEN_IDENTIFIER))
  {
    int len = parser.previous.length + strlen(vm.extension) + 1;
    char *str = malloc(sizeof(char) * len);
    strncpy(str, parser.previous.start, parser.previous.length);
    strcat(str, vm.extension);
    emitConstant(OBJ_VAL(copyString(str, strlen(str))));
    free(str);
  }
  else
  {
    consume(TOKEN_STRING, "Expect string after import.");

    emitConstant(OBJ_VAL(
        copyString(parser.previous.start + 1, parser.previous.length - 2)));
  }
  consume(TOKEN_SEMICOLON, "Expect ';' after import.");

  emitByte(OP_IMPORT);
}

static void whileStatement()
{
  int loopStart = currentChunk()->count;

  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  int exitJump = emitJump(OP_JUMP_IF_FALSE);

  emitByte(OP_POP);
  statement();

  emitLoop(loopStart);

  patchJump(exitJump);
  emitByte(OP_POP);
}

static void synchronize()
{
  parser.panicMode = false;

  while (parser.current.type != TOKEN_EOF)
  {
    if (parser.previous.type == TOKEN_SEMICOLON)
      return;

    switch (parser.current.type)
    {
    case TOKEN_CLASS:
    case TOKEN_FUNC:
    case TOKEN_VAR:
    case TOKEN_FOR:
    case TOKEN_IF:
    case TOKEN_WHILE:
    case TOKEN_RETURN:
    case TOKEN_IMPORT:
    case TOKEN_WITH:
      return;

    default:
        // Do nothing.
        ;
    }

    advance();
  }
}

static void declaration(bool checkEnd)
{
  if (match(TOKEN_CLASS))
  {
    classDeclaration();
  }
  else if (match(TOKEN_FUNC))
  {
    funDeclaration();
  }
  else if (match(TOKEN_VAR))
  {
    varDeclaration(checkEnd);
  }
  else
  {
    statement();
  }

  if (parser.panicMode)
    synchronize();
}

static void statement()
{
  if (match(TOKEN_FOR))
  {
    forStatement();
  }
  else if (match(TOKEN_IF))
  {
    ifStatement();
  }
  else if (match(TOKEN_RETURN))
  {
    returnStatement();
  }
  else if (match(TOKEN_WITH))
  {
    withStatement();
  }
  else if (match(TOKEN_IMPORT))
  {
    importStatement();
  }
  else if (match(TOKEN_WHILE))
  {
    whileStatement();
  }
  else if (match(TOKEN_LEFT_BRACE))
  {
    beginScope();
    block();
    endScope();
  }
  else
  {
    expressionStatement();
  }
}

ObjFunction *compile(const char *source)
{
  initScanner(source);
  Compiler compiler;

  initCompiler(&compiler, TYPE_SCRIPT);

  parser.hadError = false;
  parser.panicMode = false;

  advance();

  while (!match(TOKEN_EOF))
  {
    declaration(true);
  }

  ObjFunction *function = endCompiler();
  return parser.hadError ? NULL : function;
}

void grayCompilerRoots()
{
  Compiler *compiler = current;
  while (compiler != NULL)
  {
    grayObject((Obj *)compiler->function);
    compiler = compiler->enclosing;
  }
}