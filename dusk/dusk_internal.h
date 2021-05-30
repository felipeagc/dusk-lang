#ifndef DUSK_INTERNAL_H
#define DUSK_INTERNAL_H

#include "dusk.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>

#if defined(_MSC_VER)
#define DUSK_INLINE __forceinline
#elif defined(__clang__) || defined(__GNUC__)
#define DUSK_INLINE                                                            \
    __attribute__((always_inline)) __attribute__((unused)) inline
#else
#define DUSK_INLINE inline
#endif

#ifdef __GNUC__
#define DUSK_PRINTF_FORMATTING(x, y) __attribute__((format(printf, x, y)))
#else
#define DUSK_PRINTF_FORMATTING(x, y)
#endif

#define DUSK_CARRAY_LENGTH(arr) (sizeof(arr) / sizeof(arr[0]))

#define DUSK_STR(a) #a

#define DUSK_ASSERT(value)                                                     \
    do                                                                         \
    {                                                                          \
        if (!(value))                                                          \
        {                                                                      \
            fprintf(                                                           \
                stderr,                                                        \
                "Dusk assertion failed: '%s' at %s:%d\n",                      \
                DUSK_STR(value),                                               \
                __FILE__,                                                      \
                __LINE__);                                                     \
            abort();                                                           \
        }                                                                      \
    } while (0)

#ifndef __cplusplus
#define DUSK_STATIC_ASSERT(value, msg) _Static_assert(value, msg)
#else
#define DUSK_STATIC_ASSERT(value, msg) static_assert(value, msg)
#endif

// Rounds to the next multiple of four
#define DUSK_ROUND_TO_4(x) (((x) + 3) & ~0x03)

// Allocator {{{
typedef struct DuskAllocator DuskAllocator;

struct DuskAllocator
{
    void *(*allocate)(DuskAllocator *allocator, size_t size);
    void *(*reallocate)(DuskAllocator *allocator, void *ptr, size_t size);
    void (*free)(DuskAllocator *allocator, void *ptr);
};

void *duskAllocate(DuskAllocator *allocator, size_t size);
void *duskAllocateZeroed(DuskAllocator *allocator, size_t size);
void *duskReallocate(DuskAllocator *allocator, void *ptr, size_t size);
void duskFree(DuskAllocator *allocator, void *ptr);

#define DUSK_NEW(allocator, type)                                              \
    ((type *)duskAllocateZeroed(allocator, sizeof(type)))
#define DUSK_NEW_ARRAY(allocator, type, count)                                 \
    ((type *)duskAllocateZeroed(allocator, sizeof(type) * count))

typedef struct DuskArena DuskArena;

DuskArena *
duskArenaCreate(DuskAllocator *parent_allocator, size_t default_size);
DuskAllocator *duskArenaGetAllocator(DuskArena *arena);
void duskArenaDestroy(DuskArena *arena);

const char *duskStrdup(DuskAllocator *allocator, const char *str);
const char *
duskNullTerminate(DuskAllocator *allocator, const char *str, size_t length);

DUSK_PRINTF_FORMATTING(2, 3)
const char *duskSprintf(DuskAllocator *allocator, const char *format, ...);
const char *
duskVsprintf(DuskAllocator *allocator, const char *format, va_list args);
// }}}

// Array {{{
#define DuskArray(type) type *

#define duskArrayItemSize(arr) ((arr) != NULL ? _duskArrayItemSize(arr) : 0)
#define duskArrayLength(arr) ((arr) != NULL ? _duskArrayLength(arr) : 0)
#define duskArrayCapacity(arr) ((arr) != NULL ? _duskArrayCapacity(arr) : 0)
#define duskArrayAllocator(arr) ((arr) != NULL ? _duskArrayAllocator(arr) : 0)

#define _duskArrayItemSize(arr) (*((uint64_t *)(arr)-1))
#define _duskArrayLength(arr) (*((uint64_t *)(arr)-2))
#define _duskArrayCapacity(arr) (*((uint64_t *)(arr)-3))
#define _duskArrayAllocator(arr) (*((DuskAllocator **)(arr)-4))

