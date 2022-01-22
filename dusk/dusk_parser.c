#include "dusk_internal.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct TokenizerState {
    DuskFile *file;
    size_t pos;
    size_t line;
    size_t col;
} TokenizerState;

static const char *tokenTypeToString(DuskTokenType token_type)
{
    switch (token_type) {
    case DUSK_TOKEN_ERROR: return "<error>";

    case DUSK_TOKEN_IDENT: return "identifier";
    case DUSK_TOKEN_BUILTIN_IDENT: return "builtin identifier";
    case DUSK_TOKEN_STRING_LITERAL: return "string literal";
    case DUSK_TOKEN_INT_LITERAL: return "integer literal";
    case DUSK_TOKEN_FLOAT_LITERAL: return "float literal";

    case DUSK_TOKEN_LET: return "let";
    case DUSK_TOKEN_FN: return "fn";
    case DUSK_TOKEN_CONST: return "const";
    case DUSK_TOKEN_STRUCT: return "struct";
    case DUSK_TOKEN_TYPE: return "type";
    case DUSK_TOKEN_IMPORT: return "import";
    case DUSK_TOKEN_BREAK: return "break";
    case DUSK_TOKEN_CONTINUE: return "continue";
    case DUSK_TOKEN_RETURN: return "return";
    case DUSK_TOKEN_DISCARD: return "discard";
    case DUSK_TOKEN_WHILE: return "while";
    case DUSK_TOKEN_IF: return "if";
    case DUSK_TOKEN_ELSE: return "else";
    case DUSK_TOKEN_SWITCH: return "switch";
    case DUSK_TOKEN_TRUE: return "true";
    case DUSK_TOKEN_FALSE: return "false";

    case DUSK_TOKEN_VOID: return "void";
    case DUSK_TOKEN_BOOL: return "bool";

    case DUSK_TOKEN_HALF: return "half";
    case DUSK_TOKEN_HALF2: return "half2";
    case DUSK_TOKEN_HALF3: return "half3";
    case DUSK_TOKEN_HALF4: return "half4";
    case DUSK_TOKEN_HALF2X2: return "half2x2";
    case DUSK_TOKEN_HALF3X3: return "half3x3";
    case DUSK_TOKEN_HALF4X4: return "half4x4";

    case DUSK_TOKEN_FLOAT: return "float";
    case DUSK_TOKEN_FLOAT2: return "float2";
    case DUSK_TOKEN_FLOAT3: return "float3";
    case DUSK_TOKEN_FLOAT4: return "float4";
    case DUSK_TOKEN_FLOAT2X2: return "float2x2";
    case DUSK_TOKEN_FLOAT3X3: return "float3x3";
    case DUSK_TOKEN_FLOAT4X4: return "float4x4";

    case DUSK_TOKEN_DOUBLE: return "double";
    case DUSK_TOKEN_DOUBLE2: return "double2";
    case DUSK_TOKEN_DOUBLE3: return "double3";
    case DUSK_TOKEN_DOUBLE4: return "double4";
    case DUSK_TOKEN_DOUBLE2X2: return "double2x2";
    case DUSK_TOKEN_DOUBLE3X3: return "double3x3";
    case DUSK_TOKEN_DOUBLE4X4: return "double4x4";

    case DUSK_TOKEN_BYTE: return "byte";
    case DUSK_TOKEN_BYTE2: return "byte2";
    case DUSK_TOKEN_BYTE3: return "byte3";
    case DUSK_TOKEN_BYTE4: return "byte4";
    case DUSK_TOKEN_BYTE2X2: return "byte2x2";
    case DUSK_TOKEN_BYTE3X3: return "byte3x3";
    case DUSK_TOKEN_BYTE4X4: return "byte4x4";

    case DUSK_TOKEN_UBYTE: return "ubyte";
    case DUSK_TOKEN_UBYTE2: return "ubyte2";
    case DUSK_TOKEN_UBYTE3: return "ubyte3";
    case DUSK_TOKEN_UBYTE4: return "ubyte4";
    case DUSK_TOKEN_UBYTE2X2: return "ubyte2x2";
    case DUSK_TOKEN_UBYTE3X3: return "ubyte3x3";
    case DUSK_TOKEN_UBYTE4X4: return "ubyte4x4";

    case DUSK_TOKEN_SHORT: return "short";
    case DUSK_TOKEN_SHORT2: return "short2";
    case DUSK_TOKEN_SHORT3: return "short3";
    case DUSK_TOKEN_SHORT4: return "short4";
    case DUSK_TOKEN_SHORT2X2: return "short2x2";
    case DUSK_TOKEN_SHORT3X3: return "short3x3";
    case DUSK_TOKEN_SHORT4X4: return "short4x4";

    case DUSK_TOKEN_USHORT: return "ushort";
    case DUSK_TOKEN_USHORT2: return "ushort2";
    case DUSK_TOKEN_USHORT3: return "ushort3";
    case DUSK_TOKEN_USHORT4: return "ushort4";
    case DUSK_TOKEN_USHORT2X2: return "ushort2x2";
    case DUSK_TOKEN_USHORT3X3: return "ushort3x3";
    case DUSK_TOKEN_USHORT4X4: return "ushort4x4";

    case DUSK_TOKEN_INT: return "int";
    case DUSK_TOKEN_INT2: return "int2";
    case DUSK_TOKEN_INT3: return "int3";
    case DUSK_TOKEN_INT4: return "int4";
    case DUSK_TOKEN_INT2X2: return "int2x2";
    case DUSK_TOKEN_INT3X3: return "int3x3";
    case DUSK_TOKEN_INT4X4: return "int4x4";

    case DUSK_TOKEN_UINT: return "uint";
    case DUSK_TOKEN_UINT2: return "uint2";
    case DUSK_TOKEN_UINT3: return "uint3";
    case DUSK_TOKEN_UINT4: return "uint4";
    case DUSK_TOKEN_UINT2X2: return "uint2x2";
    case DUSK_TOKEN_UINT3X3: return "uint3x3";
    case DUSK_TOKEN_UINT4X4: return "uint4x4";

    case DUSK_TOKEN_LONG: return "long";
    case DUSK_TOKEN_LONG2: return "long2";
    case DUSK_TOKEN_LONG3: return "long3";
    case DUSK_TOKEN_LONG4: return "long4";
    case DUSK_TOKEN_LONG2X2: return "long2x2";
    case DUSK_TOKEN_LONG3X3: return "long3x3";
    case DUSK_TOKEN_LONG4X4: return "long4x4";

    case DUSK_TOKEN_ULONG: return "ulong";
    case DUSK_TOKEN_ULONG2: return "ulong2";
    case DUSK_TOKEN_ULONG3: return "ulong3";
    case DUSK_TOKEN_ULONG4: return "ulong4";
    case DUSK_TOKEN_ULONG2X2: return "ulong2x2";
    case DUSK_TOKEN_ULONG3X3: return "ulong3x3";
    case DUSK_TOKEN_ULONG4X4: return "ulong4x4";

    case DUSK_TOKEN_LCURLY: return "{";
    case DUSK_TOKEN_RCURLY: return "}";
    case DUSK_TOKEN_LBRACKET: return "[";
    case DUSK_TOKEN_RBRACKET: return "]";
    case DUSK_TOKEN_LPAREN: return "(";
    case DUSK_TOKEN_RPAREN: return ")";

    case DUSK_TOKEN_ADD: return "+";
    case DUSK_TOKEN_SUB: return "-";
    case DUSK_TOKEN_MUL: return "*";
    case DUSK_TOKEN_DIV: return "/";
    case DUSK_TOKEN_MOD: return "%";

    case DUSK_TOKEN_ADDADD: return "++";
    case DUSK_TOKEN_SUBSUB: return "--";

    case DUSK_TOKEN_BITOR: return "|";
    case DUSK_TOKEN_BITXOR: return "^";
    case DUSK_TOKEN_BITAND: return "&";
    case DUSK_TOKEN_BITNOT: return "~";

    case DUSK_TOKEN_LSHIFT: return "<<";
    case DUSK_TOKEN_RSHIFT: return ">>";

    case DUSK_TOKEN_DOT: return ".";
    case DUSK_TOKEN_COMMA: return ",";
    case DUSK_TOKEN_QUESTION: return "?";

    case DUSK_TOKEN_COLON: return ":";
    case DUSK_TOKEN_SEMICOLON: return ";";

    case DUSK_TOKEN_NOT: return "!";
    case DUSK_TOKEN_ASSIGN: return "=";

    case DUSK_TOKEN_EQ: return "==";
    case DUSK_TOKEN_NOTEQ: return "!=";
    case DUSK_TOKEN_LESS: return "<";
    case DUSK_TOKEN_LESSEQ: return "<=";
    case DUSK_TOKEN_GREATER: return ">";
    case DUSK_TOKEN_GREATEREQ: return ">=";

    case DUSK_TOKEN_ADD_ASSIGN: return "+=";
    case DUSK_TOKEN_SUB_ASSIGN: return "-=";
    case DUSK_TOKEN_MUL_ASSIGN: return "*=";
    case DUSK_TOKEN_DIV_ASSIGN: return "/=";
    case DUSK_TOKEN_MOD_ASSIGN: return "%=";

    case DUSK_TOKEN_BITAND_ASSIGN: return "&=";
    case DUSK_TOKEN_BITOR_ASSIGN: return "|=";
    case DUSK_TOKEN_BITXOR_ASSIGN: return "^=";
    case DUSK_TOKEN_BITNOT_ASSIGN: return "~=";

    case DUSK_TOKEN_LSHIFT_ASSIGN: return "<<=";
    case DUSK_TOKEN_RSHIFT_ASSIGN: return ">>=";

    case DUSK_TOKEN_AND: return "&&";
    case DUSK_TOKEN_OR: return "||";

    case DUSK_TOKEN_EOF: return "<eof>";
    }

    return "<unknown>";
}

