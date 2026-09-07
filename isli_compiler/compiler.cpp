#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common.hpp"
#include "compiler.hpp"
#include "memory.hpp"
#include "object.hpp"
#include "scanner.hpp"

#ifdef DEBUG_PRINT_CODE
#include "debug.hpp"
#endif

// ── Parser state ──────────────────────────────────────────────────
struct Parser {
    Token current;
    Token previous;
    bool hadError  = false;
    bool panicMode = false;
    bool isAddressOf = false;
};

enum Precedence {
    PREC_NONE,
    PREC_ASSIGNMENT,
    PREC_OR,
    PREC_AND,
    PREC_EQUALITY,
    PREC_COMPARISON,
    PREC_TERM,
    PREC_FACTOR,
    PREC_UNARY,
    PREC_CALL,
    PREC_PRIMARY
};

enum class IsliType : uint8_t {
    TYPE_UNKNOWN = 0,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STRING,
    TYPE_BOOL,
    TYPE_VOID,
    TYPE_STRUCT,
    TYPE_ARRAY,
    TYPE_ANY
};

struct TypeInfo {
    IsliType kind = IsliType::TYPE_UNKNOWN;
    ObjString* structName = nullptr;

    bool isNumeric() const {
        return kind == IsliType::TYPE_INT || kind == IsliType::TYPE_FLOAT;
    }

    bool matches(const TypeInfo& other) const {
        if (kind == IsliType::TYPE_ANY || other.kind == IsliType::TYPE_ANY) return true;
        if (kind == IsliType::TYPE_UNKNOWN || other.kind == IsliType::TYPE_UNKNOWN) return true;
        if (isNumeric() && other.isNumeric()) return true;
        if ((kind == IsliType::TYPE_ARRAY && other.kind == IsliType::TYPE_STRUCT) ||
            (kind == IsliType::TYPE_STRUCT && other.kind == IsliType::TYPE_ARRAY)) return true;
        if (kind != other.kind) return false;
        if (kind == IsliType::TYPE_STRUCT && structName != nullptr && other.structName != nullptr) {
            if (structName == other.structName) return true;
            return structName->length == other.structName->length &&
                   structName->hash == other.structName->hash &&
                   std::memcmp(structName->chars, other.structName->chars, structName->length) == 0;
        }
        return true;
    }
};

using ParseFn = TypeInfo (*)(bool canAssign);

struct ParseRule {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
};

// ── Local / Upvalue / Compiler ────────────────────────────────────
struct Local {
    Token name;
    int depth      = 0;
    bool isCaptured = false;
    TypeInfo type;
};

struct Upvalue {
    uint8_t index = 0;
    bool isLocal  = false;
};

enum FunctionType {
    TYPE_FUNCTION,
    TYPE_SCRIPT
};

struct Compiler {
    Compiler* enclosing = nullptr;
    ObjFunction* function = nullptr;
    FunctionType type;
    TypeInfo returnType;

    Local locals[UINT8_COUNT];
    int localCount = 0;
    Upvalue upvalues[UINT8_COUNT];
    int scopeDepth = 0;
};

// ── Module-level state ────────────────────────────────────────────
static Parser parser;
static Compiler* current = nullptr;
static Scanner scannerInstance;

static Chunk* currentChunk() {
    return &current->function->chunk;
}

// ── Error handling ────────────────────────────────────────────────
static void errorAt(Token* token, const char* message) {
    if (parser.panicMode) return;
    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void error(const char* message) {
    errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
    errorAt(&parser.current, message);
}

// ── Token consumption ─────────────────────────────────────────────
static void advance() {
    parser.previous = parser.current;

    for (;;) {
        parser.current = scannerInstance.scanToken();
        if (parser.current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser.current.start);
    }
}

static void consume(IsliTokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }

    errorAtCurrent(message);
}

static bool check(IsliTokenType type) {
    return parser.current.type == type;
}

static bool match(IsliTokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

// ── Emit helpers ──────────────────────────────────────────────────
static void emitByte(uint8_t byte) {
    currentChunk()->write(byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

static void emitReturn() {
    emitByte(OP_NIL);
    emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value) {
    int constant = currentChunk()->addConstant(value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }

    return static_cast<uint8_t>(constant);
}

static void emitConstant(Value value) {
    emitBytes(OP_CONSTANT, makeConstant(value));
}

static int emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
}

static void patchJump(int offset) {
    int jump = currentChunk()->count - offset - 2;

    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }

    currentChunk()->code[offset]     = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

static void emitLoop(int loopStart) {
    emitByte(OP_LOOP);

    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");

    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

// ── Compiler init / end ───────────────────────────────────────────
static void initCompiler(Compiler* compiler, FunctionType type) {
    compiler->enclosing  = current;
    compiler->function   = nullptr;
    compiler->type       = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->function   = newFunction();
    current = compiler;

    if (type != TYPE_SCRIPT) {
        current->function->name = copyString(parser.previous.start,
                                             parser.previous.length);
    }

    Local* local     = &current->locals[current->localCount++];
    local->depth     = 0;
    local->isCaptured = false;
    local->name.start  = "";
    local->name.length = 0;
}

static ObjFunction* endCompiler() {
    emitReturn();
    ObjFunction* function = current->function;

#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
        disassembleChunk(currentChunk(), function->name != nullptr
            ? function->name->chars : "<script>");
    }
#endif

    current = current->enclosing;
    return function;
}

// ── Scope ─────────────────────────────────────────────────────────
static void beginScope() {
    current->scopeDepth++;
}

static void endScope() {
    current->scopeDepth--;

    while (current->localCount > 0 &&
           current->locals[current->localCount - 1].depth > current->scopeDepth) {
        if (current->locals[current->localCount - 1].isCaptured) {
            emitByte(OP_CLOSE_UPVALUE);
        } else {
            emitByte(OP_POP);
        }
        current->localCount--;
    }
}

// ── Forward declarations ──────────────────────────────────────────
static TypeInfo expression();
static void statement();
static void declaration();
static TypeInfo parsePrecedence(Precedence precedence);
static ParseRule* getRule(IsliTokenType type);

static inline bool isTypeToken(IsliTokenType type) {
    return type == TOKEN_INT_TYPE || type == TOKEN_FLOAT_TYPE ||
           type == TOKEN_DOUBLE_TYPE || type == TOKEN_STRING_TYPE ||
           type == TOKEN_BOOL_TYPE || type == TOKEN_VOID_TYPE;
}

static inline bool matchPrimitiveType() {
    return match(TOKEN_INT_TYPE) || match(TOKEN_FLOAT_TYPE) ||
           match(TOKEN_DOUBLE_TYPE) || match(TOKEN_STRING_TYPE) ||
           match(TOKEN_BOOL_TYPE) || match(TOKEN_VOID_TYPE);
}

static inline bool checkTypeDeclaration() {
    if (isTypeToken(parser.current.type)) return true;
    if (parser.current.type == TOKEN_IDENTIFIER && 
        scannerInstance.peekToken().type == TOKEN_IDENTIFIER) return true;
    return false;
}

static inline bool matchTypeDeclaration() {
    if (matchPrimitiveType()) return true;
    if (parser.current.type == TOKEN_IDENTIFIER && 
        scannerInstance.peekToken().type == TOKEN_IDENTIFIER) {
        advance();
        return true;
    }
    return false;
}

static TypeInfo parseTypeSpecifier() {
    TypeInfo t;
    if (match(TOKEN_INT_TYPE)) {
        t.kind = IsliType::TYPE_INT;
    } else if (match(TOKEN_FLOAT_TYPE) || match(TOKEN_DOUBLE_TYPE)) {
        t.kind = IsliType::TYPE_FLOAT;
    } else if (match(TOKEN_STRING_TYPE)) {
        t.kind = IsliType::TYPE_STRING;
    } else if (match(TOKEN_BOOL_TYPE)) {
        t.kind = IsliType::TYPE_BOOL;
    } else if (match(TOKEN_VOID_TYPE)) {
        t.kind = IsliType::TYPE_VOID;
    } else if (check(TOKEN_IDENTIFIER)) {
        advance();
        t.kind = IsliType::TYPE_STRUCT;
        t.structName = copyString(parser.previous.start, parser.previous.length);
    } else {
        errorAtCurrent("Expect type name.");
    }
    return t;
}

// ── Variable helpers ──────────────────────────────────────────────
static uint8_t identifierConstant(Token* name) {
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token* a, Token* b) {
    if (a->length != b->length) return false;
    return std::memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            return i;
        }
    }

    return -1;
}

