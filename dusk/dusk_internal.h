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
    do {                                                                       \
        if (!(value)) {                                                        \
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

#define DUSK_ROUND_UP(to, x) ((((x) + (to)-1) / (to)) * (to))

// Allocator {{{
typedef struct DuskAllocator DuskAllocator;

struct DuskAllocator {
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

    if (wanted_capacity > array_capacity) {
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
    while (*string) {
        hash = ((hash)*1099511628211) ^ (*string);
        ++string;
    }
    return hash;
}

typedef struct DuskMapSlot {
    const char *key;
    uint64_t hash;
    void *value;
} DuskMapSlot;

typedef struct DuskMap {
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

typedef struct DuskLocation {
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
    DUSK_SCALAR_TYPE_HALF,
    DUSK_SCALAR_TYPE_FLOAT,
    DUSK_SCALAR_TYPE_DOUBLE,
    DUSK_SCALAR_TYPE_BYTE,
    DUSK_SCALAR_TYPE_UBYTE,
    DUSK_SCALAR_TYPE_SHORT,
    DUSK_SCALAR_TYPE_USHORT,
    DUSK_SCALAR_TYPE_INT,
    DUSK_SCALAR_TYPE_UINT,
    DUSK_SCALAR_TYPE_LONG,
    DUSK_SCALAR_TYPE_ULONG,
} DuskScalarType;

typedef enum DuskStorageClass {
    DUSK_STORAGE_CLASS_FUNCTION,
    DUSK_STORAGE_CLASS_PARAMETER,
    DUSK_STORAGE_CLASS_UNIFORM,
    DUSK_STORAGE_CLASS_STORAGE,
    DUSK_STORAGE_CLASS_UNIFORM_CONSTANT,
    DUSK_STORAGE_CLASS_INPUT,
    DUSK_STORAGE_CLASS_OUTPUT,
    DUSK_STORAGE_CLASS_PUSH_CONSTANT,
    DUSK_STORAGE_CLASS_WORKGROUP,
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

typedef struct DuskScope {
    DuskScopeOwnerType type;
    struct DuskScope *parent;
    DuskMap *map;
    union {
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
typedef enum DuskIRDecorationKind {
    DUSK_IR_DECORATION_LOCATION,
    DUSK_IR_DECORATION_BUILTIN,
    DUSK_IR_DECORATION_SET,
    DUSK_IR_DECORATION_BINDING,
    DUSK_IR_DECORATION_BLOCK,
    DUSK_IR_DECORATION_OFFSET,
    DUSK_IR_DECORATION_ARRAY_STRIDE,
    DUSK_IR_DECORATION_NON_WRITABLE,
} DuskIRDecorationKind;

typedef struct DuskIRDecoration {
    DuskIRDecorationKind kind;
    uint32_t *literals;
    size_t literal_count;
} DuskIRDecoration;

typedef enum DuskAttributeKind {
    DUSK_ATTRIBUTE_UNKNOWN = 0,
    DUSK_ATTRIBUTE_STAGE,
    DUSK_ATTRIBUTE_LOCATION,
    DUSK_ATTRIBUTE_SET,
    DUSK_ATTRIBUTE_BINDING,
    DUSK_ATTRIBUTE_BUILTIN,
    DUSK_ATTRIBUTE_OFFSET,
    DUSK_ATTRIBUTE_READ_ONLY,
} DuskAttributeKind;

typedef struct DuskAttribute {
    DuskAttributeKind kind;
    const char *name;
    size_t value_expr_count;
    DuskExpr **value_exprs;
} DuskAttribute;

typedef enum DuskImageDimension {
    DUSK_IMAGE_DIMENSION_1D,
    DUSK_IMAGE_DIMENSION_2D,
    DUSK_IMAGE_DIMENSION_3D,
    DUSK_IMAGE_DIMENSION_CUBE,
} DuskImageDimension;

typedef enum DuskStructLayout {
    DUSK_STRUCT_LAYOUT_UNKNOWN,
    DUSK_STRUCT_LAYOUT_STD140,
    DUSK_STRUCT_LAYOUT_STD430,
} DuskStructLayout;

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

struct DuskType {
    DuskTypeKind kind;
    uint32_t id;
    bool emit; // This flag is set before SPIRV emission in order to emit the
               // type. Once the type is emitted, the flag is set to false.
    const char *string;
    const char *pretty_string;
    DuskArray(DuskIRDecoration) decorations_arr;

    union {
        struct {
            uint32_t bits;
            bool is_signed;
        } int_;
        struct {
            uint32_t bits;
        } float_;
        struct {
            DuskType *sub;
            uint32_t size;
        } vector;
        struct {
            DuskType *col_type;
            uint32_t cols;
        } matrix;
        struct {
            DuskType *sub;
            size_t size;
            DuskIRValue *size_ir_value;
            DuskStructLayout layout;
        } array;
        struct {
            const char *name;
            size_t field_count;
            DuskType **field_types;
            const char **field_names;
            DuskArray(DuskAttribute) * field_attribute_arrays;
            DuskArray(DuskIRDecoration) * field_decoration_arrays;
            DuskMap *index_map;
            DuskStructLayout layout;
        } struct_;
        struct {
            DuskType *return_type;
            size_t param_type_count;
            DuskType **param_types;
        } function;
        struct {
            DuskType *sub;
            DuskStorageClass storage_class;
        } pointer;
        struct {
            DuskType *sampled_type;
            DuskImageDimension dim;
            uint32_t depth;
            uint32_t arrayed;
            uint32_t multisampled;
            uint32_t sampled;
        } image;
        struct {
            DuskType *image_type;
        } sampled_image;
    };
};

bool duskTypeIsRuntime(DuskType *type);
DuskType *duskGetVecScalarType(DuskType *type);

// Gets the type's pretty string
const char *duskTypeToPrettyString(DuskAllocator *allocator, DuskType *type);

// Gets the type's internal unique string representation for use as a key in
// hash tables
const char *duskTypeToString(DuskAllocator *allocator, DuskType *type);

uint32_t duskTypeAlignOf(
    DuskAllocator *allocator, DuskType *type, DuskStructLayout layout);
uint32_t duskTypeSizeOf(
    DuskAllocator *allocator, DuskType *type, DuskStructLayout layout);

DuskType *duskTypeNewBasic(DuskCompiler *compiler, DuskTypeKind kind);
DuskType *duskTypeNewScalar(DuskCompiler *compiler, DuskScalarType scalar_type);
DuskType *
duskTypeNewVector(DuskCompiler *compiler, DuskType *sub, uint32_t size);
DuskType *
duskTypeNewMatrix(DuskCompiler *compiler, DuskType *col_type, uint32_t cols);
DuskType *duskTypeNewRuntimeArray(
    DuskCompiler *compiler, DuskStructLayout layout, DuskType *sub);
DuskType *duskTypeNewArray(
    DuskCompiler *compiler,
    DuskStructLayout layout,
    DuskType *sub,
    size_t size);
DuskType *duskTypeNewStruct(
    DuskCompiler *compiler,
    const char *name,
    DuskStructLayout layout,
    size_t field_count,
    const char **field_names,
    DuskType **field_types,
    DuskArray(DuskAttribute) * field_attribute_arrays);
DuskType *duskTypeNewFunction(
    DuskCompiler *compiler,
    DuskType *return_type,
    size_t param_type_count,
    DuskType **param_types);
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
typedef struct DuskIREntryPoint {
    DuskShaderStage stage;
    const char *name;
    DuskIRValue *function;
    DuskArray(DuskIRValue *) referenced_globals_arr;
} DuskIREntryPoint;

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

    DUSK_BUILTIN_FUNCTION_SIN,
    DUSK_BUILTIN_FUNCTION_COS,
    DUSK_BUILTIN_FUNCTION_TAN,
    DUSK_BUILTIN_FUNCTION_ASIN,
    DUSK_BUILTIN_FUNCTION_ACOS,
    DUSK_BUILTIN_FUNCTION_ATAN,
    DUSK_BUILTIN_FUNCTION_SINH,
    DUSK_BUILTIN_FUNCTION_COSH,
    DUSK_BUILTIN_FUNCTION_TANH,
    DUSK_BUILTIN_FUNCTION_ASINH,
    DUSK_BUILTIN_FUNCTION_ACOSH,
    DUSK_BUILTIN_FUNCTION_ATANH,

    DUSK_BUILTIN_FUNCTION_RADIANS,
    DUSK_BUILTIN_FUNCTION_DEGREES,
    DUSK_BUILTIN_FUNCTION_ROUND,
    DUSK_BUILTIN_FUNCTION_TRUNC,
    DUSK_BUILTIN_FUNCTION_FLOOR,
    DUSK_BUILTIN_FUNCTION_CEIL,
    DUSK_BUILTIN_FUNCTION_FRACT,
    DUSK_BUILTIN_FUNCTION_SQRT,
    DUSK_BUILTIN_FUNCTION_INVERSE_SQRT,
    DUSK_BUILTIN_FUNCTION_LOG,
    DUSK_BUILTIN_FUNCTION_LOG2,
    DUSK_BUILTIN_FUNCTION_EXP,
    DUSK_BUILTIN_FUNCTION_EXP2,

    DUSK_BUILTIN_FUNCTION_MAX,
} DuskBuiltinFunctionKind;

const char *duskGetBuiltinFunctionName(DuskBuiltinFunctionKind kind);

typedef enum DuskIRValueKind {
    DUSK_IR_VALUE_CONSTANT_BOOL,
    DUSK_IR_VALUE_CONSTANT,
    DUSK_IR_VALUE_CONSTANT_COMPOSITE,
    DUSK_IR_VALUE_FUNCTION,
    DUSK_IR_VALUE_FUNCTION_PARAMETER,
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
    DUSK_IR_VALUE_CAST,
    DUSK_IR_VALUE_BUILTIN_CALL,
} DuskIRValueKind;

struct DuskIRValue {
    uint32_t id;
    DuskIRValueKind kind;
    const char *name;
    DuskType *type;
    const char *const_string;
    bool emitted;
    DuskArray(DuskIRDecoration) decorations_arr;

    union {
        struct {
            bool value;
        } const_bool;
        struct {
            uint32_t *value_words;
            size_t value_word_count;
        } constant;
        struct {
            DuskArray(DuskIRValue *) values_arr;
        } constant_composite;
        struct {
            const char *name;
            DuskArray(DuskIRValue *) params_arr;
            DuskArray(DuskIRValue *) variables_arr;
            DuskArray(DuskIRValue *) blocks_arr;
        } function;
        struct {
            DuskStorageClass storage_class;
        } var;
        struct {
            DuskArray(DuskIRValue *) insts_arr;
        } block;
        struct {
            DuskIRValue *value;
        } return_;
        struct {
            DuskIRValue *pointer;
            DuskIRValue *value;
        } store;
        struct {
            DuskIRValue *pointer;
        } load;
        struct {
            DuskIRValue *function;
            DuskArray(DuskIRValue *) params_arr;
        } function_call;
        struct {
            DuskIRValue *base;
            DuskArray(DuskIRValue *) indices_arr;
        } access_chain;
        struct {
            DuskIRValue *composite;
            DuskArray(uint32_t) indices_arr;
        } composite_extract;
        struct {
            DuskIRValue *vec1;
            DuskIRValue *vec2;
            DuskArray(uint32_t) indices_arr;
        } vector_shuffle;
        struct {
            DuskArray(DuskIRValue *) values_arr;
        } composite_construct;
        struct {
            DuskIRValue *value;
        } cast;
        struct {
            DuskBuiltinFunctionKind builtin_kind;
            DuskIRValue **params;
            size_t param_count;
        } builtin_call;
    };
};

typedef struct DuskIRModule {
    DuskCompiler *compiler;
    DuskAllocator *allocator;
    DuskArray(uint32_t) stream_arr;
    DuskArray(const char *) extensions_arr;
    DuskArray(uint32_t) capabilities_arr;
    uint32_t last_id;

    DuskMap *const_cache;
    DuskArray(DuskIRValue *) consts_arr;

    uint32_t glsl_ext_inst_id;

    DuskArray(DuskIRValue *) functions_arr;
    DuskArray(DuskIRValue *) globals_arr;

    DuskArray(DuskIREntryPoint *) entry_points_arr;
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
    DuskShaderStage stage,
    size_t referenced_global_count,
    DuskIRValue **referenced_globals);
DuskIRModule *duskIRModuleCreate(DuskCompiler *compiler);

DuskIRValue *duskIRConstBoolCreate(DuskIRModule *module, bool bool_value);
DuskIRValue *
duskIRConstIntCreate(DuskIRModule *module, DuskType *type, uint64_t int_value);
DuskIRValue *duskIRConstFloatCreate(
    DuskIRModule *module, DuskType *type, double double_value);
DuskIRValue *duskIRConstCompositeCreate(
    DuskIRModule *module,
    DuskType *type,
    size_t value_count,
    DuskIRValue **values);

DuskIRDecoration duskIRCreateDecoration(
    DuskAllocator *allocator,
    DuskIRDecorationKind kind,
    size_t literal_count,
    uint32_t *literals);

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
    DuskType *accessed_type,
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
DuskIRValue *duskIRCreateCast(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskType *destination_type,
    DuskIRValue *value);
DuskIRValue *duskIRCreateBuiltinCall(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskBuiltinFunctionKind builtin_kind,
    DuskType *destination_type,
    size_t param_count,
    DuskIRValue **params);

bool duskIRValueIsConstant(DuskIRValue *value);
bool duskIRIsLvalue(DuskIRValue *value);
DuskIRValue *
duskIRLoadLvalue(DuskIRModule *module, DuskIRValue *block, DuskIRValue *value);
// }}}

// Token {{{
typedef enum {
    DUSK_TOKEN_ERROR = 0,

    DUSK_TOKEN_IDENT,
    DUSK_TOKEN_BUILTIN_IDENT,
    DUSK_TOKEN_STRING_LITERAL,
    DUSK_TOKEN_INT_LITERAL,
    DUSK_TOKEN_FLOAT_LITERAL,

    DUSK_TOKEN_FN,
    DUSK_TOKEN_LET,
    DUSK_TOKEN_CONST,
    DUSK_TOKEN_STRUCT,
    DUSK_TOKEN_TYPE,
    DUSK_TOKEN_IMPORT,
    DUSK_TOKEN_BREAK,
    DUSK_TOKEN_CONTINUE,
    DUSK_TOKEN_RETURN,
    DUSK_TOKEN_DISCARD,
    DUSK_TOKEN_WHILE,
    DUSK_TOKEN_IF,
    DUSK_TOKEN_ELSE,
    DUSK_TOKEN_SWITCH,
    DUSK_TOKEN_TRUE,
    DUSK_TOKEN_FALSE,

    DUSK_TOKEN_VOID,
    DUSK_TOKEN_BOOL,

    DUSK_TOKEN_HALF,
    DUSK_TOKEN_HALF2,
    DUSK_TOKEN_HALF2X2,
    DUSK_TOKEN_HALF3,
    DUSK_TOKEN_HALF3X3,
    DUSK_TOKEN_HALF4,
    DUSK_TOKEN_HALF4X4,

    DUSK_TOKEN_FLOAT,
    DUSK_TOKEN_FLOAT2,
    DUSK_TOKEN_FLOAT2X2,
    DUSK_TOKEN_FLOAT3,
    DUSK_TOKEN_FLOAT3X3,
    DUSK_TOKEN_FLOAT4,
    DUSK_TOKEN_FLOAT4X4,

    DUSK_TOKEN_DOUBLE,
    DUSK_TOKEN_DOUBLE2,
    DUSK_TOKEN_DOUBLE2X2,
    DUSK_TOKEN_DOUBLE3,
    DUSK_TOKEN_DOUBLE3X3,
    DUSK_TOKEN_DOUBLE4,
    DUSK_TOKEN_DOUBLE4X4,

    DUSK_TOKEN_BYTE,
    DUSK_TOKEN_BYTE2,
    DUSK_TOKEN_BYTE2X2,
    DUSK_TOKEN_BYTE3,
    DUSK_TOKEN_BYTE3X3,
    DUSK_TOKEN_BYTE4,
    DUSK_TOKEN_BYTE4X4,

    DUSK_TOKEN_UBYTE,
    DUSK_TOKEN_UBYTE2,
    DUSK_TOKEN_UBYTE2X2,
    DUSK_TOKEN_UBYTE3,
    DUSK_TOKEN_UBYTE3X3,
    DUSK_TOKEN_UBYTE4,
    DUSK_TOKEN_UBYTE4X4,

    DUSK_TOKEN_SHORT,
    DUSK_TOKEN_SHORT2,
    DUSK_TOKEN_SHORT2X2,
    DUSK_TOKEN_SHORT3,
    DUSK_TOKEN_SHORT3X3,
    DUSK_TOKEN_SHORT4,
    DUSK_TOKEN_SHORT4X4,

    DUSK_TOKEN_USHORT,
    DUSK_TOKEN_USHORT2,
    DUSK_TOKEN_USHORT2X2,
    DUSK_TOKEN_USHORT3,
    DUSK_TOKEN_USHORT3X3,
    DUSK_TOKEN_USHORT4,
    DUSK_TOKEN_USHORT4X4,

    DUSK_TOKEN_INT,
    DUSK_TOKEN_INT2,
    DUSK_TOKEN_INT2X2,
    DUSK_TOKEN_INT3,
    DUSK_TOKEN_INT3X3,
    DUSK_TOKEN_INT4,
    DUSK_TOKEN_INT4X4,

    DUSK_TOKEN_UINT,
    DUSK_TOKEN_UINT2,
    DUSK_TOKEN_UINT2X2,
    DUSK_TOKEN_UINT3,
    DUSK_TOKEN_UINT3X3,
    DUSK_TOKEN_UINT4,
    DUSK_TOKEN_UINT4X4,

    DUSK_TOKEN_LONG,
    DUSK_TOKEN_LONG2,
    DUSK_TOKEN_LONG2X2,
    DUSK_TOKEN_LONG3,
    DUSK_TOKEN_LONG3X3,
    DUSK_TOKEN_LONG4,
    DUSK_TOKEN_LONG4X4,

    DUSK_TOKEN_ULONG,
    DUSK_TOKEN_ULONG2,
    DUSK_TOKEN_ULONG2X2,
    DUSK_TOKEN_ULONG3,
    DUSK_TOKEN_ULONG3X3,
    DUSK_TOKEN_ULONG4,
    DUSK_TOKEN_ULONG4X4,

    DUSK_TOKEN_LCURLY,   // {
    DUSK_TOKEN_RCURLY,   // }
    DUSK_TOKEN_LBRACKET, // [
    DUSK_TOKEN_RBRACKET, // ]
    DUSK_TOKEN_LPAREN,   // (
    DUSK_TOKEN_RPAREN,   // )

    DUSK_TOKEN_ADD, // +
    DUSK_TOKEN_SUB, // -
    DUSK_TOKEN_MUL, // *
    DUSK_TOKEN_DIV, // /
    DUSK_TOKEN_MOD, // %

    DUSK_TOKEN_ADDADD, // ++
    DUSK_TOKEN_SUBSUB, // --

    DUSK_TOKEN_BITOR,  // |
    DUSK_TOKEN_BITXOR, // ^
    DUSK_TOKEN_BITAND, // &
    DUSK_TOKEN_BITNOT, // ~

    DUSK_TOKEN_LSHIFT, // <<
    DUSK_TOKEN_RSHIFT, // >>

    DUSK_TOKEN_DOT,      // .
    DUSK_TOKEN_COMMA,    // ,
    DUSK_TOKEN_QUESTION, // ?

    DUSK_TOKEN_COLON,     // :
    DUSK_TOKEN_SEMICOLON, // ;

    DUSK_TOKEN_NOT,    // !
    DUSK_TOKEN_ASSIGN, // =

    DUSK_TOKEN_EQ,        // ==
    DUSK_TOKEN_NOTEQ,     // !=
    DUSK_TOKEN_LESS,      // <
    DUSK_TOKEN_LESSEQ,    // <=
    DUSK_TOKEN_GREATER,   // >
    DUSK_TOKEN_GREATEREQ, // >=

    DUSK_TOKEN_ADD_ASSIGN, // +=
    DUSK_TOKEN_SUB_ASSIGN, // -=
    DUSK_TOKEN_MUL_ASSIGN, // *=
    DUSK_TOKEN_DIV_ASSIGN, // /=
    DUSK_TOKEN_MOD_ASSIGN, // %=

    DUSK_TOKEN_BITAND_ASSIGN, // &=
    DUSK_TOKEN_BITOR_ASSIGN,  // |=
    DUSK_TOKEN_BITXOR_ASSIGN, // ^=
    DUSK_TOKEN_BITNOT_ASSIGN, // ~=

    DUSK_TOKEN_LSHIFT_ASSIGN, // <<=
    DUSK_TOKEN_RSHIFT_ASSIGN, // >>=

    DUSK_TOKEN_AND, // &&
    DUSK_TOKEN_OR,  // ||

    DUSK_TOKEN_EOF,
} DuskTokenType;

typedef struct {
    DuskTokenType type;
    DuskLocation location;
    union {
        const char *str;
        int64_t int_;
        double float_;
        DuskScalarType scalar_type;
        struct {
            DuskScalarType scalar_type;
            uint32_t length;
        } vector_type;
        struct {
            DuskScalarType scalar_type;
            uint32_t rows;
            uint32_t cols;
        } matrix_type;
    };
} DuskToken;
// }}}

// AST {{{
typedef enum DuskDeclKind {
    DUSK_DECL_FUNCTION,
    DUSK_DECL_VAR,
    DUSK_DECL_TYPE,
} DuskDeclKind;

struct DuskDecl {
    DuskDeclKind kind;
    DuskLocation location;
    const char *name;
    DuskArray(DuskAttribute) attributes_arr;
    DuskType *type;
    DuskIRValue *ir_value;

    union {
        struct {
            bool is_entry_point;
            DuskShaderStage entry_point_stage;
            DuskArray(DuskIRValue *) entry_point_inputs_arr;
            DuskArray(DuskIRValue *) entry_point_outputs_arr;

            const char *link_name;
            DuskScope *scope;

            DuskArray(DuskDecl *) parameter_decls_arr;

            DuskExpr *return_type_expr;
            DuskArray(DuskAttribute) return_type_attributes_arr;

            DuskArray(DuskStmt *) stmts_arr;
        } function;
        struct {
            DuskScope *scope;
            DuskExpr *type_expr;
            DuskExpr *value_expr;
            DuskStorageClass storage_class;
            bool read_only;
        } var;
        struct {
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

struct DuskStmt {
    DuskStmtKind kind;
    DuskLocation location;

    union {
        DuskDecl *decl;
        DuskExpr *expr;
        struct {
            DuskExpr *assigned_expr;
            DuskExpr *value_expr;
        } assign;
        struct {
            DuskArray(DuskStmt *) stmts_arr;
            DuskScope *scope;
        } block;
        struct {
            DuskExpr *expr;
        } return_;
    };
};

typedef enum {
    DUSK_BINARY_OP_ADD,
    DUSK_BINARY_OP_SUB,
    DUSK_BINARY_OP_MUL,
    DUSK_BINARY_OP_DIV,
    DUSK_BINARY_OP_MOD,
    DUSK_BINARY_OP_BITAND,
    DUSK_BINARY_OP_BITOR,
    DUSK_BINARY_OP_BITXOR,
    DUSK_BINARY_OP_LSHIFT,
    DUSK_BINARY_OP_RSHIFT,
    DUSK_BINARY_OP_EQ,
    DUSK_BINARY_OP_NOTEQ,
    DUSK_BINARY_OP_LESS,
    DUSK_BINARY_OP_LESSEQ,
    DUSK_BINARY_OP_GREATER,
    DUSK_BINARY_OP_GREATEREQ,
    DUSK_BINARY_OP_MAX,
} DuskBinaryOp;

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
    DUSK_EXPR_STRUCT_LITERAL,
    DUSK_EXPR_IDENT,
    DUSK_EXPR_STRUCT_TYPE,
    DUSK_EXPR_ARRAY_TYPE,
    DUSK_EXPR_RUNTIME_ARRAY_TYPE,
    DUSK_EXPR_FUNCTION_CALL,
    DUSK_EXPR_BUILTIN_FUNCTION_CALL,
    DUSK_EXPR_ACCESS,
    DUSK_EXPR_ARRAY_ACCESS,
    DUSK_EXPR_BINARY,
} DuskExprKind;

struct DuskExpr {
    DuskExprKind kind;
    DuskLocation location;
    DuskType *type;
    DuskType *as_type;
    DuskIRValue *ir_value;
    int64_t *resolved_int; // To be filled after semantic analysis

    union {
        DuskScalarType scalar_type;
        struct {
            DuskScalarType scalar_type;
            uint32_t length;
        } vector_type;
        struct {
            DuskScalarType scalar_type;
            uint32_t cols;
            uint32_t rows;
        } matrix_type;
        int64_t int_literal;
        double float_literal;
        bool bool_literal;
        struct {
            DuskExpr *type_expr;
            DuskArray(const char *) field_names_arr;
            DuskArray(DuskExpr *) field_values_arr;
        } struct_literal;
        struct {
            const char *str;
            DuskDecl *decl;
            DuskArray(uint32_t) shuffle_indices_arr;
        } identifier;
        struct {
            const char *str;
        } string;
        struct {
            DuskStructLayout layout;
            const char *name;
            size_t field_count;
            const char **field_names;
            DuskExpr **field_type_exprs;
            DuskArray(DuskAttribute) * field_attribute_arrays;
        } struct_type;
        struct {
            DuskExpr *sub_expr;
            DuskExpr *size_expr;
        } array_type;
        struct {
            DuskExpr *func_expr;
            DuskArray(DuskExpr *) params_arr;
        } function_call;
        struct {
            DuskBuiltinFunctionKind kind;
            DuskArray(DuskExpr *) params_arr;
        } builtin_call;
        struct {
            DuskExpr *base_expr;
            DuskArray(DuskExpr *) chain_arr;
        } access;
        struct {
            DuskBinaryOp op;
            DuskExpr *left;
            DuskExpr *right;
        } binary;
    };
};
// }}}

// Compiler {{{
struct DuskFile {
    const char *path;

    const char *text;
    size_t text_length;

    DuskScope *scope;
    DuskArray(DuskDecl *) decls_arr;
};

typedef struct DuskError {
    DuskLocation location;
    const char *message;
} DuskError;

typedef struct DuskCompiler {
    DuskArena *main_arena;
    DuskArray(DuskError) errors_arr;
    DuskMap *type_cache;
    DuskArray(DuskType *) types_arr;
    jmp_buf jump_buffer;

    DuskMap *keyword_map;
    DuskMap *builtin_function_map;
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