static const char *
tokenToString(DuskAllocator *allocator, const DuskToken *token)
{
    switch (token->type) {
    case DUSK_TOKEN_ERROR: return "<error>";

    case DUSK_TOKEN_IDENT:
        return duskSprintf(allocator, "identifier '%s'", token->str);
    case DUSK_TOKEN_BUILTIN_IDENT:
        return duskSprintf(allocator, "@%s", token->str);
    case DUSK_TOKEN_STRING_LITERAL:
        return duskSprintf(allocator, "\"%s\"", token->str);
    case DUSK_TOKEN_INT_LITERAL:
        return duskSprintf(allocator, "%ld", token->int_);
    case DUSK_TOKEN_FLOAT_LITERAL:
        return duskSprintf(allocator, "%lf", token->float_);

    case DUSK_TOKEN_LET: return "let";
    case DUSK_TOKEN_FN: return "fn";
    case DUSK_TOKEN_CONST: return "const";
    case DUSK_TOKEN_STRUCT: return "struct";
    case DUSK_TOKEN_TYPE: return "type";
    case DUSK_TOKEN_IMPORT: return "import";
    case DUSK_TOKEN_BREAK: return "break";
    case DUSK_TOKEN_CONTINUE: return "continue";
    case DUSK_TOKEN_RETURN: return "return";
    case DUSK_TOKEN_DISCARD: return "discard";
    case DUSK_TOKEN_WHILE: return "while";
    case DUSK_TOKEN_IF: return "if";
    case DUSK_TOKEN_ELSE: return "else";
    case DUSK_TOKEN_SWITCH: return "switch";
    case DUSK_TOKEN_TRUE: return "true";
    case DUSK_TOKEN_FALSE: return "false";

    case DUSK_TOKEN_VOID: return "void";
    case DUSK_TOKEN_BOOL: return "bool";

    case DUSK_TOKEN_HALF: return "half";
    case DUSK_TOKEN_HALF2: return "half2";
    case DUSK_TOKEN_HALF3: return "half3";
    case DUSK_TOKEN_HALF4: return "half4";
    case DUSK_TOKEN_HALF2X2: return "half2x2";
    case DUSK_TOKEN_HALF3X3: return "half3x3";
    case DUSK_TOKEN_HALF4X4: return "half4x4";

    case DUSK_TOKEN_FLOAT: return "float";
    case DUSK_TOKEN_FLOAT2: return "float2";
    case DUSK_TOKEN_FLOAT3: return "float3";
    case DUSK_TOKEN_FLOAT4: return "float4";
    case DUSK_TOKEN_FLOAT2X2: return "float2x2";
    case DUSK_TOKEN_FLOAT3X3: return "float3x3";
    case DUSK_TOKEN_FLOAT4X4: return "float4x4";

    case DUSK_TOKEN_DOUBLE: return "double";
    case DUSK_TOKEN_DOUBLE2: return "double2";
    case DUSK_TOKEN_DOUBLE3: return "double3";
    case DUSK_TOKEN_DOUBLE4: return "double4";
    case DUSK_TOKEN_DOUBLE2X2: return "double2x2";
    case DUSK_TOKEN_DOUBLE3X3: return "double3x3";
    case DUSK_TOKEN_DOUBLE4X4: return "double4x4";

    case DUSK_TOKEN_BYTE: return "byte";
    case DUSK_TOKEN_BYTE2: return "byte2";
    case DUSK_TOKEN_BYTE3: return "byte3";
    case DUSK_TOKEN_BYTE4: return "byte4";
    case DUSK_TOKEN_BYTE2X2: return "byte2x2";
    case DUSK_TOKEN_BYTE3X3: return "byte3x3";
    case DUSK_TOKEN_BYTE4X4: return "byte4x4";

    case DUSK_TOKEN_UBYTE: return "ubyte";
    case DUSK_TOKEN_UBYTE2: return "ubyte2";
    case DUSK_TOKEN_UBYTE3: return "ubyte3";
    case DUSK_TOKEN_UBYTE4: return "ubyte4";
    case DUSK_TOKEN_UBYTE2X2: return "ubyte2x2";
    case DUSK_TOKEN_UBYTE3X3: return "ubyte3x3";
    case DUSK_TOKEN_UBYTE4X4: return "ubyte4x4";

    case DUSK_TOKEN_SHORT: return "short";
    case DUSK_TOKEN_SHORT2: return "short2";
    case DUSK_TOKEN_SHORT3: return "short3";
    case DUSK_TOKEN_SHORT4: return "short4";
    case DUSK_TOKEN_SHORT2X2: return "short2x2";
    case DUSK_TOKEN_SHORT3X3: return "short3x3";
    case DUSK_TOKEN_SHORT4X4: return "short4x4";

    case DUSK_TOKEN_USHORT: return "ushort";
    case DUSK_TOKEN_USHORT2: return "ushort2";
    case DUSK_TOKEN_USHORT3: return "ushort3";
    case DUSK_TOKEN_USHORT4: return "ushort4";
    case DUSK_TOKEN_USHORT2X2: return "ushort2x2";
    case DUSK_TOKEN_USHORT3X3: return "ushort3x3";
    case DUSK_TOKEN_USHORT4X4: return "ushort4x4";

    case DUSK_TOKEN_INT: return "int";
    case DUSK_TOKEN_INT2: return "int2";
    case DUSK_TOKEN_INT3: return "int3";
    case DUSK_TOKEN_INT4: return "int4";
    case DUSK_TOKEN_INT2X2: return "int2x2";
    case DUSK_TOKEN_INT3X3: return "int3x3";
    case DUSK_TOKEN_INT4X4: return "int4x4";

    case DUSK_TOKEN_UINT: return "uint";
    case DUSK_TOKEN_UINT2: return "uint2";
    case DUSK_TOKEN_UINT3: return "uint3";
    case DUSK_TOKEN_UINT4: return "uint4";
    case DUSK_TOKEN_UINT2X2: return "uint2x2";
    case DUSK_TOKEN_UINT3X3: return "uint3x3";
    case DUSK_TOKEN_UINT4X4: return "uint4x4";

    case DUSK_TOKEN_LONG: return "long";
    case DUSK_TOKEN_LONG2: return "long2";
    case DUSK_TOKEN_LONG3: return "long3";
    case DUSK_TOKEN_LONG4: return "long4";
    case DUSK_TOKEN_LONG2X2: return "long2x2";
    case DUSK_TOKEN_LONG3X3: return "long3x3";
    case DUSK_TOKEN_LONG4X4: return "long4x4";

    case DUSK_TOKEN_ULONG: return "ulong";
    case DUSK_TOKEN_ULONG2: return "ulong2";
    case DUSK_TOKEN_ULONG3: return "ulong3";
    case DUSK_TOKEN_ULONG4: return "ulong4";
    case DUSK_TOKEN_ULONG2X2: return "ulong2x2";
    case DUSK_TOKEN_ULONG3X3: return "ulong3x3";
    case DUSK_TOKEN_ULONG4X4: return "ulong4x4";

    case DUSK_TOKEN_LCURLY: return "{";
    case DUSK_TOKEN_RCURLY: return "}";
    case DUSK_TOKEN_LBRACKET: return "[";
    case DUSK_TOKEN_RBRACKET: return "]";
    case DUSK_TOKEN_LPAREN: return "(";
    case DUSK_TOKEN_RPAREN: return ")";

    case DUSK_TOKEN_ADD: return "+";
    case DUSK_TOKEN_SUB: return "-";
    case DUSK_TOKEN_MUL: return "*";
    case DUSK_TOKEN_DIV: return "/";
    case DUSK_TOKEN_MOD: return "%";

    case DUSK_TOKEN_ADDADD: return "++";
    case DUSK_TOKEN_SUBSUB: return "--";

    case DUSK_TOKEN_BITOR: return "|";
    case DUSK_TOKEN_BITXOR: return "^";
    case DUSK_TOKEN_BITAND: return "&";
    case DUSK_TOKEN_BITNOT: return "~";

    case DUSK_TOKEN_LSHIFT: return "<<";
    case DUSK_TOKEN_RSHIFT: return ">>";

    case DUSK_TOKEN_DOT: return ".";
    case DUSK_TOKEN_COMMA: return ",";
    case DUSK_TOKEN_QUESTION: return "?";

    case DUSK_TOKEN_COLON: return ":";
    case DUSK_TOKEN_SEMICOLON: return ";";

    case DUSK_TOKEN_NOT: return "!";
    case DUSK_TOKEN_ASSIGN: return "=";

    case DUSK_TOKEN_EQ: return "==";
    case DUSK_TOKEN_NOTEQ: return "!=";
    case DUSK_TOKEN_LESS: return "<";
    case DUSK_TOKEN_LESSEQ: return "<=";
    case DUSK_TOKEN_GREATER: return ">";
    case DUSK_TOKEN_GREATEREQ: return ">=";

    case DUSK_TOKEN_ADD_ASSIGN: return "+=";
    case DUSK_TOKEN_SUB_ASSIGN: return "-=";
    case DUSK_TOKEN_MUL_ASSIGN: return "*=";
    case DUSK_TOKEN_DIV_ASSIGN: return "/=";
    case DUSK_TOKEN_MOD_ASSIGN: return "%=";

    case DUSK_TOKEN_BITAND_ASSIGN: return "&=";
    case DUSK_TOKEN_BITOR_ASSIGN: return "|=";
    case DUSK_TOKEN_BITXOR_ASSIGN: return "^=";
    case DUSK_TOKEN_BITNOT_ASSIGN: return "~=";

    case DUSK_TOKEN_LSHIFT_ASSIGN: return "<<=";
    case DUSK_TOKEN_RSHIFT_ASSIGN: return ">>=";

    case DUSK_TOKEN_AND: return "&&";
    case DUSK_TOKEN_OR: return "||";

    case DUSK_TOKEN_EOF: return "<eof>";
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

static TokenizerState tokenizerNextToken(
    DuskCompiler *compiler, TokenizerState state, DuskToken *token)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

begin:
    *token = (DuskToken){0};

    // Skip whitespace
    for (size_t i = state.pos; i < state.file->text_length; ++i) {
        if (isWhitespace(state.file->text[i])) {
            state.pos++;
            state.col++;
            if (state.file->text[i] == '\n') {
                state.line++;
                state.col = 1;
            }
        } else
            break;
    }

    token->location.offset = state.pos;
    token->location.line = state.line;
    token->location.col = state.col;
    token->location.length = 1;
    token->location.file = state.file;

    if (tokenizerLengthLeft(state, 0) <= 0) {
        token->type = DUSK_TOKEN_EOF;
        return state;
    }

    char c = state.file->text[state.pos];
    switch (c) {
    case '\"': {
        // String
        state.pos++;

        const char *string = &state.file->text[state.pos];

        size_t content_length = 0;
        while (tokenizerLengthLeft(state, content_length) > 0 &&
               state.file->text[state.pos + content_length] != '\"') {
            content_length++;
        }

        state.pos += content_length;

        if (tokenizerLengthLeft(state, 0) > 0 &&
            state.file->text[state.pos] == '\"') {
            state.pos++;
        } else {
            token->type = DUSK_TOKEN_ERROR;
            token->str = duskStrdup(allocator, "unclosed string");
            break;
        }

        token->type = DUSK_TOKEN_STRING_LITERAL;
        token->str = duskNullTerminate(allocator, string, content_length);

        break;
    }

    case '{':
        state.pos++;
        token->type = DUSK_TOKEN_LCURLY;
        break;
    case '}':
        state.pos++;
        token->type = DUSK_TOKEN_RCURLY;
        break;
    case '[':
        state.pos++;
        token->type = DUSK_TOKEN_LBRACKET;
        break;
    case ']':
        state.pos++;
        token->type = DUSK_TOKEN_RBRACKET;
        break;
    case '(':
        state.pos++;
        token->type = DUSK_TOKEN_LPAREN;
        break;
    case ')':
        state.pos++;
        token->type = DUSK_TOKEN_RPAREN;
        break;

    case '=': {
        state.pos++;
        token->type = DUSK_TOKEN_ASSIGN;
        if (tokenizerLengthLeft(state, 0) > 0 &&
            state.file->text[state.pos] == '=') {
            state.pos++;
            token->type = DUSK_TOKEN_EQ;
        }
        break;
    }

    case '+': {
        state.pos++;
        token->type = DUSK_TOKEN_ADD;
        if (tokenizerLengthLeft(state, 0) > 0) {
            switch (state.file->text[state.pos]) {
            case '=':
                state.pos++;
                token->type = DUSK_TOKEN_ADD_ASSIGN;
                break;
            case '+':
                state.pos++;
                token->type = DUSK_TOKEN_ADDADD;
                break;
            default: break;
            }
        }
        break;
    }

    case '-': {
        state.pos++;
        token->type = DUSK_TOKEN_SUB;
        if (tokenizerLengthLeft(state, 0) > 0) {
            switch (state.file->text[state.pos]) {
            case '=':
                state.pos++;
                token->type = DUSK_TOKEN_SUB_ASSIGN;
                break;
            case '-':
                state.pos++;
                token->type = DUSK_TOKEN_SUBSUB;
                break;
            default: break;
            }
        }
        break;
    }

    case '*': {
        state.pos++;
        token->type = DUSK_TOKEN_MUL;
        if (tokenizerLengthLeft(state, 0) > 0 &&
            state.file->text[state.pos] == '=') {
            state.pos++;
            token->type = DUSK_TOKEN_MUL_ASSIGN;
        }
        break;
    }

    case '/': {
        state.pos++;
        token->type = DUSK_TOKEN_DIV;
        if (tokenizerLengthLeft(state, 0) > 0) {
            switch (state.file->text[state.pos]) {
            case '=':
                state.pos++;
                token->type = DUSK_TOKEN_DIV_ASSIGN;
                break;
            case '/':
                state.pos++;
                while (tokenizerLengthLeft(state, 0) > 0 &&
                       state.file->text[state.pos] != '\n') {
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
        token->type = DUSK_TOKEN_MOD;
        if (tokenizerLengthLeft(state, 0) > 0 &&
            state.file->text[state.pos] == '=') {
            state.pos++;
            token->type = DUSK_TOKEN_MOD_ASSIGN;
        }
        break;
    }

    case '|': {
        state.pos++;
        token->type = DUSK_TOKEN_BITOR;
        if (tokenizerLengthLeft(state, 0) > 0) {
            switch (state.file->text[state.pos]) {
            case '=':
                state.pos++;
                token->type = DUSK_TOKEN_BITOR_ASSIGN;
                break;
            case '|':
                state.pos++;
                token->type = DUSK_TOKEN_OR;
                break;
            default: break;
            }
        }
        break;
    }

    case '&': {
        state.pos++;
        token->type = DUSK_TOKEN_BITAND;
        if (tokenizerLengthLeft(state, 0) > 0) {
            switch (state.file->text[state.pos]) {
            case '=':
                state.pos++;
                token->type = DUSK_TOKEN_BITAND_ASSIGN;
                break;
            case '&':
                state.pos++;
                token->type = DUSK_TOKEN_AND;
                break;
            default: break;
            }
        }
        break;
    }

    case '^': {
        state.pos++;
        token->type = DUSK_TOKEN_BITXOR;
        if (tokenizerLengthLeft(state, 0) > 0 &&
            state.file->text[state.pos] == '=') {
            state.pos++;
            token->type = DUSK_TOKEN_BITXOR_ASSIGN;
        }
        break;
    }

    case '~': {
        state.pos++;
        token->type = DUSK_TOKEN_BITNOT;
        if (tokenizerLengthLeft(state, 0) > 0 &&
            state.file->text[state.pos] == '=') {
            state.pos++;
            token->type = DUSK_TOKEN_BITNOT_ASSIGN;
        }
        break;
    }

    case '!': {
        state.pos++;
        token->type = DUSK_TOKEN_NOT;
        if (tokenizerLengthLeft(state, 0) > 0 &&
            state.file->text[state.pos] == '=') {
            state.pos++;
            token->type = DUSK_TOKEN_NOTEQ;
        }
        break;
    }

    case '<': {
        state.pos++;
        token->type = DUSK_TOKEN_LESS;
        if (tokenizerLengthLeft(state, 0) > 0) {
            switch (state.file->text[state.pos]) {
            case '=':
                state.pos++;
                token->type = DUSK_TOKEN_LESSEQ;
                break;
            case '<':
                state.pos++;
                token->type = DUSK_TOKEN_LSHIFT;
                if (tokenizerLengthLeft(state, 0) > 0 &&
                    state.file->text[state.pos] == '=') {
                    token->type = DUSK_TOKEN_LSHIFT_ASSIGN;
                }
                break;
            default: break;
            }
        }
        break;
    }

    case '>': {
        state.pos++;
        token->type = DUSK_TOKEN_GREATER;
        if (tokenizerLengthLeft(state, 0) > 0) {
            switch (state.file->text[state.pos]) {
            case '=':
                state.pos++;
                token->type = DUSK_TOKEN_GREATEREQ;
                break;
            case '>':
                state.pos++;
                token->type = DUSK_TOKEN_RSHIFT;
                if (tokenizerLengthLeft(state, 0) > 0 &&
                    state.file->text[state.pos] == '=') {
                    token->type = DUSK_TOKEN_RSHIFT_ASSIGN;
                }
                break;
            default: break;
            }
        }
        break;
    }

    case ':':
        state.pos++;
        token->type = DUSK_TOKEN_COLON;
        break;
    case ';':
        state.pos++;
        token->type = DUSK_TOKEN_SEMICOLON;
        break;
    case '.':
        state.pos++;
        token->type = DUSK_TOKEN_DOT;
        break;
    case ',':
        state.pos++;
        token->type = DUSK_TOKEN_COMMA;
        break;
    case '?':
        state.pos++;
        token->type = DUSK_TOKEN_QUESTION;
        break;

    default: {
        if (isAlpha(c)) {
            // Identifier
            size_t ident_length = 0;
            while (tokenizerLengthLeft(state, ident_length) > 0 &&
                   isAlphaNum(state.file->text[state.pos + ident_length])) {
                ident_length++;
            }

            const char *ident_start = &state.file->text[state.pos];

            char *ident_zstr =
                DUSK_NEW_ARRAY(allocator, char, ident_length + 1);
            memcpy(ident_zstr, ident_start, ident_length);

            uintptr_t token_type = 0;
            if (duskMapGet(
                    compiler->keyword_map, ident_zstr, (void **)&token_type)) {
                token->type = (DuskTokenType)token_type;
            } else {
                token->type = DUSK_TOKEN_IDENT;
                token->str =
                    duskNullTerminate(allocator, ident_start, ident_length);
            }

            state.pos += ident_length;
        } else if (c == '@' && isAlpha(state.file->text[state.pos + 1])) {
            // Builtin Identifier
            state.pos++;
            size_t ident_length = 0;
            while (tokenizerLengthLeft(state, ident_length) > 0 &&
                   isAlphaNum(state.file->text[state.pos + ident_length])) {
                ident_length++;
            }

            const char *ident_start = &state.file->text[state.pos];

            token->type = DUSK_TOKEN_BUILTIN_IDENT;
            token->str =
                duskNullTerminate(allocator, ident_start, ident_length);
            state.pos += ident_length;
        } else if (isNum(c)) {
            if (tokenizerLengthLeft(state, 0) >= 3 &&
                state.file->text[state.pos] == '0' &&
                state.file->text[state.pos + 1] == 'x' &&
                isHex(state.file->text[state.pos + 2])) {
                token->type = DUSK_TOKEN_INT_LITERAL;
                state.pos += 2;

                size_t number_length = 0;
                while (tokenizerLengthLeft(state, number_length) > 0 &&
                       isHex(state.file->text[state.pos + number_length])) {
                    number_length++;
                }

                const char *int_str = duskNullTerminate(
                    allocator, &state.file->text[state.pos], number_length);
                state.pos += number_length;
                token->int_ = strtol(int_str, NULL, 16);
            } else {
                token->type = DUSK_TOKEN_INT_LITERAL;

                size_t number_length = 0;
                while (tokenizerLengthLeft(state, number_length) > 0 &&
                       isNum(state.file->text[state.pos + number_length])) {
                    number_length++;
                }

                if (tokenizerLengthLeft(state, number_length) > 1 &&
                    state.file->text[state.pos + number_length] == '.' &&
                    isNum(state.file->text[state.pos + number_length + 1])) {
                    token->type = DUSK_TOKEN_FLOAT_LITERAL;
                    number_length++;
                }

                while (tokenizerLengthLeft(state, number_length) > 0 &&
                       isNum(state.file->text[state.pos + number_length])) {
                    number_length++;
                }

                switch (token->type) {
                case DUSK_TOKEN_INT_LITERAL: {
                    const char *int_str = duskNullTerminate(
                        allocator, &state.file->text[state.pos], number_length);
                    token->int_ = strtol(int_str, NULL, 10);
                    break;
                }
                case DUSK_TOKEN_FLOAT_LITERAL: {
                    const char *float_str = duskNullTerminate(
                        allocator, &state.file->text[state.pos], number_length);
                    token->float_ = strtod(float_str, NULL);
                    break;
                }
                default: DUSK_ASSERT(0); break;
                }

                state.pos += number_length;
            }
        } else {
            token->type = DUSK_TOKEN_ERROR;
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

static DuskToken consumeToken(
    DuskCompiler *compiler, TokenizerState *state, DuskTokenType token_type)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    DuskToken token = {0};
    *state = tokenizerNextToken(compiler, *state, &token);
    if (token.type != token_type) {
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

static DuskExpr *
parseExpr(DuskCompiler *compiler, TokenizerState *state, bool only_types);

static void parseAttributes(
    DuskCompiler *compiler,
    TokenizerState *state,
    DuskArray(DuskAttribute) * attributes)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    DuskToken next_token = {0};
    tokenizerNextToken(compiler, *state, &next_token);
    while (next_token.type == DUSK_TOKEN_LBRACKET) {
        consumeToken(compiler, state, DUSK_TOKEN_LBRACKET);

        tokenizerNextToken(compiler, *state, &next_token);
        while (next_token.type != DUSK_TOKEN_RBRACKET) {
            DuskToken attrib_name_token =
                consumeToken(compiler, state, DUSK_TOKEN_IDENT);

            DuskAttribute attrib = {0};
            if (strcmp(attrib_name_token.str, "location") == 0) {
                attrib.kind = DUSK_ATTRIBUTE_LOCATION;
            } else if (strcmp(attrib_name_token.str, "set") == 0) {
                attrib.kind = DUSK_ATTRIBUTE_SET;
            } else if (strcmp(attrib_name_token.str, "binding") == 0) {
                attrib.kind = DUSK_ATTRIBUTE_BINDING;
            } else if (strcmp(attrib_name_token.str, "stage") == 0) {
                attrib.kind = DUSK_ATTRIBUTE_STAGE;
            } else if (strcmp(attrib_name_token.str, "builtin") == 0) {
                attrib.kind = DUSK_ATTRIBUTE_BUILTIN;
            } else if (strcmp(attrib_name_token.str, "offset") == 0) {
                attrib.kind = DUSK_ATTRIBUTE_OFFSET;
            } else if (strcmp(attrib_name_token.str, "read_only") == 0) {
                attrib.kind = DUSK_ATTRIBUTE_READ_ONLY;
            } else {
                duskAddError(
                    compiler,
                    attrib_name_token.location,
                    "invalid attribute: '%s'",
                    attrib_name_token.str);
            }
            attrib.name = attrib_name_token.str;
            DuskArray(DuskExpr *) value_exprs =
                duskArrayCreate(allocator, DuskExpr *);

            tokenizerNextToken(compiler, *state, &next_token);
            if (next_token.type == DUSK_TOKEN_LPAREN) {
                consumeToken(compiler, state, DUSK_TOKEN_LPAREN);

                while (next_token.type != DUSK_TOKEN_RPAREN) {
                    DuskExpr *value_expr = parseExpr(compiler, state, false);
                    duskArrayPush(&value_exprs, value_expr);

                    tokenizerNextToken(compiler, *state, &next_token);
                    if (next_token.type != DUSK_TOKEN_RPAREN) {
                        consumeToken(compiler, state, DUSK_TOKEN_COMMA);
                    }

                    tokenizerNextToken(compiler, *state, &next_token);
                }

                consumeToken(compiler, state, DUSK_TOKEN_RPAREN);
            }

            tokenizerNextToken(compiler, *state, &next_token);
            if (next_token.type != DUSK_TOKEN_RBRACKET) {
                consumeToken(compiler, state, DUSK_TOKEN_COMMA);
                tokenizerNextToken(compiler, *state, &next_token);
            }

            attrib.value_expr_count = duskArrayLength(value_exprs);
            attrib.value_exprs =
                DUSK_NEW_ARRAY(allocator, DuskExpr *, attrib.value_expr_count);
            memcpy(
                attrib.value_exprs,
                value_exprs,
                attrib.value_expr_count * sizeof(DuskExpr *));

            duskArrayPush(attributes, attrib);
        }

        consumeToken(compiler, state, DUSK_TOKEN_RBRACKET);

        tokenizerNextToken(compiler, *state, &next_token);
    }
}

static DuskExpr *parsePrimaryExpr(DuskCompiler *compiler, TokenizerState *state)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    DuskExpr *expr = DUSK_NEW(allocator, DuskExpr);

    DuskToken token = {0};
    *state = tokenizerNextToken(compiler, *state, &token);
    expr->location = token.location;

    switch (token.type) {
    case DUSK_TOKEN_VOID: {
        expr->kind = DUSK_EXPR_VOID_TYPE;
        break;
    }
    case DUSK_TOKEN_BOOL: {
        expr->kind = DUSK_EXPR_BOOL_TYPE;
        break;
    }

    case DUSK_TOKEN_HALF: {
        expr->kind = DUSK_EXPR_SCALAR_TYPE;
        expr->scalar_type = DUSK_SCALAR_TYPE_HALF;
        break;
    }
    case DUSK_TOKEN_HALF2: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_HALF;
        expr->vector_type.length = 2;
        break;
    }
    case DUSK_TOKEN_HALF3: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_HALF;
        expr->vector_type.length = 3;
        break;
    }
    case DUSK_TOKEN_HALF4: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_HALF;
        expr->vector_type.length = 4;
        break;
    }
    case DUSK_TOKEN_HALF2X2: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_HALF;
        expr->matrix_type.rows = 2;
        expr->matrix_type.cols = 2;
        break;
    }
    case DUSK_TOKEN_HALF3X3: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_HALF;
        expr->matrix_type.rows = 3;
        expr->matrix_type.cols = 3;
        break;
    }
    case DUSK_TOKEN_HALF4X4: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_HALF;
        expr->matrix_type.rows = 4;
        expr->matrix_type.cols = 4;
        break;
    }

    case DUSK_TOKEN_FLOAT: {
        expr->kind = DUSK_EXPR_SCALAR_TYPE;
        expr->scalar_type = DUSK_SCALAR_TYPE_FLOAT;
        break;
    }
    case DUSK_TOKEN_FLOAT2: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_FLOAT;
        expr->vector_type.length = 2;
        break;
    }
    case DUSK_TOKEN_FLOAT3: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_FLOAT;
        expr->vector_type.length = 3;
        break;
    }
    case DUSK_TOKEN_FLOAT4: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_FLOAT;
        expr->vector_type.length = 4;
        break;
    }
    case DUSK_TOKEN_FLOAT2X2: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_FLOAT;
        expr->matrix_type.rows = 2;
        expr->matrix_type.cols = 2;
        break;
    }
    case DUSK_TOKEN_FLOAT3X3: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_FLOAT;
        expr->matrix_type.rows = 3;
        expr->matrix_type.cols = 3;
        break;
    }
    case DUSK_TOKEN_FLOAT4X4: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_FLOAT;
        expr->matrix_type.rows = 4;
        expr->matrix_type.cols = 4;
        break;
    }

    case DUSK_TOKEN_DOUBLE: {
        expr->kind = DUSK_EXPR_SCALAR_TYPE;
        expr->scalar_type = DUSK_SCALAR_TYPE_DOUBLE;
        break;
    }
    case DUSK_TOKEN_DOUBLE2: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_DOUBLE;
        expr->vector_type.length = 2;
        break;
    }
    case DUSK_TOKEN_DOUBLE3: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_DOUBLE;
        expr->vector_type.length = 3;
        break;
    }
    case DUSK_TOKEN_DOUBLE4: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_DOUBLE;
        expr->vector_type.length = 4;
        break;
    }
    case DUSK_TOKEN_DOUBLE2X2: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_DOUBLE;
        expr->matrix_type.rows = 2;
        expr->matrix_type.cols = 2;
        break;
    }
    case DUSK_TOKEN_DOUBLE3X3: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_DOUBLE;
        expr->matrix_type.rows = 3;
        expr->matrix_type.cols = 3;
        break;
    }
    case DUSK_TOKEN_DOUBLE4X4: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_DOUBLE;
        expr->matrix_type.rows = 4;
        expr->matrix_type.cols = 4;
        break;
    }

    case DUSK_TOKEN_BYTE: {
        expr->kind = DUSK_EXPR_SCALAR_TYPE;
        expr->scalar_type = DUSK_SCALAR_TYPE_BYTE;
        break;
    }
    case DUSK_TOKEN_BYTE2: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_BYTE;
        expr->vector_type.length = 2;
        break;
    }
    case DUSK_TOKEN_BYTE3: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_BYTE;
        expr->vector_type.length = 3;
        break;
    }
    case DUSK_TOKEN_BYTE4: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_BYTE;
        expr->vector_type.length = 4;
        break;
    }
    case DUSK_TOKEN_BYTE2X2: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_BYTE;
        expr->matrix_type.rows = 2;
        expr->matrix_type.cols = 2;
        break;
    }
    case DUSK_TOKEN_BYTE3X3: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_BYTE;
        expr->matrix_type.rows = 3;
        expr->matrix_type.cols = 3;
        break;
    }
    case DUSK_TOKEN_BYTE4X4: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_BYTE;
        expr->matrix_type.rows = 4;
        expr->matrix_type.cols = 4;
        break;
    }

    case DUSK_TOKEN_UBYTE: {
        expr->kind = DUSK_EXPR_SCALAR_TYPE;
        expr->scalar_type = DUSK_SCALAR_TYPE_UBYTE;
        break;
    }
    case DUSK_TOKEN_UBYTE2: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_UBYTE;
        expr->vector_type.length = 2;
        break;
    }
    case DUSK_TOKEN_UBYTE3: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_UBYTE;
        expr->vector_type.length = 3;
        break;
    }
    case DUSK_TOKEN_UBYTE4: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_UBYTE;
        expr->vector_type.length = 4;
        break;
    }
    case DUSK_TOKEN_UBYTE2X2: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_UBYTE;
        expr->matrix_type.rows = 2;
        expr->matrix_type.cols = 2;
        break;
    }
    case DUSK_TOKEN_UBYTE3X3: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_UBYTE;
        expr->matrix_type.rows = 3;
        expr->matrix_type.cols = 3;
        break;
    }
    case DUSK_TOKEN_UBYTE4X4: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_UBYTE;
        expr->matrix_type.rows = 4;
        expr->matrix_type.cols = 4;
        break;
    }

    case DUSK_TOKEN_SHORT: {
        expr->kind = DUSK_EXPR_SCALAR_TYPE;
        expr->scalar_type = DUSK_SCALAR_TYPE_SHORT;
        break;
    }
    case DUSK_TOKEN_SHORT2: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_SHORT;
        expr->vector_type.length = 2;
        break;
    }
    case DUSK_TOKEN_SHORT3: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_SHORT;
        expr->vector_type.length = 3;
        break;
    }
    case DUSK_TOKEN_SHORT4: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_SHORT;
        expr->vector_type.length = 4;
        break;
    }
    case DUSK_TOKEN_SHORT2X2: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_SHORT;
        expr->matrix_type.rows = 2;
        expr->matrix_type.cols = 2;
        break;
    }
    case DUSK_TOKEN_SHORT3X3: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_SHORT;
        expr->matrix_type.rows = 3;
        expr->matrix_type.cols = 3;
        break;
    }
    case DUSK_TOKEN_SHORT4X4: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_SHORT;
        expr->matrix_type.rows = 4;
        expr->matrix_type.cols = 4;
        break;
    }

    case DUSK_TOKEN_USHORT: {
        expr->kind = DUSK_EXPR_SCALAR_TYPE;
        expr->scalar_type = DUSK_SCALAR_TYPE_USHORT;
        break;
    }
    case DUSK_TOKEN_USHORT2: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_USHORT;
        expr->vector_type.length = 2;
        break;
    }
    case DUSK_TOKEN_USHORT3: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_USHORT;
        expr->vector_type.length = 3;
        break;
    }
    case DUSK_TOKEN_USHORT4: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_USHORT;
        expr->vector_type.length = 4;
        break;
    }
    case DUSK_TOKEN_USHORT2X2: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_USHORT;
        expr->matrix_type.rows = 2;
        expr->matrix_type.cols = 2;
        break;
    }
    case DUSK_TOKEN_USHORT3X3: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_USHORT;
        expr->matrix_type.rows = 3;
        expr->matrix_type.cols = 3;
        break;
    }
    case DUSK_TOKEN_USHORT4X4: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_USHORT;
        expr->matrix_type.rows = 4;
        expr->matrix_type.cols = 4;
        break;
    }

    case DUSK_TOKEN_INT: {
        expr->kind = DUSK_EXPR_SCALAR_TYPE;
        expr->scalar_type = DUSK_SCALAR_TYPE_INT;
        break;
    }
    case DUSK_TOKEN_INT2: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_INT;
        expr->vector_type.length = 2;
        break;
    }
    case DUSK_TOKEN_INT3: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_INT;
        expr->vector_type.length = 3;
        break;
    }
    case DUSK_TOKEN_INT4: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_INT;
        expr->vector_type.length = 4;
        break;
    }
    case DUSK_TOKEN_INT2X2: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_INT;
        expr->matrix_type.rows = 2;
        expr->matrix_type.cols = 2;
        break;
    }
    case DUSK_TOKEN_INT3X3: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_INT;
        expr->matrix_type.rows = 3;
        expr->matrix_type.cols = 3;
        break;
    }
    case DUSK_TOKEN_INT4X4: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_INT;
        expr->matrix_type.rows = 4;
        expr->matrix_type.cols = 4;
        break;
    }

    case DUSK_TOKEN_UINT: {
        expr->kind = DUSK_EXPR_SCALAR_TYPE;
        expr->scalar_type = DUSK_SCALAR_TYPE_UINT;
        break;
    }
    case DUSK_TOKEN_UINT2: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_UINT;
        expr->vector_type.length = 2;
        break;
    }
    case DUSK_TOKEN_UINT3: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_UINT;
        expr->vector_type.length = 3;
        break;
    }
    case DUSK_TOKEN_UINT4: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_UINT;
        expr->vector_type.length = 4;
        break;
    }
    case DUSK_TOKEN_UINT2X2: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_UINT;
        expr->matrix_type.rows = 2;
        expr->matrix_type.cols = 2;
        break;
    }
    case DUSK_TOKEN_UINT3X3: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_UINT;
        expr->matrix_type.rows = 3;
        expr->matrix_type.cols = 3;
        break;
    }
    case DUSK_TOKEN_UINT4X4: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_UINT;
        expr->matrix_type.rows = 4;
        expr->matrix_type.cols = 4;
        break;
    }

    case DUSK_TOKEN_LONG: {
        expr->kind = DUSK_EXPR_SCALAR_TYPE;
        expr->scalar_type = DUSK_SCALAR_TYPE_LONG;
        break;
    }
    case DUSK_TOKEN_LONG2: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_LONG;
        expr->vector_type.length = 2;
        break;
    }
    case DUSK_TOKEN_LONG3: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_LONG;
        expr->vector_type.length = 3;
        break;
    }
    case DUSK_TOKEN_LONG4: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_LONG;
        expr->vector_type.length = 4;
        break;
    }
    case DUSK_TOKEN_LONG2X2: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_LONG;
        expr->matrix_type.rows = 2;
        expr->matrix_type.cols = 2;
        break;
    }
    case DUSK_TOKEN_LONG3X3: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_LONG;
        expr->matrix_type.rows = 3;
        expr->matrix_type.cols = 3;
        break;
    }
    case DUSK_TOKEN_LONG4X4: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_LONG;
        expr->matrix_type.rows = 4;
        expr->matrix_type.cols = 4;
        break;
    }

    case DUSK_TOKEN_ULONG: {
        expr->kind = DUSK_EXPR_SCALAR_TYPE;
        expr->scalar_type = DUSK_SCALAR_TYPE_ULONG;
        break;
    }
    case DUSK_TOKEN_ULONG2: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_ULONG;
        expr->vector_type.length = 2;
        break;
    }
    case DUSK_TOKEN_ULONG3: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_ULONG;
        expr->vector_type.length = 3;
        break;
    }
    case DUSK_TOKEN_ULONG4: {
        expr->kind = DUSK_EXPR_VECTOR_TYPE;
        expr->vector_type.scalar_type = DUSK_SCALAR_TYPE_ULONG;
        expr->vector_type.length = 4;
        break;
    }
    case DUSK_TOKEN_ULONG2X2: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_ULONG;
        expr->matrix_type.rows = 2;
        expr->matrix_type.cols = 2;
        break;
    }
    case DUSK_TOKEN_ULONG3X3: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_ULONG;
        expr->matrix_type.rows = 3;
        expr->matrix_type.cols = 3;
        break;
    }
    case DUSK_TOKEN_ULONG4X4: {
        expr->kind = DUSK_EXPR_MATRIX_TYPE;
        expr->matrix_type.scalar_type = DUSK_SCALAR_TYPE_ULONG;
        expr->matrix_type.rows = 4;
        expr->matrix_type.cols = 4;
        break;
    }

    case DUSK_TOKEN_INT_LITERAL: {
        expr->kind = DUSK_EXPR_INT_LITERAL;
        expr->int_literal = token.int_;
        break;
    }
    case DUSK_TOKEN_FLOAT_LITERAL: {
        expr->kind = DUSK_EXPR_FLOAT_LITERAL;
        expr->float_literal = token.float_;
        break;
    }
    case DUSK_TOKEN_TRUE: {
        expr->kind = DUSK_EXPR_BOOL_LITERAL;
        expr->bool_literal = true;
        break;
    }
    case DUSK_TOKEN_FALSE: {
        expr->kind = DUSK_EXPR_BOOL_LITERAL;
        expr->bool_literal = false;
        break;
    }
    case DUSK_TOKEN_IDENT: {
        expr->kind = DUSK_EXPR_IDENT;
        expr->identifier.str = token.str;
        break;
    }
    case DUSK_TOKEN_STRING_LITERAL: {
        expr->kind = DUSK_EXPR_STRING_LITERAL;
        expr->string.str = token.str;
        break;
    }
    case DUSK_TOKEN_LPAREN: {
        expr = parseExpr(compiler, state, false);
        consumeToken(compiler, state, DUSK_TOKEN_RPAREN);
        break;
    }
    case DUSK_TOKEN_LBRACKET: {
        expr->kind = DUSK_EXPR_RUNTIME_ARRAY_TYPE;

        tokenizerNextToken(compiler, *state, &token);
        if (token.type != DUSK_TOKEN_RBRACKET) {
            expr->kind = DUSK_EXPR_ARRAY_TYPE;
            expr->array_type.size_expr = parseExpr(compiler, state, false);
        }

        consumeToken(compiler, state, DUSK_TOKEN_RBRACKET);

        expr->array_type.sub_expr = parseExpr(compiler, state, true);
        break;
    }
    case DUSK_TOKEN_STRUCT: {
        expr->kind = DUSK_EXPR_STRUCT_TYPE;

        DuskArray(DuskExpr *) field_type_exprs =
            duskArrayCreate(allocator, DuskExpr *);
        DuskArray(const char *) field_names =
            duskArrayCreate(allocator, const char *);
        DuskArray(DuskArray(DuskAttribute)) field_attribute_arrays =
            duskArrayCreate(allocator, DuskArray(DuskAttribute));
        expr->struct_type.layout = DUSK_STRUCT_LAYOUT_UNKNOWN;

        DuskToken next_token = {0};
        tokenizerNextToken(compiler, *state, &next_token);
        if (next_token.type == DUSK_TOKEN_LPAREN) {
            consumeToken(compiler, state, DUSK_TOKEN_LPAREN);
            DuskToken layout_ident =
                consumeToken(compiler, state, DUSK_TOKEN_IDENT);
            if (strcmp(layout_ident.str, "std140") == 0) {
                expr->struct_type.layout = DUSK_STRUCT_LAYOUT_STD140;
            } else if (strcmp(layout_ident.str, "std430") == 0) {
                expr->struct_type.layout = DUSK_STRUCT_LAYOUT_STD430;
            } else {
                duskAddError(
                    compiler,
                    layout_ident.location,
                    "expected either 'std140' or 'std430' struct layouts");
            }

            consumeToken(compiler, state, DUSK_TOKEN_RPAREN);
        }

        consumeToken(compiler, state, DUSK_TOKEN_LCURLY);

        tokenizerNextToken(compiler, *state, &next_token);
        while (next_token.type != DUSK_TOKEN_RCURLY) {
            DuskArray(DuskAttribute) field_attributes =
                duskArrayCreate(allocator, DuskAttribute);
            parseAttributes(compiler, state, &field_attributes);

            DuskToken field_name_token =
                consumeToken(compiler, state, DUSK_TOKEN_IDENT);
            consumeToken(compiler, state, DUSK_TOKEN_COLON);

            DuskExpr *type_expr = parseExpr(compiler, state, true);

            duskArrayPush(&field_type_exprs, type_expr);
            duskArrayPush(&field_names, field_name_token.str);
            duskArrayPush(&field_attribute_arrays, field_attributes);

            tokenizerNextToken(compiler, *state, &next_token);
            if (next_token.type != DUSK_TOKEN_RCURLY) {
                consumeToken(compiler, state, DUSK_TOKEN_COMMA);
                tokenizerNextToken(compiler, *state, &next_token);
            }
        }

        expr->struct_type.field_count = duskArrayLength(field_type_exprs);

        expr->struct_type.field_names = DUSK_NEW_ARRAY(
            allocator, const char *, expr->struct_type.field_count);
        expr->struct_type.field_type_exprs = DUSK_NEW_ARRAY(
            allocator, DuskExpr *, expr->struct_type.field_count);
        expr->struct_type.field_attribute_arrays = DUSK_NEW_ARRAY(
            allocator, DuskArray(DuskAttribute), expr->struct_type.field_count);

        memcpy(
            expr->struct_type.field_names,
            field_names,
            expr->struct_type.field_count * sizeof(const char *));

        memcpy(
            expr->struct_type.field_type_exprs,
            field_type_exprs,
            expr->struct_type.field_count * sizeof(DuskExpr *));

        memcpy(
            expr->struct_type.field_attribute_arrays,
            field_attribute_arrays,
            expr->struct_type.field_count * sizeof(DuskArray(DuskAttribute)));

        consumeToken(compiler, state, DUSK_TOKEN_RCURLY);
        break;
    }
    case DUSK_TOKEN_BUILTIN_IDENT: {
        expr->kind = DUSK_EXPR_BUILTIN_FUNCTION_CALL;
        expr->builtin_call.params_arr = duskArrayCreate(allocator, DuskExpr *);

        uintptr_t builtin_function_type = 0;
        if (duskMapGet(
                compiler->builtin_function_map,
                token.str,
                (void **)&builtin_function_type)) {
            expr->builtin_call.kind = builtin_function_type;
        } else {
            duskAddError(
                compiler,
                token.location,
                "invalid builtin identifier: %s does not exist",
                tokenToString(allocator, &token));
            duskThrow(compiler);
        }

        consumeToken(compiler, state, DUSK_TOKEN_LPAREN);

        DuskToken next_token = {0};
        tokenizerNextToken(compiler, *state, &next_token);
        while (next_token.type != DUSK_TOKEN_RPAREN) {
            DuskExpr *param_expr = parseExpr(compiler, state, false);
            duskArrayPush(&expr->builtin_call.params_arr, param_expr);

            tokenizerNextToken(compiler, *state, &next_token);
            if (next_token.type != DUSK_TOKEN_RPAREN) {
                consumeToken(compiler, state, DUSK_TOKEN_COMMA);
                tokenizerNextToken(compiler, *state, &next_token);
            }
        }

        consumeToken(compiler, state, DUSK_TOKEN_RPAREN);
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

static DuskExpr *parseAccessOrFunctionCallExpr(
    DuskCompiler *compiler, TokenizerState *state, bool only_types)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    DuskExpr *expr = parsePrimaryExpr(compiler, state);
    DUSK_ASSERT(expr);

    DuskToken next_token = {0};
    TokenizerState next_state =
        tokenizerNextToken(compiler, *state, &next_token);

    while (next_token.type == DUSK_TOKEN_LPAREN ||
           next_token.type == DUSK_TOKEN_LBRACKET ||
           (next_token.type == DUSK_TOKEN_LCURLY && !only_types) ||
           next_token.type == DUSK_TOKEN_DOT) {
        if (next_token.type == DUSK_TOKEN_LPAREN) {
            // Function call expression
            DuskExpr *func_expr = expr;
            expr = DUSK_NEW(allocator, DuskExpr);
            expr->location = func_expr->location;
            expr->kind = DUSK_EXPR_FUNCTION_CALL;
            expr->function_call.func_expr = func_expr;
            expr->function_call.params_arr =
                duskArrayCreate(allocator, DuskExpr *);

            consumeToken(compiler, state, DUSK_TOKEN_LPAREN);

            tokenizerNextToken(compiler, *state, &next_token);
            while (next_token.type != DUSK_TOKEN_RPAREN) {
                DuskExpr *param_expr = parseExpr(compiler, state, false);
                duskArrayPush(&expr->function_call.params_arr, param_expr);

                tokenizerNextToken(compiler, *state, &next_token);
                if (next_token.type != DUSK_TOKEN_RPAREN) {
                    consumeToken(compiler, state, DUSK_TOKEN_COMMA);
                    tokenizerNextToken(compiler, *state, &next_token);
                }
            }

            consumeToken(compiler, state, DUSK_TOKEN_RPAREN);
        } else if (next_token.type == DUSK_TOKEN_DOT) {
            // Access expr
            tokenizerNextToken(compiler, next_state, &next_token);
            DuskExpr *base_expr = expr;
            expr = DUSK_NEW(allocator, DuskExpr);
            expr->location = base_expr->location;
            expr->kind = DUSK_EXPR_ACCESS;
            expr->access.base_expr = base_expr;
            expr->access.chain_arr = duskArrayCreate(allocator, DuskExpr *);

            tokenizerNextToken(compiler, *state, &next_token);
            while (next_token.type == DUSK_TOKEN_DOT) {
                consumeToken(compiler, state, DUSK_TOKEN_DOT);

                DuskToken ident_token =
                    consumeToken(compiler, state, DUSK_TOKEN_IDENT);

                DuskExpr *ident_expr = DUSK_NEW(allocator, DuskExpr);
                ident_expr->location = ident_token.location;
                ident_expr->kind = DUSK_EXPR_IDENT;
                ident_expr->identifier.str = ident_token.str;

                duskArrayPush(&expr->access.chain_arr, ident_expr);

                tokenizerNextToken(compiler, *state, &next_token);
            }
        } else if (next_token.type == DUSK_TOKEN_LBRACKET) {
            // Array access expression
            DuskExpr *base_expr = expr;
            expr = DUSK_NEW(allocator, DuskExpr);
            expr->location = base_expr->location;
            expr->kind = DUSK_EXPR_ARRAY_ACCESS;
            expr->access.base_expr = base_expr;
            expr->access.chain_arr = duskArrayCreate(allocator, DuskExpr *);

            tokenizerNextToken(compiler, *state, &next_token);
            while (next_token.type == DUSK_TOKEN_LBRACKET) {
                consumeToken(compiler, state, DUSK_TOKEN_LBRACKET);

                DuskExpr *index_expr = parseExpr(compiler, state, false);

                duskArrayPush(&expr->access.chain_arr, index_expr);

                consumeToken(compiler, state, DUSK_TOKEN_RBRACKET);

                tokenizerNextToken(compiler, *state, &next_token);
            }
        } else if (next_token.type == DUSK_TOKEN_LCURLY && !only_types) {
            // Struct literal
            consumeToken(compiler, state, DUSK_TOKEN_LCURLY);

            DuskExpr *type_expr = expr;
            expr = DUSK_NEW(allocator, DuskExpr);
            expr->location = type_expr->location;
            expr->kind = DUSK_EXPR_STRUCT_LITERAL;
            expr->struct_literal.type_expr = type_expr;
            expr->struct_literal.field_names_arr =
                duskArrayCreate(allocator, const char *);
            expr->struct_literal.field_values_arr =
                duskArrayCreate(allocator, DuskExpr *);

            tokenizerNextToken(compiler, *state, &next_token);
            while (next_token.type != DUSK_TOKEN_RCURLY) {
                consumeToken(compiler, state, DUSK_TOKEN_DOT);
                DuskToken ident_token =
                    consumeToken(compiler, state, DUSK_TOKEN_IDENT);
                consumeToken(compiler, state, DUSK_TOKEN_ASSIGN);
                DuskExpr *field_value_expr = parseExpr(compiler, state, false);

                duskArrayPush(
                    &expr->struct_literal.field_names_arr, ident_token.str);
                duskArrayPush(
                    &expr->struct_literal.field_values_arr, field_value_expr);

                tokenizerNextToken(compiler, *state, &next_token);
                if (next_token.type != DUSK_TOKEN_RCURLY) {
                    consumeToken(compiler, state, DUSK_TOKEN_COMMA);
                    tokenizerNextToken(compiler, *state, &next_token);
                }
            }

            consumeToken(compiler, state, DUSK_TOKEN_RCURLY);
        }

        next_state = tokenizerNextToken(compiler, *state, &next_token);
    }

    return expr;
}