static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
    int upvalueCount = compiler->function->upvalueCount;

    for (int i = 0; i < upvalueCount; i++) {
        Upvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) {
            return i;
        }
    }

    if (upvalueCount == UINT8_COUNT) {
        error("Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index   = index;
    return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler* compiler, Token* name) {
    if (compiler->enclosing == nullptr) return -1;

    int local = resolveLocal(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(compiler, static_cast<uint8_t>(local), true);
    }

    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(compiler, static_cast<uint8_t>(upvalue), false);
    }

    return -1;
}

static void addLocal(Token name) {
    if (current->localCount == UINT8_COUNT) {
        error("Too many local variables in function.");
        return;
    }

    Local* local     = &current->locals[current->localCount++];
    local->name      = name;
    local->depth     = -1;
    local->isCaptured = false;
}

static void declareVariable() {
    if (current->scopeDepth == 0) return;

    Token* name = &parser.previous;
    for (int i = current->localCount - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) {
            break;
        }

        if (identifiersEqual(name, &local->name)) {
            error("Already a variable with this name in this scope.");
        }
    }

    addLocal(*name);
}

static uint8_t parseVariable(const char* errorMessage) {
    consume(TOKEN_IDENTIFIER, errorMessage);

    declareVariable();
    if (current->scopeDepth > 0) return 0;

    return identifierConstant(&parser.previous);
}

