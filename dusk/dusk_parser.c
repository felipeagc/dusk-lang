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

    case DUSK_TOKEN_FLOAT: return "float";
    case DUSK_TOKEN_FLOAT2: return "float2";
    case DUSK_TOKEN_FLOAT3: return "float3";
    case DUSK_TOKEN_FLOAT4: return "float4";
    case DUSK_TOKEN_FLOAT2X2: return "float2x2";
    case DUSK_TOKEN_FLOAT3X3: return "float3x3";
    case DUSK_TOKEN_FLOAT4X4: return "float4x4";

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

    case DUSK_TOKEN_FLOAT: return "float";
    case DUSK_TOKEN_FLOAT2: return "float2";
    case DUSK_TOKEN_FLOAT3: return "float3";
    case DUSK_TOKEN_FLOAT4: return "float4";
    case DUSK_TOKEN_FLOAT2X2: return "float2x2";
    case DUSK_TOKEN_FLOAT3X3: return "float3x3";
    case DUSK_TOKEN_FLOAT4X4: return "float4x4";

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

static DuskExpr *parseExpr(DuskCompiler *compiler, TokenizerState *state);

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
            } else if (strcmp(attrib_name_token.str, "uniform") == 0) {
                attrib.kind = DUSK_ATTRIBUTE_UNIFORM;
            } else if (strcmp(attrib_name_token.str, "storage") == 0) {
                attrib.kind = DUSK_ATTRIBUTE_STORAGE;
            } else if (strcmp(attrib_name_token.str, "push_constant") == 0) {
                attrib.kind = DUSK_ATTRIBUTE_PUSH_CONSTANT;
            } else if (strcmp(attrib_name_token.str, "offset") == 0) {
                attrib.kind = DUSK_ATTRIBUTE_OFFSET;
            }
            attrib.name = attrib_name_token.str;
            DuskArray(DuskExpr *) value_exprs =
                duskArrayCreate(allocator, DuskExpr *);

            tokenizerNextToken(compiler, *state, &next_token);
            if (next_token.type == DUSK_TOKEN_LPAREN) {
                consumeToken(compiler, state, DUSK_TOKEN_LPAREN);

                while (next_token.type != DUSK_TOKEN_RPAREN) {
                    DuskExpr *value_expr = parseExpr(compiler, state);
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
        expr = parseExpr(compiler, state);
        consumeToken(compiler, state, DUSK_TOKEN_RPAREN);
        break;
    }
    case DUSK_TOKEN_LBRACKET: {
        expr->kind = DUSK_EXPR_RUNTIME_ARRAY_TYPE;

        tokenizerNextToken(compiler, *state, &token);
        if (token.type != DUSK_TOKEN_RBRACKET) {
            expr->kind = DUSK_EXPR_ARRAY_TYPE;
            expr->array_type.size_expr = parseExpr(compiler, state);
        }

        consumeToken(compiler, state, DUSK_TOKEN_RBRACKET);

        expr->array_type.sub_expr = parseExpr(compiler, state);
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

            DuskExpr *type_expr = parseExpr(compiler, state);

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
            DuskExpr *param_expr = parseExpr(compiler, state);
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

static DuskExpr *parseAccessExpr(DuskCompiler *compiler, TokenizerState *state)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    DuskExpr *expr = parsePrimaryExpr(compiler, state);
    DUSK_ASSERT(expr);

    DuskToken next_token = {0};
    TokenizerState next_state =
        tokenizerNextToken(compiler, *state, &next_token);

    if (next_token.type == DUSK_TOKEN_DOT) {
        tokenizerNextToken(compiler, next_state, &next_token);
        if (next_token.type == DUSK_TOKEN_LCURLY) {
            // Struct literal
            consumeToken(compiler, state, DUSK_TOKEN_DOT);
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
                DuskToken ident_token =
                    consumeToken(compiler, state, DUSK_TOKEN_IDENT);
                consumeToken(compiler, state, DUSK_TOKEN_COLON);
                DuskExpr *field_value_expr = parseExpr(compiler, state);

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
        } else {
            // Access expr
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
        }
    }

    return expr;
}

static DuskExpr *
parseFunctionCallExpr(DuskCompiler *compiler, TokenizerState *state)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    DuskExpr *expr = parseAccessExpr(compiler, state);
    DUSK_ASSERT(expr);

    DuskToken next_token = {0};
    tokenizerNextToken(compiler, *state, &next_token);

    if (next_token.type == DUSK_TOKEN_LPAREN) {
        DuskExpr *func_expr = expr;
        expr = DUSK_NEW(allocator, DuskExpr);
        expr->location = func_expr->location;
        expr->kind = DUSK_EXPR_FUNCTION_CALL;
        expr->function_call.func_expr = func_expr;
        expr->function_call.params_arr = duskArrayCreate(allocator, DuskExpr *);

        consumeToken(compiler, state, DUSK_TOKEN_LPAREN);

        tokenizerNextToken(compiler, *state, &next_token);
        while (next_token.type != DUSK_TOKEN_RPAREN) {
            DuskExpr *param_expr = parseExpr(compiler, state);
            duskArrayPush(&expr->function_call.params_arr, param_expr);

            tokenizerNextToken(compiler, *state, &next_token);
            if (next_token.type != DUSK_TOKEN_RPAREN) {
                consumeToken(compiler, state, DUSK_TOKEN_COMMA);
                tokenizerNextToken(compiler, *state, &next_token);
            }
        }

        consumeToken(compiler, state, DUSK_TOKEN_RPAREN);
    }

    return expr;
}

static DuskExpr *parseExpr(DuskCompiler *compiler, TokenizerState *state)
{
    return parseFunctionCallExpr(compiler, state);
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
        DuskExpr *type_expr = parseExpr(compiler, state);

        tokenizerNextToken(compiler, *state, &next_token);
        if (next_token.type == DUSK_TOKEN_ASSIGN) {
            consumeToken(compiler, state, DUSK_TOKEN_ASSIGN);
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
            stmt->return_.expr = parseExpr(compiler, state);
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

    default: {
        DuskExpr *expr = parseExpr(compiler, state);

        tokenizerNextToken(compiler, *state, &next_token);
        switch (next_token.type) {
        case DUSK_TOKEN_ASSIGN: {
            consumeToken(compiler, state, DUSK_TOKEN_ASSIGN);

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

            DuskExpr *param_type_expr = parseExpr(compiler, state);

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

        decl->function.return_type_expr = parseExpr(compiler, state);
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

        DuskToken name_token = consumeToken(compiler, state, DUSK_TOKEN_IDENT);

        consumeToken(compiler, state, DUSK_TOKEN_COLON);
        DuskExpr *type_expr = parseExpr(compiler, state);

        decl->kind = DUSK_DECL_VAR;
        decl->name = name_token.str;
        decl->var.storage_class = DUSK_STORAGE_CLASS_FUNCTION;
        decl->var.type_expr = type_expr;
        decl->var.value_expr = NULL;

        consumeToken(compiler, state, DUSK_TOKEN_SEMICOLON);

        break;
    }
    case DUSK_TOKEN_TYPE: {
        consumeToken(compiler, state, DUSK_TOKEN_TYPE);

        DuskToken name_token = consumeToken(compiler, state, DUSK_TOKEN_IDENT);

        DuskExpr *type_expr = parseExpr(compiler, state);

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