static DuskExpr *
parseUnaryExpr(DuskCompiler *compiler, TokenizerState *state, bool only_types)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    DuskExpr *expr = NULL;

    DuskToken next_token = {0};
    tokenizerNextToken(compiler, *state, &next_token);
    while (next_token.type == DUSK_TOKEN_NOT ||
           next_token.type == DUSK_TOKEN_SUB ||
           next_token.type == DUSK_TOKEN_BITNOT) {
        consumeToken(compiler, state, next_token.type);

        DuskUnaryOp op = 0;
        switch (next_token.type) {
        case DUSK_TOKEN_NOT: op = DUSK_UNARY_OP_NOT; break;
        case DUSK_TOKEN_SUB: op = DUSK_UNARY_OP_NEGATE; break;
        case DUSK_TOKEN_BITNOT: op = DUSK_UNARY_OP_BITNOT; break;
        default: DUSK_ASSERT(0); break;
        }

        DuskExpr *new_expr = DUSK_NEW(allocator, DuskExpr);
        new_expr->location = next_token.location;
        new_expr->kind = DUSK_EXPR_UNARY;
        new_expr->unary.op = op;
        new_expr->unary.right = NULL;

        if (expr) {
            DUSK_ASSERT(expr->kind == DUSK_EXPR_UNARY);
            expr->unary.right = new_expr;
        }

        expr = new_expr;

        tokenizerNextToken(compiler, *state, &next_token);
    }

    if (!expr) {
        expr = parseAccessOrFunctionCallExpr(compiler, state, only_types);
    } else {
        DUSK_ASSERT(expr->kind == DUSK_EXPR_UNARY);
        expr->unary.right =
            parseAccessOrFunctionCallExpr(compiler, state, only_types);
    }

    return expr;
}

