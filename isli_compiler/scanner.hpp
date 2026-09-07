#ifndef ISLI_SCANNER_HPP
#define ISLI_SCANNER_HPP

enum IsliTokenType {
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
    TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET,
    TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
    TOKEN_SEMICOLON, TOKEN_COLON, TOKEN_SLASH, TOKEN_STAR, TOKEN_PERCENT,
    TOKEN_PLUS_EQUAL, TOKEN_MINUS_EQUAL, TOKEN_STAR_EQUAL, TOKEN_SLASH_EQUAL,
    TOKEN_BANG, TOKEN_BANG_EQUAL,
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL,
    TOKEN_AMPERSAND, TOKEN_ARROW, TOKEN_PIPE,
    TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER,
    TOKEN_AND, TOKEN_DISPATCH, TOKEN_ELSE, TOKEN_FALSE,
    TOKEN_FOR, TOKEN_FUN, TOKEN_IF, TOKEN_INTO, TOKEN_NULL, TOKEN_OR,
    TOKEN_PARALLEL, TOKEN_PRINT, TOKEN_REDUCE, TOKEN_RETURN, TOKEN_STRUCT,
    TOKEN_TILE, TOKEN_UNIFORM,
    TOKEN_TRUE, TOKEN_WHILE,
    TOKEN_INT_TYPE, TOKEN_FLOAT_TYPE, TOKEN_DOUBLE_TYPE, TOKEN_STRING_TYPE, TOKEN_BOOL_TYPE, TOKEN_VOID_TYPE,
    TOKEN_ERROR, TOKEN_EOF
};

struct Token {
    IsliTokenType type;
    const char* start;
    int length;
    int line;
};

struct Scanner {
    const char* start   = nullptr;
    const char* current = nullptr;
    int line = 1;

    void init(const char* source);
    Token scanToken();
    Token peekToken();
    Token peekToken2(Token* secondToken = nullptr);

private:
    bool isAlpha(char c);
    bool isDigit(char c);
    bool isAtEnd();
    Token makeToken(IsliTokenType type);
    Token errorToken(const char* message);
    IsliTokenType checkKeyword(int start, int length, const char* rest, IsliTokenType type);
    IsliTokenType identifierType();
    Token identifier();
    Token number();
    Token string();
    char advance();
    char peek();
    char peekNext();
    bool match(char expected);
    void skipWhitespace();
};

#endif // ISLI_SCANNER_HPP