#define duskArrayCreate(allocator, type)                                       \
    ((type *)_duskArrayCreate(allocator, sizeof(type)))
#define duskArrayPush(arr, value)                                              \
    (_duskArrayEnsure((void **)arr, duskArrayLength(*arr) + 1),                \
     (*arr)[_duskArrayLength(*arr)++] = value)
#define duskArrayPop(arr)                                                      \
    ((duskArrayLength(*arr) > 0) ? (--_duskArrayLength(*arr)) : 0)
#define duskArrayEnsure(arr, wanted_capacity)                                  \
    _duskArrayEnsure((void **)arr, wanted_capacity)
#define duskArrayResize(arr, wanted_size)                                      \
    (_duskArrayEnsure((void **)arr, wanted_size),                              \
     _duskArrayLength(*arr) = wanted_size)
#define duskArrayFree(arr)                                                     \
    ((*arr) != NULL ? (_duskArrayFree(*(arr)), (*(arr)) = NULL) : 0)

#define DUSK_ARRAY_HEADER_SIZE (sizeof(uint64_t) * 4)
#define DUSK_ARRAY_INITIAL_CAPACITY 8

DUSK_INLINE static void *
_duskArrayCreate(DuskAllocator *allocator, size_t item_size)
{
    void *ptr = ((uint64_t *)duskAllocate(
                    allocator,
                    DUSK_ARRAY_HEADER_SIZE +
                        (item_size * DUSK_ARRAY_INITIAL_CAPACITY))) +
                4;

    _duskArrayItemSize(ptr) = item_size;
    _duskArrayAllocator(ptr) = allocator;
    _duskArrayLength(ptr) = 0;
    _duskArrayCapacity(ptr) = DUSK_ARRAY_INITIAL_CAPACITY;

    return ptr;
}

DUSK_INLINE static void _duskArrayFree(void *arr)
{
    DuskAllocator *allocator = duskArrayAllocator(arr);
    duskFree(allocator, ((uint64_t *)arr) - 4);
}

static inline void _duskArrayEnsure(void **arr_ptr, size_t wanted_capacity)
{
    void *arr = *arr_ptr;

    size_t item_size = duskArrayItemSize(arr);
    DuskAllocator *allocator = duskArrayAllocator(arr);
    size_t array_capacity = duskArrayCapacity(arr);

    if (wanted_capacity > array_capacity)
    {
        array_capacity *= 2;
        if (array_capacity < wanted_capacity) array_capacity = wanted_capacity;

        arr = ((uint64_t *)duskReallocate(
                  allocator,
                  ((uint64_t *)arr - 4),
                  DUSK_ARRAY_HEADER_SIZE + (item_size * array_capacity))) +
              4;
        _duskArrayCapacity(arr) = array_capacity;
    }

    *arr_ptr = arr;
}
// }}}

// String map {{{
static inline uint64_t duskStringMapHash(const char *string)
{
    uint64_t hash = 14695981039346656037ULL;
    while (*string)
    {
        hash = ((hash)*1099511628211) ^ (*string);
        ++string;
    }
    return hash;
}

typedef struct DuskMapSlot
{
    const char *key;
    uint64_t hash;
    void *value;
} DuskMapSlot;

typedef struct DuskMap
{
    DuskAllocator *allocator;
    DuskMapSlot *slots;
    uint64_t size;
} DuskMap;

DuskMap *duskMapCreate(DuskAllocator *allocator, size_t size);
void duskMapDestroy(DuskMap *map);

void duskMapSet(DuskMap *map, const char *key, void *value);
bool duskMapGet(DuskMap *map, const char *key, void **value_ptr);
void duskMapRemove(DuskMap *map, const char *key);
// }}}

// String builder {{{
typedef struct DuskStringBuilder DuskStringBuilder;

