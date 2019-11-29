#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

typedef struct
{
  const char *start;
  const char *current;
  int line;
} Scanner;

Scanner scanner;
void initScanner(const char *source)
{
  scanner.start = source;
  scanner.current = source;
  scanner.line = 1;
}

static bool isAlpha(char c)
{
  return (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
         c == '_';
}

static bool isDigit(char c)
{
  return c >= '0' && c <= '9';
}

static bool isAtEnd()
{
  return *scanner.current == '\0';
}

static char advance()
{
  scanner.current++;
  return scanner.current[-1];
}

static char peek()
{
  return *scanner.current;
}

static char peekNext()
{
  if (isAtEnd())
    return '\0';
  return scanner.current[1];
}

static char peekNextNext()
{
  if (isAtEnd())
    return '\0';
  return scanner.current[2];
}

static bool match(char expected)
{
  if (isAtEnd())
    return false;
  if (*scanner.current != expected)
    return false;

  scanner.current++;
  return true;
}

static Token makeToken(TokenType type)
{
  Token token;
  token.type = type;
  token.start = scanner.start;
  token.length = (int)(scanner.current - scanner.start);
  token.line = scanner.line;

  return token;
}

static Token errorToken(const char *message)
{
  Token token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (int)strlen(message);
  token.line = scanner.line;

  return token;
}

static void skipWhitespace()
{
  for (;;)
  {
    char c = peek();
    switch (c)
    {
    case ' ':
    case '\r':
    case '\t':
      advance();
      break;

    case '\n':
      scanner.line++;
      advance();
      break;

    case '/':
      if (peekNext() == '/')
      {
        // A comment goes until the end of the line.
        while (peek() != '\n' && !isAtEnd())
          advance();
      }
      else if (peekNext() == '*')
      {
        // A block comment goes until */
        advance();
        advance();

        bool done = peek() == '*' && peekNext() == '/';
        while (!done && !isAtEnd())
        {
          advance();
          done = peek() == '*' && peekNext() == '/';
        }
        if (!isAtEnd())
        {
          advance();
          advance();
        }
      }
      else
      {
        return;
      }
      break;

    default:
      return;
    }
  }
}

static TokenType checkKeyword(int start, int length,
                              const char *rest, TokenType type)
{
  if (scanner.current - scanner.start == start + length &&
      memcmp(scanner.start + start, rest, length) == 0)
  {
    return type;
  }

  return TOKEN_IDENTIFIER;
}

static TokenType identifierType()
{
  switch (scanner.start[0])
  {
  case 'a':
    return checkKeyword(1, 2, "nd", TOKEN_AND);
  case 'c':
    return checkKeyword(1, 4, "lass", TOKEN_CLASS);
  case 'e':
    return checkKeyword(1, 3, "lse", TOKEN_ELSE);
  case 'f':
    if (scanner.current - scanner.start > 1)
    {
      switch (scanner.start[1])
      {
      case 'a':
        return checkKeyword(2, 3, "lse", TOKEN_FALSE);
      case 'o':
        return checkKeyword(2, 1, "r", TOKEN_FOR);
      case 'u':
        return checkKeyword(2, 2, "nc", TOKEN_FUNC);
      }
    }
    break;
  case 'i':
    if (scanner.current - scanner.start > 1)
      switch (scanner.start[1])
      {
      case 'f':
        return checkKeyword(2, 0, "", TOKEN_IF);
      case 'n':
        return checkKeyword(2, 0, "", TOKEN_IN);
      case 'm':
        return checkKeyword(2, 4, "port", TOKEN_IMPORT);
      }
    break;
  case 'l':
    return checkKeyword(1, 2, "et", TOKEN_LET);
  case 'n':
    return checkKeyword(1, 3, "one", TOKEN_NONE);
  case 'o':
    return checkKeyword(1, 1, "r", TOKEN_OR);
    break;
  case 'r':
    return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
  case 's':
    return checkKeyword(1, 4, "uper", TOKEN_SUPER);
  case 't':
    if (scanner.current - scanner.start > 1)
    {
      switch (scanner.start[1])
      {
      case 'h':
        return checkKeyword(2, 2, "is", TOKEN_THIS);
      case 'r':
        return checkKeyword(2, 2, "ue", TOKEN_TRUE);
      }
    }
    break;
  case 'v':
    return checkKeyword(1, 2, "ar", TOKEN_VAR);
  case 'w':
    if (scanner.current - scanner.start > 1)
      switch (scanner.start[1])
      {
      case 'h':
        return checkKeyword(2, 3, "ile", TOKEN_WHILE);
      case 'i':
        return checkKeyword(2, 2, "th", TOKEN_WITH);
      }
    break;
  }

  return TOKEN_IDENTIFIER;
}

static Token identifier()
{
  while (isAlpha(peek()) || isDigit(peek()))
    advance();

  return makeToken(identifierType());
}

static Token number()
{
  while (isDigit(peek()))
    advance();

  // Look for a fractional part.
  if (peek() == '.' && isDigit(peekNext()))
  {
    // Consume the ".".
    advance();

    while (isDigit(peek()))
      advance();
  }
  else if (peek() == 'e' && isDigit(peekNext()))
  {
    // Consume the ".".
    advance();

    while (isDigit(peek()))
      advance();
  }
  else if (peek() == 'e' && ((peekNext() == '-' || peekNext() == '+') && isDigit(peekNextNext())))
  {
    // Consume the ".".
    advance();
    advance();

    while (isDigit(peek()))
      advance();
  }

  return makeToken(TOKEN_NUMBER);
}

static Token string(char c)
{
  while (peek() != c && !isAtEnd())
  {
    if (peek() == '\n')
      scanner.line++;
    advance();
  }

  if (isAtEnd())
    return errorToken("Unterminated string.");

  advance();
  return makeToken(TOKEN_STRING);
}

Token scanToken()
{
  skipWhitespace();

  scanner.start = scanner.current;

  if (isAtEnd())
    return makeToken(TOKEN_EOF);

  char c = advance();

  if (isAlpha(c))
    return identifier();
  if (isDigit(c))
    return number();

  switch (c)
  {
  case '(':
    return makeToken(TOKEN_LEFT_PAREN);
  case ')':
    return makeToken(TOKEN_RIGHT_PAREN);
  case '{':
    return makeToken(TOKEN_LEFT_BRACE);
  case '}':
    return makeToken(TOKEN_RIGHT_BRACE);
  case '[':
    return makeToken(TOKEN_LEFT_BRACKET);
  case ']':
    return makeToken(TOKEN_RIGHT_BRACKET);
  case ';':
    return makeToken(TOKEN_SEMICOLON);
  case ':':
    return makeToken(TOKEN_COLON);
  case ',':
    return makeToken(TOKEN_COMMA);
  case '.':
    return makeToken(TOKEN_DOT);
  case '%':
    return makeToken(TOKEN_PERCENT);
  case '-':
  {
    if (match('-'))
      return makeToken(TOKEN_DEC);
    else if (match('='))
      return makeToken(TOKEN_MINUS_EQUALS);
    else
      return makeToken(TOKEN_MINUS);
  }
  case '+':
  {
    if (match('+'))
      return makeToken(TOKEN_INC);
    else if (match('='))
      return makeToken(TOKEN_PLUS_EQUALS);
    else
      return makeToken(TOKEN_PLUS);
  }
  case '/':
  {
    if (match('='))
      return makeToken(TOKEN_DIVIDE_EQUALS);
    else
      return makeToken(TOKEN_SLASH);
  }
  case '*':
  {
    if (match('='))
      return makeToken(TOKEN_MULTIPLY_EQUALS);
    else
      return makeToken(TOKEN_STAR);
  }
  case '^':
    return makeToken(TOKEN_POW);
  case '!':
    return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
  case '=':
    return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
  case '<':
    return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
  case '>':
    return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);

  case '@':
    return makeToken(TOKEN_LAMBDA);

  case '"':
  case '\'':
    return string(c);
  }

  return errorToken("Unexpected character.");
}