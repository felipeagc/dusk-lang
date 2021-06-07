#include "dusk_internal.h"

#include <stdio.h>
#include <stdlib.h>

static const char *DUSK_BUILTIN_FUNCTION_NAMES[DUSK_BUILTIN_FUNCTION_MAX] = {
    [DUSK_BUILTIN_FUNCTION_SAMPLER_TYPE] = "Sampler",
    [DUSK_BUILTIN_FUNCTION_IMAGE_1D_TYPE] = "Image1D",
    [DUSK_BUILTIN_FUNCTION_IMAGE_2D_TYPE] = "Image2D",
    [DUSK_BUILTIN_FUNCTION_IMAGE_2D_ARRAY_TYPE] = "Image2DArray",
    [DUSK_BUILTIN_FUNCTION_IMAGE_3D_TYPE] = "Image3D",
    [DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_TYPE] = "ImageCube",
    [DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_ARRAY_TYPE] = "ImageCubeArray",
    [DUSK_BUILTIN_FUNCTION_IMAGE_1D_SAMPLER_TYPE] = "Image1DSampler",
    [DUSK_BUILTIN_FUNCTION_IMAGE_2D_SAMPLER_TYPE] = "Image2DSampler",
    [DUSK_BUILTIN_FUNCTION_IMAGE_2D_ARRAY_SAMPLER_TYPE] = "Image2DArraySampler",
    [DUSK_BUILTIN_FUNCTION_IMAGE_3D_SAMPLER_TYPE] = "Image3DSampler",
    [DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_SAMPLER_TYPE] = "ImageCubeSampler",
    [DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_ARRAY_SAMPLER_TYPE] =
        "ImageCubeArraySampler",

    [DUSK_BUILTIN_FUNCTION_SIN] = "sin",
    [DUSK_BUILTIN_FUNCTION_COS] = "cos",
    [DUSK_BUILTIN_FUNCTION_TAN] = "tan",
    [DUSK_BUILTIN_FUNCTION_ASIN] = "asin",
    [DUSK_BUILTIN_FUNCTION_ACOS] = "acos",
    [DUSK_BUILTIN_FUNCTION_ATAN] = "atan",
    [DUSK_BUILTIN_FUNCTION_SINH] = "sinh",
    [DUSK_BUILTIN_FUNCTION_COSH] = "cosh",
    [DUSK_BUILTIN_FUNCTION_TANH] = "tanh",
    [DUSK_BUILTIN_FUNCTION_ASINH] = "asinh",
    [DUSK_BUILTIN_FUNCTION_ACOSH] = "acosh",
    [DUSK_BUILTIN_FUNCTION_ATANH] = "atanh",

    [DUSK_BUILTIN_FUNCTION_RADIANS] = "radians",
    [DUSK_BUILTIN_FUNCTION_DEGREES] = "degrees",
    [DUSK_BUILTIN_FUNCTION_ROUND] = "round",
    [DUSK_BUILTIN_FUNCTION_TRUNC] = "trunc",
    [DUSK_BUILTIN_FUNCTION_FLOOR] = "floor",
    [DUSK_BUILTIN_FUNCTION_CEIL] = "ceil",
    [DUSK_BUILTIN_FUNCTION_FRACT] = "fract",
    [DUSK_BUILTIN_FUNCTION_SQRT] = "sqrt",
    [DUSK_BUILTIN_FUNCTION_INVERSE_SQRT] = "inverseSqrt",
    [DUSK_BUILTIN_FUNCTION_LOG] = "log",
    [DUSK_BUILTIN_FUNCTION_LOG2] = "log2",
    [DUSK_BUILTIN_FUNCTION_EXP] = "exp",
    [DUSK_BUILTIN_FUNCTION_EXP2] = "exp2",
};