DuskStringBuilder *
duskStringBuilderCreate(DuskAllocator *allocator, size_t initial_length);
void duskStringBuilderDestroy(DuskStringBuilder *sb);

void duskStringBuilderAppend(DuskStringBuilder *sb, const char *str);
void duskStringBuilderAppendLen(
    DuskStringBuilder *sb, const char *str, size_t length);
DUSK_PRINTF_FORMATTING(2, 3)
void duskStringBuilderAppendFormat(
    DuskStringBuilder *sb, const char *format, ...);
const char *
duskStringBuilderBuild(DuskStringBuilder *sb, DuskAllocator *allocator);
// }}}

// Typedefs {{{
typedef struct DuskFile DuskFile;

typedef struct DuskDecl DuskDecl;
typedef struct DuskStmt DuskStmt;
typedef struct DuskExpr DuskExpr;

typedef struct DuskIRValue DuskIRValue;

typedef struct DuskLocation
{
    DuskFile *file;
    size_t offset;
    size_t length;
    size_t line;
    size_t col;
} DuskLocation;

typedef enum DuskShaderStage {
    DUSK_SHADER_STAGE_FRAGMENT,
    DUSK_SHADER_STAGE_VERTEX,
    DUSK_SHADER_STAGE_COMPUTE,
} DuskShaderStage;

typedef enum DuskScalarType {
    DUSK_SCALAR_TYPE_FLOAT,
    DUSK_SCALAR_TYPE_DOUBLE,
    DUSK_SCALAR_TYPE_INT,
    DUSK_SCALAR_TYPE_UINT,
} DuskScalarType;

typedef enum DuskStorageClass {
    DUSK_STORAGE_CLASS_FUNCTION,
    DUSK_STORAGE_CLASS_PARAMETER,
    DUSK_STORAGE_CLASS_UNIFORM,
    DUSK_STORAGE_CLASS_UNIFORM_CONSTANT,
    DUSK_STORAGE_CLASS_INPUT,
    DUSK_STORAGE_CLASS_OUTPUT,
    DUSK_STORAGE_CLASS_PUSH_CONSTANT,
} DuskStorageClass;
// }}}

// Scope {{{
typedef enum DuskScopeOwnerType {
    DUSK_SCOPE_OWNER_TYPE_NONE = 0,
    DUSK_SCOPE_OWNER_TYPE_FUNCTION,
    DUSK_SCOPE_OWNER_TYPE_MODULE,
    DUSK_SCOPE_OWNER_TYPE_STRUCT,
    DUSK_SCOPE_OWNER_TYPE_VARIABLE,
} DuskScopeOwnerType;

typedef struct DuskScope
{
    DuskScopeOwnerType type;
    struct DuskScope *parent;
    DuskMap *map;
    union
    {
        DuskDecl *decl;
        DuskExpr *expr;
        DuskStmt *stmt;
    } owner;
} DuskScope;

DuskScope *duskScopeCreate(
    DuskAllocator *allocator,
    DuskScope *parent,
    DuskScopeOwnerType type,
    void *owner);
DuskDecl *duskScopeLookup(DuskScope *scope, const char *name);
void duskScopeSet(DuskScope *scope, const char *name, DuskDecl *decl);
// }}}

// Type {{{
typedef enum DuskImageDimension {
    DUSK_IMAGE_DIMENSION_1D,
    DUSK_IMAGE_DIMENSION_2D,
    DUSK_IMAGE_DIMENSION_3D,
    DUSK_IMAGE_DIMENSION_CUBE,
} DuskImageDimension;

