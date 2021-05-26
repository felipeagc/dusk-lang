#include "dusk_internal.h"

#include <stdio.h>
#include <stdlib.h>

typedef enum TokenType {
    TOKEN_ERROR = 0,

    TOKEN_IDENT,
    TOKEN_BUILTIN_IDENT,
    TOKEN_STRING_LITERAL,
    TOKEN_INT_LITERAL,
    TOKEN_FLOAT_LITERAL,

    TOKEN_FN,
    TOKEN_LET,
    TOKEN_CONST,
    TOKEN_STRUCT,
    TOKEN_TYPE,
    TOKEN_IMPORT,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_RETURN,
    TOKEN_WHILE,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_SWITCH,
    TOKEN_TRUE,
    TOKEN_FALSE,

    TOKEN_VOID,
    TOKEN_BOOL,
    TOKEN_SCALAR_TYPE,
    TOKEN_VECTOR_TYPE,
    TOKEN_MATRIX_TYPE,

    TOKEN_LCURLY,   // {
    TOKEN_RCURLY,   // }
    TOKEN_LBRACKET, // [
    TOKEN_RBRACKET, // ]
    TOKEN_LPAREN,   // (
    TOKEN_RPAREN,   // )

    TOKEN_ADD, // +
    TOKEN_SUB, // -
    TOKEN_MUL, // *
    TOKEN_DIV, // /
    TOKEN_MOD, // %

    TOKEN_ADDADD, // ++
    TOKEN_SUBSUB, // --

    TOKEN_BITOR,  // |
    TOKEN_BITXOR, // ^
    TOKEN_BITAND, // &
    TOKEN_BITNOT, // ~

    TOKEN_LSHIFT, // <<
    TOKEN_RSHIFT, // >>

    TOKEN_DOT,      // .
    TOKEN_COMMA,    // ,
    TOKEN_QUESTION, // ?

    TOKEN_COLON,     // :
    TOKEN_SEMICOLON, // ;

    TOKEN_NOT,    // !
    TOKEN_ASSIGN, // =

    TOKEN_EQ,        // ==
    TOKEN_NOTEQ,     // !=
    TOKEN_LESS,      // <
    TOKEN_LESSEQ,    // <=
    TOKEN_GREATER,   // >
    TOKEN_GREATEREQ, // >=

    TOKEN_ADD_ASSIGN, // +=
    TOKEN_SUB_ASSIGN, // -=
    TOKEN_MUL_ASSIGN, // *=
    TOKEN_DIV_ASSIGN, // /=
    TOKEN_MOD_ASSIGN, // %=

    TOKEN_BITAND_ASSIGN, // &=
    TOKEN_BITOR_ASSIGN,  // |=
    TOKEN_BITXOR_ASSIGN, // ^=
    TOKEN_BITNOT_ASSIGN, // ~=

    TOKEN_LSHIFT_ASSIGN, // <<=
    TOKEN_RSHIFT_ASSIGN, // >>=

    TOKEN_AND, // &&
    TOKEN_OR,  // ||

    TOKEN_EOF,
} TokenType;

typedef struct Token
{
    TokenType type;
    DuskLocation location;
    union
    {
        const char *str;
        int64_t int_;
        double float_;
        DuskScalarType scalar_type;
        struct
        {
            DuskScalarType scalar_type;
            uint32_t length;
        } vector_type;
        struct
        {
            DuskScalarType scalar_type;
            uint32_t rows;
            uint32_t cols;
        } matrix_type;
    };
} Token;

typedef struct TokenizerState
{
    DuskFile *file;
    size_t pos;
    size_t line;
    size_t col;
} TokenizerState;

static const char *tokenTypeToString(TokenType token_type)
{
    switch (token_type)
    {
    case TOKEN_ERROR: return "<error>";

    case TOKEN_IDENT: return "identifier";
    case TOKEN_BUILTIN_IDENT: return "builtin identifier";
    case TOKEN_STRING_LITERAL: return "string literal";
    case TOKEN_INT_LITERAL: return "integer literal";
    case TOKEN_FLOAT_LITERAL: return "float literal";

    case TOKEN_LET: return "let";
    case TOKEN_FN: return "fn";
    case TOKEN_CONST: return "const";
    case TOKEN_STRUCT: return "struct";
    case TOKEN_TYPE: return "type";
    case TOKEN_IMPORT: return "import";
    case TOKEN_BREAK: return "break";
    case TOKEN_CONTINUE: return "continue";
    case TOKEN_RETURN: return "return";
    case TOKEN_WHILE: return "while";
    case TOKEN_IF: return "if";
    case TOKEN_ELSE: return "else";
    case TOKEN_SWITCH: return "switch";
    case TOKEN_TRUE: return "true";
    case TOKEN_FALSE: return "false";

    case TOKEN_VOID: return "void";
    case TOKEN_BOOL: return "bool";

    case TOKEN_SCALAR_TYPE: return "scalar type";
    case TOKEN_VECTOR_TYPE: return "vector type";
    case TOKEN_MATRIX_TYPE: return "matrix type";

    case TOKEN_LCURLY: return "{";
    case TOKEN_RCURLY: return "}";
    case TOKEN_LBRACKET: return "[";
    case TOKEN_RBRACKET: return "]";
    case TOKEN_LPAREN: return "(";
    case TOKEN_RPAREN: return ")";

    case TOKEN_ADD: return "+";
    case TOKEN_SUB: return "-";
    case TOKEN_MUL: return "*";
    case TOKEN_DIV: return "/";
    case TOKEN_MOD: return "%";

    case TOKEN_ADDADD: return "++";
    case TOKEN_SUBSUB: return "--";

    case TOKEN_BITOR: return "|";
    case TOKEN_BITXOR: return "^";
    case TOKEN_BITAND: return "&";
    case TOKEN_BITNOT: return "~";

    case TOKEN_LSHIFT: return "<<";
    case TOKEN_RSHIFT: return ">>";

    case TOKEN_DOT: return ".";
    case TOKEN_COMMA: return ",";
    case TOKEN_QUESTION: return "?";

    case TOKEN_COLON: return ":";
    case TOKEN_SEMICOLON: return ";";

    case TOKEN_NOT: return "!";
    case TOKEN_ASSIGN: return "=";

    case TOKEN_EQ: return "==";
    case TOKEN_NOTEQ: return "!=";
    case TOKEN_LESS: return "<";
    case TOKEN_LESSEQ: return "<=";
    case TOKEN_GREATER: return ">";
    case TOKEN_GREATEREQ: return ">=";

    case TOKEN_ADD_ASSIGN: return "+=";
    case TOKEN_SUB_ASSIGN: return "-=";
    case TOKEN_MUL_ASSIGN: return "*=";
    case TOKEN_DIV_ASSIGN: return "/=";
    case TOKEN_MOD_ASSIGN: return "%=";

    case TOKEN_BITAND_ASSIGN: return "&=";
    case TOKEN_BITOR_ASSIGN: return "|=";
    case TOKEN_BITXOR_ASSIGN: return "^=";
    case TOKEN_BITNOT_ASSIGN: return "~=";

    case TOKEN_LSHIFT_ASSIGN: return "<<=";
    case TOKEN_RSHIFT_ASSIGN: return ">>=";

    case TOKEN_AND: return "&&";
    case TOKEN_OR: return "||";

    case TOKEN_EOF: return "<eof>";
    }

    return "<unknown>";
}

