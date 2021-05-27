#include "dusk_internal.h"

const char *duskTypeToPrettyString(DuskAllocator *allocator, DuskType *type)
{
    if (type->pretty_string) return type->pretty_string;

    switch (type->kind)
    {
    case DUSK_TYPE_VOID: {
        type->pretty_string = "void";
        break;
    }
    case DUSK_TYPE_BOOL: {
        type->pretty_string = "bool";
        break;
    }
    case DUSK_TYPE_TYPE: {
        type->pretty_string = "type";
        break;
    }
    case DUSK_TYPE_STRING: {
        type->pretty_string = "string";
        break;
    }
    case DUSK_TYPE_UNTYPED_INT: {
        type->pretty_string = "untyped int";
        break;
    }
    case DUSK_TYPE_UNTYPED_FLOAT: {
        type->pretty_string = "untyped float";
        break;
    }
    case DUSK_TYPE_INT: {
        if (type->int_.is_signed)
        {
            switch (type->int_.bits)
            {
            case 32: type->pretty_string = "int"; break;
            default: DUSK_ASSERT(0); break;
            }
        }
        else
        {
            switch (type->int_.bits)
            {
            case 32: type->pretty_string = "uint"; break;
            default: DUSK_ASSERT(0); break;
            }
        }
        break;
    }
    case DUSK_TYPE_FLOAT: {
        switch (type->float_.bits)
        {
        case 32: type->pretty_string = "float"; break;
        case 64: type->pretty_string = "double"; break;
        default: DUSK_ASSERT(0); break;
        }
        break;
    }
    case DUSK_TYPE_VECTOR: {
        const char *sub_str =
            duskTypeToPrettyString(allocator, type->vector.sub);
        type->pretty_string =
            duskSprintf(allocator, "%s%u", sub_str, type->vector.size);
        break;
    }
    case DUSK_TYPE_MATRIX: {
        DuskType *col_type = type->matrix.col_type;
        DUSK_ASSERT(col_type->kind == DUSK_TYPE_VECTOR);
        const char *sub_str =
            duskTypeToPrettyString(allocator, col_type->vector.sub);
        type->pretty_string = duskSprintf(
            allocator,
            "%s%ux%u",
            sub_str,
            type->matrix.cols,
            col_type->vector.size);
        break;
    }
    case DUSK_TYPE_RUNTIME_ARRAY: {
        const char *sub_str =
            duskTypeToPrettyString(allocator, type->array.sub);
        type->pretty_string = duskSprintf(allocator, "[]%s", sub_str);
        break;
    }
    case DUSK_TYPE_ARRAY: {
        const char *sub_str =
            duskTypeToPrettyString(allocator, type->array.sub);
        type->pretty_string =
            duskSprintf(allocator, "[%zu]%s", type->array.size, sub_str);
        break;
    }
    case DUSK_TYPE_STRUCT: {
        if (type->struct_.name)
        {
            type->pretty_string = type->struct_.name;
        }
        else
        {
            DuskStringBuilder *sb = duskStringBuilderCreate(NULL, 0);

            duskStringBuilderAppend(sb, "struct{");

            for (size_t i = 0; i < duskArrayLength(type->struct_.field_types);
                 ++i)
            {
                if (i > 0) duskStringBuilderAppend(sb, ", ");

                DuskType *field_type = type->struct_.field_types[i];
                const char *field_string =
                    duskTypeToPrettyString(allocator, field_type);
                duskStringBuilderAppend(sb, field_string);
            }

            duskStringBuilderAppend(sb, "}");

            type->pretty_string = duskStringBuilderBuild(sb, allocator);
            duskStringBuilderDestroy(sb);
        }
        break;
    }
    case DUSK_TYPE_FUNCTION: {
        DuskStringBuilder *sb = duskStringBuilderCreate(NULL, 0);

        duskStringBuilderAppend(sb, "fn (");

        for (size_t i = 0; i < duskArrayLength(type->function.param_types); ++i)
        {
            if (i > 0) duskStringBuilderAppend(sb, ", ");

            DuskType *field_type = type->function.param_types[i];
            const char *field_string =
                duskTypeToPrettyString(allocator, field_type);
            duskStringBuilderAppend(sb, field_string);
        }

        duskStringBuilderAppend(sb, ") ");

        duskStringBuilderAppend(
            sb, duskTypeToPrettyString(allocator, type->function.return_type));

        type->pretty_string = duskStringBuilderBuild(sb, allocator);
        duskStringBuilderDestroy(sb);
        break;
    }
    case DUSK_TYPE_POINTER: {
        const char *sub_str =
            duskTypeToPrettyString(allocator, type->pointer.sub);
        type->pretty_string = duskSprintf(allocator, "*%s", sub_str);
        break;
    }
    case DUSK_TYPE_SAMPLER: {
        type->pretty_string = "@Sampler";
        break;
    }
    case DUSK_TYPE_SAMPLED_IMAGE: {
        const char *image_str = "";
        switch (type->sampled_image.image_type->image.dim)
        {
        case DUSK_IMAGE_DIMENSION_1D: image_str = "@SampledImage1D"; break;
        case DUSK_IMAGE_DIMENSION_2D: image_str = "@SampledImage2D"; break;
        case DUSK_IMAGE_DIMENSION_3D: image_str = "@SampledImage3D"; break;
        case DUSK_IMAGE_DIMENSION_CUBE: image_str = "@SampledImageCube"; break;
        }

        const char *sampled_type_str = duskTypeToPrettyString(
            allocator, type->sampled_image.image_type->image.sampled_type);
        type->pretty_string =
            duskSprintf(allocator, "%s(%s)", image_str, sampled_type_str);
        break;
    }
    case DUSK_TYPE_IMAGE: {
        const char *image_str = "";
        switch (type->image.dim)
        {
        case DUSK_IMAGE_DIMENSION_1D: image_str = "@Image1D"; break;
        case DUSK_IMAGE_DIMENSION_2D: {
            if (type->image.arrayed)
                image_str = "@Image2DArray";
            else
                image_str = "@Image2D";
            break;
        }
        case DUSK_IMAGE_DIMENSION_3D: image_str = "@Image3D"; break;
        case DUSK_IMAGE_DIMENSION_CUBE: {
            if (type->image.arrayed)
                image_str = "@ImageCubeArray";
            else
                image_str = "@ImageCube";
            break;
        }
        }

        const char *sampled_type_str =
            duskTypeToPrettyString(allocator, type->image.sampled_type);
        type->pretty_string =
            duskSprintf(allocator, "%s(%s)", image_str, sampled_type_str);
        break;
    }
    }

    DUSK_ASSERT(type->pretty_string != NULL);

    return type->pretty_string;
}