static void markInitialized() {
    if (current->scopeDepth == 0) return;
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void defineVariable(uint8_t global) {
    if (current->scopeDepth > 0) {
        markInitialized();
        return;
    }

    emitBytes(OP_DEFINE_GLOBAL, global);
}

static uint8_t argumentList() {
    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression();
            if (argCount == 255) {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

// ── Parse functions ───────────────────────────────────────────────
static TypeInfo and_(bool canAssign) {
    (void)canAssign;
    int endJump = emitJump(OP_JUMP_IF_FALSE);

    emitByte(OP_POP);
    parsePrecedence(PREC_AND);

    patchJump(endJump);
    return TypeInfo{IsliType::TYPE_BOOL};
}

static TypeInfo binary(bool canAssign) {
    (void)canAssign;
    IsliTokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    TypeInfo rightType = parsePrecedence(static_cast<Precedence>(rule->precedence + 1));

    switch (operatorType) {
        case TOKEN_BANG_EQUAL:
            emitBytes(OP_EQUAL, OP_NOT);
            return TypeInfo{IsliType::TYPE_BOOL};
        case TOKEN_EQUAL_EQUAL:
            emitByte(OP_EQUAL);
            return TypeInfo{IsliType::TYPE_BOOL};
        case TOKEN_GREATER:
            emitByte(OP_GREATER);
            return TypeInfo{IsliType::TYPE_BOOL};
        case TOKEN_GREATER_EQUAL:
            emitBytes(OP_LESS, OP_NOT);
            return TypeInfo{IsliType::TYPE_BOOL};
        case TOKEN_LESS:
            emitByte(OP_LESS);
            return TypeInfo{IsliType::TYPE_BOOL};
        case TOKEN_LESS_EQUAL:
            emitBytes(OP_GREATER, OP_NOT);
            return TypeInfo{IsliType::TYPE_BOOL};
        case TOKEN_PLUS:
            if (rightType.kind == IsliType::TYPE_STRING) {
                emitByte(OP_ADD);
                return TypeInfo{IsliType::TYPE_STRING};
            }
            emitByte(OP_ADD_NUM);
            return TypeInfo{IsliType::TYPE_FLOAT};
        case TOKEN_MINUS:
            emitByte(OP_SUBTRACT_NUM);
            return TypeInfo{IsliType::TYPE_FLOAT};
        case TOKEN_STAR:
            emitByte(OP_MULTIPLY_NUM);
            return TypeInfo{IsliType::TYPE_FLOAT};
        case TOKEN_SLASH:
            emitByte(OP_DIVIDE_NUM);
            return TypeInfo{IsliType::TYPE_FLOAT};
        case TOKEN_PERCENT:
            emitByte(OP_MODULO_NUM);
            return TypeInfo{IsliType::TYPE_FLOAT};
        default:
            return TypeInfo{IsliType::TYPE_ANY};
    }
}

static TypeInfo call(bool canAssign) {
    (void)canAssign;
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
    return TypeInfo{IsliType::TYPE_ANY};
}

static TypeInfo dot(bool canAssign) {
    consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    uint8_t name = identifierConstant(&parser.previous);

    if (parser.isAddressOf && !check(TOKEN_DOT) && !check(TOKEN_LEFT_BRACKET)) {
        parser.isAddressOf = false;
        emitBytes(OP_GET_PROPERTY_PTR, name);
    } else if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(OP_SET_PROPERTY, name);
    } else if (check(TOKEN_DOT) || check(TOKEN_LEFT_BRACKET)) {
        // If followed by another dot or bracket, push pointer of this property to allow nested in-place mutation
        emitBytes(OP_GET_PROPERTY_PTR, name);
    } else {
        emitBytes(OP_GET_PROPERTY, name);
    }
    return TypeInfo{IsliType::TYPE_ANY};
}

static TypeInfo subscript(bool canAssign) {
    expression();
    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after index.");

    if (parser.isAddressOf && !check(TOKEN_DOT) && !check(TOKEN_LEFT_BRACKET)) {
        parser.isAddressOf = false;
        emitByte(OP_GET_INDEX_PTR);
    } else if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitByte(OP_SET_INDEX);
    } else if (check(TOKEN_DOT) || check(TOKEN_LEFT_BRACKET)) {
        // If followed by dot or bracket, push pointer to allow in-place nested mutation
        emitByte(OP_GET_INDEX_PTR);
    } else {
        emitByte(OP_GET_INDEX);
    }
    return TypeInfo{IsliType::TYPE_ANY};
}

static TypeInfo arrayLiteral(bool canAssign) {
    (void)canAssign;
    uint8_t itemCount = 0;
    if (!check(TOKEN_RIGHT_BRACKET)) {
        do {
            expression();
            if (itemCount == 255) {
                error("Cannot have more than 255 elements in array literal.");
            }
            itemCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after array elements.");
    emitBytes(OP_BUILD_ARRAY, itemCount);
    return TypeInfo{IsliType::TYPE_ARRAY};
}

static TypeInfo literal(bool canAssign) {
    (void)canAssign;
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); return TypeInfo{IsliType::TYPE_BOOL};
        case TOKEN_NULL:  emitByte(OP_NIL); return TypeInfo{IsliType::TYPE_ANY};
        case TOKEN_TRUE:  emitByte(OP_TRUE); return TypeInfo{IsliType::TYPE_BOOL};
        default: return TypeInfo{IsliType::TYPE_ANY};
    }
}

static TypeInfo grouping(bool canAssign) {
    (void)canAssign;
    TypeInfo t = expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
    return t;
}

static TypeInfo number(bool canAssign) {
    (void)canAssign;
    double value = strtod(parser.previous.start, nullptr);
    emitConstant(NUMBER_VAL(value));
    return TypeInfo{IsliType::TYPE_FLOAT};
}

static TypeInfo or_(bool canAssign) {
    (void)canAssign;
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    int endJump  = emitJump(OP_JUMP);

    patchJump(elseJump);
    emitByte(OP_POP);

    parsePrecedence(PREC_OR);
    patchJump(endJump);
    return TypeInfo{IsliType::TYPE_BOOL};
}

static ObjString* unescapeAndCopyString(const char* start, int length) {
    char* buffer = static_cast<char*>(std::malloc(length + 1));
    if (buffer == nullptr) std::exit(1);

    int writeIdx = 0;
    for (int i = 0; i < length; i++) {
        if (start[i] == '\\' && i + 1 < length) {
            i++;
            switch (start[i]) {
                case 'n': buffer[writeIdx++] = '\n'; break;
                case 't': buffer[writeIdx++] = '\t'; break;
                case 'r': buffer[writeIdx++] = '\r'; break;
                case '"': buffer[writeIdx++] = '"';  break;
                case '\\': buffer[writeIdx++] = '\\'; break;
                case '0': buffer[writeIdx++] = '\0'; break;
                default:
                    buffer[writeIdx++] = '\\';
                    buffer[writeIdx++] = start[i];
                    break;
            }
        } else {
            buffer[writeIdx++] = start[i];
        }
    }
    buffer[writeIdx] = '\0';
    return takeString(buffer, writeIdx);
}

static TypeInfo string(bool canAssign) {
    (void)canAssign;
    emitConstant(OBJ_VAL(unescapeAndCopyString(parser.previous.start + 1,
                                               parser.previous.length - 2)));
    return TypeInfo{IsliType::TYPE_STRING};
}

static TypeInfo namedVariable(Token name, bool canAssign) {
    uint8_t getOp, setOp, getPtrOp;
    TypeInfo varType{IsliType::TYPE_ANY};
    int arg = resolveLocal(current, &name);
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
        getPtrOp = OP_GET_LOCAL_PTR;
        varType = current->locals[arg].type;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
        getPtrOp = OP_GET_UPVALUE_PTR;
    } else {
        arg   = identifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
        getPtrOp = OP_GET_GLOBAL_PTR;
    }

    if (parser.isAddressOf && !check(TOKEN_DOT) && !check(TOKEN_LEFT_BRACKET)) {
        parser.isAddressOf = false;
        emitBytes(getPtrOp, static_cast<uint8_t>(arg));
        return varType;
    } else if (canAssign && match(TOKEN_EQUAL)) {
        TypeInfo rightType = expression();
        if (varType.kind != IsliType::TYPE_ANY && varType.kind != IsliType::TYPE_UNKNOWN && !rightType.matches(varType)) {
            error("Type mismatch in variable assignment.");
        }
        emitBytes(setOp, static_cast<uint8_t>(arg));
        return rightType;
    } else if (canAssign && (match(TOKEN_PLUS_EQUAL) || match(TOKEN_MINUS_EQUAL) || match(TOKEN_STAR_EQUAL) || match(TOKEN_SLASH_EQUAL))) {
        IsliTokenType assignOp = parser.previous.type;
        emitBytes(getOp, static_cast<uint8_t>(arg));
        expression();
        if (assignOp == TOKEN_PLUS_EQUAL) emitByte(OP_ADD);
        else if (assignOp == TOKEN_MINUS_EQUAL) emitByte(OP_SUBTRACT);
        else if (assignOp == TOKEN_STAR_EQUAL) emitByte(OP_MULTIPLY);
        else emitByte(OP_DIVIDE);
        emitBytes(setOp, static_cast<uint8_t>(arg));
        return varType;
    } else {
        emitBytes(getOp, static_cast<uint8_t>(arg));
        return varType;
    }
}

