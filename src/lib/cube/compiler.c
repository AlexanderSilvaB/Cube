#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
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
    PREC_ASSIGNMENT,  // =
    PREC_OR,          // or
    PREC_AND,         // and
    PREC_EQUALITY,    // == !=    
    PREC_COMPARISON,  // < > <= >=
    PREC_TERM,        // + -
    PREC_FACTOR,      // * /      
    PREC_UNARY,       // ! -
    PREC_PREFIX,      // ++ --
    PREC_CALL,        // . () []
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
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
	TYPE_STATIC,
    TYPE_SCRIPT
} FunctionType;

typedef struct Compiler
{
    struct Compiler* enclosing;
    ObjFunction* function;
    FunctionType type;
    Local locals[UINT8_COUNT];
    int localCount;
    Upvalue upvalues[UINT8_COUNT];
    int scopeDepth;
    int loopDepth;
} Compiler;

typedef struct ClassCompiler 
{
	struct ClassCompiler *enclosing;
	Token name;
	bool hasSuperclass;
} ClassCompiler;

Parser parser;

Compiler* current = NULL;
ClassCompiler *currentClass = NULL;
bool staticMethod = false;
Chunk* compilingChunk;

// Used for "continue" statements
int innermostLoopStart = -1;
int innermostLoopScopeDepth = 0;

static Chunk* currentChunk()
{
    return &current->function->chunk;
}

static void errorAt(Token* token, const char* message)
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

static void error(const char* message)
{
    errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message)
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

static void consume(TokenType type, const char* message)
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

static void emitBytes3(uint8_t byte1, uint8_t byte2, uint8_t byte3)
{
    emitByte(byte1);
    emitByte(byte2);
    emitByte(byte3);
}

static void emitLoop(int loopStart)
{
    emitByte(OP_LOOP);

    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");

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
    emitByte(OP_NONE);
    emitByte(OP_RETURN);
}

static int makeConstant(Value value)
{
    int constant = addConstant(currentChunk(), value);
    /*
    if (constant > UINT8_MAX)
    {
        error("Too many constants in one chunk.");
        return 0;
    }

    return (uint8_t)constant;
    */
   return constant;
}

static void emitConstant(Value value)
{
    int constant = makeConstant(value);
    if(constant <= UINT8_MAX)
        emitBytes(OP_CONSTANT, constant);
    else if(constant < 0xFFFFFF)
        emitBytes(OP_CONSTANT_LONG, constant);
    else
    {
        error("Too many constants in one chunk.");
        emitBytes(OP_CONSTANT, 0);
    }
}

static void patchJump(int offset)
{
    // -2 to adjust for the bytecode for the jump offset itself.
    int jump = currentChunk()->count - offset - 2;

    if (jump > UINT16_MAX)
    {
        error("Too much code to jump over.");
    }

    currentChunk()->code[offset] = (jump >> 8) & 0xff;          
    currentChunk()->code[offset + 1] = jump & 0xff;
}

static void initCompiler(Compiler* compiler, int scopeDepth, FunctionType type)
{
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = scopeDepth;
    compiler->loopDepth = 0;
    compiler->function = newFunction(type == TYPE_STATIC);
    current = compiler;

    switch (type) 
    {
		case TYPE_INITIALIZER:
		case TYPE_METHOD:
		case TYPE_STATIC:
		case TYPE_FUNCTION:
			current->function->name =
				copyString(parser.previous.start, parser.previous.length);
			break;
		case TYPE_SCRIPT:
			current->function->name = NULL;
			break;
	}

    Local* local = &current->locals[current->localCount++];
    local->depth = current->scopeDepth;
    local->isCaptured = false;
    if (type != TYPE_FUNCTION && type != TYPE_STATIC) 
    {
		// In a method, it holds the receiver, "this".
		local->name.start = "this";
		local->name.length = 4;
	} 
    else 
    {
		// In a function, it holds the function, but cannot be referenced,
		// so has no name.
		local->name.start = "";
		local->name.length = 0;
	}
}

static ObjFunction* endCompiler()
{
    emitReturn();
    ObjFunction* function = current->function;

    #ifdef DEBUG_PRINT_CODE
    if (!parser.hadError)
    {
        disassembleChunk(currentChunk(), function->name != NULL ? function->name->chars : "<script>");
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
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static uint8_t identifierConstant(Token* name)
{
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token* a, Token* b)
{
    if (a->length != b->length)
        return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name, bool inFunction)
{
    for (int i = compiler->localCount - 1; i >= 0; i--)
    {
        Local* local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name))
        {
            if (!inFunction && local->depth == -1)
            {
                error("Cannot read local variable in its own initializer.");
            }
            return i;
        }
    }

    return -1;
}