static const char *tokenToString(DuskAllocator *allocator, const Token *token)
{
    switch (token->type)
    {
    case TOKEN_ERROR: return "<error>";

    case TOKEN_IDENT:
        return duskSprintf(allocator, "identifier '%s'", token->str);
    case TOKEN_BUILTIN_IDENT: return duskSprintf(allocator, "@%s", token->str);
    case TOKEN_STRING_LITERAL:
        return duskSprintf(allocator, "\"%s\"", token->str);
    case TOKEN_INT_LITERAL: return duskSprintf(allocator, "%ld", token->int_);
    case TOKEN_FLOAT_LITERAL:
        return duskSprintf(allocator, "%lf", token->float_);

    case TOKEN_LET: return "let";
    case TOKEN_FN: return "fn";
    case TOKEN_CONST: return "const";
    case TOKEN_STRUCT: return "struct";
    case TOKEN_TYPE: return "type";
    case TOKEN_IMPORT: return "import";
    case TOKEN_BREAK: return "break";
    case TOKEN_CONTINUE: return "continue";
    case TOKEN_RETURN: return "return";
    case TOKEN_WHILE: return "while";
    case TOKEN_IF: return "if";
    case TOKEN_ELSE: return "else";
    case TOKEN_SWITCH: return "switch";
    case TOKEN_TRUE: return "true";
    case TOKEN_FALSE: return "false";

    case TOKEN_VOID: return "void";
    case TOKEN_BOOL: return "bool";

    case TOKEN_SCALAR_TYPE: {
        switch (token->scalar_type)
        {
        case DUSK_SCALAR_TYPE_FLOAT: return "float";
        case DUSK_SCALAR_TYPE_DOUBLE: return "double";
        case DUSK_SCALAR_TYPE_INT: return "int";
        case DUSK_SCALAR_TYPE_UINT: return "uint";
        }
        break;
    }

    case TOKEN_VECTOR_TYPE: {
        const char *scalar_type = "";
        switch (token->vector_type.scalar_type)
        {
        case DUSK_SCALAR_TYPE_FLOAT: scalar_type = "float"; break;
        case DUSK_SCALAR_TYPE_DOUBLE: scalar_type = "double"; break;
        case DUSK_SCALAR_TYPE_INT: scalar_type = "int"; break;
        case DUSK_SCALAR_TYPE_UINT: scalar_type = "uint"; break;
        }

        return duskSprintf(
            allocator, "%s%u", scalar_type, token->vector_type.length);
    }

    case TOKEN_MATRIX_TYPE: {
        const char *scalar_type = "";
        switch (token->matrix_type.scalar_type)
        {
        case DUSK_SCALAR_TYPE_FLOAT: scalar_type = "float"; break;
        case DUSK_SCALAR_TYPE_DOUBLE: scalar_type = "double"; break;
        case DUSK_SCALAR_TYPE_INT: scalar_type = "int"; break;
        case DUSK_SCALAR_TYPE_UINT: scalar_type = "uint"; break;
        }

        return duskSprintf(
            allocator,
            "%s%ux%u",
            scalar_type,
            token->matrix_type.cols,
            token->matrix_type.rows);
    }

    case TOKEN_LCURLY: return "{";
    case TOKEN_RCURLY: return "}";
    case TOKEN_LBRACKET: return "[";
    case TOKEN_RBRACKET: return "]";
    case TOKEN_LPAREN: return "(";
    case TOKEN_RPAREN: return ")";

    case TOKEN_ADD: return "+";
    case TOKEN_SUB: return "-";
    case TOKEN_MUL: return "*";
    case TOKEN_DIV: return "/";
    case TOKEN_MOD: return "%";

    case TOKEN_ADDADD: return "++";
    case TOKEN_SUBSUB: return "--";

    case TOKEN_BITOR: return "|";
    case TOKEN_BITXOR: return "^";
    case TOKEN_BITAND: return "&";
    case TOKEN_BITNOT: return "~";

    case TOKEN_LSHIFT: return "<<";
    case TOKEN_RSHIFT: return ">>";

    case TOKEN_DOT: return ".";
    case TOKEN_COMMA: return ",";
    case TOKEN_QUESTION: return "?";

    case TOKEN_COLON: return ":";
    case TOKEN_SEMICOLON: return ";";

    case TOKEN_NOT: return "!";
    case TOKEN_ASSIGN: return "=";

    case TOKEN_EQ: return "==";
    case TOKEN_NOTEQ: return "!=";
    case TOKEN_LESS: return "<";
    case TOKEN_LESSEQ: return "<=";
    case TOKEN_GREATER: return ">";
    case TOKEN_GREATEREQ: return ">=";

    case TOKEN_ADD_ASSIGN: return "+=";
    case TOKEN_SUB_ASSIGN: return "-=";
    case TOKEN_MUL_ASSIGN: return "*=";
    case TOKEN_DIV_ASSIGN: return "/=";
    case TOKEN_MOD_ASSIGN: return "%=";

    case TOKEN_BITAND_ASSIGN: return "&=";
    case TOKEN_BITOR_ASSIGN: return "|=";
    case TOKEN_BITXOR_ASSIGN: return "^=";
    case TOKEN_BITNOT_ASSIGN: return "~=";

    case TOKEN_LSHIFT_ASSIGN: return "<<=";
    case TOKEN_RSHIFT_ASSIGN: return ">>=";

    case TOKEN_AND: return "&&";
    case TOKEN_OR: return "||";

    case TOKEN_EOF: return "<eof>";
    }

    return "<unknown>";
}

DUSK_INLINE static int64_t
tokenizerLengthLeft(TokenizerState state, size_t offset)
{
    return ((int64_t)state.file->text_length) - (int64_t)(state.pos + offset);
}

DUSK_INLINE static bool isWhitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

DUSK_INLINE static bool isAlpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '_');
}

DUSK_INLINE static bool isAlphaNum(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '_') ||
           (c >= '0' && c <= '9');
}