static const char *duskTypeToString(DuskAllocator *allocator, DuskType *type)
{
    if (type->string) return type->string;

    switch (type->kind)
    {
    case DUSK_TYPE_VOID: {
        type->string = "@void";
        break;
    }
    case DUSK_TYPE_BOOL: {
        type->string = "@bool";
        break;
    }
    case DUSK_TYPE_TYPE: {
        type->string = "@type";
        break;
    }
    case DUSK_TYPE_STRING: {
        type->string = "@string";
        break;
    }
    case DUSK_TYPE_UNTYPED_INT: {
        type->string = "@untyped_int";
        break;
    }
    case DUSK_TYPE_UNTYPED_FLOAT: {
        type->string = "@untyped_float";
        break;
    }
    case DUSK_TYPE_INT: {
        if (type->int_.is_signed)
        {
            type->string = duskSprintf(allocator, "@int%u", type->int_.bits);
        }
        else
        {
            type->string = duskSprintf(allocator, "@uint%u", type->int_.bits);
        }
        break;
    }
    case DUSK_TYPE_FLOAT: {
        type->string = duskSprintf(allocator, "@float%u", type->float_.bits);
        break;
    }
    case DUSK_TYPE_VECTOR: {
        const char *sub_str = duskTypeToString(allocator, type->vector.sub);
        type->string = duskSprintf(
            allocator, "@vector(%s,%u)", sub_str, type->vector.size);
        break;
    }
    case DUSK_TYPE_MATRIX: {
        const char *sub_str =
            duskTypeToString(allocator, type->matrix.col_type);
        type->string = duskSprintf(
            allocator, "@matrix(%u,%s)", type->matrix.cols, sub_str);
        break;
    }
    case DUSK_TYPE_RUNTIME_ARRAY: {
        const char *sub_str = duskTypeToString(allocator, type->array.sub);
        type->string = duskSprintf(allocator, "@rArray(%s)", sub_str);
        break;
    }
    case DUSK_TYPE_ARRAY: {
        const char *sub_str = duskTypeToString(allocator, type->array.sub);
        type->string =
            duskSprintf(allocator, "@array(%s,%zu)", sub_str, type->array.size);
        break;
    }
    case DUSK_TYPE_STRUCT: {
        if (type->struct_.name)
        {
            type->string =
                duskSprintf(allocator, "@named_struct(%s)", type->struct_.name);
        }
        else
        {
            DuskStringBuilder *sb = duskStringBuilderCreate(NULL, 0);

            duskStringBuilderAppend(sb, "@struct(");

            for (size_t i = 0; i < duskArrayLength(type->struct_.field_types);
                 ++i)
            {
                if (i > 0) duskStringBuilderAppend(sb, ",");

                DuskType *field_type = type->struct_.field_types[i];
                const char *field_string =
                    duskTypeToString(allocator, field_type);
                duskStringBuilderAppend(sb, field_string);
            }

            duskStringBuilderAppend(sb, ")");

            type->string = duskStringBuilderBuild(sb, allocator);
            duskStringBuilderDestroy(sb);
        }
        break;
    }
    case DUSK_TYPE_FUNCTION: {
        DuskStringBuilder *sb = duskStringBuilderCreate(NULL, 0);

        duskStringBuilderAppend(sb, "@fn((");

        for (size_t i = 0; i < duskArrayLength(type->function.param_types); ++i)
        {
            if (i > 0) duskStringBuilderAppend(sb, ",");

            DuskType *field_type = type->function.param_types[i];
            const char *field_string = duskTypeToString(allocator, field_type);
            duskStringBuilderAppend(sb, field_string);
        }

        duskStringBuilderAppend(sb, "),");

        const char *return_type_string =
            duskTypeToString(allocator, type->function.return_type);
        duskStringBuilderAppend(sb, return_type_string);

        duskStringBuilderAppend(sb, ")");

        type->string = duskStringBuilderBuild(sb, allocator);
        duskStringBuilderDestroy(sb);
        break;
    }
    case DUSK_TYPE_POINTER: {
        const char *storage_class = "default";
        const char *sub_str = duskTypeToString(allocator, type->pointer.sub);

        switch (type->pointer.storage_class)
        {
        case DUSK_STORAGE_CLASS_UNIFORM: storage_class = "uniform"; break;
        case DUSK_STORAGE_CLASS_UNIFORM_CONSTANT:
            storage_class = "uniform_constant";
            break;
        case DUSK_STORAGE_CLASS_PARAMETER: storage_class = "parameter"; break;
        case DUSK_STORAGE_CLASS_FUNCTION: storage_class = "function"; break;
        case DUSK_STORAGE_CLASS_INPUT: storage_class = "input"; break;
        case DUSK_STORAGE_CLASS_OUTPUT: storage_class = "output"; break;
        case DUSK_STORAGE_CLASS_PUSH_CONSTANT:
            storage_class = "push_constant";
            break;
        }
        type->string =
            duskSprintf(allocator, "@ptr(%s, %s)", sub_str, storage_class);
        break;
    }
    case DUSK_TYPE_SAMPLER: {
        type->string = "@sampler";
        break;
    }
    case DUSK_TYPE_IMAGE: {
        const char *sampled_type_str =
            duskTypeToString(allocator, type->image.sampled_type);
        type->string = duskSprintf(
            allocator,
            "@image(%s, %u, %u, %u, %u, %u)",
            sampled_type_str,
            (uint32_t)type->image.dim,
            type->image.depth,
            type->image.arrayed,
            type->image.multisampled,
            type->image.sampled);
        break;
    }
    case DUSK_TYPE_SAMPLED_IMAGE: {
        const char *image_type_str =
            duskTypeToString(allocator, type->sampled_image.image_type);
        type->string =
            duskSprintf(allocator, "@sampled_image(%s)", image_type_str);
        break;
    }
    }

    DUSK_ASSERT(type->string != NULL);

    return type->string;
}