static TypeInfo variable(bool canAssign) {
    return namedVariable(parser.previous, canAssign);
}

static TypeInfo unary(bool canAssign) {
    (void)canAssign;
    IsliTokenType operatorType = parser.previous.type;

    if (operatorType == TOKEN_AMPERSAND) {
        parser.isAddressOf = true;
        TypeInfo t = parsePrecedence(PREC_UNARY);
        if (parser.isAddressOf) {
            error("Invalid target for address-of '&'.");
            parser.isAddressOf = false;
        }
        return t;
    }
    
    if (operatorType == TOKEN_STAR) {
        parsePrecedence(PREC_UNARY);
        if (canAssign && match(TOKEN_EQUAL)) {
            TypeInfo valType = expression();
            emitByte(OP_SET_DEREF);
            return valType;
        } else {
            emitByte(OP_DEREF);
            return TypeInfo{IsliType::TYPE_ANY};
        }
    }

    TypeInfo operandType = parsePrecedence(PREC_UNARY);

    switch (operatorType) {
        case TOKEN_BANG:  emitByte(OP_NOT); return TypeInfo{IsliType::TYPE_BOOL};
        case TOKEN_MINUS: emitByte(OP_NEGATE); return operandType;
        default: return operandType;
    }
}

static void structDeclaration();
static TypeInfo anonymousStruct(bool canAssign);

// ── Parse rules table ─────────────────────────────────────────────
// C++ does not support designated initializers for arrays in the same way as C99.
// We use a helper function to build the table.
static ParseRule rules[TOKEN_EOF + 1];

static void initRules() {
    // Zero-init all
    for (int i = 0; i <= TOKEN_EOF; i++) {
        rules[i] = {nullptr, nullptr, PREC_NONE};
    }

    rules[TOKEN_LEFT_PAREN]    = {grouping,     call,      PREC_CALL};
    rules[TOKEN_RIGHT_PAREN]   = {nullptr,      nullptr,   PREC_NONE};
    rules[TOKEN_LEFT_BRACE]    = {anonymousStruct, nullptr,PREC_NONE};
    rules[TOKEN_RIGHT_BRACE]   = {nullptr,      nullptr,   PREC_NONE};
    rules[TOKEN_LEFT_BRACKET]  = {arrayLiteral, subscript, PREC_CALL};
    rules[TOKEN_RIGHT_BRACKET] = {nullptr,      nullptr,   PREC_NONE};
    rules[TOKEN_COMMA]         = {nullptr,      nullptr,   PREC_NONE};
    rules[TOKEN_DOT]           = {nullptr,      dot,       PREC_CALL};
    rules[TOKEN_MINUS]         = {unary,        binary,    PREC_TERM};
    rules[TOKEN_PLUS]          = {nullptr,      binary,    PREC_TERM};
    rules[TOKEN_SEMICOLON]     = {nullptr,      nullptr,   PREC_NONE};
    rules[TOKEN_SLASH]         = {nullptr,      binary,    PREC_FACTOR};
    rules[TOKEN_STAR]          = {unary,        binary,    PREC_FACTOR};
    rules[TOKEN_PERCENT]       = {nullptr,      binary,    PREC_FACTOR};
    rules[TOKEN_AMPERSAND]     = {unary,        nullptr,   PREC_NONE};
    rules[TOKEN_BANG]          = {unary,        nullptr,   PREC_NONE};
    rules[TOKEN_BANG_EQUAL]    = {nullptr,      binary,    PREC_EQUALITY};
    rules[TOKEN_EQUAL]         = {nullptr,      nullptr,   PREC_NONE};
    rules[TOKEN_EQUAL_EQUAL]   = {nullptr,      binary,    PREC_EQUALITY};
    rules[TOKEN_GREATER]       = {nullptr,      binary,    PREC_COMPARISON};
    rules[TOKEN_GREATER_EQUAL] = {nullptr,      binary,    PREC_COMPARISON};
    rules[TOKEN_LESS]          = {nullptr,      binary,    PREC_COMPARISON};
    rules[TOKEN_LESS_EQUAL]    = {nullptr,      binary,    PREC_COMPARISON};
    rules[TOKEN_IDENTIFIER]    = {variable,     nullptr,   PREC_NONE};
    rules[TOKEN_STRING]        = {string,       nullptr,   PREC_NONE};
    rules[TOKEN_NUMBER]        = {number,       nullptr,   PREC_NONE};
    rules[TOKEN_AND]           = {nullptr,      and_,      PREC_AND};
    rules[TOKEN_ELSE]          = {nullptr,      nullptr,   PREC_NONE};
    rules[TOKEN_FALSE]         = {literal,      nullptr,   PREC_NONE};
    rules[TOKEN_FOR]           = {nullptr,      nullptr,   PREC_NONE};
    rules[TOKEN_FUN]           = {nullptr,      nullptr,   PREC_NONE};
    rules[TOKEN_IF]            = {nullptr,      nullptr,   PREC_NONE};
    rules[TOKEN_NULL]          = {literal,      nullptr,   PREC_NONE};
    rules[TOKEN_OR]            = {nullptr,      or_,       PREC_OR};
    rules[TOKEN_PRINT]         = {nullptr,      nullptr,   PREC_NONE};
    rules[TOKEN_RETURN]        = {nullptr,      nullptr,   PREC_NONE};
    rules[TOKEN_STRUCT]        = {nullptr,      nullptr,   PREC_NONE};
    rules[TOKEN_TRUE]          = {literal,      nullptr,   PREC_NONE};
    rules[TOKEN_WHILE]         = {nullptr,      nullptr,   PREC_NONE};
    rules[TOKEN_INT_TYPE]      = {nullptr,      nullptr,   PREC_NONE};
    rules[TOKEN_FLOAT_TYPE]    = {nullptr,      nullptr,   PREC_NONE};
    rules[TOKEN_DOUBLE_TYPE]   = {nullptr,      nullptr,   PREC_NONE};
    rules[TOKEN_STRING_TYPE]   = {nullptr,      nullptr,   PREC_NONE};
    rules[TOKEN_BOOL_TYPE]     = {nullptr,      nullptr,   PREC_NONE};
    rules[TOKEN_VOID_TYPE]     = {nullptr,      nullptr,   PREC_NONE};
    rules[TOKEN_ERROR]         = {nullptr,      nullptr,   PREC_NONE};
    rules[TOKEN_EOF]           = {nullptr,      nullptr,   PREC_NONE};
}