typedef struct {
    enum {
        DUSK_BINARY_OP_SYMBOL_EXPR,
        DUSK_BINARY_OP_SYMBOL_OPERATOR,
    } kind;
    union {
        DuskExpr *expr;
        DuskBinaryOp op;
    };
} DuskBinaryOpSymbol;

static DuskExpr *parseBinaryExpr(DuskCompiler *compiler, TokenizerState *state, bool only_types)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    DuskExpr *expr = parseUnaryExpr(compiler, state, only_types);
    DUSK_ASSERT(expr);

    DuskToken next_token = {0};
    tokenizerNextToken(compiler, *state, &next_token);
    switch (next_token.type) {
    case DUSK_TOKEN_ADD:
    case DUSK_TOKEN_SUB:
    case DUSK_TOKEN_MUL:
    case DUSK_TOKEN_DIV:
    case DUSK_TOKEN_MOD:
    case DUSK_TOKEN_BITOR:
    case DUSK_TOKEN_BITXOR:
    case DUSK_TOKEN_BITAND:
    case DUSK_TOKEN_LSHIFT:
    case DUSK_TOKEN_RSHIFT:
    case DUSK_TOKEN_EQ:
    case DUSK_TOKEN_NOTEQ:
    case DUSK_TOKEN_LESS:
    case DUSK_TOKEN_LESSEQ:
    case DUSK_TOKEN_GREATER:
    case DUSK_TOKEN_GREATEREQ:
    case DUSK_TOKEN_AND:
    case DUSK_TOKEN_OR: break;
    default: return expr;
    }

    DuskArray(DuskBinaryOp) op_stack_arr =
        duskArrayCreate(allocator, DuskBinaryOp);
    DuskArray(DuskBinaryOpSymbol) symbol_queue_arr =
        duskArrayCreate(allocator, DuskBinaryOpSymbol);

    static uint8_t precedences[DUSK_BINARY_OP_MAX] = {
        [DUSK_BINARY_OP_ADD] = 4,
        [DUSK_BINARY_OP_SUB] = 4,
        [DUSK_BINARY_OP_MUL] = 3,
        [DUSK_BINARY_OP_DIV] = 3,
        [DUSK_BINARY_OP_MOD] = 3,
        [DUSK_BINARY_OP_BITAND] = 8,
        [DUSK_BINARY_OP_BITOR] = 10,
        [DUSK_BINARY_OP_BITXOR] = 9,
        [DUSK_BINARY_OP_LSHIFT] = 5,
        [DUSK_BINARY_OP_RSHIFT] = 5,
        [DUSK_BINARY_OP_EQ] = 7,
        [DUSK_BINARY_OP_NOTEQ] = 7,
        [DUSK_BINARY_OP_LESS] = 6,
        [DUSK_BINARY_OP_LESSEQ] = 6,
        [DUSK_BINARY_OP_GREATER] = 6,
        [DUSK_BINARY_OP_GREATEREQ] = 6,
        [DUSK_BINARY_OP_AND] = 11,
        [DUSK_BINARY_OP_OR] = 12,
    };

    {
        DuskBinaryOpSymbol expr_symbol = {0};
        expr_symbol.kind = DUSK_BINARY_OP_SYMBOL_EXPR;
        expr_symbol.expr = expr;
        duskArrayPush(&symbol_queue_arr, expr_symbol);
    }

    while (next_token.type == DUSK_TOKEN_ADD ||
           next_token.type == DUSK_TOKEN_SUB ||
           next_token.type == DUSK_TOKEN_MUL ||
           next_token.type == DUSK_TOKEN_DIV ||
           next_token.type == DUSK_TOKEN_MOD ||
           next_token.type == DUSK_TOKEN_BITAND ||
           next_token.type == DUSK_TOKEN_BITOR ||
           next_token.type == DUSK_TOKEN_BITXOR ||
           next_token.type == DUSK_TOKEN_RSHIFT ||
           next_token.type == DUSK_TOKEN_LSHIFT ||
           next_token.type == DUSK_TOKEN_EQ ||
           next_token.type == DUSK_TOKEN_NOTEQ ||
           next_token.type == DUSK_TOKEN_LESS ||
           next_token.type == DUSK_TOKEN_LESSEQ ||
           next_token.type == DUSK_TOKEN_GREATER ||
           next_token.type == DUSK_TOKEN_GREATEREQ ||
           next_token.type == DUSK_TOKEN_AND ||
           next_token.type == DUSK_TOKEN_OR) {
        consumeToken(compiler, state, next_token.type);

        DuskBinaryOp op = 0;
        switch (next_token.type) {
        case DUSK_TOKEN_ADD: op = DUSK_BINARY_OP_ADD; break;
        case DUSK_TOKEN_SUB: op = DUSK_BINARY_OP_SUB; break;
        case DUSK_TOKEN_MUL: op = DUSK_BINARY_OP_MUL; break;
        case DUSK_TOKEN_DIV: op = DUSK_BINARY_OP_DIV; break;
        case DUSK_TOKEN_MOD: op = DUSK_BINARY_OP_MOD; break;
        case DUSK_TOKEN_BITAND: op = DUSK_BINARY_OP_BITAND; break;
        case DUSK_TOKEN_BITOR: op = DUSK_BINARY_OP_BITOR; break;
        case DUSK_TOKEN_BITXOR: op = DUSK_BINARY_OP_BITXOR; break;
        case DUSK_TOKEN_LSHIFT: op = DUSK_BINARY_OP_LSHIFT; break;
        case DUSK_TOKEN_RSHIFT: op = DUSK_BINARY_OP_RSHIFT; break;
        case DUSK_TOKEN_EQ: op = DUSK_BINARY_OP_EQ; break;
        case DUSK_TOKEN_NOTEQ: op = DUSK_BINARY_OP_NOTEQ; break;
        case DUSK_TOKEN_LESS: op = DUSK_BINARY_OP_LESS; break;
        case DUSK_TOKEN_LESSEQ: op = DUSK_BINARY_OP_LESSEQ; break;
        case DUSK_TOKEN_GREATER: op = DUSK_BINARY_OP_GREATER; break;
        case DUSK_TOKEN_GREATEREQ: op = DUSK_BINARY_OP_GREATEREQ; break;
        case DUSK_TOKEN_AND: op = DUSK_BINARY_OP_AND; break;
        case DUSK_TOKEN_OR: op = DUSK_BINARY_OP_OR; break;
        default: DUSK_ASSERT(0); break;
        }

        while (duskArrayLength(op_stack_arr) > 0 &&
               precedences[op_stack_arr[duskArrayLength(op_stack_arr) - 1]] <
                   precedences[op]) {
            DuskBinaryOp popped_op =
                op_stack_arr[duskArrayLength(op_stack_arr) - 1];
            duskArrayPop(&op_stack_arr);

            DuskBinaryOpSymbol op_symbol = {0};
            op_symbol.kind = DUSK_BINARY_OP_SYMBOL_OPERATOR;
            op_symbol.op = popped_op;
            duskArrayPush(&symbol_queue_arr, op_symbol);
        }

        duskArrayPush(&op_stack_arr, op);

        DuskExpr *right_expr = parseUnaryExpr(compiler, state, only_types);

        {
            DuskBinaryOpSymbol expr_symbol = {0};
            expr_symbol.kind = DUSK_BINARY_OP_SYMBOL_EXPR;
            expr_symbol.expr = right_expr;
            duskArrayPush(&symbol_queue_arr, expr_symbol);
        }

        tokenizerNextToken(compiler, *state, &next_token);
    }

    while (duskArrayLength(op_stack_arr) > 0) {
        DuskBinaryOp popped_op =
            op_stack_arr[duskArrayLength(op_stack_arr) - 1];
        duskArrayPop(&op_stack_arr);

        DuskBinaryOpSymbol op_symbol = {0};
        op_symbol.kind = DUSK_BINARY_OP_SYMBOL_OPERATOR;
        op_symbol.op = popped_op;
        duskArrayPush(&symbol_queue_arr, op_symbol);
    }

    DuskArray(DuskExpr *) expr_stack_arr =
        duskArrayCreate(allocator, DuskExpr *);

    for (size_t i = 0; i < duskArrayLength(symbol_queue_arr); ++i) {
        DuskBinaryOpSymbol symbol = symbol_queue_arr[i];
        if (symbol.kind == DUSK_BINARY_OP_SYMBOL_OPERATOR) {
            DUSK_ASSERT(duskArrayLength(expr_stack_arr) >= 2);
            DuskExpr *right_expr =
                expr_stack_arr[duskArrayLength(expr_stack_arr) - 1];
            DuskExpr *left_expr =
                expr_stack_arr[duskArrayLength(expr_stack_arr) - 2];
            duskArrayPop(&expr_stack_arr);
            duskArrayPop(&expr_stack_arr);

            DuskExpr *bin_expr = DUSK_NEW(allocator, DuskExpr);
            bin_expr->kind = DUSK_EXPR_BINARY;
            bin_expr->location = left_expr->location;
            bin_expr->binary.op = symbol.op;
            bin_expr->binary.left = left_expr;
            bin_expr->binary.right = right_expr;

            duskArrayPush(&expr_stack_arr, bin_expr);
        } else {
            duskArrayPush(&expr_stack_arr, symbol.expr);
        }
    }

    DUSK_ASSERT(duskArrayLength(expr_stack_arr) == 1);

    return expr_stack_arr[duskArrayLength(expr_stack_arr) - 1];
}