typedef enum DuskTypeKind {
    DUSK_TYPE_VOID,
    DUSK_TYPE_TYPE,
    DUSK_TYPE_BOOL,
    DUSK_TYPE_UNTYPED_INT,
    DUSK_TYPE_UNTYPED_FLOAT,
    DUSK_TYPE_INT,
    DUSK_TYPE_FLOAT,
    DUSK_TYPE_VECTOR,
    DUSK_TYPE_MATRIX,
    DUSK_TYPE_RUNTIME_ARRAY,
    DUSK_TYPE_ARRAY,
    DUSK_TYPE_STRUCT,
    DUSK_TYPE_FUNCTION,
    DUSK_TYPE_POINTER,
    DUSK_TYPE_SAMPLER,
    DUSK_TYPE_IMAGE,
    DUSK_TYPE_SAMPLED_IMAGE,
    DUSK_TYPE_STRING,
} DuskTypeKind;

typedef struct DuskType DuskType;

struct DuskType
{
    DuskTypeKind kind;
    const char *string;
    const char *pretty_string;
    uint32_t id;
    bool emit; // This flag is set before SPIRV emission in order to emit the
               // type. Once the type is emitted, the flag is set to false.

    union
    {
        struct
        {
            uint32_t bits;
            bool is_signed;
        } int_;
        struct
        {
            uint32_t bits;
        } float_;
        struct
        {
            DuskType *sub;
            uint32_t size;
        } vector;
        struct
        {
            DuskType *col_type;
            uint32_t cols;
        } matrix;
        struct
        {
            DuskType *sub;
            size_t size;
            DuskIRValue *size_ir_value;
        } array;
        struct
        {
            const char *name;
            DuskArray(DuskType *) field_types;
            DuskArray(const char *) field_names;
            DuskMap *index_map;
        } struct_;
        struct
        {
            DuskType *return_type;
            DuskArray(DuskType *) param_types;
        } function;
        struct
        {
            DuskType *sub;
            DuskStorageClass storage_class;
        } pointer;
        struct
        {
            DuskType *sampled_type;
            DuskImageDimension dim;
            uint32_t depth;
            uint32_t arrayed;
            uint32_t multisampled;
            uint32_t sampled;
        } image;
        struct
        {
            DuskType *image_type;
        } sampled_image;
    };
};

bool duskTypeIsRuntime(DuskType *type);
const char *duskTypeToPrettyString(DuskAllocator *allocator, DuskType *type);
DuskType *duskTypeNewBasic(DuskCompiler *compiler, DuskTypeKind kind);
DuskType *duskTypeNewScalar(DuskCompiler *compiler, DuskScalarType scalar_type);
DuskType *
duskTypeNewVector(DuskCompiler *compiler, DuskType *sub, uint32_t size);
DuskType *
duskTypeNewMatrix(DuskCompiler *compiler, DuskType *col_type, uint32_t cols);
DuskType *duskTypeNewRuntimeArray(DuskCompiler *compiler, DuskType *sub);
DuskType *duskTypeNewArray(DuskCompiler *compiler, DuskType *sub, size_t size);
DuskType *duskTypeNewStruct(
    DuskCompiler *compiler,
    const char *name,
    DuskArray(const char *) field_names,
    DuskArray(DuskType *) field_types);
DuskType *duskTypeNewFunction(
    DuskCompiler *compiler,
    DuskType *return_type,
    DuskArray(DuskType *) param_types);
DuskType *duskTypeNewPointer(
    DuskCompiler *compiler, DuskType *sub, DuskStorageClass storage_class);
DuskType *duskTypeNewImage(
    DuskCompiler *compiler,
    DuskType *sampled_type,
    DuskImageDimension dim,
    bool depth,
    bool arrayed,
    bool multisampled,
    bool sampled);
DuskType *duskTypeNewSampledImage(DuskCompiler *compiler, DuskType *image_type);

void duskTypeMarkNotDead(DuskType *type);
// }}}