DUSK_INLINE static bool isHex(char c)
{
    return (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') ||
           (c >= '0' && c <= '9');
}

DUSK_INLINE static bool isNum(char c)
{
    return (c >= '0' && c <= '9');
}

static TokenizerState tokenizerCreate(DuskFile *file)
{
    TokenizerState state = {0};
    state.file = file;
    state.pos = 0;
    state.line = 1;
    state.col = 1;
    return state;
}

static TokenizerState
tokenizerNextToken(DuskAllocator *allocator, TokenizerState state, Token *token)
{
begin:
    (void)allocator;
    *token = (Token){0};

    // Skip whitespace
    for (size_t i = state.pos; i < state.file->text_length; ++i)
    {
        if (isWhitespace(state.file->text[i]))
        {
            state.pos++;
            state.col++;
            if (state.file->text[i] == '\n')
            {
                state.line++;
                state.col = 1;
            }
        }
        else
            break;
    }

    token->location.offset = state.pos;
    token->location.line = state.line;
    token->location.col = state.col;
    token->location.length = 1;
    token->location.file = state.file;

    if (tokenizerLengthLeft(state, 0) <= 0)
    {
        token->type = TOKEN_EOF;
        return state;
    }

    char c = state.file->text[state.pos];
    switch (c)
    {
    case '\"': {
        // String
        state.pos++;

        const char *string = &state.file->text[state.pos];

        size_t content_length = 0;
        while (tokenizerLengthLeft(state, content_length) > 0 &&
               state.file->text[state.pos + content_length] != '\"')
        {
            content_length++;
        }

        state.pos += content_length;

        if (tokenizerLengthLeft(state, 0) > 0 &&
            state.file->text[state.pos] == '\"')
        {
            state.pos++;
        }
        else
        {
            token->type = TOKEN_ERROR;
            token->str = duskStrdup(allocator, "unclosed string");
            break;
        }

        token->type = TOKEN_STRING_LITERAL;
        token->str = duskNullTerminate(allocator, string, content_length);

        break;
    }

    case '{':
        state.pos++;
        token->type = TOKEN_LCURLY;
        break;
    case '}':
        state.pos++;
        token->type = TOKEN_RCURLY;
        break;
    case '[':
        state.pos++;
        token->type = TOKEN_LBRACKET;
        break;
    case ']':
        state.pos++;
        token->type = TOKEN_RBRACKET;
        break;
    case '(':
        state.pos++;
        token->type = TOKEN_LPAREN;
        break;
    case ')':
        state.pos++;
        token->type = TOKEN_RPAREN;
        break;

    case '=': {
        state.pos++;
        token->type = TOKEN_ASSIGN;
        if (tokenizerLengthLeft(state, 0) > 0 &&
            state.file->text[state.pos] == '=')
        {
            state.pos++;
            token->type = TOKEN_EQ;
        }
        break;
    }

    case '+': {
        state.pos++;
        token->type = TOKEN_ADD;
        if (tokenizerLengthLeft(state, 0) > 0)
        {
            switch (state.file->text[state.pos])
            {
            case '=':
                state.pos++;
                token->type = TOKEN_ADD_ASSIGN;
                break;
            case '+':
                state.pos++;
                token->type = TOKEN_ADDADD;
                break;
            default: break;
            }
        }
        break;
    }

    case '-': {
        state.pos++;
        token->type = TOKEN_SUB;
        if (tokenizerLengthLeft(state, 0) > 0)
        {
            switch (state.file->text[state.pos])
            {
            case '=':
                state.pos++;
                token->type = TOKEN_SUB_ASSIGN;
                break;
            case '-':
                state.pos++;
                token->type = TOKEN_SUBSUB;
                break;
            default: break;
            }
        }
        break;
    }

    case '*': {
        state.pos++;
        token->type = TOKEN_MUL;
        if (tokenizerLengthLeft(state, 0) > 0 &&
            state.file->text[state.pos] == '=')
        {
            state.pos++;
            token->type = TOKEN_MUL_ASSIGN;
        }
        break;
    }

    case '/': {
        state.pos++;
        token->type = TOKEN_DIV;
        if (tokenizerLengthLeft(state, 0) > 0)
        {
            switch (state.file->text[state.pos])
            {
            case '=':
                state.pos++;
                token->type = TOKEN_DIV_ASSIGN;
                break;
            case '/':
                state.pos++;
                while (tokenizerLengthLeft(state, 0) > 0 &&
                       state.file->text[state.pos] != '\n')
                {
                    state.pos++;
                }
                goto begin;
                break;
            default: break;
            }
        }
        break;
    }

    case '%': {
        state.pos++;
        token->type = TOKEN_MOD;
        if (tokenizerLengthLeft(state, 0) > 0 &&
            state.file->text[state.pos] == '=')
        {
            state.pos++;
            token->type = TOKEN_MOD_ASSIGN;
        }
        break;
    }

    case '|': {
        state.pos++;
        token->type = TOKEN_BITOR;
        if (tokenizerLengthLeft(state, 0) > 0)
        {
            switch (state.file->text[state.pos])
            {
            case '=':
                state.pos++;
                token->type = TOKEN_BITOR_ASSIGN;
                break;
            case '|':
                state.pos++;
                token->type = TOKEN_OR;
                break;
            default: break;
            }
        }
        break;
    }

    case '&': {
        state.pos++;
        token->type = TOKEN_BITAND;
        if (tokenizerLengthLeft(state, 0) > 0)
        {
            switch (state.file->text[state.pos])
            {
            case '=':
                state.pos++;
                token->type = TOKEN_BITAND_ASSIGN;
                break;
            case '&':
                state.pos++;
                token->type = TOKEN_AND;
                break;
            default: break;
            }
        }
        break;
    }

    case '^': {
        state.pos++;
        token->type = TOKEN_BITXOR;
        if (tokenizerLengthLeft(state, 0) > 0 &&
            state.file->text[state.pos] == '=')
        {
            state.pos++;
            token->type = TOKEN_BITXOR_ASSIGN;
        }
        break;
    }

    case '~': {
        state.pos++;
        token->type = TOKEN_BITNOT;
        if (tokenizerLengthLeft(state, 0) > 0 &&
            state.file->text[state.pos] == '=')
        {
            state.pos++;
            token->type = TOKEN_BITNOT_ASSIGN;
        }
        break;
    }

    case '!': {
        state.pos++;
        token->type = TOKEN_NOT;
        if (tokenizerLengthLeft(state, 0) > 0 &&
            state.file->text[state.pos] == '=')
        {
            state.pos++;
            token->type = TOKEN_NOTEQ;
        }
        break;
    }

    case '<': {
        state.pos++;
        token->type = TOKEN_LESS;
        if (tokenizerLengthLeft(state, 0) > 0)
        {
            switch (state.file->text[state.pos])
            {
            case '=':
                state.pos++;
                token->type = TOKEN_LESSEQ;
                break;
            case '<':
                state.pos++;
                token->type = TOKEN_LSHIFT;
                if (tokenizerLengthLeft(state, 0) > 0 &&
                    state.file->text[state.pos] == '=')
                {
                    token->type = TOKEN_LSHIFT_ASSIGN;
                }
                break;
            default: break;
            }
        }
        break;
    }

    case '>': {
        state.pos++;
        token->type = TOKEN_GREATER;
        if (tokenizerLengthLeft(state, 0) > 0)
        {
            switch (state.file->text[state.pos])
            {
            case '=':
                state.pos++;
                token->type = TOKEN_GREATEREQ;
                break;
            case '>':
                state.pos++;
                token->type = TOKEN_RSHIFT;
                if (tokenizerLengthLeft(state, 0) > 0 &&
                    state.file->text[state.pos] == '=')
                {
                    token->type = TOKEN_RSHIFT_ASSIGN;
                }
                break;
            default: break;
            }
        }
        break;
    }

    case ':':
        state.pos++;
        token->type = TOKEN_COLON;
        break;
    case ';':
        state.pos++;
        token->type = TOKEN_SEMICOLON;
        break;
    case '.':
        state.pos++;
        token->type = TOKEN_DOT;
        break;
    case ',':
        state.pos++;
        token->type = TOKEN_COMMA;
        break;
    case '?':
        state.pos++;
        token->type = TOKEN_QUESTION;
        break;

    default: {
        if (isAlpha(c))
        {
            // Identifier
            size_t ident_length = 0;
            while (tokenizerLengthLeft(state, ident_length) > 0 &&
                   isAlphaNum(state.file->text[state.pos + ident_length]))
            {
                ident_length++;
            }

            const char *ident_start = &state.file->text[state.pos];

            switch (ident_start[0])
            {
            case 'f': {
                if (ident_length == 2 &&
                    strncmp(ident_start, "fn", ident_length) == 0)
                {
                    token->type = TOKEN_FN;
                }
                else if (ident_length == 5)
                {
                    if (strncmp(ident_start, "float", ident_length) == 0)
                    {
                        token->type = TOKEN_SCALAR_TYPE;
                        token->scalar_type = DUSK_SCALAR_TYPE_FLOAT;
                    }
                    else if (strncmp(ident_start, "false", ident_length) == 0)
                    {
                        token->type = TOKEN_FALSE;
                    }
                }
                else if (ident_length == 6)
                {
                    if (strncmp(ident_start, "float2", ident_length) == 0)
                    {
                        token->type = TOKEN_VECTOR_TYPE;
                        token->vector_type.scalar_type = DUSK_SCALAR_TYPE_FLOAT;
                        token->vector_type.length = 2;
                    }
                    else if (strncmp(ident_start, "float3", ident_length) == 0)
                    {
                        token->type = TOKEN_VECTOR_TYPE;
                        token->vector_type.scalar_type = DUSK_SCALAR_TYPE_FLOAT;
                        token->vector_type.length = 3;
                    }
                    else if (strncmp(ident_start, "float4", ident_length) == 0)
                    {
                        token->type = TOKEN_VECTOR_TYPE;
                        token->vector_type.scalar_type = DUSK_SCALAR_TYPE_FLOAT;
                        token->vector_type.length = 4;
                    }
                }
                else if (ident_length == 8)
                {
                    if (strncmp(ident_start, "float2x2", ident_length) == 0)
                    {
                        token->type = TOKEN_MATRIX_TYPE;
                        token->matrix_type.scalar_type = DUSK_SCALAR_TYPE_FLOAT;
                        token->matrix_type.cols = 2;
                        token->matrix_type.rows = 2;
                    }
                    else if (
                        strncmp(ident_start, "float3x3", ident_length) == 0)
                    {
                        token->type = TOKEN_MATRIX_TYPE;
                        token->matrix_type.scalar_type = DUSK_SCALAR_TYPE_FLOAT;
                        token->matrix_type.cols = 3;
                        token->matrix_type.rows = 3;
                    }
                    else if (
                        strncmp(ident_start, "float4x4", ident_length) == 0)
                    {
                        token->type = TOKEN_MATRIX_TYPE;
                        token->matrix_type.scalar_type = DUSK_SCALAR_TYPE_FLOAT;
                        token->matrix_type.cols = 4;
                        token->matrix_type.rows = 4;
                    }
                }
                break;
            }
            case 'd': {
                if (ident_length == 6 &&
                    strncmp(ident_start, "double", ident_length) == 0)
                {
                    token->type = TOKEN_SCALAR_TYPE;
                    token->scalar_type = DUSK_SCALAR_TYPE_DOUBLE;
                }
                else if (ident_length == 7)
                {
                    if (strncmp(ident_start, "double2", ident_length) == 0)
                    {
                        token->type = TOKEN_VECTOR_TYPE;
                        token->vector_type.scalar_type =
                            DUSK_SCALAR_TYPE_DOUBLE;
                        token->vector_type.length = 2;
                    }
                    else if (strncmp(ident_start, "double3", ident_length) == 0)
                    {
                        token->type = TOKEN_VECTOR_TYPE;
                        token->vector_type.scalar_type =
                            DUSK_SCALAR_TYPE_DOUBLE;
                        token->vector_type.length = 3;
                    }
                    else if (strncmp(ident_start, "double4", ident_length) == 0)
                    {
                        token->type = TOKEN_VECTOR_TYPE;
                        token->vector_type.scalar_type =
                            DUSK_SCALAR_TYPE_DOUBLE;
                        token->vector_type.length = 4;
                    }
                }
                else if (ident_length == 9)
                {
                    if (strncmp(ident_start, "double2x2", ident_length) == 0)
                    {
                        token->type = TOKEN_MATRIX_TYPE;
                        token->matrix_type.scalar_type =
                            DUSK_SCALAR_TYPE_DOUBLE;
                        token->matrix_type.cols = 2;
                        token->matrix_type.rows = 2;
                    }
                    else if (
                        strncmp(ident_start, "double3x3", ident_length) == 0)
                    {
                        token->type = TOKEN_MATRIX_TYPE;
                        token->matrix_type.scalar_type =
                            DUSK_SCALAR_TYPE_DOUBLE;
                        token->matrix_type.cols = 3;
                        token->matrix_type.rows = 3;
                    }
                    else if (
                        strncmp(ident_start, "double4x4", ident_length) == 0)
                    {
                        token->type = TOKEN_MATRIX_TYPE;
                        token->matrix_type.scalar_type =
                            DUSK_SCALAR_TYPE_DOUBLE;
                        token->matrix_type.cols = 4;
                        token->matrix_type.rows = 4;
                    }
                }
                break;
            }
            case 'l': {
                if (ident_length == 3 &&
                    strncmp(ident_start, "let", ident_length) == 0)
                {
                    token->type = TOKEN_LET;
                }
                break;
            }
            case 'c': {
                if (ident_length == 5 &&
                    strncmp(ident_start, "const", ident_length) == 0)
                {
                    token->type = TOKEN_CONST;
                }
                else if (
                    ident_length == 8 &&
                    strncmp(ident_start, "continue", ident_length) == 0)
                {
                    token->type = TOKEN_CONTINUE;
                }
                break;
            }
            case 'b': {
                if (ident_length == 5 &&
                    strncmp(ident_start, "break", ident_length) == 0)
                {
                    token->type = TOKEN_BREAK;
                }
                else if (
                    ident_length == 4 &&
                    strncmp(ident_start, "bool", ident_length) == 0)
                {
                    token->type = TOKEN_BOOL;
                }
                break;
            }
            case 'r': {
                if (ident_length == 6 &&
                    strncmp(ident_start, "return", ident_length) == 0)
                {
                    token->type = TOKEN_RETURN;
                }
                break;
            }
            case 'w': {
                if (ident_length == 5 &&
                    strncmp(ident_start, "while", ident_length) == 0)
                {
                    token->type = TOKEN_WHILE;
                }
                break;
            }
            case 'e': {
                if (ident_length == 4 &&
                    strncmp(ident_start, "else", ident_length) == 0)
                {
                    token->type = TOKEN_ELSE;
                }
                break;
            }
            case 's': {
                if (ident_length == 6 &&
                    strncmp(ident_start, "struct", ident_length) == 0)
                {
                    token->type = TOKEN_STRUCT;
                }
                else if (
                    ident_length == 6 &&
                    strncmp(ident_start, "switch", ident_length) == 0)
                {
                    token->type = TOKEN_SWITCH;
                }
                break;
            }
            case 't': {
                if (ident_length == 4 &&
                    strncmp(ident_start, "type", ident_length) == 0)
                {
                    token->type = TOKEN_TYPE;
                }
                else if (
                    ident_length == 4 &&
                    strncmp(ident_start, "true", ident_length) == 0)
                {
                    token->type = TOKEN_TRUE;
                }
                break;
            }
            case 'v': {
                if (ident_length == 4 &&
                    strncmp(ident_start, "void", ident_length) == 0)
                {
                    token->type = TOKEN_VOID;
                }
                break;
            }
            case 'i': {
                if (ident_length == 2 &&
                    strncmp(ident_start, "if", ident_length) == 0)
                {
                    token->type = TOKEN_IF;
                }
                else if (
                    ident_length == 6 &&
                    strncmp(ident_start, "import", ident_length) == 0)
                {
                    token->type = TOKEN_IMPORT;
                }
                else if (
                    ident_length == 3 &&
                    strncmp(ident_start, "int", ident_length) == 0)
                {
                    token->type = TOKEN_SCALAR_TYPE;
                    token->scalar_type = DUSK_SCALAR_TYPE_INT;
                }
                else if (ident_length == 4)
                {
                    if (strncmp(ident_start, "int2", ident_length) == 0)
                    {
                        token->type = TOKEN_VECTOR_TYPE;
                        token->vector_type.scalar_type = DUSK_SCALAR_TYPE_INT;
                        token->vector_type.length = 2;
                    }
                    else if (strncmp(ident_start, "int3", ident_length) == 0)
                    {
                        token->type = TOKEN_VECTOR_TYPE;
                        token->vector_type.scalar_type = DUSK_SCALAR_TYPE_INT;
                        token->vector_type.length = 3;
                    }
                    else if (strncmp(ident_start, "int4", ident_length) == 0)
                    {
                        token->type = TOKEN_VECTOR_TYPE;
                        token->vector_type.scalar_type = DUSK_SCALAR_TYPE_INT;
                        token->vector_type.length = 4;
                    }
                }
                else if (ident_length == 6)
                {
                    if (strncmp(ident_start, "int2x2", ident_length) == 0)
                    {
                        token->type = TOKEN_MATRIX_TYPE;
                        token->matrix_type.scalar_type = DUSK_SCALAR_TYPE_INT;
                        token->matrix_type.cols = 2;
                        token->matrix_type.rows = 2;
                    }
                    else if (strncmp(ident_start, "int3x3", ident_length) == 0)
                    {
                        token->type = TOKEN_MATRIX_TYPE;
                        token->matrix_type.scalar_type = DUSK_SCALAR_TYPE_INT;
                        token->matrix_type.cols = 3;
                        token->matrix_type.rows = 3;
                    }
                    else if (strncmp(ident_start, "int4x4", ident_length) == 0)
                    {
                        token->type = TOKEN_MATRIX_TYPE;
                        token->matrix_type.scalar_type = DUSK_SCALAR_TYPE_INT;
                        token->matrix_type.cols = 4;
                        token->matrix_type.rows = 4;
                    }
                }
                break;
            }
            case 'u': {
                if (ident_length == 4 &&
                    strncmp(ident_start, "uint", ident_length) == 0)
                {
                    token->type = TOKEN_SCALAR_TYPE;
                    token->scalar_type = DUSK_SCALAR_TYPE_UINT;
                }
                else if (ident_length == 5)
                {
                    if (strncmp(ident_start, "uint2", ident_length) == 0)
                    {
                        token->type = TOKEN_VECTOR_TYPE;
                        token->vector_type.scalar_type = DUSK_SCALAR_TYPE_UINT;
                        token->vector_type.length = 2;
                    }
                    else if (strncmp(ident_start, "uint3", ident_length) == 0)
                    {
                        token->type = TOKEN_VECTOR_TYPE;
                        token->vector_type.scalar_type = DUSK_SCALAR_TYPE_UINT;
                        token->vector_type.length = 3;
                    }
                    else if (strncmp(ident_start, "uint4", ident_length) == 0)
                    {
                        token->type = TOKEN_VECTOR_TYPE;
                        token->vector_type.scalar_type = DUSK_SCALAR_TYPE_UINT;
                        token->vector_type.length = 4;
                    }
                }
                else if (ident_length == 7)
                {
                    if (strncmp(ident_start, "uint2x2", ident_length) == 0)
                    {
                        token->type = TOKEN_MATRIX_TYPE;
                        token->matrix_type.scalar_type = DUSK_SCALAR_TYPE_UINT;
                        token->matrix_type.cols = 2;
                        token->matrix_type.rows = 2;
                    }
                    else if (strncmp(ident_start, "uint3x3", ident_length) == 0)
                    {
                        token->type = TOKEN_MATRIX_TYPE;
                        token->matrix_type.scalar_type = DUSK_SCALAR_TYPE_UINT;
                        token->matrix_type.cols = 3;
                        token->matrix_type.rows = 3;
                    }
                    else if (strncmp(ident_start, "uint4x4", ident_length) == 0)
                    {
                        token->type = TOKEN_MATRIX_TYPE;
                        token->matrix_type.scalar_type = DUSK_SCALAR_TYPE_UINT;
                        token->matrix_type.cols = 4;
                        token->matrix_type.rows = 4;
                    }
                }
            }
            default: break;
            }

            if (token->type == 0)
            {
                token->type = TOKEN_IDENT;
                token->str =
                    duskNullTerminate(allocator, ident_start, ident_length);
            }

            state.pos += ident_length;
        }
        else if (c == '@' && isAlpha(state.file->text[state.pos + 1]))
        {
            // Builtin Identifier
            state.pos++;
            size_t ident_length = 0;
            while (tokenizerLengthLeft(state, ident_length) > 0 &&
                   isAlphaNum(state.file->text[state.pos + ident_length]))
            {
                ident_length++;
            }

            const char *ident_start = &state.file->text[state.pos];

            token->type = TOKEN_BUILTIN_IDENT;
            token->str =
                duskNullTerminate(allocator, ident_start, ident_length);
            state.pos += ident_length;
        }
        else if (isNum(c))
        {
            if (tokenizerLengthLeft(state, 0) >= 3 &&
                state.file->text[state.pos] == '0' &&
                state.file->text[state.pos + 1] == 'x' &&
                isHex(state.file->text[state.pos + 2]))
            {
                token->type = TOKEN_INT_LITERAL;
                state.pos += 2;

                size_t number_length = 0;
                while (tokenizerLengthLeft(state, number_length) > 0 &&
                       isHex(state.file->text[state.pos + number_length]))
                {
                    number_length++;
                }

                const char *int_str = duskNullTerminate(
                    allocator, &state.file->text[state.pos], number_length);
                state.pos += number_length;
                token->int_ = strtol(int_str, NULL, 16);
            }
            else
            {
                token->type = TOKEN_INT_LITERAL;

                size_t number_length = 0;
                while (tokenizerLengthLeft(state, number_length) > 0 &&
                       isNum(state.file->text[state.pos + number_length]))
                {
                    number_length++;
                }

                if (tokenizerLengthLeft(state, number_length) > 1 &&
                    state.file->text[state.pos + number_length] == '.' &&
                    isNum(state.file->text[state.pos + number_length + 1]))
                {
                    token->type = TOKEN_FLOAT_LITERAL;
                    number_length++;
                }

                while (tokenizerLengthLeft(state, number_length) > 0 &&
                       isNum(state.file->text[state.pos + number_length]))
                {
                    number_length++;
                }

                switch (token->type)
                {
                case TOKEN_INT_LITERAL: {
                    const char *int_str = duskNullTerminate(
                        allocator, &state.file->text[state.pos], number_length);
                    token->int_ = strtol(int_str, NULL, 10);
                    break;
                }
                case TOKEN_FLOAT_LITERAL: {
                    const char *float_str = duskNullTerminate(
                        allocator, &state.file->text[state.pos], number_length);
                    token->float_ = strtod(float_str, NULL);
                    break;
                }
                default: DUSK_ASSERT(0); break;
                }

                state.pos += number_length;
            }
        }
        else
        {
            token->type = TOKEN_ERROR;
            token->str = duskSprintf(
                allocator, "unknown token: '%c'", state.file->text[state.pos]);
            state.pos++;
        }
        break;
    }
    }

    token->location.length = state.pos - token->location.offset;
    state.col += token->location.length;

    return state;
}

static Token consumeToken(
    DuskCompiler *compiler, TokenizerState *state, TokenType token_type)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    Token token = {0};
    *state = tokenizerNextToken(allocator, *state, &token);
    if (token.type != token_type)
    {
        duskAddError(
            compiler,
            token.location,
            "unexpected token: '%s', expecting '%s'",
            tokenToString(allocator, &token),
            tokenTypeToString(token_type));
        duskThrow(compiler);
    }
    return token;
}