static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal)
{
    int upvalueCount = compiler->function->upvalueCount;

    for (int i = 0; i < upvalueCount; i++)
    {
        Upvalue* upvalue = &compiler->upvalues[i];
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

static int resolveUpvalue(Compiler* compiler, Token* name)
{
    if (compiler->enclosing == NULL)
        return -1;

    int local = resolveLocal(compiler->enclosing, name, true);
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

    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
    local->isCaptured = false;
}

static void declareVariable()
{
    // Global variables are implicitly declared.
    if (current->scopeDepth == 0)
        return;

    Token* name = &parser.previous;

    for (int i = current->localCount - 1; i >= 0; i--)
    {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth)
        {
            break;
        }

        if (identifiersEqual(name, &local->name))
        {
            error("Variable with this name already declared in this scope.");
        }
    }

    addLocal(*name);
}

static uint8_t parseVariable(const char* errorMessage)
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
    current->locals[current->localCount - 1].depth = current->scopeDepth;
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
            argCount++;

            if (argCount == 255)
            {
                error("Cannot have more than 255 arguments.");
            }


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
    // Remember the operator.
    TokenType operatorType = parser.previous.type;

    // Compile the right operand.
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    // Emit the operator instruction.
    switch (operatorType)
    {
        case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT); break;
        case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
        case TOKEN_GREATER:       emitByte(OP_GREATER); break;         
        case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;   
        case TOKEN_LESS:          emitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL:    emitBytes(OP_GREATER, OP_NOT); break;
        case TOKEN_PLUS:          emitByte(OP_ADD); break;
        case TOKEN_MINUS:         emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
        case TOKEN_PERCENT:       emitByte(OP_MOD); break;
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
		//emitBytes(OP_INVOKE_0 + argCount, name);
        emitBytes3(OP_INVOKE, argCount, name);
	} 
    else
		emitBytes(OP_GET_PROPERTY, name);
}