static bool rulesInitialized = false;

static void ensureRulesInitialized() {
    if (!rulesInitialized) {
        initRules();
        rulesInitialized = true;
    }
}

// ── Pratt parser core ─────────────────────────────────────────────
static TypeInfo parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == nullptr) {
        error("Expect expression.");
        return TypeInfo{IsliType::TYPE_ANY};
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    TypeInfo currentType = prefixRule(canAssign);

    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        currentType = infixRule(canAssign);
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
    }
    return currentType;
}

static ParseRule* getRule(IsliTokenType type) {
    return &rules[type];
}

static TypeInfo expression() {
    return parsePrecedence(PREC_ASSIGNMENT);
}

// ── Statements ────────────────────────────────────────────────────
static void block() {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(FunctionType type, TypeInfo returnType = TypeInfo{IsliType::TYPE_VOID}) {
    Compiler compiler;
    initCompiler(&compiler, type);
    compiler.returnType = returnType;
    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) {
                errorAtCurrent("Can't have more than 255 parameters.");
            }
            TypeInfo paramType{IsliType::TYPE_ANY};
            if (checkTypeDeclaration()) {
                paramType = parseTypeSpecifier();
                uint8_t constant = parseVariable("Expect parameter name.");
                defineVariable(constant);
                current->locals[current->localCount - 1].type = paramType;
                if (paramType.kind == IsliType::TYPE_STRUCT && paramType.structName != nullptr) {
                    if (current->function->paramTypes == nullptr) {
                        current->function->paramTypes = ALLOCATE(ObjString*, UINT8_COUNT);
                        for (int p = 0; p < UINT8_COUNT; p++) current->function->paramTypes[p] = nullptr;
                    }
                    current->function->paramTypes[current->function->arity - 1] = paramType.structName;
                }
            } else {
                uint8_t constant = parseVariable("Expect parameter name.");
                defineVariable(constant);
                if (match(TOKEN_COLON)) {
                    paramType = parseTypeSpecifier();
                    current->locals[current->localCount - 1].type = paramType;
                    if (paramType.kind == IsliType::TYPE_STRUCT && paramType.structName != nullptr) {
                        if (current->function->paramTypes == nullptr) {
                            current->function->paramTypes = ALLOCATE(ObjString*, UINT8_COUNT);
                            for (int p = 0; p < UINT8_COUNT; p++) current->function->paramTypes[p] = nullptr;
                        }
                        current->function->paramTypes[current->function->arity - 1] = paramType.structName;
                    }
                }
            }
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    
    if (match(TOKEN_ARROW)) {
        compiler.returnType = parseTypeSpecifier();
    }
    
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    ObjFunction* fn = endCompiler();
    emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(fn)));

    for (int i = 0; i < fn->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}