static DuskType *duskTypeGetCached(DuskCompiler *compiler, DuskType *type)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);
    const char *type_str = duskTypeToString(allocator, type);

    DuskType *existing_type = NULL;
    if (duskMapGet(compiler->type_cache, type_str, (void **)&existing_type))
    {
        DUSK_ASSERT(existing_type != NULL);
        return existing_type;
    }

    duskMapSet(compiler->type_cache, type_str, type);
    duskArrayPush(&compiler->types, type);

    return type;
}

bool duskTypeIsRuntime(DuskType *type)
{
    switch (type->kind)
    {
    case DUSK_TYPE_FUNCTION:
    case DUSK_TYPE_TYPE:
    case DUSK_TYPE_STRING:
    case DUSK_TYPE_UNTYPED_FLOAT:
    case DUSK_TYPE_UNTYPED_INT: return false;
    default: return true;
    }

    return true;
}

DuskType *duskTypeNewBasic(DuskCompiler *compiler, DuskTypeKind kind)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);
    DuskType *type = DUSK_NEW(allocator, DuskType);
    type->kind = kind;
    return duskTypeGetCached(compiler, type);
}

DuskType *duskTypeNewScalar(DuskCompiler *compiler, DuskScalarType scalar_type)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);
    DuskType *type = DUSK_NEW(allocator, DuskType);

    switch (scalar_type)
    {
    case DUSK_SCALAR_TYPE_INT: {
        type->kind = DUSK_TYPE_INT;
        type->int_.is_signed = true;
        type->int_.bits = 32;
        break;
    }
    case DUSK_SCALAR_TYPE_UINT: {
        type->kind = DUSK_TYPE_INT;
        type->int_.is_signed = false;
        type->int_.bits = 32;
        break;
    }
    case DUSK_SCALAR_TYPE_FLOAT: {
        type->kind = DUSK_TYPE_FLOAT;
        type->float_.bits = 32;
        break;
    }
    case DUSK_SCALAR_TYPE_DOUBLE: {
        type->kind = DUSK_TYPE_FLOAT;
        type->float_.bits = 64;
        break;
    }
    }

    return duskTypeGetCached(compiler, type);
}