static void literal(bool canAssign)
{
    switch (parser.previous.type)
    {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NONE: emitByte(OP_NONE); break;
        case TOKEN_TRUE: emitByte(OP_TRUE); break;
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

		expression();
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

static void namedVariable(Token name, bool canAssign)
{
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &name, false);
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
		return;
	} 
    else 
    {
		emitBytes(getOp, (uint8_t)arg);
		return;
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

static void pushSuperclass() 
{
	if (currentClass == NULL)
		return;
	namedVariable(syntheticToken("super"), false);
}

static void super_(bool canAssign) 
{
	if (currentClass == NULL)
		error("Cannot use 'super' outside of a class.");
	else if (!currentClass->hasSuperclass)
		error("Cannot use 'super' in a class with no superclass.");

	consume(TOKEN_DOT, "Expect '.' after 'super'.");
	consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
	uint8_t name = identifierConstant(&parser.previous);

	// Push the receiver.
	namedVariable(syntheticToken("this"), false);

	if (match(TOKEN_LEFT_PAREN)) 
    {
		uint8_t argCount = argumentList();

		pushSuperclass();
		//emitBytes(OP_SUPER_0 + argCount, name);
        emitBytes3(OP_SUPER, argCount, name);
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
		error("Cannot use 'this' outside of a class.");
	else if (staticMethod)
		error("Cannot use 'this' inside a static method.");
	else
		variable(false);
}

static void static_(bool canAssign) {
	if (currentClass == NULL)
		error("Cannot use 'static' outside of a class.");
}

static void unary(bool canAssign)
{
    TokenType operatorType = parser.previous.type;

    // Compile the operand.
    parsePrecedence(PREC_UNARY);

    // Emit the operator instruction.
    switch (operatorType)
    {
        case TOKEN_BANG: emitByte(OP_NOT); break;
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        default:
            return; // Unreachable.
    }
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
        int arg = resolveLocal(current, &cur, false);

		if (arg != -1)
			setOp = OP_SET_LOCAL;
		else if ((arg = resolveUpvalue(current, &cur)) != -1)
			setOp = OP_SET_UPVALUE;
		else {
			arg = identifierConstant(&cur);
			setOp = OP_SET_GLOBAL;
		}

		emitBytes(setOp, (uint8_t)arg);
	}
}

ParseRule rules[] =
{
    { grouping, call,    PREC_CALL },       // TOKEN_LEFT_PAREN
    { NULL,     NULL,    PREC_NONE },       // TOKEN_RIGHT_PAREN
    { dict,     NULL,    PREC_NONE },	    // TOKEN_LEFT_BRACE [big]
    { NULL,     NULL,    PREC_NONE },       // TOKEN_RIGHT_BRACE
    { list,     subscript,PREC_CALL},	    // TOKEN_LEFT_BRACKET
	{ NULL,     NULL,    PREC_NONE},		// TOKEN_RIGHT_BRACKET
    { NULL,     NULL,    PREC_NONE },       // TOKEN_COMMA
    { NULL,     dot,     PREC_CALL },       // TOKEN_DOT
    { unary,    binary,  PREC_TERM },       // TOKEN_MINUS
    { NULL,     binary,  PREC_TERM },       // TOKEN_PLUS
    { prefix,   NULL,    PREC_NONE },       // TOKEN_INC
    { prefix,   NULL,    PREC_NONE },       // TOKEN_DEC
    { NULL,     NULL,    PREC_NONE },		// TOKEN_PLUS_EQUALS
	{ NULL,     NULL,    PREC_NONE },	    // TOKEN_MINUS_EQUALS
	{ NULL,     NULL,    PREC_NONE },	    // TOKEN_MULTIPLY_EQUALS
	{ NULL,     NULL,    PREC_NONE },	    // TOKEN_DIVIDE_EQUALS
    { NULL,     NULL,    PREC_NONE },       // TOKEN_SEMICOLON
    { NULL,     NULL,    PREC_NONE },		// TOKEN_COLON
    { NULL,     binary,  PREC_FACTOR },     // TOKEN_SLASH           
    { NULL,     binary,  PREC_FACTOR },     // TOKEN_STAR
    { NULL,     binary,  PREC_FACTOR},	   // TOKEN_PERCENT
    { unary,    NULL,    PREC_NONE },       // TOKEN_BANG
    { NULL,     binary,  PREC_EQUALITY },   // TOKEN_BANG_EQUAL
    { NULL,     NULL,    PREC_NONE },       // TOKEN_EQUAL
    { NULL,     binary,  PREC_EQUALITY },   // TOKEN_EQUAL_EQUAL  
    { NULL,     binary,  PREC_COMPARISON }, // TOKEN_GREATER
    { NULL,     binary,  PREC_COMPARISON }, // TOKEN_GREATER_EQUAL
    { NULL,     binary,  PREC_COMPARISON }, // TOKEN_LESS
    { NULL,     binary,  PREC_COMPARISON }, // TOKEN_LESS_EQUAL
    { variable, NULL,    PREC_NONE },       // TOKEN_IDENTIFIER
    { string,   NULL,    PREC_NONE },       // TOKEN_STRING
    { number,   NULL,    PREC_NONE },       // TOKEN_NUMBER
    { NULL,     and_,    PREC_AND  },       // TOKEN_AND
    { NULL,     NULL,    PREC_NONE },       // TOKEN_CLASS           
    { static_,  NULL,    PREC_NONE },       // TOKEN_STATIC
    { NULL,     NULL,    PREC_NONE },       // TOKEN_ELSE            
    { literal,  NULL,    PREC_NONE },       // TOKEN_FALSE
    { NULL,     NULL,    PREC_NONE },       // TOKEN_FOR
    { NULL,     NULL,    PREC_NONE },       // TOKEN_FUNC
    { NULL,     NULL,    PREC_NONE },       // TOKEN_IF
    { literal,  NULL,    PREC_NONE },       // TOKEN_NONE
    { NULL,     or_,     PREC_OR   },       // TOKEN_OR
    { NULL,     NULL,    PREC_NONE },       // TOKEN_PRINT           
    { NULL,     NULL,    PREC_NONE },       // TOKEN_RETURN          
    { super_,   NULL,    PREC_NONE },       // TOKEN_SUPER
    { this_,    NULL,    PREC_NONE },       // TOKEN_THIS            
    { literal,  NULL,    PREC_NONE },       // TOKEN_TRUE
    { NULL,     NULL,    PREC_NONE },       // TOKEN_VAR             
    { NULL,     NULL,    PREC_NONE },       // TOKEN_WHILE
    { NULL,     NULL,    PREC_NONE },		// TOKEN_BREAK
    { NULL,     NULL,    PREC_NONE },	    // TOKEN_CONTINUE
    { NULL,     NULL,    PREC_NONE },       // TOKEN_ERROR
    { NULL,     NULL,    PREC_NONE },       // TOKEN_EOF
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
        expression();
    }
}

static ParseRule* getRule(TokenType type)
{
    return &rules[type];
}

void expression()
{
    parsePrecedence(PREC_ASSIGNMENT);
}

static void block()
{
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
    {
        declaration();
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(FunctionType type)
{
    Compiler compiler;
    initCompiler(&compiler, 1, type);
    beginScope();

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
    ObjFunction* function = endCompiler();
    emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

    for (int i = 0; i < function->upvalueCount; i++)
    {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}

static void method() 
{
	FunctionType type;

	if (check(TOKEN_STATIC)) 
    {
		type = TYPE_STATIC;
		consume(TOKEN_STATIC, "Expect static.");
		staticMethod = true;
	} 
    else
		type = TYPE_METHOD;

	consume(TOKEN_IDENTIFIER, "Expect method name.");
	uint8_t constant = identifierConstant(&parser.previous);

	// If the method is named "init", it's an initializer.
	if (parser.previous.length == 4 &&
		memcmp(parser.previous.start, "init", 4) == 0) 
    {
		type = TYPE_INITIALIZER;
	}

	function(type);

	emitBytes(OP_METHOD, constant);
}

static void classDeclaration() 
{
	consume(TOKEN_IDENTIFIER, "Expect class name.");
	uint8_t nameConstant = identifierConstant(&parser.previous);
	declareVariable();

	ClassCompiler classCompiler;
	classCompiler.name = parser.previous;
	classCompiler.hasSuperclass = false;
	classCompiler.enclosing = currentClass;
	currentClass = &classCompiler;

	if (match(TOKEN_LESS)) 
    {
		consume(TOKEN_IDENTIFIER, "Expect superclass name.");
		classCompiler.hasSuperclass = true;

		beginScope();

		// Store the superclass in a local variable named "super".
		variable(false);
		addLocal(syntheticToken("super"));

		emitBytes(OP_SUBCLASS, nameConstant);
	} 
    else
		emitBytes(OP_CLASS, nameConstant);

	consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
		method();

	consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
	if (classCompiler.hasSuperclass)
		endScope();

	defineVariable(nameConstant);

	currentClass = currentClass->enclosing;
}

static void funDeclaration()
{
    uint8_t global = parseVariable("Expect function name.");
    markInitialized();
    function(TYPE_FUNCTION);                                
    defineVariable(global);
}

static void varDeclaration()
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
    current->loopDepth++;

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    if (match(TOKEN_SEMICOLON))
    {
        // No initializer.
    }
    else if (match(TOKEN_VAR))
    {
        varDeclaration();
    }
    else
    {
        expressionStatement();
    }

    int surroundingLoopStart = innermostLoopStart;
	int surroundingLoopScopeDepth = innermostLoopScopeDepth;
	innermostLoopStart = currentChunk()->count;
	innermostLoopScopeDepth = current->scopeDepth;

    int exitJump = -1;
    if (!match(TOKEN_SEMICOLON))
    {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

        // Jump out of the loop if the condition is false.
        exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP); // Condition.
    }

    if (!match(TOKEN_RIGHT_PAREN))
    {
        int bodyJump = emitJump(OP_JUMP);

        int incrementStart = currentChunk()->count;
        expression();
        emitByte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

        emitLoop(innermostLoopStart);
        innermostLoopStart = incrementStart;
        patchJump(bodyJump);
    }

    statement();

    emitLoop(innermostLoopStart);

    if (exitJump != -1)
    {
        patchJump(exitJump);
        emitByte(OP_POP); // Condition.
    }

    innermostLoopStart = surroundingLoopStart;
	innermostLoopScopeDepth = surroundingLoopScopeDepth;

    endScope();
    current->loopDepth--;
}

static void continueStatement()
{
	if (innermostLoopStart == -1)
        error("Cannot use 'continue' outside of a loop.");

	consume(TOKEN_SEMICOLON, "Expect ';' after 'continue'.");

	// Discard any locals created inside the loop.
	for (int i = current->localCount - 1;
		 i >= 0 && current->locals[i].depth > innermostLoopScopeDepth; i--)
		emitByte(OP_POP);

	// Jump to top of current innermost loop.
	emitLoop(innermostLoopStart);
}

static void ifStatement()
{
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();                                                        
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

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
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        emitByte(OP_RETURN);
    }
}

static void breakStatement()
{
	if (current->loopDepth == 0)
    {
		error("Cannot use 'break' outside of a loop.");
		return;
	}

	consume(TOKEN_SEMICOLON, "Expected semicolon after break");
	emitByte(OP_BREAK);
}

static void whileStatement()
{
    current->loopDepth++;

    int surroundingLoopStart = innermostLoopStart;
	int surroundingLoopScopeDepth = innermostLoopScopeDepth;
	innermostLoopStart = currentChunk()->count;
	innermostLoopScopeDepth = current->scopeDepth;

    if (check(TOKEN_LEFT_BRACE))
		emitByte(OP_TRUE);
	else
    {
		consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
		expression();
		consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
	}

    int exitJump = emitJump(OP_JUMP_IF_FALSE);

    emitByte(OP_POP);
    statement();

    emitLoop(innermostLoopStart);

    patchJump(exitJump);
    emitByte(OP_POP);

    innermostLoopStart = surroundingLoopStart;
	innermostLoopScopeDepth = surroundingLoopScopeDepth;

	current->loopDepth--;
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
            case TOKEN_STATIC:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_BREAK:
            case TOKEN_PRINT:                                 
            case TOKEN_RETURN:                                
                return;

            default:
                // Do nothing.
                break;
        }

        advance();
    }
}