static TypeInfo anonymousStruct(bool canAssign) {
    (void)canAssign;
    uint8_t fieldNames[256];
    int fieldCount = 0;
    if (!check(TOKEN_RIGHT_BRACE)) {
        do {
            consume(TOKEN_IDENTIFIER, "Expect property name.");
            if (fieldCount == 255) {
                error("Cannot have more than 255 fields in anonymous struct.");
            }
            fieldNames[fieldCount] = identifierConstant(&parser.previous);
            consume(TOKEN_COLON, "Expect ':' after property name.");
            expression();
            fieldCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after anonymous struct body.");
    emitBytes(OP_ANONYMOUS_STRUCT, static_cast<uint8_t>(fieldCount));
    for (int i = 0; i < fieldCount; i++) {
        emitByte(fieldNames[i]);
    }
    return TypeInfo{IsliType::TYPE_STRUCT};
}

static void structDeclaration() {
    consume(TOKEN_IDENTIFIER, "Expect struct name.");
    uint8_t nameConstant = identifierConstant(&parser.previous);
    declareVariable();

    emitBytes(OP_STRUCT, nameConstant);
    defineVariable(nameConstant);

    consume(TOKEN_LEFT_BRACE, "Expect '{' before struct body.");
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after struct body.");
}

static void funDeclaration() {
    uint8_t global = parseVariable("Expect function name.");
    markInitialized();
    function(TYPE_FUNCTION, TypeInfo{IsliType::TYPE_ANY});
    defineVariable(global);
}

static void cFunctionDeclaration(TypeInfo returnType) {
    uint8_t global = parseVariable("Expect function name.");
    markInitialized();
    function(TYPE_FUNCTION, returnType);
    defineVariable(global);
}

static void varDeclaration(TypeInfo declaredType = TypeInfo{IsliType::TYPE_ANY}) {
    uint8_t global = parseVariable("Expect variable name.");

    if (current->scopeDepth > 0) {
        current->locals[current->localCount - 1].type = declaredType;
    }

    if (match(TOKEN_EQUAL)) {
        TypeInfo initType = expression();
        if (declaredType.kind != IsliType::TYPE_ANY && declaredType.kind != IsliType::TYPE_UNKNOWN && !initType.matches(declaredType)) {
            error("Type mismatch in variable initialization.");
        }
    } else {
        emitByte(OP_NIL);
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    defineVariable(global);
}

static void expressionStatement() {
    TypeInfo exprType = expression();
    if (match(TOKEN_SEMICOLON)) {
        emitByte(OP_POP);
    } else if (current->type == TYPE_FUNCTION && current->scopeDepth == 1 && check(TOKEN_RIGHT_BRACE)) {
        if (current->returnType.kind == IsliType::TYPE_VOID) {
            emitByte(OP_POP);
            emitReturn();
        } else if (current->returnType.kind != IsliType::TYPE_ANY && 
                   current->returnType.kind != IsliType::TYPE_UNKNOWN && 
                   !exprType.matches(current->returnType)) {
            error("Implicit return type does not match function signature.");
        } else {
            emitByte(OP_RETURN);
        }
    } else {
        consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    }
}

// ── Helper to compile startExpr & endExpr cleanly upon successful optimization (D.R.Y.) ──
static void compileLoopBoundsHelper(const Scanner& savedScanner, const Parser& savedParser,
                                    const Scanner& endScanner, const Parser& endParser) {
    scannerInstance = savedScanner;
    parser = savedParser;
    match(TOKEN_LEFT_PAREN);
    matchTypeDeclaration();
    advance(); // iterVar
    match(TOKEN_EQUAL);
    expression(); // startExpr
    match(TOKEN_SEMICOLON);
    advance(); // iterVar
    match(TOKEN_LESS);
    expression(); // endExpr

    scannerInstance = endScanner;
    parser = endParser;
}

static bool tryOptimizeForLoop() {
    Parser savedParser = parser;
    Scanner savedScanner = scannerInstance;
    int savedLocalCount = current->localCount;
    int savedScopeDepth = current->scopeDepth;
    int savedFnUpvalueCount = current->function->upvalueCount;
    int savedChunkCount = currentChunk()->count;
    int savedConstantsCount = currentChunk()->constants.count;

    Token iterVar;
    Token destVar;
    Token srcVar;
    Scanner peekScanner;
    Parser peekParser;
    Scanner endScanner;
    Parser endParser;
    bool hasBraces = false;

    if (!match(TOKEN_LEFT_PAREN)) return false;

    // 1. int <iterVar> = <startExpr>;
    if (!matchTypeDeclaration()) goto rollback;

    if (!check(TOKEN_IDENTIFIER)) goto rollback;
    iterVar = parser.current;
    advance();
    if (!match(TOKEN_EQUAL)) goto rollback;

    // Speculatively compile startExpr
    expression();
    if (!match(TOKEN_SEMICOLON)) goto rollback;

    // 2. <iterVar> < <endExpr>;
    if (!check(TOKEN_IDENTIFIER) || !identifiersEqual(&parser.current, &iterVar)) goto rollback;
    advance();
    if (!match(TOKEN_LESS)) goto rollback;

    // Speculatively compile endExpr
    expression();
    if (!match(TOKEN_SEMICOLON)) goto rollback;

    // 3. <iterVar> = <iterVar> + 1 (or 1 + <iterVar>)
    if (!check(TOKEN_IDENTIFIER) || !identifiersEqual(&parser.current, &iterVar)) goto rollback;
    advance();
    if (!match(TOKEN_EQUAL)) goto rollback;

    if (check(TOKEN_IDENTIFIER) && identifiersEqual(&parser.current, &iterVar)) {
        advance();
        if (!match(TOKEN_PLUS)) goto rollback;
        if (!check(TOKEN_NUMBER) || parser.current.start[0] != '1') goto rollback;
        advance();
    } else if (check(TOKEN_NUMBER) && parser.current.start[0] == '1') {
        advance();
        if (!match(TOKEN_PLUS)) goto rollback;
        if (!check(TOKEN_IDENTIFIER) || !identifiersEqual(&parser.current, &iterVar)) goto rollback;
        advance();
    } else {
        goto rollback;
    }

    if (!match(TOKEN_RIGHT_PAREN)) goto rollback;

    // 4. Body:
    hasBraces = match(TOKEN_LEFT_BRACE);

    // Look for dest[iterVar] = ...
    if (!check(TOKEN_IDENTIFIER)) goto rollback;
    destVar = parser.current;
    advance();

    if (!match(TOKEN_LEFT_BRACKET)) goto rollback;
    if (!check(TOKEN_IDENTIFIER) || !identifiersEqual(&parser.current, &iterVar)) goto rollback;
    advance();
    if (!match(TOKEN_RIGHT_BRACKET)) goto rollback;

    if (!match(TOKEN_EQUAL)) goto rollback;

    peekScanner = scannerInstance;
    peekParser = parser;

    // Case A: dest[iterVar] = src[iterVar]; (Array Copy)
    if (check(TOKEN_IDENTIFIER)) {
        srcVar = parser.current;
        advance(); // consume srcVar
        if (match(TOKEN_LEFT_BRACKET) && check(TOKEN_IDENTIFIER) && identifiersEqual(&parser.current, &iterVar)) {
            advance(); // consume iterVar
            if (match(TOKEN_RIGHT_BRACKET) && match(TOKEN_SEMICOLON)) {
                if (hasBraces && !match(TOKEN_RIGHT_BRACE)) goto rollback;

                // Save exact position after loop
                endScanner = scannerInstance;
                endParser = parser;

                // SUCCESS: It is a canonical ARRAY COPY loop!
                currentChunk()->count = savedChunkCount;
                currentChunk()->constants.count = savedConstantsCount;

                // Push destVar, srcVar
                namedVariable(destVar, false);
                namedVariable(srcVar, false);

                compileLoopBoundsHelper(savedScanner, savedParser, endScanner, endParser);

                emitByte(OP_SIMD_ARRAY_COPY);
                return true;
            }
        }
        // If not src[iterVar], restore peek
        scannerInstance = peekScanner;
        parser = peekParser;
    }

    // Case B: dest[iterVar] = <fillExpr>; (Array Fill)
    {
        // Scan tokens up to ';' or '}' to ensure iterVar does not appear in fillExpr
        Scanner checkScanner = scannerInstance;
        Parser checkParser = parser;
        bool usesIter = false;
        while (!check(TOKEN_SEMICOLON) && !check(TOKEN_EOF) && !check(TOKEN_RIGHT_BRACE)) {
            if (check(TOKEN_IDENTIFIER) && identifiersEqual(&parser.current, &iterVar)) {
                usesIter = true;
                break;
            }
            advance();
        }
        scannerInstance = checkScanner;
        parser = checkParser;
        if (usesIter) goto rollback;

        currentChunk()->count = savedChunkCount;
        currentChunk()->constants.count = savedConstantsCount;

        // Push destVar
        namedVariable(destVar, false);

        scannerInstance = peekScanner;
        parser = peekParser;

        // Compile fillExpr
        expression();
        if (!match(TOKEN_SEMICOLON)) goto rollback;
        if (hasBraces && !match(TOKEN_RIGHT_BRACE)) goto rollback;

        endScanner = scannerInstance;
        endParser = parser;

        compileLoopBoundsHelper(savedScanner, savedParser, endScanner, endParser);

        emitByte(OP_SIMD_ARRAY_FILL);
        return true;
    }

rollback:
    parser = savedParser;
    scannerInstance = savedScanner;
    current->localCount = savedLocalCount;
    current->scopeDepth = savedScopeDepth;
    current->function->upvalueCount = savedFnUpvalueCount;
    currentChunk()->count = savedChunkCount;
    currentChunk()->constants.count = savedConstantsCount;
    return false;
}

static bool tryOptimizeCountedForLoop() {
    Parser savedParser = parser;
    Scanner savedScanner = scannerInstance;
    int savedLocalCount = current->localCount;
    int savedScopeDepth = current->scopeDepth;
    int savedFnUpvalueCount = current->function->upvalueCount;
    int savedChunkCount = currentChunk()->count;
    int savedConstantsCount = currentChunk()->constants.count;

    Token iterVar;
    Token limitToken;
    bool isLeq = false;
    uint8_t iterSlot = 0;
    uint8_t limitSlot = 0;
    int exitJump = -1;
    int endJump = -1;
    int loopBodyStart = 0;
    int offset = 0;

    if (!match(TOKEN_LEFT_PAREN)) return false;

    // 1. int <iterVar> = <startExpr>;
    if (!matchTypeDeclaration()) goto rollback;

    if (!check(TOKEN_IDENTIFIER)) goto rollback;
    iterVar = parser.current;
    advance();
    if (!match(TOKEN_EQUAL)) goto rollback;

    beginScope();

    // Compile startExpr
    expression();
    if (!match(TOKEN_SEMICOLON)) { endScope(); goto rollback; }
    addLocal(iterVar);
    markInitialized();
    iterSlot = static_cast<uint8_t>(current->localCount - 1);

    // 2. <iterVar> < <endExpr>; or <iterVar> <= <endExpr>;
    if (!check(TOKEN_IDENTIFIER) || !identifiersEqual(&parser.current, &iterVar)) { endScope(); goto rollback; }
    advance();
    if (match(TOKEN_LESS)) {
        isLeq = false;
    } else if (match(TOKEN_LESS_EQUAL)) {
        isLeq = true;
    } else {
        endScope();
        goto rollback;
    }

    // Compile endExpr and store as $limit
    expression();
    if (!match(TOKEN_SEMICOLON)) { endScope(); goto rollback; }
    limitToken.start = "$limit";
    limitToken.length = 6;
    limitToken.line = iterVar.line;
    limitToken.type = TOKEN_IDENTIFIER;
    addLocal(limitToken);
    markInitialized();
    limitSlot = static_cast<uint8_t>(current->localCount - 1);

    // 3. <iterVar> = <iterVar> + 1 (or 1 + <iterVar>)
    if (!check(TOKEN_IDENTIFIER) || !identifiersEqual(&parser.current, &iterVar)) { endScope(); goto rollback; }
    advance();
    if (!match(TOKEN_EQUAL)) { endScope(); goto rollback; }

    if (check(TOKEN_IDENTIFIER) && identifiersEqual(&parser.current, &iterVar)) {
        advance();
        if (!match(TOKEN_PLUS)) { endScope(); goto rollback; }
        if (!check(TOKEN_NUMBER) || parser.current.start[0] != '1') { endScope(); goto rollback; }
        advance();
    } else if (check(TOKEN_NUMBER) && parser.current.start[0] == '1') {
        advance();
        if (!match(TOKEN_PLUS)) { endScope(); goto rollback; }
        if (!check(TOKEN_IDENTIFIER) || !identifiersEqual(&parser.current, &iterVar)) { endScope(); goto rollback; }
        advance();
    } else {
        endScope();
        goto rollback;
    }

    if (!match(TOKEN_RIGHT_PAREN)) { endScope(); goto rollback; }

    // Initial check: if iter >= limit (or iter > limit for <=), skip loop body
    emitBytes(OP_GET_LOCAL, iterSlot);
    emitBytes(OP_GET_LOCAL, limitSlot);
    if (isLeq) {
        emitBytes(OP_GREATER, OP_NOT);
    } else {
        emitByte(OP_LESS);
    }
    exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);

    // 4. Body
    loopBodyStart = currentChunk()->count;
    statement();

    // Superinstruction at end of body
    emitByte(isLeq ? OP_LOOP_INCR_LEQ : OP_LOOP_INCR_LESS);
    emitByte(iterSlot);
    emitByte(limitSlot);

    offset = currentChunk()->count - loopBodyStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");
    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);

    // Normal loop termination jumps over initial condition POP
    endJump = emitJump(OP_JUMP);

    // Patch initial exit jump
    patchJump(exitJump);
    emitByte(OP_POP);

    patchJump(endJump);

    endScope();
    return true;

rollback:
    parser = savedParser;
    scannerInstance = savedScanner;
    current->localCount = savedLocalCount;
    current->scopeDepth = savedScopeDepth;
    current->function->upvalueCount = savedFnUpvalueCount;
    currentChunk()->count = savedChunkCount;
    currentChunk()->constants.count = savedConstantsCount;
    return false;
}

static void forStatement() {
    if (tryOptimizeForLoop()) {
        return;
    }
    if (tryOptimizeCountedForLoop()) {
        return;
    }
    beginScope();
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
    if (match(TOKEN_SEMICOLON)) {
    } else if (checkTypeDeclaration()) {
        TypeInfo t = parseTypeSpecifier();
        varDeclaration(t);
    } else {
        expressionStatement();
    }

    int loopStart = currentChunk()->count;
    int exitJump = -1;
    if (!match(TOKEN_SEMICOLON)) {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

        exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);
    }

    if (!match(TOKEN_RIGHT_PAREN)) {
        int bodyJump = emitJump(OP_JUMP);
        int incrementStart = currentChunk()->count;
        expression();
        emitByte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

        emitLoop(loopStart);
        loopStart = incrementStart;
        patchJump(bodyJump);
    }

    statement();
    emitLoop(loopStart);

    if (exitJump != -1) {
        patchJump(exitJump);
        emitByte(OP_POP);
    }

    endScope();
}