static DuskExpr *
parseExpr(DuskCompiler *compiler, TokenizerState *state, bool only_types)
{
    return parseBinaryExpr(compiler, state, only_types);
}

static DuskStmt *parseStmt(DuskCompiler *compiler, TokenizerState *state)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    DuskStmt *stmt = DUSK_NEW(allocator, DuskStmt);

    DuskToken next_token = {0};
    tokenizerNextToken(compiler, *state, &next_token);
    stmt->location = next_token.location;

    switch (next_token.type) {
    case DUSK_TOKEN_LET: {
        consumeToken(compiler, state, DUSK_TOKEN_LET);

        DuskDecl *decl = DUSK_NEW(allocator, DuskDecl);

        DuskToken name_token = consumeToken(compiler, state, DUSK_TOKEN_IDENT);

        DuskExpr *value_expr = NULL;

        consumeToken(compiler, state, DUSK_TOKEN_COLON);
        DuskExpr *type_expr = parseExpr(compiler, state, true);

        tokenizerNextToken(compiler, *state, &next_token);
        if (next_token.type == DUSK_TOKEN_ASSIGN) {
            consumeToken(compiler, state, DUSK_TOKEN_ASSIGN);
            value_expr = parseExpr(compiler, state, false);
        }

        decl->kind = DUSK_DECL_VAR;
        decl->location = stmt->location;
        decl->name = name_token.str;
        decl->var.storage_class = DUSK_STORAGE_CLASS_FUNCTION;
        decl->var.type_expr = type_expr;
        decl->var.value_expr = value_expr;

        stmt->kind = DUSK_STMT_DECL;
        stmt->decl = decl;

        consumeToken(compiler, state, DUSK_TOKEN_SEMICOLON);
        break;
    }

    case DUSK_TOKEN_LCURLY: {
        stmt->kind = DUSK_STMT_BLOCK;
        stmt->block.stmts_arr = duskArrayCreate(allocator, DuskStmt *);

        consumeToken(compiler, state, DUSK_TOKEN_LCURLY);

        tokenizerNextToken(compiler, *state, &next_token);
        while (next_token.type != DUSK_TOKEN_RCURLY) {
            DuskStmt *sub_stmt = parseStmt(compiler, state);
            duskArrayPush(&stmt->block.stmts_arr, sub_stmt);

            tokenizerNextToken(compiler, *state, &next_token);
        }

        consumeToken(compiler, state, DUSK_TOKEN_RCURLY);
        break;
    }

    case DUSK_TOKEN_RETURN: {
        stmt->kind = DUSK_STMT_RETURN;
        stmt->return_.expr = NULL;

        consumeToken(compiler, state, DUSK_TOKEN_RETURN);

        tokenizerNextToken(compiler, *state, &next_token);
        if (next_token.type != DUSK_TOKEN_SEMICOLON) {
            stmt->return_.expr = parseExpr(compiler, state, false);
        }

        consumeToken(compiler, state, DUSK_TOKEN_SEMICOLON);
        break;
    }

    case DUSK_TOKEN_DISCARD: {
        stmt->kind = DUSK_STMT_DISCARD;
        consumeToken(compiler, state, DUSK_TOKEN_DISCARD);
        consumeToken(compiler, state, DUSK_TOKEN_SEMICOLON);
        break;
    }

    case DUSK_TOKEN_IF: {
        stmt->kind = DUSK_STMT_IF;

        consumeToken(compiler, state, DUSK_TOKEN_IF);
        consumeToken(compiler, state, DUSK_TOKEN_LPAREN);
        stmt->if_.cond_expr = parseExpr(compiler, state, false);
        consumeToken(compiler, state, DUSK_TOKEN_RPAREN);

        stmt->if_.true_stmt = parseStmt(compiler, state);
        stmt->if_.false_stmt = NULL;

        tokenizerNextToken(compiler, *state, &next_token);
        if (next_token.type == DUSK_TOKEN_ELSE) {
            consumeToken(compiler, state, DUSK_TOKEN_ELSE);
            stmt->if_.false_stmt = parseStmt(compiler, state);
        }
        break;
    }

    case DUSK_TOKEN_WHILE: {
        stmt->kind = DUSK_STMT_WHILE;

        consumeToken(compiler, state, DUSK_TOKEN_WHILE);
        consumeToken(compiler, state, DUSK_TOKEN_LPAREN);
        stmt->while_.cond_expr = parseExpr(compiler, state, false);
        consumeToken(compiler, state, DUSK_TOKEN_RPAREN);

        stmt->while_.stmt = parseStmt(compiler, state);

        break;
    }

    case DUSK_TOKEN_CONTINUE: {
        stmt->kind = DUSK_STMT_CONTINUE;
        consumeToken(compiler, state, DUSK_TOKEN_CONTINUE);
        consumeToken(compiler, state, DUSK_TOKEN_SEMICOLON);
        break;
    }

    case DUSK_TOKEN_BREAK: {
        stmt->kind = DUSK_STMT_BREAK;
        consumeToken(compiler, state, DUSK_TOKEN_BREAK);
        consumeToken(compiler, state, DUSK_TOKEN_SEMICOLON);
        break;
    }

    default: {
        DuskExpr *expr = parseExpr(compiler, state, false);

        tokenizerNextToken(compiler, *state, &next_token);
        switch (next_token.type) {
        case DUSK_TOKEN_ASSIGN: {
            consumeToken(compiler, state, DUSK_TOKEN_ASSIGN);

            DuskExpr *value_expr = parseExpr(compiler, state, false);

            stmt->kind = DUSK_STMT_ASSIGN;
            stmt->assign.assigned_expr = expr;
            stmt->assign.value_expr = value_expr;
            break;
        }
        case DUSK_TOKEN_ADD_ASSIGN:
        case DUSK_TOKEN_SUB_ASSIGN:
        case DUSK_TOKEN_MUL_ASSIGN:
        case DUSK_TOKEN_DIV_ASSIGN:
        case DUSK_TOKEN_MOD_ASSIGN:
        case DUSK_TOKEN_BITAND_ASSIGN:
        case DUSK_TOKEN_BITOR_ASSIGN:
        case DUSK_TOKEN_BITXOR_ASSIGN:
        case DUSK_TOKEN_LSHIFT_ASSIGN:
        case DUSK_TOKEN_RSHIFT_ASSIGN: {
            DuskBinaryOp op = 0;
            switch (next_token.type) {
            case DUSK_TOKEN_ADD_ASSIGN: op = DUSK_BINARY_OP_ADD; break;
            case DUSK_TOKEN_SUB_ASSIGN: op = DUSK_BINARY_OP_SUB; break;
            case DUSK_TOKEN_MUL_ASSIGN: op = DUSK_BINARY_OP_MUL; break;
            case DUSK_TOKEN_DIV_ASSIGN: op = DUSK_BINARY_OP_DIV; break;
            case DUSK_TOKEN_MOD_ASSIGN: op = DUSK_BINARY_OP_MOD; break;
            case DUSK_TOKEN_BITAND_ASSIGN: op = DUSK_BINARY_OP_BITAND; break;
            case DUSK_TOKEN_BITOR_ASSIGN: op = DUSK_BINARY_OP_BITOR; break;
            case DUSK_TOKEN_BITXOR_ASSIGN: op = DUSK_BINARY_OP_BITXOR; break;
            case DUSK_TOKEN_LSHIFT_ASSIGN: op = DUSK_BINARY_OP_LSHIFT; break;
            case DUSK_TOKEN_RSHIFT_ASSIGN: op = DUSK_BINARY_OP_RSHIFT; break;
            default: DUSK_ASSERT(0);
            }

            consumeToken(compiler, state, next_token.type);

            DuskExpr *value_expr = parseExpr(compiler, state, false);

            DuskExpr *bin_expr = DUSK_NEW(allocator, DuskExpr);
            bin_expr->kind = DUSK_EXPR_BINARY;
            bin_expr->location = stmt->location;
            bin_expr->binary.op = op;
            bin_expr->binary.left = expr;
            bin_expr->binary.right = value_expr;

            stmt->kind = DUSK_STMT_ASSIGN;
            stmt->assign.assigned_expr = expr;
            stmt->assign.value_expr = bin_expr;

            break;
        }
        default: {
            stmt->kind = DUSK_STMT_EXPR;
            stmt->expr = expr;
            break;
        }
        }

        consumeToken(compiler, state, DUSK_TOKEN_SEMICOLON);

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

    decl->attributes_arr = duskArrayCreate(allocator, DuskAttribute);
    parseAttributes(compiler, state, &decl->attributes_arr);

    DuskToken next_token = {0};
    tokenizerNextToken(compiler, *state, &next_token);
    decl->location = next_token.location;
    switch (next_token.type) {
    case DUSK_TOKEN_FN: {
        consumeToken(compiler, state, DUSK_TOKEN_FN);

        DuskToken name_token = consumeToken(compiler, state, DUSK_TOKEN_IDENT);

        decl->kind = DUSK_DECL_FUNCTION;
        decl->name = name_token.str;
        decl->function.parameter_decls_arr =
            duskArrayCreate(allocator, DuskDecl *);
        decl->function.stmts_arr = duskArrayCreate(allocator, DuskStmt *);

        consumeToken(compiler, state, DUSK_TOKEN_LPAREN);

        DuskToken next_token = {0};
        tokenizerNextToken(compiler, *state, &next_token);
        while (next_token.type != DUSK_TOKEN_RPAREN) {
            DuskArray(DuskAttribute) param_attributes =
                duskArrayCreate(allocator, DuskAttribute);
            parseAttributes(compiler, state, &param_attributes);

            DuskToken param_ident =
                consumeToken(compiler, state, DUSK_TOKEN_IDENT);

            consumeToken(compiler, state, DUSK_TOKEN_COLON);

            DuskExpr *param_type_expr = parseExpr(compiler, state, true);

            DuskDecl *param_decl = DUSK_NEW(allocator, DuskDecl);
            param_decl->kind = DUSK_DECL_VAR;
            param_decl->location = param_ident.location;
            param_decl->name = param_ident.str;
            param_decl->attributes_arr = param_attributes;
            param_decl->var.type_expr = param_type_expr;
            param_decl->var.value_expr = NULL;
            param_decl->var.storage_class = DUSK_STORAGE_CLASS_PARAMETER;
            duskArrayPush(&decl->function.parameter_decls_arr, param_decl);

            tokenizerNextToken(compiler, *state, &next_token);
            if (next_token.type != DUSK_TOKEN_RPAREN) {
                consumeToken(compiler, state, DUSK_TOKEN_COMMA);
                tokenizerNextToken(compiler, *state, &next_token);
            }
        }

        consumeToken(compiler, state, DUSK_TOKEN_RPAREN);

        DuskArray(DuskAttribute) return_type_attributes =
            duskArrayCreate(allocator, DuskAttribute);
        parseAttributes(compiler, state, &return_type_attributes);

        decl->function.return_type_expr = parseExpr(compiler, state, true);
        decl->function.return_type_attributes_arr = return_type_attributes;

        consumeToken(compiler, state, DUSK_TOKEN_LCURLY);

        tokenizerNextToken(compiler, *state, &next_token);
        while (next_token.type != DUSK_TOKEN_RCURLY) {
            DuskStmt *stmt = parseStmt(compiler, state);
            duskArrayPush(&decl->function.stmts_arr, stmt);

            tokenizerNextToken(compiler, *state, &next_token);
        }

        consumeToken(compiler, state, DUSK_TOKEN_RCURLY);
        break;
    }
    case DUSK_TOKEN_LET: {
        consumeToken(compiler, state, DUSK_TOKEN_LET);

        DuskStorageClass storage_class = DUSK_STORAGE_CLASS_UNIFORM_CONSTANT;

        tokenizerNextToken(compiler, *state, &next_token);
        if (next_token.type == DUSK_TOKEN_LPAREN) {
            consumeToken(compiler, state, DUSK_TOKEN_LPAREN);
            DuskToken storage_class_ident =
                consumeToken(compiler, state, DUSK_TOKEN_IDENT);
            consumeToken(compiler, state, DUSK_TOKEN_RPAREN);

            if (strcmp(storage_class_ident.str, "uniform") == 0) {
                storage_class = DUSK_STORAGE_CLASS_UNIFORM;
            } else if (strcmp(storage_class_ident.str, "storage") == 0) {
                storage_class = DUSK_STORAGE_CLASS_STORAGE;
            } else if (strcmp(storage_class_ident.str, "push_constant") == 0) {
                storage_class = DUSK_STORAGE_CLASS_PUSH_CONSTANT;
            } else if (strcmp(storage_class_ident.str, "workgroup") == 0) {
                storage_class = DUSK_STORAGE_CLASS_WORKGROUP;
            } else {
                duskAddError(
                    compiler,
                    storage_class_ident.location,
                    "invalid storage class: '%s'",
                    storage_class_ident.str);
            }
        }

        DuskToken name_token = consumeToken(compiler, state, DUSK_TOKEN_IDENT);

        consumeToken(compiler, state, DUSK_TOKEN_COLON);
        DuskExpr *type_expr = parseExpr(compiler, state, true);

        decl->kind = DUSK_DECL_VAR;
        decl->name = name_token.str;
        decl->var.storage_class = storage_class;
        decl->var.type_expr = type_expr;
        decl->var.value_expr = NULL;

        consumeToken(compiler, state, DUSK_TOKEN_SEMICOLON);

        break;
    }
    case DUSK_TOKEN_TYPE: {
        consumeToken(compiler, state, DUSK_TOKEN_TYPE);

        DuskToken name_token = consumeToken(compiler, state, DUSK_TOKEN_IDENT);

        DuskExpr *type_expr = parseExpr(compiler, state, true);

        decl->kind = DUSK_DECL_TYPE;
        decl->name = name_token.str;
        decl->typedef_.type_expr = type_expr;

        consumeToken(compiler, state, DUSK_TOKEN_SEMICOLON);
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
    TokenizerState state = tokenizerCreate(file);

    while (1) {
        DuskToken token = {0};
        tokenizerNextToken(compiler, state, &token);
        if (token.type == DUSK_TOKEN_ERROR) {
            duskAddError(
                compiler, token.location, "unexpected token: %s", token.str);
            duskThrow(compiler);
        }

        if (token.type == DUSK_TOKEN_EOF) {
            break;
        }

        DuskDecl *decl = parseTopLevelDecl(compiler, &state);
        duskArrayPush(&file->decls_arr, decl);
    }
}