DuskCompiler *duskCompilerCreate(void)
{
    DuskCompiler *compiler = malloc(sizeof(*compiler));

    DuskArena *arena = duskArenaCreate(NULL, 1 << 13);
    DuskAllocator *allocator = duskArenaGetAllocator(arena);

    *compiler = (DuskCompiler){
        .main_arena = arena,
        .errors_arr = duskArrayCreate(allocator, DuskError),
        .type_cache = duskMapCreate(allocator, 32),
        .types_arr = duskArrayCreate(allocator, DuskType *),

        .keyword_map = duskMapCreate(allocator, 128),
        .builtin_function_map = duskMapCreate(allocator, 32),
    };

    duskMapSet(compiler->keyword_map, "let", (void *)DUSK_TOKEN_LET);
    duskMapSet(compiler->keyword_map, "fn", (void *)DUSK_TOKEN_FN);
    duskMapSet(compiler->keyword_map, "const", (void *)DUSK_TOKEN_CONST);
    duskMapSet(compiler->keyword_map, "struct", (void *)DUSK_TOKEN_STRUCT);
    duskMapSet(compiler->keyword_map, "type", (void *)DUSK_TOKEN_TYPE);
    duskMapSet(compiler->keyword_map, "import", (void *)DUSK_TOKEN_IMPORT);
    duskMapSet(compiler->keyword_map, "break", (void *)DUSK_TOKEN_BREAK);
    duskMapSet(compiler->keyword_map, "continue", (void *)DUSK_TOKEN_CONTINUE);
    duskMapSet(compiler->keyword_map, "return", (void *)DUSK_TOKEN_RETURN);
    duskMapSet(compiler->keyword_map, "discard", (void *)DUSK_TOKEN_DISCARD);
    duskMapSet(compiler->keyword_map, "while", (void *)DUSK_TOKEN_WHILE);
    duskMapSet(compiler->keyword_map, "if", (void *)DUSK_TOKEN_IF);
    duskMapSet(compiler->keyword_map, "else", (void *)DUSK_TOKEN_ELSE);
    duskMapSet(compiler->keyword_map, "switch", (void *)DUSK_TOKEN_SWITCH);
    duskMapSet(compiler->keyword_map, "true", (void *)DUSK_TOKEN_TRUE);
    duskMapSet(compiler->keyword_map, "false", (void *)DUSK_TOKEN_FALSE);
    duskMapSet(compiler->keyword_map, "void", (void *)DUSK_TOKEN_VOID);
    duskMapSet(compiler->keyword_map, "bool", (void *)DUSK_TOKEN_BOOL);

    duskMapSet(compiler->keyword_map, "half", (void *)DUSK_TOKEN_HALF);
    duskMapSet(compiler->keyword_map, "half2", (void *)DUSK_TOKEN_HALF2);
    duskMapSet(compiler->keyword_map, "half3", (void *)DUSK_TOKEN_HALF3);
    duskMapSet(compiler->keyword_map, "half4", (void *)DUSK_TOKEN_HALF4);
    duskMapSet(compiler->keyword_map, "half2x2", (void *)DUSK_TOKEN_HALF2X2);
    duskMapSet(compiler->keyword_map, "half3x3", (void *)DUSK_TOKEN_HALF3X3);
    duskMapSet(compiler->keyword_map, "half4x4", (void *)DUSK_TOKEN_HALF4X4);

    duskMapSet(compiler->keyword_map, "float", (void *)DUSK_TOKEN_FLOAT);
    duskMapSet(compiler->keyword_map, "float2", (void *)DUSK_TOKEN_FLOAT2);
    duskMapSet(compiler->keyword_map, "float3", (void *)DUSK_TOKEN_FLOAT3);
    duskMapSet(compiler->keyword_map, "float4", (void *)DUSK_TOKEN_FLOAT4);
    duskMapSet(compiler->keyword_map, "float2x2", (void *)DUSK_TOKEN_FLOAT2X2);
    duskMapSet(compiler->keyword_map, "float3x3", (void *)DUSK_TOKEN_FLOAT3X3);
    duskMapSet(compiler->keyword_map, "float4x4", (void *)DUSK_TOKEN_FLOAT4X4);

    duskMapSet(compiler->keyword_map, "double", (void *)DUSK_TOKEN_DOUBLE);
    duskMapSet(compiler->keyword_map, "double2", (void *)DUSK_TOKEN_DOUBLE2);
    duskMapSet(compiler->keyword_map, "double3", (void *)DUSK_TOKEN_DOUBLE3);
    duskMapSet(compiler->keyword_map, "double4", (void *)DUSK_TOKEN_DOUBLE4);
    duskMapSet(compiler->keyword_map, "double2x2", (void *)DUSK_TOKEN_DOUBLE2X2);
    duskMapSet(compiler->keyword_map, "double3x3", (void *)DUSK_TOKEN_DOUBLE3X3);
    duskMapSet(compiler->keyword_map, "double4x4", (void *)DUSK_TOKEN_DOUBLE4X4);

    duskMapSet(compiler->keyword_map, "byte", (void *)DUSK_TOKEN_BYTE);
    duskMapSet(compiler->keyword_map, "byte2", (void *)DUSK_TOKEN_BYTE2);
    duskMapSet(compiler->keyword_map, "byte3", (void *)DUSK_TOKEN_BYTE3);
    duskMapSet(compiler->keyword_map, "byte4", (void *)DUSK_TOKEN_BYTE4);
    duskMapSet(compiler->keyword_map, "byte2x2", (void *)DUSK_TOKEN_BYTE2X2);
    duskMapSet(compiler->keyword_map, "byte3x3", (void *)DUSK_TOKEN_BYTE3X3);
    duskMapSet(compiler->keyword_map, "byte4x4", (void *)DUSK_TOKEN_BYTE4X4);

    duskMapSet(compiler->keyword_map, "ubyte", (void *)DUSK_TOKEN_UBYTE);
    duskMapSet(compiler->keyword_map, "ubyte2", (void *)DUSK_TOKEN_UBYTE2);
    duskMapSet(compiler->keyword_map, "ubyte3", (void *)DUSK_TOKEN_UBYTE3);
    duskMapSet(compiler->keyword_map, "ubyte4", (void *)DUSK_TOKEN_UBYTE4);
    duskMapSet(compiler->keyword_map, "ubyte2x2", (void *)DUSK_TOKEN_UBYTE2X2);
    duskMapSet(compiler->keyword_map, "ubyte3x3", (void *)DUSK_TOKEN_UBYTE3X3);
    duskMapSet(compiler->keyword_map, "ubyte4x4", (void *)DUSK_TOKEN_UBYTE4X4);

    duskMapSet(compiler->keyword_map, "short", (void *)DUSK_TOKEN_SHORT);
    duskMapSet(compiler->keyword_map, "short2", (void *)DUSK_TOKEN_SHORT2);
    duskMapSet(compiler->keyword_map, "short3", (void *)DUSK_TOKEN_SHORT3);
    duskMapSet(compiler->keyword_map, "short4", (void *)DUSK_TOKEN_SHORT4);
    duskMapSet(compiler->keyword_map, "short2x2", (void *)DUSK_TOKEN_SHORT2X2);
    duskMapSet(compiler->keyword_map, "short3x3", (void *)DUSK_TOKEN_SHORT3X3);
    duskMapSet(compiler->keyword_map, "short4x4", (void *)DUSK_TOKEN_SHORT4X4);

    duskMapSet(compiler->keyword_map, "ushort", (void *)DUSK_TOKEN_USHORT);
    duskMapSet(compiler->keyword_map, "ushort2", (void *)DUSK_TOKEN_USHORT2);
    duskMapSet(compiler->keyword_map, "ushort3", (void *)DUSK_TOKEN_USHORT3);
    duskMapSet(compiler->keyword_map, "ushort4", (void *)DUSK_TOKEN_USHORT4);
    duskMapSet(compiler->keyword_map, "ushort2x2", (void *)DUSK_TOKEN_USHORT2X2);
    duskMapSet(compiler->keyword_map, "ushort3x3", (void *)DUSK_TOKEN_USHORT3X3);
    duskMapSet(compiler->keyword_map, "ushort4x4", (void *)DUSK_TOKEN_USHORT4X4);

    duskMapSet(compiler->keyword_map, "int", (void *)DUSK_TOKEN_INT);
    duskMapSet(compiler->keyword_map, "int2", (void *)DUSK_TOKEN_INT2);
    duskMapSet(compiler->keyword_map, "int3", (void *)DUSK_TOKEN_INT3);
    duskMapSet(compiler->keyword_map, "int4", (void *)DUSK_TOKEN_INT4);
    duskMapSet(compiler->keyword_map, "int2x2", (void *)DUSK_TOKEN_INT2X2);
    duskMapSet(compiler->keyword_map, "int3x3", (void *)DUSK_TOKEN_INT3X3);
    duskMapSet(compiler->keyword_map, "int4x4", (void *)DUSK_TOKEN_INT4X4);

    duskMapSet(compiler->keyword_map, "uint", (void *)DUSK_TOKEN_UINT);
    duskMapSet(compiler->keyword_map, "uint2", (void *)DUSK_TOKEN_UINT2);
    duskMapSet(compiler->keyword_map, "uint3", (void *)DUSK_TOKEN_UINT3);
    duskMapSet(compiler->keyword_map, "uint4", (void *)DUSK_TOKEN_UINT4);
    duskMapSet(compiler->keyword_map, "uint2x2", (void *)DUSK_TOKEN_UINT2X2);
    duskMapSet(compiler->keyword_map, "uint3x3", (void *)DUSK_TOKEN_UINT3X3);
    duskMapSet(compiler->keyword_map, "uint4x4", (void *)DUSK_TOKEN_UINT4X4);

    duskMapSet(compiler->keyword_map, "long", (void *)DUSK_TOKEN_LONG);
    duskMapSet(compiler->keyword_map, "long2", (void *)DUSK_TOKEN_LONG2);
    duskMapSet(compiler->keyword_map, "long3", (void *)DUSK_TOKEN_LONG3);
    duskMapSet(compiler->keyword_map, "long4", (void *)DUSK_TOKEN_LONG4);
    duskMapSet(compiler->keyword_map, "long2x2", (void *)DUSK_TOKEN_LONG2X2);
    duskMapSet(compiler->keyword_map, "long3x3", (void *)DUSK_TOKEN_LONG3X3);
    duskMapSet(compiler->keyword_map, "long4x4", (void *)DUSK_TOKEN_LONG4X4);

    duskMapSet(compiler->keyword_map, "ulong", (void *)DUSK_TOKEN_ULONG);
    duskMapSet(compiler->keyword_map, "ulong2", (void *)DUSK_TOKEN_ULONG2);
    duskMapSet(compiler->keyword_map, "ulong3", (void *)DUSK_TOKEN_ULONG3);
    duskMapSet(compiler->keyword_map, "ulong4", (void *)DUSK_TOKEN_ULONG4);
    duskMapSet(compiler->keyword_map, "ulong2x2", (void *)DUSK_TOKEN_ULONG2X2);
    duskMapSet(compiler->keyword_map, "ulong3x3", (void *)DUSK_TOKEN_ULONG3X3);
    duskMapSet(compiler->keyword_map, "ulong4x4", (void *)DUSK_TOKEN_ULONG4X4);

    for (size_t i = 0; i < DUSK_BUILTIN_FUNCTION_MAX; ++i) {
        duskMapSet(
            compiler->builtin_function_map,
            duskGetBuiltinFunctionName((DuskBuiltinFunctionKind)i),
            (void *)i);
    }

    return compiler;
}