static DuskExpr *parseExpr(DuskCompiler *compiler, TokenizerState *state);

static DuskExpr *parsePrimaryExpr(DuskCompiler *compiler, TokenizerState *state)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    DuskExpr *expr = DUSK_NEW(allocator, DuskExpr);

    Token token = {0};
    *state = tokenizerNextToken(allocator, *state, &token);
    expr->location = token.location;

    switch (token.type)
    {
    case TOKEN_VOID: {
        expr->kind = DUSK_EXPR_VOID_TYPE;
        break;
    }
    case TOKEN_BOOL: {
        expr->kind = DUSK_EXPR_BOOL_TYPE;
        break;
    }
    case TOKEN_SCALAR_TYPE: {
        expr->kind = DUSK_EXPR_SCALAR_TYPE;
        expr->scalar_type = token.scalar_type;
        break;
    }
    case TOKEN_VECTOR_TYPE: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = token.vector_type.scalar_type;
        expr->vector_type.length = token.vector_type.length;
        break;
    }
    case TOKEN_MATRIX_TYPE: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = token.matrix_type.scalar_type;
        expr->matrix_type.rows = token.matrix_type.rows;
        expr->matrix_type.cols = token.matrix_type.cols;
        break;
    }
    case TOKEN_INT_LITERAL: {
        expr->kind = DUSK_EXPR_INT_LITERAL;
        expr->int_literal = token.int_;
        break;
    }
    case TOKEN_FLOAT_LITERAL: {
        expr->kind = DUSK_EXPR_FLOAT_LITERAL;
        expr->int_literal = token.float_;
        break;
    }
    case TOKEN_TRUE: {
        expr->kind = DUSK_EXPR_BOOL_LITERAL;
        expr->bool_literal = true;
        break;
    }
    case TOKEN_FALSE: {
        expr->kind = DUSK_EXPR_BOOL_LITERAL;
        expr->bool_literal = false;
        break;
    }
    case TOKEN_IDENT: {
        expr->kind = DUSK_EXPR_IDENT;
        expr->identifier.str = token.str;
        break;
    }
    case TOKEN_STRING_LITERAL: {
        expr->kind = DUSK_EXPR_STRING_LITERAL;
        expr->string.str = token.str;
        break;
    }
    case TOKEN_LPAREN: {
        expr = parseExpr(compiler, state);
        consumeToken(compiler, state, TOKEN_RPAREN);
        break;
    }
    case TOKEN_LBRACKET: {
        expr->kind = DUSK_EXPR_RUNTIME_ARRAY_TYPE;

        tokenizerNextToken(allocator, *state, &token);
        if (token.type != TOKEN_RBRACKET)
        {
            expr->kind = DUSK_EXPR_ARRAY_TYPE;
            expr->array_type.size_expr = parseExpr(compiler, state);
        }

        consumeToken(compiler, state, TOKEN_RBRACKET);

        expr->array_type.sub_expr = parseExpr(compiler, state);
        break;
    }
    case TOKEN_STRUCT: {
        expr->kind = DUSK_EXPR_STRUCT_TYPE;
        expr->struct_type.field_type_exprs =
            duskArrayCreate(allocator, DuskExpr *);
        expr->struct_type.field_names =
            duskArrayCreate(allocator, const char *);

        consumeToken(compiler, state, TOKEN_LCURLY);

        Token next_token = {0};
        tokenizerNextToken(allocator, *state, &next_token);
        while (next_token.type != TOKEN_RCURLY)
        {
            Token field_name_token = consumeToken(compiler, state, TOKEN_IDENT);
            consumeToken(compiler, state, TOKEN_COLON);

            DuskExpr *type_expr = parseExpr(compiler, state);

            duskArrayPush(&expr->struct_type.field_type_exprs, type_expr);
            duskArrayPush(&expr->struct_type.field_names, field_name_token.str);

            tokenizerNextToken(allocator, *state, &next_token);
            if (next_token.type != TOKEN_RCURLY)
            {
                consumeToken(compiler, state, TOKEN_COMMA);
                tokenizerNextToken(allocator, *state, &next_token);
            }
        }

        consumeToken(compiler, state, TOKEN_RCURLY);
        break;
    }
    case TOKEN_BUILTIN_IDENT: {
        expr->kind = DUSK_EXPR_BUILTIN_FUNCTION_CALL;
        expr->builtin_call.params = duskArrayCreate(allocator, DuskExpr *);

        if (strcmp(token.str, "Sampler") == 0)
        {
            expr->builtin_call.kind = DUSK_BUILTIN_FUNCTION_SAMPLER_TYPE;
        }
        else if (strcmp(token.str, "Image1D") == 0)
        {
            expr->builtin_call.kind = DUSK_BUILTIN_FUNCTION_IMAGE_1D_TYPE;
        }
        else if (strcmp(token.str, "Image2D") == 0)
        {
            expr->builtin_call.kind = DUSK_BUILTIN_FUNCTION_IMAGE_2D_TYPE;
        }
        else if (strcmp(token.str, "Image2DArray") == 0)
        {
            expr->builtin_call.kind = DUSK_BUILTIN_FUNCTION_IMAGE_2D_ARRAY_TYPE;
        }
        else if (strcmp(token.str, "Image3D") == 0)
        {
            expr->builtin_call.kind = DUSK_BUILTIN_FUNCTION_IMAGE_3D_TYPE;
        }
        else if (strcmp(token.str, "ImageCube") == 0)
        {
            expr->builtin_call.kind = DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_TYPE;
        }
        else if (strcmp(token.str, "ImageCubeArray") == 0)
        {
            expr->builtin_call.kind =
                DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_ARRAY_TYPE;
        }
        else
        {
            duskAddError(
                compiler,
                token.location,
                "invalid builtin identifier: %s does not exist",
                tokenToString(allocator, &token));
            duskThrow(compiler);
        }

        consumeToken(compiler, state, TOKEN_LPAREN);

        Token next_token = {0};
        tokenizerNextToken(allocator, *state, &next_token);
        while (next_token.type != TOKEN_RPAREN)
        {
            DuskExpr *param_expr = parseExpr(compiler, state);
            duskArrayPush(&expr->builtin_call.params, param_expr);

            tokenizerNextToken(allocator, *state, &next_token);
            if (next_token.type != TOKEN_RPAREN)
            {
                consumeToken(compiler, state, TOKEN_COMMA);
                tokenizerNextToken(allocator, *state, &next_token);
            }
        }

        consumeToken(compiler, state, TOKEN_RPAREN);
        break;
    }
    default: {
        duskAddError(
            compiler,
            token.location,
            "unexpected token: %s, expecting primary expression",
            tokenToString(allocator, &token));
        duskThrow(compiler);
        break;
    }
    }

    return expr;
}