DuskType *
duskTypeNewVector(DuskCompiler *compiler, DuskType *sub, uint32_t size)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);
    DuskType *type = DUSK_NEW(allocator, DuskType);
    type->kind = DUSK_TYPE_VECTOR;
    type->vector.sub = sub;
    type->vector.size = size;
    return duskTypeGetCached(compiler, type);
}

DuskType *
duskTypeNewMatrix(DuskCompiler *compiler, DuskType *col_type, uint32_t cols)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);
    DuskType *type = DUSK_NEW(allocator, DuskType);
    type->kind = DUSK_TYPE_MATRIX;
    type->matrix.col_type = col_type;
    type->matrix.cols = cols;
    return duskTypeGetCached(compiler, type);
}

DuskType *duskTypeNewRuntimeArray(DuskCompiler *compiler, DuskType *sub)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);
    DuskType *type = DUSK_NEW(allocator, DuskType);
    type->kind = DUSK_TYPE_RUNTIME_ARRAY;
    type->array.sub = sub;
    return duskTypeGetCached(compiler, type);
}

DuskType *duskTypeNewArray(DuskCompiler *compiler, DuskType *sub, size_t size)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);
    DuskType *type = DUSK_NEW(allocator, DuskType);
    type->kind = DUSK_TYPE_ARRAY;
    type->array.sub = sub;
    type->array.size = size;

    return duskTypeGetCached(compiler, type);
}

DuskType *duskTypeNewStruct(
    DuskCompiler *compiler,
    const char *name,
    DuskArray(const char *) field_names,
    DuskArray(DuskType *) field_types)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);
    DuskType *type = DUSK_NEW(allocator, DuskType);
    type->kind = DUSK_TYPE_STRUCT;
    type->struct_.name = name;
    type->struct_.field_names = field_names;
    type->struct_.field_types = field_types;

    size_t field_count = duskArrayLength(field_names);
    type->struct_.index_map = duskMapCreate(allocator, field_count);
    for (uintptr_t i = 0; i < field_count; ++i)
    {
        duskMapSet(type->struct_.index_map, field_names[i], (void *)i);
    }

    return duskTypeGetCached(compiler, type);
}