// IR {{{
typedef enum DuskIRValueKind {
    DUSK_IR_VALUE_CONSTANT_BOOL,
    DUSK_IR_VALUE_CONSTANT,
    DUSK_IR_VALUE_FUNCTION,
    DUSK_IR_VALUE_BLOCK,
    DUSK_IR_VALUE_VARIABLE,
    DUSK_IR_VALUE_RETURN,
    DUSK_IR_VALUE_DISCARD,
    DUSK_IR_VALUE_STORE,
    DUSK_IR_VALUE_LOAD,
    DUSK_IR_VALUE_FUNCTION_CALL,
    DUSK_IR_VALUE_ACCESS_CHAIN,
    DUSK_IR_VALUE_COMPOSITE_EXTRACT,
    DUSK_IR_VALUE_VECTOR_SHUFFLE,
    DUSK_IR_VALUE_COMPOSITE_CONSTRUCT,
} DuskIRValueKind;

struct DuskIRValue
{
    uint32_t id;
    DuskIRValueKind kind;
    const char *name;
    DuskType *type;
    const char *const_string;
    bool emitted;

    union
    {
        struct
        {
            bool value;
        } const_bool;
        struct
        {
            uint32_t *value_words;
            size_t value_word_count;
        } constant;
        struct
        {
            const char *name;
            DuskArray(DuskIRValue *) params;
            DuskArray(DuskIRValue *) variables;
            DuskArray(DuskIRValue *) blocks;
        } function;
        struct
        {
            DuskStorageClass storage_class;
        } var;
        struct
        {
            DuskArray(DuskIRValue *) insts;
        } block;
        struct
        {
            DuskIRValue *value;
        } return_;
        struct
        {
            DuskIRValue *pointer;
            DuskIRValue *value;
        } store;
        struct
        {
            DuskIRValue *pointer;
        } load;
        struct
        {
            DuskIRValue *function;
            DuskArray(DuskIRValue *) params;
        } function_call;
        struct
        {
            DuskIRValue *base;
            DuskArray(DuskIRValue *) indices;
        } access_chain;
        struct
        {
            DuskIRValue *composite;
            DuskArray(uint32_t) indices;
        } composite_extract;
        struct
        {
            DuskIRValue *vec1;
            DuskIRValue *vec2;
            DuskArray(uint32_t) indices;
        } vector_shuffle;
        struct
        {
            DuskArray(DuskIRValue*) values;
        } composite_construct;
    };
};

typedef struct DuskIREntryPoint
{
    DuskIRValue *function;
    const char *name;
    DuskShaderStage stage;
} DuskIREntryPoint;

typedef struct DuskIRModule
{
    DuskCompiler *compiler;
    DuskAllocator *allocator;
    DuskArray(uint32_t) stream;
    uint32_t last_id;

    DuskMap *const_cache;
    DuskArray(DuskIRValue *) consts;

    uint32_t glsl_ext_inst_id;

    DuskArray(DuskIRValue *) functions;
    DuskArray(DuskIRValue *) globals;

    DuskArray(DuskIREntryPoint) entry_points;
} DuskIRModule;

bool duskIRBlockIsTerminated(DuskIRValue *block);

DuskIRValue *duskIRBlockCreate(DuskIRModule *module);
DuskIRValue *
duskIRFunctionCreate(DuskIRModule *module, DuskType *type, const char *name);
void duskIRFunctionAddBlock(DuskIRValue *function, DuskIRValue *block);
DuskIRValue *duskIRVariableCreate(
    DuskIRModule *module, DuskType *type, DuskStorageClass storage_class);
void duskIRModuleAddEntryPoint(
    DuskIRModule *module,
    DuskIRValue *function,
    const char *name,
    DuskShaderStage stage);
DuskIRModule *duskIRModuleCreate(DuskCompiler *compiler);

DuskIRValue *duskIRConstBoolCreate(DuskIRModule *module, bool bool_value);
DuskIRValue *
duskIRConstIntCreate(DuskIRModule *module, DuskType *type, uint64_t int_value);
DuskIRValue *duskIRConstFloatCreate(
    DuskIRModule *module, DuskType *type, double double_value);

void duskIRCreateReturn(
    DuskIRModule *module, DuskIRValue *block, DuskIRValue *value);
void duskIRCreateDiscard(DuskIRModule *module, DuskIRValue *block);
void duskIRCreateStore(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskIRValue *pointer,
    DuskIRValue *value);