void duskCompilerDestroy(DuskCompiler *compiler)
{
    duskArenaDestroy(compiler->main_arena);
    free(compiler);
}

void duskThrow(DuskCompiler *compiler)
{
    longjmp(compiler->jump_buffer, 1);
}

void duskAddError(
    DuskCompiler *compiler, DuskLocation loc, const char *fmt, ...)
{
    DUSK_ASSERT(loc.file);

    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);
    va_list vl;
    va_start(vl, fmt);
    const char *message = duskVsprintf(allocator, fmt, vl);

    DuskError error = {
        .message = message,
        .location = loc,
    };

    duskArrayPush(&compiler->errors_arr, error);
}

uint8_t *duskCompile(
    DuskCompiler *compiler,
    const char *path,
    const char *text,
    size_t text_length,
    size_t *spirv_byte_size)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    if (setjmp(compiler->jump_buffer) != 0) {
        for (size_t i = 0; i < duskArrayLength(compiler->errors_arr); ++i) {
            DuskError err = compiler->errors_arr[i];
            fprintf(
                stderr,
                "%s:%zu:%zu: %s\n",
                err.location.file->path,
                err.location.line,
                err.location.col,
                err.message);
        }
        return NULL;
    }

    DuskFile *file = DUSK_NEW(allocator, DuskFile);
    *file = (DuskFile){
        .path = path,
        .text = text,
        .text_length = text_length,
        .decls_arr = duskArrayCreate(allocator, DuskDecl *),
        .scope =
            duskScopeCreate(allocator, NULL, DUSK_SCOPE_OWNER_TYPE_NONE, NULL),
    };

    duskParse(compiler, file);

    duskAnalyzeFile(compiler, file);
    if (duskArrayLength(compiler->errors_arr) > 0) {
        duskThrow(compiler);
    }

    DuskIRModule *module = duskGenerateIRModule(compiler, file);
    DuskArray(uint32_t) spirv = duskIRModuleEmit(compiler, module);

    *spirv_byte_size = duskArrayLength(spirv) * 4;
    return (uint8_t *)spirv;
}

const char *duskGetBuiltinFunctionName(DuskBuiltinFunctionKind kind)
{
    if (kind >= DUSK_BUILTIN_FUNCTION_MAX) return NULL;
    return DUSK_BUILTIN_FUNCTION_NAMES[kind];
}
