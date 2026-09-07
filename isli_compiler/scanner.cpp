#include <cstdio>
#include <cstring>

#include "common.hpp"
#include "scanner.hpp"

void Scanner::init(const char* source) {
    start   = source;
    current = source;
    line    = 1;
}

bool Scanner::isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
            c == '_';
}

bool Scanner::isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool Scanner::isAtEnd() {
    return *current == '\0';
}

Token Scanner::makeToken(IsliTokenType type) {
    Token token;
    token.type   = type;
    token.start  = start;
    token.length = static_cast<int>(current - start);
    token.line   = line;
    return token;
}

Token Scanner::errorToken(const char* message) {
    Token token;
    token.type   = TOKEN_ERROR;
    token.start  = message;
    token.length = static_cast<int>(std::strlen(message));
    token.line   = line;
    return token;
}

IsliTokenType Scanner::checkKeyword(int startPos, int length, const char* rest, IsliTokenType type) {
    if (current - start == startPos + length &&
        std::memcmp(start + startPos, rest, length) == 0) {
        return type;
    }
    return TOKEN_IDENTIFIER;
}

IsliTokenType Scanner::identifierType() {
    switch (start[0]) {
        case 'a': return checkKeyword(1, 2, "nd", TOKEN_AND);
        case 'd':
            if (current - start > 1) {
                switch (start[1]) {
                    case 'i': return checkKeyword(2, 6, "spatch", TOKEN_DISPATCH);
                    case 'o': return checkKeyword(2, 4, "uble", TOKEN_DOUBLE_TYPE);
                }
            }
            break;
        case 'e': return checkKeyword(1, 3, "lse", TOKEN_ELSE);
        case 'f':
            if (current - start > 1) {
                switch (start[1]) {
                    case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
                    case 'l': return checkKeyword(2, 3, "oat", TOKEN_FLOAT_TYPE);
                    case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
                    case 'u': return checkKeyword(2, 1, "n", TOKEN_FUN);
                }
            }
            break;
        case 'i':
            if (current - start > 1) {
                switch (start[1]) {
                    case 'f': return checkKeyword(2, 0, "", TOKEN_IF);
                    case 'n':
                        if (current - start == 4 && std::memcmp(start, "into", 4) == 0) {
                            return TOKEN_INTO;
                        }
                        return checkKeyword(2, 1, "t", TOKEN_INT_TYPE);
                }
            }
            break;
        case 'n':
            if (current - start > 1) {
                switch (start[1]) {
                    case 'u': return checkKeyword(2, 2, "ll", TOKEN_NULL);
                    case 'i': return checkKeyword(2, 1, "l", TOKEN_NULL);
                }
            }
            break;
        case 'N': return checkKeyword(1, 3, "ULL", TOKEN_NULL);
        case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
        case 'p':
            if (current - start == 8 && std::memcmp(start, "parallel", 8) == 0) return TOKEN_PARALLEL;
            return checkKeyword(1, 4, "rint", TOKEN_PRINT);
        case 'r':
            if (current - start == 6 && std::memcmp(start, "reduce", 6) == 0) return TOKEN_REDUCE;
            return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
        case 's':
            if (current - start > 1) {
                switch (start[1]) {
                    case 't': 
                        if (current - start == 6 && std::memcmp(start, "struct", 6) == 0) return TOKEN_STRUCT;
                        if (current - start == 6 && std::memcmp(start, "string", 6) == 0) return TOKEN_STRING_TYPE;
                        break;
                }
            }
            break;
        case 't':
            if (current - start == 4 && std::memcmp(start, "tile", 4) == 0) return TOKEN_TILE;
            return checkKeyword(1, 3, "rue", TOKEN_TRUE);
        case 'u': return checkKeyword(1, 6, "niform", TOKEN_UNIFORM);
        case 'v':
            if (current - start > 1) {
                switch (start[1]) {
                    case 'o': return checkKeyword(2, 2, "id", TOKEN_VOID_TYPE);
                }
            }
            break;
        case 'b': return checkKeyword(1, 3, "ool", TOKEN_BOOL_TYPE);
        case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

Token Scanner::identifier() {
    while (isAlpha(peek()) || isDigit(peek())) advance();
    return makeToken(identifierType());
}

Token Scanner::number() {
    while (isDigit(peek())) advance();

    if (peek() == '.' && isDigit(peekNext())) {
        advance();
        while (isDigit(peek())) advance();
    }

    return makeToken(TOKEN_NUMBER);
}

Token Scanner::string() {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') line++;
        if (peek() == '\\' && peekNext() != '\0') {
            advance(); // consume '\'
            if (peek() == '\n') line++;
            advance(); // consume escaped character (e.g. '"', 'n', '\\')
            continue;
        }
        advance();
    }

    if (isAtEnd()) return errorToken("Unterminated string.");

    advance();
    return makeToken(TOKEN_STRING);
}

char Scanner::advance() {
    current++;
    return current[-1];
}

char Scanner::peek() {
    return *current;
}

char Scanner::peekNext() {
    if (isAtEnd()) return '\0';
    return current[1];
}

bool Scanner::match(char expected) {
    if (isAtEnd()) return false;
    if (*current != expected) return false;
    current++;
    return true;
}

void Scanner::skipWhitespace() {
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                line++;
                advance();
                break;
            case '/':
                if (peekNext() == '/') {
                    while (peek() != '\n' && !isAtEnd()) advance();
                } else if (peekNext() == '*') {
                    advance(); // consume '/'
                    advance(); // consume '*'
                    while (!isAtEnd()) {
                        if (peek() == '\n') line++;
                        if (peek() == '*' && peekNext() == '/') {
                            advance(); // consume '*'
                            advance(); // consume '/'
                            break;
                        }
                        advance();
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

Token Scanner::scanToken() {
    skipWhitespace();
    start = current;

    if (isAtEnd()) return makeToken(TOKEN_EOF);

    char c = advance();
    if (isAlpha(c)) return identifier();
    if (isDigit(c)) return number();

    switch (c) {
        case '(': return makeToken(TOKEN_LEFT_PAREN);
        case ')': return makeToken(TOKEN_RIGHT_PAREN);
        case '{': return makeToken(TOKEN_LEFT_BRACE);
        case '}': return makeToken(TOKEN_RIGHT_BRACE);
        case '[': return makeToken(TOKEN_LEFT_BRACKET);
        case ']': return makeToken(TOKEN_RIGHT_BRACKET);
        case ';': return makeToken(TOKEN_SEMICOLON);
        case ',': return makeToken(TOKEN_COMMA);
        case '.': return makeToken(TOKEN_DOT);
        case '-': return makeToken(match('>') ? TOKEN_ARROW : (match('=') ? TOKEN_MINUS_EQUAL : TOKEN_MINUS));
        case '+': return makeToken(match('=') ? TOKEN_PLUS_EQUAL : TOKEN_PLUS);
        case '/': return makeToken(match('=') ? TOKEN_SLASH_EQUAL : TOKEN_SLASH);
        case '*': return makeToken(match('=') ? TOKEN_STAR_EQUAL : TOKEN_STAR);
        case '%': return makeToken(TOKEN_PERCENT);
        case ':': return makeToken(TOKEN_COLON);
        case '|': return makeToken(TOKEN_PIPE);
        case '&': return makeToken(TOKEN_AMPERSAND);
        case '!':
            return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=':
            return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<':
            return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>':
            return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '"': return string();
    }

    return errorToken("Unexpected character.");
}

Token Scanner::peekToken() {
    const char* savedStart = start;
    const char* savedCurrent = current;
    int savedLine = line;
    
    Token token = scanToken();
    
    start = savedStart;
    current = savedCurrent;
    line = savedLine;
    
    return token;
}

Token Scanner::peekToken2(Token* secondToken) {
    const char* savedStart = start;
    const char* savedCurrent = current;
    int savedLine = line;
    
    Token t1 = scanToken();
    Token t2 = scanToken();
    
    start = savedStart;
    current = savedCurrent;
    line = savedLine;
    
    if (secondToken) *secondToken = t2;
    return t1;
}