DuskIRValue *duskIRCreateLoad(
    DuskIRModule *module, DuskIRValue *block, DuskIRValue *pointer);
DuskIRValue *duskIRCreateFunctionCall(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskIRValue *function,
    size_t param_count,
    DuskIRValue **params);
DuskIRValue *duskIRCreateAccessChain(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskType *accessed_type,
    DuskIRValue *base,
    size_t index_count,
    DuskIRValue **indices);
DuskIRValue *duskIRCreateCompositeExtract(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskIRValue *composite,
    size_t index_count,
    uint32_t *indices);
DuskIRValue *duskIRCreateVectorShuffle(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskIRValue *vec1,
    DuskIRValue *vec2,
    size_t index_count,
    uint32_t *indices);
DuskIRValue *duskIRCreateCompositeConstruct(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskType *composite_type,
    size_t value_count,
    DuskIRValue **values);

bool duskIRIsLvalue(DuskIRValue *value);
DuskIRValue *
duskIRLoadLvalue(DuskIRModule *module, DuskIRValue *block, DuskIRValue *value);
// }}}

// AST {{{
typedef enum DuskBuiltinFunctionKind {
    DUSK_BUILTIN_FUNCTION_SAMPLER_TYPE,
    DUSK_BUILTIN_FUNCTION_IMAGE_1D_TYPE,
    DUSK_BUILTIN_FUNCTION_IMAGE_2D_TYPE,
    DUSK_BUILTIN_FUNCTION_IMAGE_2D_ARRAY_TYPE,
    DUSK_BUILTIN_FUNCTION_IMAGE_3D_TYPE,
    DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_TYPE,
    DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_ARRAY_TYPE,
    DUSK_BUILTIN_FUNCTION_IMAGE_1D_SAMPLER_TYPE,
    DUSK_BUILTIN_FUNCTION_IMAGE_2D_SAMPLER_TYPE,
    DUSK_BUILTIN_FUNCTION_IMAGE_2D_ARRAY_SAMPLER_TYPE,
    DUSK_BUILTIN_FUNCTION_IMAGE_3D_SAMPLER_TYPE,
    DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_SAMPLER_TYPE,
    DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_ARRAY_SAMPLER_TYPE,
    DUSK_BUILTIN_FUNCTION_MAX,
} DuskBuiltinFunctionKind;

typedef struct DuskAttribute
{
    const char *name;
    DuskArray(DuskExpr *) value_exprs;
} DuskAttribute;

typedef enum DuskDeclKind {
    DUSK_DECL_FUNCTION,
    DUSK_DECL_VAR,
    DUSK_DECL_TYPE,
} DuskDeclKind;

struct DuskDecl
{
    DuskDeclKind kind;
    DuskLocation location;
    const char *name;
    DuskArray(DuskAttribute) attributes;
    DuskType *type;
    DuskIRValue *ir_value;

    union
    {
        struct
        {
            const char *link_name;
            DuskScope *scope;
            DuskArray(DuskDecl *) parameter_decls;
            DuskExpr *return_type_expr;
            DuskArray(DuskStmt *) stmts;
        } function;
        struct
        {
            DuskScope *scope;
            DuskExpr *type_expr;
            DuskExpr *value_expr;
            DuskStorageClass storage_class;
        } var;
        struct
        {
            DuskExpr *type_expr;
        } typedef_;
    };
};

typedef enum DuskStmtKind {
    DUSK_STMT_DECL,
    DUSK_STMT_ASSIGN,
    DUSK_STMT_EXPR,
    DUSK_STMT_BLOCK,
    DUSK_STMT_RETURN,
    DUSK_STMT_DISCARD,
} DuskStmtKind;

struct DuskStmt
{
    DuskStmtKind kind;
    DuskLocation location;