static void declaration()
{
    if (match(TOKEN_CLASS))
		classDeclaration();
    else if (match(TOKEN_FUNC))
        funDeclaration();
    else if (match(TOKEN_VAR))
        varDeclaration();
    else
        statement();

    if (parser.panicMode)
        synchronize();
}

static void statement()
{
    if (match(TOKEN_FOR))
        forStatement();
    else if (match(TOKEN_IF))
        ifStatement();
    else if (match(TOKEN_RETURN))
        returnStatement();
    else if (match(TOKEN_BREAK))
		breakStatement();
    else if (match(TOKEN_WHILE))
        whileStatement();
    else if (match(TOKEN_LEFT_BRACE))
    {
		Token previous = parser.previous;
		Token current = parser.current;
		if (check(TOKEN_STRING))
        {
			for (int i = 0;
				 i < parser.current.length - parser.previous.length + 1; ++i)
				backTrack();

			parser.current = previous;
			expressionStatement();
			return;
		}
        else if (check(TOKEN_RIGHT_BRACE))
        {
			advance();
			if (check(TOKEN_SEMICOLON))
            {
				backTrack();
				backTrack();
				parser.current = previous;
				expressionStatement();
				return;
			}
		}
		parser.current = current;

		beginScope();
		block();
		endScope();
	}
    else if (match(TOKEN_CONTINUE))
		continueStatement();
    else
        expressionStatement();
}