static DuskExpr *parseExpr(DuskCompiler *compiler, TokenizerState *state)
{
    return parsePrimaryExpr(compiler, state);
}

static DuskStmt *parseStmt(DuskCompiler *compiler, TokenizerState *state)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    DuskStmt *stmt = DUSK_NEW(allocator, DuskStmt);

    Token next_token = {0};
    tokenizerNextToken(allocator, *state, &next_token);
    stmt->location = next_token.location;

    switch (next_token.type)
    {
    case TOKEN_LET: {
        consumeToken(compiler, state, TOKEN_LET);

        DuskDecl *decl = DUSK_NEW(allocator, DuskDecl);

        Token name_token = consumeToken(compiler, state, TOKEN_IDENT);

        DuskExpr *value_expr = NULL;

        consumeToken(compiler, state, TOKEN_COLON);
        DuskExpr *type_expr = parseExpr(compiler, state);

        tokenizerNextToken(allocator, *state, &next_token);
        if (next_token.type == TOKEN_ASSIGN)
        {
            consumeToken(compiler, state, TOKEN_ASSIGN);
            value_expr = parseExpr(compiler, state);
        }

        decl->kind = DUSK_DECL_VAR;
        decl->location = stmt->location;
        decl->name = name_token.str;
        decl->var.storage_class = DUSK_STORAGE_CLASS_FUNCTION;
        decl->var.type_expr = type_expr;
        decl->var.value_expr = value_expr;

        stmt->kind = DUSK_STMT_DECL;
        stmt->decl = decl;

        consumeToken(compiler, state, TOKEN_SEMICOLON);
        break;
    }

    case TOKEN_LCURLY: {
        stmt->kind = DUSK_STMT_BLOCK;
        stmt->block.stmts = duskArrayCreate(allocator, DuskStmt *);

        consumeToken(compiler, state, TOKEN_LCURLY);

        tokenizerNextToken(allocator, *state, &next_token);
        while (next_token.type != TOKEN_RCURLY)
        {
            DuskStmt *sub_stmt = parseStmt(compiler, state);
            duskArrayPush(&stmt->block.stmts, sub_stmt);

            tokenizerNextToken(allocator, *state, &next_token);
        }

        consumeToken(compiler, state, TOKEN_RCURLY);
        break;
    }

    case TOKEN_RETURN: {
        stmt->kind = DUSK_STMT_RETURN;
        stmt->return_.expr = NULL;

        consumeToken(compiler, state, TOKEN_RETURN);

        tokenizerNextToken(allocator, *state, &next_token);
        if (next_token.type != TOKEN_SEMICOLON)
        {
            stmt->return_.expr = parseExpr(compiler, state);
        }

        consumeToken(compiler, state, TOKEN_SEMICOLON);
        break;
    }

    default: {
        DuskExpr *expr = parseExpr(compiler, state);

        tokenizerNextToken(allocator, *state, &next_token);
        switch (next_token.type)
        {
        case TOKEN_ASSIGN: {
            consumeToken(compiler, state, TOKEN_ASSIGN);

            DuskExpr *value_expr = parseExpr(compiler, state);

            stmt->kind = DUSK_STMT_ASSIGN;
            stmt->assign.assigned_expr = expr;
            stmt->assign.value_expr = value_expr;
            break;
        }
        default: {
            stmt->kind = DUSK_STMT_EXPR;
            stmt->expr = expr;
            break;
        }
        }

        consumeToken(compiler, state, TOKEN_SEMICOLON);

        break;
    }
    }

    return stmt;
}