    union
    {
        DuskDecl *decl;
        DuskExpr *expr;
        struct
        {
            DuskExpr *assigned_expr;
            DuskExpr *value_expr;
        } assign;
        struct
        {
            DuskArray(DuskStmt *) stmts;
            DuskScope *scope;
        } block;
        struct
        {
            DuskExpr *expr;
        } return_;
    };
};

typedef enum DuskExprKind {
    DUSK_EXPR_VOID_TYPE,
    DUSK_EXPR_BOOL_TYPE,
    DUSK_EXPR_SCALAR_TYPE,
    DUSK_EXPR_VECTOR_TYPE,
    DUSK_EXPR_MATRIX_TYPE,
    DUSK_EXPR_STRING_LITERAL,
    DUSK_EXPR_INT_LITERAL,
    DUSK_EXPR_FLOAT_LITERAL,
    DUSK_EXPR_BOOL_LITERAL,
    DUSK_EXPR_IDENT,
    DUSK_EXPR_STRUCT_TYPE,
    DUSK_EXPR_ARRAY_TYPE,
    DUSK_EXPR_RUNTIME_ARRAY_TYPE,
    DUSK_EXPR_FUNCTION_CALL,
    DUSK_EXPR_BUILTIN_FUNCTION_CALL,
    DUSK_EXPR_ACCESS,
} DuskExprKind;

struct DuskExpr
{
    DuskExprKind kind;
    DuskLocation location;
    DuskType *type;
    DuskType *as_type;
    DuskIRValue *ir_value;

    union
    {
        DuskScalarType scalar_type;
        struct
        {
            DuskScalarType scalar_type;
            uint32_t length;
        } vector_type;
        struct
        {
            DuskScalarType scalar_type;
            uint32_t cols;
            uint32_t rows;
        } matrix_type;
        int64_t int_literal;
        double float_literal;
        bool bool_literal;
        struct
        {
            const char *str;
            DuskDecl *decl;
            DuskArray(uint32_t) shuffle_indices;
        } identifier;
        struct
        {
            const char *str;
        } string;
        struct
        {
            const char *name;
            DuskArray(const char *) field_names;
            DuskArray(DuskExpr *) field_type_exprs;
        } struct_type;
        struct
        {
            DuskExpr *sub_expr;
            DuskExpr *size_expr;
        } array_type;
        struct
        {
            DuskExpr *func_expr;
            DuskArray(DuskExpr *) params;
        } function_call;
        struct
        {
            DuskBuiltinFunctionKind kind;
            DuskArray(DuskExpr *) params;
        } builtin_call;
        struct
        {
            DuskExpr *base_expr;
            DuskArray(DuskExpr *) chain;
        } access;
    };
};
// }}}

// Compiler {{{
typedef struct DuskEntryPoint
{
    DuskShaderStage stage;
    const char *name;
    DuskDecl *function_decl;
} DuskEntryPoint;

struct DuskFile
{
    const char *path;

    const char *text;
    size_t text_length;

    DuskScope *scope;
    DuskArray(DuskDecl *) decls;
    DuskArray(DuskEntryPoint) entry_points;
};

typedef struct DuskError
{
    DuskLocation location;
    const char *message;
} DuskError;

typedef struct DuskCompiler
{
    DuskArena *main_arena;
    DuskArray(DuskError) errors;
    DuskMap *type_cache;
    DuskArray(DuskType *) types;
    jmp_buf jump_buffer;
} DuskCompiler;
// }}}

void duskThrow(DuskCompiler *compiler);
DUSK_PRINTF_FORMATTING(3, 4)
void duskAddError(
    DuskCompiler *compiler, DuskLocation loc, const char *fmt, ...);
void duskParse(DuskCompiler *compiler, DuskFile *file);
void duskAnalyzeFile(DuskCompiler *compiler, DuskFile *file);
DuskIRModule *duskGenerateIRModule(DuskCompiler *compiler, DuskFile *file);
DuskArray(uint32_t)
    duskIRModuleEmit(DuskCompiler *compiler, DuskIRModule *module);

#endif
