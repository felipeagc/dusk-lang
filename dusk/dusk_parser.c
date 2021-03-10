#include "dusk_internal.h"

#include <stdio.h>
#include <stdlib.h>

typedef enum DuskTokenType {
    TOKEN_ERROR = 0,

    TOKEN_IDENT,
    TOKEN_BUILTIN_IDENT,
    TOKEN_STRING,
    TOKEN_INT_LITERAL,
    TOKEN_FLOAT_LITERAL,

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

typedef struct DuskToken
{
    TokenType type;
    size_t pos;
    union
    {
        const char *str;
        int64_t int_;
        double float_;
    };
} Token;

typedef struct TokenizerState
{
    const char *text;
    size_t length;
    size_t pos;
} TokenizerState;

static const char *tokenToString(DuskAllocator *allocator, const Token *token)
{
    switch (token->type)
    {
    case TOKEN_ERROR: return "<error>";

    case TOKEN_IDENT: return token->str;
    case TOKEN_BUILTIN_IDENT: return duskSprintf(allocator, "@%s", token->str);
    case TOKEN_STRING: return duskSprintf(allocator, "\"%s\"", token->str);
    case TOKEN_INT_LITERAL: return duskSprintf(allocator, "%ld", token->int_);
    case TOKEN_FLOAT_LITERAL: return duskSprintf(allocator, "%lf", token->float_);

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

DUSK_INLINE static int64_t tokenizerLengthLeft(TokenizerState state, size_t offset)
{
    return ((int64_t)state.length) - (int64_t)(state.pos + offset);
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
    return (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || (c >= '0' && c <= '9');
}

DUSK_INLINE static bool isNum(char c)
{
    return (c >= '0' && c <= '9');
}

static TokenizerState tokenizerCreate(const char *text, size_t length)
{
    TokenizerState state = {0};
    state.text = text;
    state.length = length;
    state.pos = 0;
    return state;
}

static TokenizerState
tokenizerNextToken(DuskAllocator *allocator, TokenizerState state, Token *token)
{
    (void)allocator;
    *token = (Token){0};

    // Skip whitespace
    for (size_t i = state.pos; i < state.length; ++i)
    {
        if (isWhitespace(state.text[i]))
            state.pos++;
        else
            break;
    }

    token->pos = state.pos;

    if (tokenizerLengthLeft(state, 0) <= 0)
    {
        token->type = TOKEN_EOF;
        return state;
    }

    char c = state.text[state.pos];
    switch (c)
    {
    case '\"': {
        // String
        state.pos++;

        const char *string = &state.text[state.pos];

        size_t content_length = 0;
        while (tokenizerLengthLeft(state, content_length) > 0 &&
               state.text[state.pos + content_length] != '\"')
        {
            content_length++;
        }

        state.pos += content_length;

        if (tokenizerLengthLeft(state, 0) > 0 && state.text[state.pos] == '\"')
        {
            state.pos++;
        }
        else
        {
            token->type = TOKEN_ERROR;
            token->str = duskStrdup(allocator, "unclosed string");
            break;
        }

        token->type = TOKEN_STRING;
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
        if (tokenizerLengthLeft(state, 0) > 0 && state.text[state.pos] == '=')
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
            switch (state.text[state.pos])
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
            switch (state.text[state.pos])
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
        if (tokenizerLengthLeft(state, 0) > 0 && state.text[state.pos] == '=')
        {
            state.pos++;
            token->type = TOKEN_MUL_ASSIGN;
        }
        break;
    }

    case '/': {
        state.pos++;
        token->type = TOKEN_DIV;
        if (tokenizerLengthLeft(state, 0) > 0 && state.text[state.pos] == '=')
        {
            state.pos++;
            token->type = TOKEN_DIV_ASSIGN;
        }
        break;
    }

    case '%': {
        state.pos++;
        token->type = TOKEN_MOD;
        if (tokenizerLengthLeft(state, 0) > 0 && state.text[state.pos] == '=')
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
            switch (state.text[state.pos])
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
            switch (state.text[state.pos])
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
        if (tokenizerLengthLeft(state, 0) > 0 && state.text[state.pos] == '=')
        {
            state.pos++;
            token->type = TOKEN_BITXOR_ASSIGN;
        }
        break;
    }

    case '~': {
        state.pos++;
        token->type = TOKEN_BITNOT;
        if (tokenizerLengthLeft(state, 0) > 0 && state.text[state.pos] == '=')
        {
            state.pos++;
            token->type = TOKEN_BITNOT_ASSIGN;
        }
        break;
    }

    case '!': {
        state.pos++;
        token->type = TOKEN_NOT;
        if (tokenizerLengthLeft(state, 0) > 0 && state.text[state.pos] == '=')
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
            switch (state.text[state.pos])
            {
            case '=':
                state.pos++;
                token->type = TOKEN_LESSEQ;
                break;
            case '<':
                state.pos++;
                token->type = TOKEN_LSHIFT;
                if (tokenizerLengthLeft(state, 0) > 0 && state.text[state.pos] == '=')
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
            switch (state.text[state.pos])
            {
            case '=':
                state.pos++;
                token->type = TOKEN_GREATEREQ;
                break;
            case '>':
                state.pos++;
                token->type = TOKEN_RSHIFT;
                if (tokenizerLengthLeft(state, 0) > 0 && state.text[state.pos] == '=')
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
                   isAlphaNum(state.text[state.pos + ident_length]))
            {
                ident_length++;
            }

            token->type = TOKEN_IDENT;
            token->str =
                duskNullTerminate(allocator, &state.text[state.pos], ident_length);
            state.pos += ident_length;
        }
        else if (c == '@' && isAlpha(state.text[state.pos + 1]))
        {
            // Builtin Identifier
            state.pos++;
            size_t ident_length = 0;
            while (tokenizerLengthLeft(state, ident_length) > 0 &&
                   isAlphaNum(state.text[state.pos + ident_length]))
            {
                ident_length++;
            }

            token->type = TOKEN_BUILTIN_IDENT;
            token->str =
                duskNullTerminate(allocator, &state.text[state.pos], ident_length);
            state.pos += ident_length;
        }
        else if (isNum(c))
        {
            if (tokenizerLengthLeft(state, 0) >= 3 && state.text[state.pos] == '0' &&
                state.text[state.pos + 1] == 'x' && isHex(state.text[state.pos + 2]))
            {
                token->type = TOKEN_INT_LITERAL;
                state.pos += 2;

                size_t number_length = 0;
                while (tokenizerLengthLeft(state, number_length) > 0 &&
                       isHex(state.text[state.pos + number_length]))
                {
                    number_length++;
                }

                const char *int_str =
                    duskNullTerminate(allocator, &state.text[state.pos], number_length);
                state.pos += number_length;
                token->int_ = strtol(int_str, NULL, 16);
            }
            else
            {
                token->type = TOKEN_INT_LITERAL;

                size_t number_length = 0;
                while (tokenizerLengthLeft(state, number_length) > 0 &&
                       isNum(state.text[state.pos + number_length]))
                {
                    number_length++;
                }

                if (tokenizerLengthLeft(state, number_length) > 0 &&
                    state.text[state.pos + number_length] == '.')
                {
                    token->type = TOKEN_FLOAT_LITERAL;
                    number_length++;
                }

                while (tokenizerLengthLeft(state, number_length) > 0 &&
                       isNum(state.text[state.pos + number_length]))
                {
                    number_length++;
                }

                switch (token->type)
                {
                case TOKEN_INT_LITERAL: {
                    const char *int_str = duskNullTerminate(
                        allocator, &state.text[state.pos], number_length);
                    token->int_ = strtol(int_str, NULL, 10);
                    break;
                }
                case TOKEN_FLOAT_LITERAL: {
                    const char *float_str = duskNullTerminate(
                        allocator, &state.text[state.pos], number_length);
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
            token->str =
                duskSprintf(allocator, "unknown token: '%c'", state.text[state.pos]);
            state.pos++;
        }
        break;
    }
    }

    return state;
}

void duskParse(DuskCompiler *compiler, const char *text, size_t text_size)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);
    TokenizerState state = tokenizerCreate(text, text_size);

    Token token;
    while (1)
    {
        state = tokenizerNextToken(allocator, state, &token);
        puts(tokenToString(allocator, &token));
        if (token.type == TOKEN_ERROR)
        {
            printf("tokenizer error: %s\n", token.str);
        }
        if (token.type == TOKEN_EOF)
        {
            break;
        }
    }
}