ObjFunction* compile(const char* source)
{
    initScanner(source);

    Compiler compiler;
    initCompiler(&compiler, 0, TYPE_SCRIPT);

    parser.hadError = false;
    parser.panicMode = false;

    advance();

    while (!match(TOKEN_EOF))
    {
        declaration();
    }

    ObjFunction* function = endCompiler();
    return parser.hadError ? NULL : function;
}

typedef struct
{
    char ident[8];
    uint32_t hash;
    int count;
    int constants;
}ByteCode;

ObjFunction* readByteCode(const char* path, uint32_t hash)
{
    ObjFunction* func = newFunction(TYPE_SCRIPT);
    return func;
}

void writeByteCode(const char* path, ObjFunction* func, uint32_t hash)
{
    FILE* file = fopen(path, "wb");
    if(file == NULL)
    {
        return;
    }

    ByteCode bc;
    memcpy(bc.ident, "CUBEPROG", 8);
    bc.hash = hash;
    bc.count = func->chunk.count;
    bc.constants = func->chunk.constants.count;

    fwrite(&bc, sizeof(ByteCode), 1, file);
    fwrite(func->chunk.code, sizeof(char), bc.count, file);
    fwrite(func->chunk.lines, sizeof(int), bc.count, file);

    for(int i = 0; i < func->chunk.constants.count; i++)
    {
        fwrite(&func->chunk.constants.values[i].type, sizeof(ValueType), 1, file);
        if(IS_NONE(func->chunk.constants.values[i]))
            continue;
        else if(IS_OBJ(func->chunk.constants.values[i]))
        {
            encodeObject(func->chunk.constants.values[i], file);
        }
        else
        {
            fwrite(&func->chunk.constants.values[i].as, sizeof(func->chunk.constants.values[i].as), 1, file);
        }
        
    }

    fclose(file);
}