DuskType *duskTypeNewFunction(
    DuskCompiler *compiler,
    DuskType *return_type,
    DuskArray(DuskType *) param_types)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);
    DuskType *type = DUSK_NEW(allocator, DuskType);
    type->kind = DUSK_TYPE_FUNCTION;
    type->function.return_type = return_type;
    type->function.param_types = param_types;
    return duskTypeGetCached(compiler, type);
}

DuskType *duskTypeNewPointer(
    DuskCompiler *compiler, DuskType *sub, DuskStorageClass storage_class)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);
    DuskType *type = DUSK_NEW(allocator, DuskType);
    type->kind = DUSK_TYPE_POINTER;
    type->pointer.sub = sub;
    type->pointer.storage_class = storage_class;
    return duskTypeGetCached(compiler, type);
}

DuskType *duskTypeNewImage(
    DuskCompiler *compiler,
    DuskType *sampled_type,
    DuskImageDimension dim,
    bool depth,
    bool arrayed,
    bool multisampled,
    bool sampled)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);
    DuskType *type = DUSK_NEW(allocator, DuskType);
    type->kind = DUSK_TYPE_IMAGE;
    type->image.sampled_type = sampled_type;
    type->image.dim = (uint32_t)dim;
    type->image.depth = (uint32_t)depth;
    type->image.arrayed = (uint32_t)arrayed;
    type->image.multisampled = (uint32_t)multisampled;
    type->image.sampled = (uint32_t)sampled;
    return duskTypeGetCached(compiler, type);
}

DuskType *duskTypeNewSampledImage(DuskCompiler *compiler, DuskType *image_type)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);
    DuskType *type = DUSK_NEW(allocator, DuskType);
    type->kind = DUSK_TYPE_SAMPLED_IMAGE;
    type->sampled_image.image_type = image_type;
    return duskTypeGetCached(compiler, type);
}

void duskTypeMarkNotDead(DuskType *type)
{
    DUSK_ASSERT(type);
    type->emit = true;

    switch (type->kind)
    {
    case DUSK_TYPE_POINTER: {
        duskTypeMarkNotDead(type->pointer.sub);
        break;
    }
    case DUSK_TYPE_VECTOR: {
        duskTypeMarkNotDead(type->vector.sub);
        break;
    }
    case DUSK_TYPE_RUNTIME_ARRAY:
    case DUSK_TYPE_ARRAY: {
        duskTypeMarkNotDead(type->array.sub);
        break;
    }
    case DUSK_TYPE_MATRIX: {
        duskTypeMarkNotDead(type->matrix.col_type);
        break;
    }
    case DUSK_TYPE_STRUCT: {
        for (size_t i = 0; i < duskArrayLength(type->struct_.field_types); ++i)
        {
            duskTypeMarkNotDead(type->struct_.field_types[i]);
        }
        break;
    }
    case DUSK_TYPE_FUNCTION: {
        duskTypeMarkNotDead(type->function.return_type);
        for (size_t i = 0; i < duskArrayLength(type->function.param_types); ++i)
        {
            duskTypeMarkNotDead(type->function.param_types[i]);
        }
        break;
    }
    case DUSK_TYPE_IMAGE: {
        duskTypeMarkNotDead(type->image.sampled_type);
        break;
    }
    case DUSK_TYPE_SAMPLED_IMAGE: {
        duskTypeMarkNotDead(type->sampled_image.image_type);
        break;
    }
    case DUSK_TYPE_SAMPLER:
    case DUSK_TYPE_STRING:
    case DUSK_TYPE_TYPE:
    case DUSK_TYPE_FLOAT:
    case DUSK_TYPE_INT:
    case DUSK_TYPE_UNTYPED_FLOAT:
    case DUSK_TYPE_UNTYPED_INT:
    case DUSK_TYPE_BOOL:
    case DUSK_TYPE_VOID: break;
    }
}