static void ifStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();

    int elseJump = emitJump(OP_JUMP);

    patchJump(thenJump);
    emitByte(OP_POP);

    if (match(TOKEN_ELSE)) statement();
    patchJump(elseJump);
}

static void printStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINT);
}

static void returnStatement() {
    if (current->type == TYPE_SCRIPT) {
        error("Can't return from top-level code.");
    }

    if (match(TOKEN_SEMICOLON)) {
        if (current->returnType.kind != IsliType::TYPE_VOID && 
            current->returnType.kind != IsliType::TYPE_ANY && 
            current->returnType.kind != IsliType::TYPE_UNKNOWN) {
            error("Non-void function must return a value.");
        }
        emitReturn();
    } else {
        TypeInfo retType = expression();
        if (current->returnType.kind == IsliType::TYPE_VOID) {
            error("Void function cannot return a value.");
        } else if (current->returnType.kind != IsliType::TYPE_ANY && 
                   current->returnType.kind != IsliType::TYPE_UNKNOWN && 
                   !retType.matches(current->returnType)) {
            error("Return type does not match function signature.");
        }
        consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        // Close any upvalues in scopes between function root and current return point
        for (int i = current->localCount - 1; i >= 0; i--) {
            if (current->locals[i].depth > 0 && current->locals[i].isCaptured) {
                emitByte(OP_CLOSE_UPVALUE);
            }
        }
        emitByte(OP_RETURN);
    }
}