static DuskDecl *
parseTopLevelDecl(DuskCompiler *compiler, TokenizerState *state)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    DuskDecl *decl = DUSK_NEW(allocator, DuskDecl);

    Token next_token = {0};
    tokenizerNextToken(allocator, *state, &next_token);
    decl->location = next_token.location;

    decl->attributes = duskArrayCreate(allocator, DuskAttribute);

    tokenizerNextToken(allocator, *state, &next_token);
    while (next_token.type == TOKEN_LBRACKET)
    {
        consumeToken(compiler, state, TOKEN_LBRACKET);

        tokenizerNextToken(allocator, *state, &next_token);
        while (next_token.type != TOKEN_RBRACKET)
        {
            Token attrib_name_token =
                consumeToken(compiler, state, TOKEN_IDENT);

            DuskAttribute attrib = {0};
            attrib.name = attrib_name_token.str;
            attrib.value_exprs = duskArrayCreate(allocator, DuskExpr *);

            tokenizerNextToken(allocator, *state, &next_token);
            if (next_token.type == TOKEN_LPAREN)
            {
                consumeToken(compiler, state, TOKEN_LPAREN);

                while (next_token.type != TOKEN_RPAREN)
                {
                    DuskExpr *value_expr = parseExpr(compiler, state);
                    duskArrayPush(&attrib.value_exprs, value_expr);

                    tokenizerNextToken(allocator, *state, &next_token);
                    if (next_token.type != TOKEN_RPAREN)
                    {
                        consumeToken(compiler, state, TOKEN_COMMA);
                    }

                    tokenizerNextToken(allocator, *state, &next_token);
                }

                duskArrayPush(&decl->attributes, attrib);

                consumeToken(compiler, state, TOKEN_RPAREN);
            }

            tokenizerNextToken(allocator, *state, &next_token);
            if (next_token.type != TOKEN_RBRACKET)
            {
                consumeToken(compiler, state, TOKEN_COMMA);
                tokenizerNextToken(allocator, *state, &next_token);
            }
        }

        consumeToken(compiler, state, TOKEN_RBRACKET);

        tokenizerNextToken(allocator, *state, &next_token);
    }

    switch (next_token.type)
    {
    case TOKEN_FN: {
        consumeToken(compiler, state, TOKEN_FN);

        Token name_token = consumeToken(compiler, state, TOKEN_IDENT);

        decl->kind = DUSK_DECL_FUNCTION;
        decl->name = name_token.str;
        decl->function.parameter_decls = duskArrayCreate(allocator, DuskDecl *);
        decl->function.stmts = duskArrayCreate(allocator, DuskStmt *);

        consumeToken(compiler, state, TOKEN_LPAREN);

        Token next_token = {0};
        tokenizerNextToken(allocator, *state, &next_token);
        while (next_token.type != TOKEN_RPAREN)
        {
            Token param_ident = consumeToken(compiler, state, TOKEN_IDENT);

            consumeToken(compiler, state, TOKEN_COLON);

            DuskExpr *param_type_expr = parseExpr(compiler, state);

            DuskDecl *param_decl = DUSK_NEW(allocator, DuskDecl);
            param_decl->kind = DUSK_DECL_VAR;
            param_decl->location = param_ident.location;
            param_decl->name = param_ident.str;
            param_decl->var.type_expr = param_type_expr;
            param_decl->var.value_expr = NULL;
            param_decl->var.storage_class = DUSK_STORAGE_CLASS_PARAMETER;
            duskArrayPush(&decl->function.parameter_decls, param_decl);

            tokenizerNextToken(allocator, *state, &next_token);
            if (next_token.type != TOKEN_RPAREN)
            {
                consumeToken(compiler, state, TOKEN_COMMA);
                tokenizerNextToken(allocator, *state, &next_token);
            }
        }

        consumeToken(compiler, state, TOKEN_RPAREN);

        decl->function.return_type_expr = parseExpr(compiler, state);

        consumeToken(compiler, state, TOKEN_LCURLY);

        tokenizerNextToken(allocator, *state, &next_token);
        while (next_token.type != TOKEN_RCURLY)
        {
            DuskStmt *stmt = parseStmt(compiler, state);
            duskArrayPush(&decl->function.stmts, stmt);

            tokenizerNextToken(allocator, *state, &next_token);
        }

        consumeToken(compiler, state, TOKEN_RCURLY);
        break;
    }
    case TOKEN_LET: {
        consumeToken(compiler, state, TOKEN_LET);

        Token name_token = consumeToken(compiler, state, TOKEN_IDENT);

        consumeToken(compiler, state, TOKEN_COLON);
        DuskExpr *type_expr = parseExpr(compiler, state);

        decl->kind = DUSK_DECL_VAR;
        decl->name = name_token.str;
        decl->var.storage_class = DUSK_STORAGE_CLASS_FUNCTION;
        decl->var.type_expr = type_expr;
        decl->var.value_expr = NULL;

        consumeToken(compiler, state, TOKEN_SEMICOLON);

        break;
    }
    case TOKEN_TYPE: {
        consumeToken(compiler, state, TOKEN_TYPE);

        Token name_token = consumeToken(compiler, state, TOKEN_IDENT);

        DuskExpr *type_expr = parseExpr(compiler, state);

        decl->kind = DUSK_DECL_TYPE;
        decl->name = name_token.str;
        decl->typedef_.type_expr = type_expr;

        consumeToken(compiler, state, TOKEN_SEMICOLON);
        break;
    }
    default: {
        duskAddError(
            compiler,
            next_token.location,
            "unexpected token: %s, expecting top level declaration",
            tokenToString(allocator, &next_token));
        duskThrow(compiler);
        break;
    }
    }

    return decl;
}

void duskParse(DuskCompiler *compiler, DuskFile *file)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);
    TokenizerState state = tokenizerCreate(file);

    while (1)
    {
        Token token = {0};
        tokenizerNextToken(allocator, state, &token);
        if (token.type == TOKEN_ERROR)
        {
            duskAddError(
                compiler, token.location, "unexpected token: %s", token.str);
            duskThrow(compiler);
        }

        if (token.type == TOKEN_EOF)
        {
            break;
        }

        DuskDecl *decl = parseTopLevelDecl(compiler, &state);
        duskArrayPush(&file->decls, decl);
    }
}