static void whileStatement() {
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

static void emitAssign(Token name) {
    uint8_t setOp;
    int arg = resolveLocal(current, &name);
    if (arg != -1) {
        setOp = OP_SET_LOCAL;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        setOp = OP_SET_UPVALUE;
    } else {
        arg = identifierConstant(&name);
        setOp = OP_SET_GLOBAL;
    }
    emitBytes(setOp, static_cast<uint8_t>(arg));
}

static void dispatchStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'dispatch'.");
    uint8_t dimCount = 0;
    do {
        expression();
        dimCount++;
        if (dimCount > 3) {
            error("Dispatch supports up to 3 dimensions (X, Y, Z).");
        }
    } while (match(TOKEN_COMMA));
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after dispatch dimensions.");

    uint8_t tileFlags = 0;
    if (match(TOKEN_TILE)) {
        consume(TOKEN_LEFT_PAREN, "Expect '(' after 'tile'.");
        expression();
        tileFlags = 1;
        if (match(TOKEN_COMMA)) {
            expression();
            tileFlags = 2;
        } else {
            emitConstant(NUMBER_VAL(1));
        }
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after tile sizes.");
        if (dimCount != 2) {
            error("tile requires 2D dispatch.");
        }
    }

    uint8_t reduceOp = 0;
    Token reduceTarget{};
    if (match(TOKEN_REDUCE)) {
        consume(TOKEN_LEFT_PAREN, "Expect '(' after 'reduce'.");
        if (match(TOKEN_PLUS)) {
            reduceOp = 1;
        } else if (match(TOKEN_STAR)) {
            reduceOp = 2;
        } else if (match(TOKEN_IDENTIFIER)) {
            if (parser.previous.length == 3 && std::memcmp(parser.previous.start, "min", 3) == 0) {
                reduceOp = 3;
            } else if (parser.previous.length == 3 && std::memcmp(parser.previous.start, "max", 3) == 0) {
                reduceOp = 4;
            } else {
                error("Unknown reduce operator (use +, *, min, max).");
            }
        } else {
            error("Expect reduce operator.");
        }
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after reduce operator.");
        consume(TOKEN_INTO, "Expect 'into' after reduce(...).");
        consume(TOKEN_IDENTIFIER, "Expect target variable.");
        reduceTarget = parser.previous;
    }

    consume(TOKEN_LEFT_BRACE, "Expect '{' before dispatch body.");

    Compiler compiler;
    initCompiler(&compiler, TYPE_FUNCTION);
    compiler.returnType = reduceOp != 0
        ? TypeInfo{IsliType::TYPE_FLOAT}
        : TypeInfo{IsliType::TYPE_VOID};
    current->function->name = copyString("<kernel>", 8);
    beginScope();

    consume(TOKEN_PIPE, "Expect '|' before loop variable(s).");
    do {
        current->function->arity++;
        if (current->function->arity > 255) {
            errorAtCurrent("Can't have more than 255 loop variables.");
        }
        uint8_t loopVar = parseVariable("Expect loop variable name.");
        defineVariable(loopVar);
    } while (match(TOKEN_COMMA));
    consume(TOKEN_PIPE, "Expect '|' after loop variable(s).");

    block();

    ObjFunction* fn = endCompiler();
    emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(fn)));

    for (int i = 0; i < fn->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }

    emitByte(OP_DISPATCH2);
    emitByte(dimCount);
    emitByte(tileFlags);
    emitByte(reduceOp);
    if (reduceOp != 0) {
        emitAssign(reduceTarget);
        emitByte(OP_POP);
    }
}

static void synchronize() {
    parser.panicMode = false;
    parser.isAddressOf = false;

    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) return;
        switch (parser.current.type) {
            case TOKEN_STRUCT:
            case TOKEN_FUN:
            case TOKEN_INT_TYPE:
            case TOKEN_FLOAT_TYPE:
            case TOKEN_DOUBLE_TYPE:
            case TOKEN_STRING_TYPE:
            case TOKEN_BOOL_TYPE:
            case TOKEN_VOID_TYPE:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
            case TOKEN_PARALLEL:
            case TOKEN_DISPATCH:
                return;
            default:
                ;
        }

        advance();
    }
}

static void declaration() {
    if (match(TOKEN_STRUCT)) {
        structDeclaration();
    } else if (match(TOKEN_FUN)) {
        funDeclaration();
    } else if (checkTypeDeclaration()) {
        Token nextToken;
        Token t1 = scannerInstance.peekToken2(&nextToken);
        if (t1.type == TOKEN_IDENTIFIER && nextToken.type == TOKEN_LEFT_PAREN) {
            TypeInfo returnType = parseTypeSpecifier();
            cFunctionDeclaration(returnType);
        } else {
            TypeInfo varType = parseTypeSpecifier();
            varDeclaration(varType);
        }
    } else {
        statement();
    }

    if (parser.panicMode) synchronize();
}

static void statement() {
    if (match(TOKEN_PRINT)) {
        printStatement();
    } else if (match(TOKEN_DISPATCH)) {
        dispatchStatement();
    } else if (match(TOKEN_PARALLEL)) {
        consume(TOKEN_LEFT_BRACE, "Expect '{' after 'parallel'.");
        beginScope();
        block();
        endScope();
    } else if (match(TOKEN_FOR)) {
        forStatement();
    } else if (match(TOKEN_IF)) {
        ifStatement();
    } else if (match(TOKEN_RETURN)) {
        returnStatement();
    } else if (match(TOKEN_WHILE)) {
        whileStatement();
    } else if (match(TOKEN_LEFT_BRACE)) {
        beginScope();
        block();
        endScope();
    } else {
        expressionStatement();
    }
}

// ── compile entry point ───────────────────────────────────────────
ObjFunction* compile(const char* source) {
    ensureRulesInitialized();
    scannerInstance.init(source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);

    parser.hadError  = false;
    parser.panicMode = false;
    parser.isAddressOf = false;

    advance();

    while (!match(TOKEN_EOF)) {
        declaration();
    }

    ObjFunction* function = endCompiler();
    return parser.hadError ? nullptr : function;
}
