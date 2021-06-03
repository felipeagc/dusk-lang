#include "dusk_internal.h"
#include "spirv.h"

static void duskEmitType(DuskIRModule *module, DuskType *type);
static void duskEmitValue(DuskIRModule *module, DuskIRValue *value);

static const char *
duskIRConstToString(DuskAllocator *allocator, DuskIRValue *value)
{
    if (value->const_string) return value->const_string;

    switch (value->kind)
    {
    case DUSK_IR_VALUE_CONSTANT_BOOL: {
        value->const_string =
            (value->const_bool.value ? "@bool_true" : "@bool_false");
        break;
    }
    case DUSK_IR_VALUE_CONSTANT: {
        switch (value->type->kind)
        {
        case DUSK_TYPE_INT: {
            if (value->type->int_.is_signed)
            {
                switch (value->type->int_.bits)
                {
                case 8:
                    value->const_string = duskSprintf(
                        allocator,
                        "@i8(%c)",
                        *(int8_t *)value->constant.value_words);
                    break;
                case 16:
                    value->const_string = duskSprintf(
                        allocator,
                        "@i16(%hd)",
                        *(int16_t *)value->constant.value_words);
                    break;
                case 32:
                    value->const_string = duskSprintf(
                        allocator,
                        "@i32(%d)",
                        *(int32_t *)value->constant.value_words);
                    break;
                case 64:
                    value->const_string = duskSprintf(
                        allocator,
                        "@i64(%ld)",
                        *(int64_t *)value->constant.value_words);
                    break;
                }
            }
            else
            {
                switch (value->type->int_.bits)
                {
                case 8:
                    value->const_string = duskSprintf(
                        allocator,
                        "@u8(%uc)",
                        *(uint8_t *)value->constant.value_words);
                    break;
                case 16:
                    value->const_string = duskSprintf(
                        allocator,
                        "@u16(%hu)",
                        *(uint16_t *)value->constant.value_words);
                    break;
                case 32:
                    value->const_string = duskSprintf(
                        allocator,
                        "@u32(%u)",
                        *(uint32_t *)value->constant.value_words);
                    break;
                case 64:
                    value->const_string = duskSprintf(
                        allocator,
                        "@u64(%lu)",
                        *(uint64_t *)value->constant.value_words);
                    break;
                }
            }
            break;
        }
        case DUSK_TYPE_FLOAT: {
            switch (value->type->float_.bits)
            {
            case 32:
                value->const_string = duskSprintf(
                    allocator,
                    "@f32(%f)",
                    *(float *)value->constant.value_words);
                break;
            case 64:
                value->const_string = duskSprintf(
                    allocator,
                    "@f64(%lf)",
                    *(double *)value->constant.value_words);
                break;
            }
            break;
        }
        default: DUSK_ASSERT(0); break;
        }
        break;
    }
    case DUSK_IR_VALUE_CONSTANT_COMPOSITE: {
        DuskStringBuilder *sb = duskStringBuilderCreate(allocator, 1024);
        duskStringBuilderAppend(sb, duskTypeToString(allocator, value->type));
        duskStringBuilderAppend(sb, "(");
        for (size_t i = 0;
             i < duskArrayLength(value->constant_composite.values);
             ++i)
        {
            if (i != 0) duskStringBuilderAppend(sb, ",");
            DuskIRValue *elem_value = value->constant_composite.values[i];
            const char *elem_str = duskIRConstToString(allocator, elem_value);
            duskStringBuilderAppend(sb, elem_str);
        }
        duskStringBuilderAppend(sb, ")");

        value->const_string = duskStringBuilderBuild(sb, allocator);
        duskStringBuilderDestroy(sb);
        break;
    }
    default: DUSK_ASSERT(0); break;
    }

    return value->const_string;
}

static DuskIRValue *
duskIRGetCachedConst(DuskIRModule *module, DuskIRValue *value)
{
    const char *value_str = duskIRConstToString(module->allocator, value);

    DuskIRValue *existing_value = NULL;
    if (duskMapGet(module->const_cache, value_str, (void **)&existing_value))
    {
        DUSK_ASSERT(existing_value != NULL);
        return existing_value;
    }

    duskMapSet(module->const_cache, value_str, value);
    duskArrayPush(&module->consts, value);

    return value;
}

static uint32_t duskReserveId(DuskIRModule *module)
{
    return ++module->last_id;
}

static void duskEncodeInst(
    DuskIRModule *m, SpvOp opcode, uint32_t *params, size_t params_count)
{
    uint32_t opcode_word = opcode;
    opcode_word |= ((uint16_t)(params_count + 1)) << 16;

    duskArrayPush(&m->stream, opcode_word);
    for (uint32_t i = 0; i < params_count; ++i)
    {
        duskArrayPush(&m->stream, params[i]);
    }
}

bool duskIRBlockIsTerminated(DuskIRValue *block)
{
    DUSK_ASSERT(block->kind == DUSK_IR_VALUE_BLOCK);

    size_t inst_count = duskArrayLength(block->block.insts);
    if (inst_count == 0) return false;

    DuskIRValue *inst = block->block.insts[inst_count - 1];
    switch (inst->kind)
    {
    case DUSK_IR_VALUE_RETURN:
    case DUSK_IR_VALUE_DISCARD: {
        return true;
    }
    default: break;
    }
    return false;
}

DuskIRModule *duskIRModuleCreate(DuskCompiler *compiler)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);
    DuskIRModule *module = DUSK_NEW(allocator, DuskIRModule);

    module->compiler = compiler;
    module->allocator = allocator;

    module->last_id = 0;
    module->stream = duskArrayCreate(allocator, uint32_t);

    module->const_cache = duskMapCreate(allocator, 32);
    module->consts = duskArrayCreate(allocator, DuskIRValue *);

    module->glsl_ext_inst_id = duskReserveId(module);

    module->entry_points = duskArrayCreate(allocator, DuskIREntryPoint *);
    module->functions = duskArrayCreate(allocator, DuskIRValue *);
    module->globals = duskArrayCreate(allocator, DuskIRValue *);

    return module;
}

DuskIRValue *duskIRBlockCreate(DuskIRModule *module)
{
    DuskIRValue *value = DUSK_NEW(module->allocator, DuskIRValue);
    value->kind = DUSK_IR_VALUE_BLOCK;
    value->block.insts = duskArrayCreate(module->allocator, DuskIRValue *);
    return value;
}

DuskIRValue *
duskIRFunctionCreate(DuskIRModule *module, DuskType *type, const char *name)
{
    DuskIRValue *value = DUSK_NEW(module->allocator, DuskIRValue);
    value->kind = DUSK_IR_VALUE_FUNCTION;
    value->type = type;
    value->function.name = name;
    value->function.blocks = duskArrayCreate(module->allocator, DuskIRValue *);
    value->function.variables =
        duskArrayCreate(module->allocator, DuskIRValue *);
    value->function.params = duskArrayCreate(module->allocator, DuskIRValue *);
    for (size_t i = 0; i < duskArrayLength(type->function.param_types); ++i)
    {
        DuskIRValue *param_value = DUSK_NEW(module->allocator, DuskIRValue);
        param_value->kind = DUSK_IR_VALUE_FUNCTION_PARAMETER;
        param_value->type = type->function.param_types[i];
        duskArrayPush(&value->function.params, param_value);
    }

    DuskIRValue *first_block = duskIRBlockCreate(module);
    duskIRFunctionAddBlock(value, first_block);

    duskTypeMarkNotDead(value->type);

    return value;
}

void duskIRFunctionAddBlock(DuskIRValue *function, DuskIRValue *block)
{
    DUSK_ASSERT(function->kind == DUSK_IR_VALUE_FUNCTION);
    DUSK_ASSERT(block->kind == DUSK_IR_VALUE_BLOCK);
    duskArrayPush(&function->function.blocks, block);
}

DuskIRValue *duskIRVariableCreate(
    DuskIRModule *module, DuskType *type, DuskStorageClass storage_class)
{
    DuskIRValue *value = DUSK_NEW(module->allocator, DuskIRValue);
    value->kind = DUSK_IR_VALUE_VARIABLE;
    value->type = duskTypeNewPointer(module->compiler, type, storage_class);
    value->var.storage_class = storage_class;

    duskTypeMarkNotDead(value->type);

    switch (storage_class)
    {
    case DUSK_STORAGE_CLASS_STORAGE:
    case DUSK_STORAGE_CLASS_UNIFORM:
    case DUSK_STORAGE_CLASS_INPUT:
    case DUSK_STORAGE_CLASS_OUTPUT:
    case DUSK_STORAGE_CLASS_PUSH_CONSTANT:
    case DUSK_STORAGE_CLASS_UNIFORM_CONSTANT: {
        duskArrayPush(&module->globals, value);
        break;
    }
    case DUSK_STORAGE_CLASS_PARAMETER:
    case DUSK_STORAGE_CLASS_FUNCTION: break;
    }

    return value;
}

void duskIRModuleAddEntryPoint(
    DuskIRModule *module,
    DuskIRValue *function,
    const char *name,
    DuskShaderStage stage,
    size_t referenced_global_count,
    DuskIRValue **referenced_globals)
{
    DuskIREntryPoint *entry_point =
        duskAllocateZeroed(module->allocator, sizeof(DuskIREntryPoint));

    entry_point->name = name;
    entry_point->function = function;
    entry_point->stage = stage;
    entry_point->referenced_globals =
        duskArrayCreate(module->allocator, DuskIRValue *);
    duskArrayResize(&entry_point->referenced_globals, referenced_global_count);
    memcpy(
        entry_point->referenced_globals,
        referenced_globals,
        sizeof(DuskIRValue *) * referenced_global_count);

    duskArrayPush(&module->entry_points, entry_point);
}

DuskIRValue *duskIRConstBoolCreate(DuskIRModule *module, bool bool_value)
{
    DuskIRValue *value = DUSK_NEW(module->allocator, DuskIRValue);
    value->type = duskTypeNewBasic(module->compiler, DUSK_TYPE_BOOL);
    value->kind = DUSK_IR_VALUE_CONSTANT_BOOL;
    value->const_bool.value = bool_value;
    return duskIRGetCachedConst(module, value);
}

DuskIRValue *
duskIRConstIntCreate(DuskIRModule *module, DuskType *type, uint64_t int_value)
{
    DuskIRValue *value = DUSK_NEW(module->allocator, DuskIRValue);
    value->type = type;
    value->kind = DUSK_IR_VALUE_CONSTANT;

    switch (type->int_.bits)
    {
    case 8: {
        value->constant.value_word_count = 1;
        value->constant.value_words =
            DUSK_NEW_ARRAY(module->allocator, uint32_t, 1);
        uint8_t val = (uint8_t)int_value;
        memcpy(value->constant.value_words, &val, sizeof(val));
        break;
    }
    case 16: {
        value->constant.value_word_count = 1;
        value->constant.value_words =
            DUSK_NEW_ARRAY(module->allocator, uint32_t, 1);
        uint16_t val = (uint16_t)int_value;
        memcpy(value->constant.value_words, &val, sizeof(val));
        break;
    }
    case 32: {
        value->constant.value_word_count = 1;
        value->constant.value_words =
            DUSK_NEW_ARRAY(module->allocator, uint32_t, 1);
        uint32_t val = (uint32_t)int_value;
        memcpy(value->constant.value_words, &val, sizeof(val));
        break;
    }
    case 64: {
        value->constant.value_word_count = 2;
        value->constant.value_words =
            DUSK_NEW_ARRAY(module->allocator, uint32_t, 2);
        memcpy(value->constant.value_words, &int_value, sizeof(uint64_t));
        break;
    }
    default: DUSK_ASSERT(0); break;
    }

    return duskIRGetCachedConst(module, value);
}

DuskIRValue *duskIRConstFloatCreate(
    DuskIRModule *module, DuskType *type, double double_value)
{
    DuskIRValue *value = DUSK_NEW(module->allocator, DuskIRValue);
    value->type = type;
    value->kind = DUSK_IR_VALUE_CONSTANT;

    switch (type->float_.bits)
    {
    case 32: {
        value->constant.value_word_count = 1;
        value->constant.value_words =
            DUSK_NEW_ARRAY(module->allocator, uint32_t, 1);
        float val = (float)double_value;
        memcpy(value->constant.value_words, &val, sizeof(val));
        break;
    }
    case 64: {
        value->constant.value_word_count = 2;
        value->constant.value_words =
            DUSK_NEW_ARRAY(module->allocator, uint32_t, 2);
        memcpy(value->constant.value_words, &double_value, sizeof(uint64_t));
        break;
    }
    default: DUSK_ASSERT(0); break;
    }

    return duskIRGetCachedConst(module, value);
}

DuskIRValue *duskIRConstCompositeCreate(
    DuskIRModule *module,
    DuskType *type,
    size_t value_count,
    DuskIRValue **values)
{
    DuskIRValue *value = DUSK_NEW(module->allocator, DuskIRValue);
    value->type = type;
    duskTypeMarkNotDead(value->type);
    value->kind = DUSK_IR_VALUE_CONSTANT_COMPOSITE;

    value->constant_composite.values =
        duskArrayCreate(module->allocator, DuskIRValue *);
    duskArrayResize(&value->constant_composite.values, value_count);
    memcpy(
        value->constant_composite.values,
        values,
        value_count * sizeof(DuskIRValue *));

    return duskIRGetCachedConst(module, value);
}

static void duskIRBlockAppendInst(DuskIRValue *block, DuskIRValue *inst)
{
    if (!duskIRBlockIsTerminated(block))
    {
        duskArrayPush(&block->block.insts, inst);
    }
}

DuskIRDecoration duskIRCreateDecoration(
    DuskIRModule *module,
    DuskIRDecorationKind kind,
    size_t literal_count,
    uint32_t *literals)
{
    DuskIRDecoration decoration = {0};
    decoration.kind = kind;
    decoration.literals = duskArrayCreate(module->allocator, uint32_t);

    if (literal_count > 0)
    {
        duskArrayResize(&decoration.literals, literal_count);
        memcpy(decoration.literals, literals, sizeof(uint32_t) * literal_count);
    }

    return decoration;
}

void duskIRCreateReturn(
    DuskIRModule *module, DuskIRValue *block, DuskIRValue *value)
{
    DuskIRValue *inst = DUSK_NEW(module->allocator, DuskIRValue);
    inst->type = duskTypeNewBasic(module->compiler, DUSK_TYPE_VOID);
    inst->kind = DUSK_IR_VALUE_RETURN;
    inst->return_.value = value;
    duskIRBlockAppendInst(block, inst);
}

void duskIRCreateDiscard(DuskIRModule *module, DuskIRValue *block)
{
    DuskIRValue *inst = DUSK_NEW(module->allocator, DuskIRValue);
    inst->type = duskTypeNewBasic(module->compiler, DUSK_TYPE_VOID);
    inst->kind = DUSK_IR_VALUE_DISCARD;
    duskIRBlockAppendInst(block, inst);
}

void duskIRCreateStore(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskIRValue *pointer,
    DuskIRValue *value)
{
    DuskIRValue *inst = DUSK_NEW(module->allocator, DuskIRValue);
    inst->type = duskTypeNewBasic(module->compiler, DUSK_TYPE_VOID);
    inst->kind = DUSK_IR_VALUE_STORE;
    inst->store.pointer = pointer;
    inst->store.value = value;
    duskIRBlockAppendInst(block, inst);
}

DuskIRValue *
duskIRCreateLoad(DuskIRModule *module, DuskIRValue *block, DuskIRValue *pointer)
{
    DUSK_ASSERT(pointer->type->kind == DUSK_TYPE_POINTER);

    DuskIRValue *inst = DUSK_NEW(module->allocator, DuskIRValue);
    inst->type = pointer->type->pointer.sub;
    inst->kind = DUSK_IR_VALUE_LOAD;
    inst->load.pointer = pointer;
    duskIRBlockAppendInst(block, inst);
    return inst;
}

DuskIRValue *duskIRCreateFunctionCall(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskIRValue *function,
    size_t param_count,
    DuskIRValue **params)
{
    DuskIRValue *inst = DUSK_NEW(module->allocator, DuskIRValue);
    inst->type = function->type->function.return_type;
    inst->kind = DUSK_IR_VALUE_FUNCTION_CALL;
    inst->function_call.function = function;
    inst->function_call.params =
        duskArrayCreate(module->allocator, DuskIRValue *);
    duskArrayResize(&inst->function_call.params, param_count);

    for (size_t i = 0; i < param_count; ++i)
    {
        inst->function_call.params[i] = params[i];
    }

    duskIRBlockAppendInst(block, inst);
    return inst;
}

DuskIRValue *duskIRCreateAccessChain(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskType *accessed_type,
    DuskIRValue *base,
    size_t index_count,
    DuskIRValue **indices)
{
    DuskIRValue *inst = DUSK_NEW(module->allocator, DuskIRValue);
    inst->kind = DUSK_IR_VALUE_ACCESS_CHAIN;
    inst->access_chain.base = base;
    inst->access_chain.indices =
        duskArrayCreate(module->allocator, DuskIRValue *);
    duskArrayResize(&inst->access_chain.indices, index_count);
    memcpy(
        inst->access_chain.indices,
        indices,
        index_count * sizeof(DuskIRValue *));

    for (size_t i = 0; i < index_count; ++i)
    {
        duskTypeMarkNotDead(indices[i]->type);
    }

    inst->type = duskTypeNewPointer(
        module->compiler, accessed_type, DUSK_STORAGE_CLASS_FUNCTION);
    duskTypeMarkNotDead(inst->type);

    duskIRBlockAppendInst(block, inst);
    return inst;
}

DuskIRValue *duskIRCreateCompositeExtract(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskType *accessed_type,
    DuskIRValue *composite,
    size_t index_count,
    uint32_t *indices)
{
    DuskIRValue *inst = DUSK_NEW(module->allocator, DuskIRValue);
    inst->kind = DUSK_IR_VALUE_COMPOSITE_EXTRACT;
    inst->composite_extract.composite = composite;
    inst->composite_extract.indices =
        duskArrayCreate(module->allocator, uint32_t);
    duskArrayResize(&inst->composite_extract.indices, index_count);
    memcpy(
        inst->composite_extract.indices,
        indices,
        index_count * sizeof(uint32_t));

    inst->type = accessed_type;
    duskTypeMarkNotDead(inst->type);

    duskIRBlockAppendInst(block, inst);
    return inst;
}

DuskIRValue *duskIRCreateVectorShuffle(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskIRValue *vec1,
    DuskIRValue *vec2,
    size_t index_count,
    uint32_t *indices)
{
    DuskIRValue *inst = DUSK_NEW(module->allocator, DuskIRValue);
    inst->kind = DUSK_IR_VALUE_VECTOR_SHUFFLE;
    inst->vector_shuffle.vec1 = vec1;
    inst->vector_shuffle.vec2 = vec2;
    inst->vector_shuffle.indices = duskArrayCreate(module->allocator, uint32_t);
    duskArrayResize(&inst->vector_shuffle.indices, index_count);
    memcpy(
        inst->vector_shuffle.indices, indices, index_count * sizeof(uint32_t));

    DUSK_ASSERT(vec1->type->kind == DUSK_TYPE_VECTOR);
    DUSK_ASSERT(vec2->type->kind == DUSK_TYPE_VECTOR);
    DUSK_ASSERT(vec1->type->vector.sub == vec2->type->vector.sub);

    inst->type = duskTypeNewVector(
        module->compiler, vec1->type->vector.sub, index_count);
    duskTypeMarkNotDead(inst->type);

    duskIRBlockAppendInst(block, inst);
    return inst;
}

DuskIRValue *duskIRCreateCompositeConstruct(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskType *composite_type,
    size_t value_count,
    DuskIRValue **values)
{
    DuskIRValue *inst = DUSK_NEW(module->allocator, DuskIRValue);
    inst->kind = DUSK_IR_VALUE_COMPOSITE_CONSTRUCT;
    inst->composite_construct.values =
        duskArrayCreate(module->allocator, DuskIRValue *);
    duskArrayResize(&inst->composite_construct.values, value_count);
    memcpy(
        inst->composite_construct.values,
        values,
        value_count * sizeof(DuskIRValue *));

    inst->type = composite_type;
    duskTypeMarkNotDead(inst->type);

    duskIRBlockAppendInst(block, inst);
    return inst;
}

bool duskIRValueIsConstant(DuskIRValue *value)
{
    return value->kind == DUSK_IR_VALUE_CONSTANT ||
           value->kind == DUSK_IR_VALUE_CONSTANT_BOOL ||
           value->kind == DUSK_IR_VALUE_CONSTANT_COMPOSITE;
}

bool duskIRIsLvalue(DuskIRValue *value)
{
    return value->kind == DUSK_IR_VALUE_VARIABLE ||
           value->kind == DUSK_IR_VALUE_ACCESS_CHAIN;
}

DuskIRValue *
duskIRLoadLvalue(DuskIRModule *module, DuskIRValue *block, DuskIRValue *value)
{
    if (duskIRIsLvalue(value))
    {
        return duskIRCreateLoad(module, block, value);
    }

    return value;
}

static void duskEmitType(DuskIRModule *module, DuskType *type)
{
    if (!type->emit) return;

    type->emit = false;

    DuskAllocator *allocator = module->allocator;

    switch (type->kind)
    {
    case DUSK_TYPE_VOID: {
        duskEncodeInst(module, SpvOpTypeVoid, &type->id, 1);
        break;
    }
    case DUSK_TYPE_BOOL: {
        duskEncodeInst(module, SpvOpTypeBool, &type->id, 1);
        break;
    }
    case DUSK_TYPE_STRING: {
        DUSK_ASSERT(0);
        break;
    }
    case DUSK_TYPE_INT: {
        uint32_t params[3] = {
            type->id, type->int_.bits, (uint32_t)type->int_.is_signed};
        duskEncodeInst(
            module, SpvOpTypeInt, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_TYPE_FLOAT: {
        uint32_t params[2] = {type->id, type->float_.bits};
        duskEncodeInst(
            module, SpvOpTypeFloat, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_TYPE_POINTER: {
        duskEmitType(module, type->pointer.sub);

        SpvStorageClass storage_class = 0;

        switch (type->pointer.storage_class)
        {
        case DUSK_STORAGE_CLASS_FUNCTION:
            storage_class = SpvStorageClassFunction;
            break;
        case DUSK_STORAGE_CLASS_INPUT:
            storage_class = SpvStorageClassInput;
            break;
        case DUSK_STORAGE_CLASS_OUTPUT:
            storage_class = SpvStorageClassOutput;
            break;
        case DUSK_STORAGE_CLASS_UNIFORM:
            storage_class = SpvStorageClassUniform;
            break;
        case DUSK_STORAGE_CLASS_STORAGE:
            storage_class = SpvStorageClassStorageBuffer;
            break;
        case DUSK_STORAGE_CLASS_UNIFORM_CONSTANT:
            storage_class = SpvStorageClassUniformConstant;
            break;
        case DUSK_STORAGE_CLASS_PUSH_CONSTANT:
            storage_class = SpvStorageClassPushConstant;
            break;
        case DUSK_STORAGE_CLASS_PARAMETER: DUSK_ASSERT(0); break;
        }

        uint32_t params[3] = {type->id, storage_class, type->pointer.sub->id};
        duskEncodeInst(
            module, SpvOpTypePointer, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_TYPE_RUNTIME_ARRAY: {
        duskEmitType(module, type->array.sub);

        uint32_t params[2] = {type->id, type->array.sub->id};
        duskEncodeInst(
            module, SpvOpTypeRuntimeArray, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_TYPE_ARRAY: {
        DUSK_ASSERT(type->array.size_ir_value);

        duskEmitType(module, type->array.sub);
        duskEmitValue(module, type->array.size_ir_value);

        uint32_t params[3] = {
            type->id, type->array.sub->id, type->array.size_ir_value->id};
        duskEncodeInst(
            module, SpvOpTypeArray, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_TYPE_VECTOR: {
        duskEmitType(module, type->vector.sub);

        uint32_t params[3] = {
            type->id, type->vector.sub->id, type->vector.size};
        duskEncodeInst(
            module, SpvOpTypeVector, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_TYPE_MATRIX: {
        duskEmitType(module, type->matrix.col_type);

        uint32_t params[3] = {
            type->id, type->matrix.col_type->id, type->matrix.cols};
        duskEncodeInst(
            module, SpvOpTypeMatrix, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_TYPE_FUNCTION: {
        size_t func_param_count = duskArrayLength(type->function.param_types);

        duskEmitType(module, type->function.return_type);
        for (size_t i = 0; i < func_param_count; ++i)
        {
            DuskType *param_type = type->function.param_types[i];
            duskEmitType(module, param_type);
        }

        uint32_t param_count = 2 + func_param_count;
        uint32_t *params = DUSK_NEW_ARRAY(allocator, uint32_t, param_count);

        params[0] = type->id;
        params[1] = type->function.return_type->id;

        for (size_t i = 0; i < func_param_count; ++i)
        {
            DuskType *param_type = type->function.param_types[i];
            params[2 + i] = param_type->id;
        }

        duskEncodeInst(module, SpvOpTypeFunction, params, param_count);
        break;
    }
    case DUSK_TYPE_STRUCT: {
        size_t field_count = duskArrayLength(type->struct_.field_types);

        for (size_t i = 0; i < field_count; ++i)
        {
            duskEmitType(module, type->struct_.field_types[i]);
        }

        uint32_t word_count = 1 + (uint32_t)field_count;
        uint32_t *params = DUSK_NEW_ARRAY(allocator, uint32_t, word_count);
        params[0] = type->id;

        for (size_t i = 0; i < field_count; ++i)
        {
            params[1 + i] = type->struct_.field_types[i]->id;
        }

        duskEncodeInst(module, SpvOpTypeStruct, params, word_count);
        break;
    }
    case DUSK_TYPE_SAMPLER: {
        duskEncodeInst(module, SpvOpTypeSampler, &type->id, 1);
        break;
    }
    case DUSK_TYPE_IMAGE: {
        duskEmitType(module, type->image.sampled_type);

        SpvDim dim = SpvDim2D;
        switch (type->image.dim)
        {
        case DUSK_IMAGE_DIMENSION_1D: dim = SpvDim1D; break;
        case DUSK_IMAGE_DIMENSION_2D: dim = SpvDim2D; break;
        case DUSK_IMAGE_DIMENSION_3D: dim = SpvDim3D; break;
        case DUSK_IMAGE_DIMENSION_CUBE: dim = SpvDimCube; break;
        }

        uint32_t params[] = {
            type->id,
            type->image.sampled_type->id,
            dim,
            type->image.depth,
            type->image.arrayed,
            type->image.multisampled,
            type->image.sampled,
            SpvImageFormatUnknown,
        };
        duskEncodeInst(
            module, SpvOpTypeImage, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_TYPE_SAMPLED_IMAGE: {
        duskEmitType(module, type->sampled_image.image_type);

        uint32_t params[] = {
            type->id,
            type->sampled_image.image_type->id,
        };
        duskEncodeInst(
            module, SpvOpTypeSampledImage, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_TYPE_UNTYPED_INT:
    case DUSK_TYPE_UNTYPED_FLOAT:
    case DUSK_TYPE_TYPE: break;
    }
}

static void duskEmitDecorations(
    DuskIRModule *module, uint32_t id, DuskArray(DuskIRDecoration) decorations)
{
    if (decorations == NULL) return;
    if (id == 0) return;

    for (size_t i = 0; i < duskArrayLength(decorations); ++i)
    {
        DuskIRDecoration *decoration = &decorations[i];
        DUSK_ASSERT(decoration->literals != NULL);

        size_t param_count = 2 + duskArrayLength(decoration->literals);
        uint32_t *params = duskAllocateZeroed(
            module->allocator, param_count * sizeof(uint32_t));
        params[0] = id;

        switch (decoration->kind)
        {
        case DUSK_IR_DECORATION_LOCATION:
            params[1] = SpvDecorationLocation;
            break;
        case DUSK_IR_DECORATION_BUILTIN:
            params[1] = SpvDecorationBuiltIn;
            break;
        case DUSK_IR_DECORATION_SET:
            params[1] = SpvDecorationDescriptorSet;
            break;
        case DUSK_IR_DECORATION_BINDING:
            params[1] = SpvDecorationBinding;
            break;
        case DUSK_IR_DECORATION_BLOCK: params[1] = SpvDecorationBlock; break;

        case DUSK_IR_DECORATION_OFFSET: continue;
        }

        memcpy(
            &params[2],
            decoration->literals,
            sizeof(uint32_t) * duskArrayLength(decoration->literals));

        duskEncodeInst(module, SpvOpDecorate, params, param_count);
    }
}

static void duskEmitMemberDecorations(
    DuskIRModule *module,
    uint32_t id,
    uint32_t member_index,
    DuskArray(DuskIRDecoration) decorations)
{
    if (decorations == NULL) return;
    if (id == 0) return;

    for (size_t i = 0; i < duskArrayLength(decorations); ++i)
    {
        DuskIRDecoration *decoration = &decorations[i];
        DUSK_ASSERT(decoration->literals != NULL);

        size_t param_count = 3 + duskArrayLength(decoration->literals);
        uint32_t *params = duskAllocateZeroed(
            module->allocator, param_count * sizeof(uint32_t));
        params[0] = id;
        params[1] = member_index;

        switch (decoration->kind)
        {
        case DUSK_IR_DECORATION_OFFSET: params[2] = SpvDecorationOffset; break;
        case DUSK_IR_DECORATION_LOCATION:
        case DUSK_IR_DECORATION_BUILTIN:
        case DUSK_IR_DECORATION_SET:
        case DUSK_IR_DECORATION_BINDING:
        case DUSK_IR_DECORATION_BLOCK: continue;
        }

        memcpy(
            &params[3],
            decoration->literals,
            sizeof(uint32_t) * duskArrayLength(decoration->literals));

        duskEncodeInst(module, SpvOpMemberDecorate, params, param_count);
    }
}

static void duskEmitValue(DuskIRModule *module, DuskIRValue *value)
{
    if (value->emitted) return;
    value->emitted = true;

    DuskAllocator *allocator = module->allocator;

    switch (value->kind)
    {
    case DUSK_IR_VALUE_VARIABLE: {
        SpvStorageClass storage_class = 0;
        switch (value->var.storage_class)
        {
        case DUSK_STORAGE_CLASS_FUNCTION:
            storage_class = SpvStorageClassFunction;
            break;
        case DUSK_STORAGE_CLASS_INPUT:
            storage_class = SpvStorageClassInput;
            break;
        case DUSK_STORAGE_CLASS_OUTPUT:
            storage_class = SpvStorageClassOutput;
            break;
        case DUSK_STORAGE_CLASS_UNIFORM:
            storage_class = SpvStorageClassUniform;
            break;
        case DUSK_STORAGE_CLASS_STORAGE:
            storage_class = SpvStorageClassStorageBuffer;
            break;
        case DUSK_STORAGE_CLASS_UNIFORM_CONSTANT:
            storage_class = SpvStorageClassUniformConstant;
            break;
        case DUSK_STORAGE_CLASS_PUSH_CONSTANT:
            storage_class = SpvStorageClassPushConstant;
            break;
        case DUSK_STORAGE_CLASS_PARAMETER: DUSK_ASSERT(0); break;
        }

        DUSK_ASSERT(value->id != 0);
        DUSK_ASSERT(value->type->id != 0);

        uint32_t params[3] = {
            value->type->id,
            value->id,
            (uint32_t)storage_class,
        };

        duskEncodeInst(
            module, SpvOpVariable, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_IR_VALUE_RETURN: {
        if (value->return_.value)
        {
            duskEmitValue(module, value->return_.value);
            uint32_t params[1] = {value->return_.value->id};
            duskEncodeInst(
                module, SpvOpReturnValue, params, DUSK_CARRAY_LENGTH(params));
        }
        else
        {
            duskEncodeInst(module, SpvOpReturn, NULL, 0);
        }
        break;
    }
    case DUSK_IR_VALUE_DISCARD: {
        duskEncodeInst(module, SpvOpKill, NULL, 0);
        break;
    }
    case DUSK_IR_VALUE_CONSTANT: {
        DUSK_ASSERT(
            value->type->kind == DUSK_TYPE_INT ||
            value->type->kind == DUSK_TYPE_FLOAT);

        duskEmitType(module, value->type);

        size_t param_count = 2 + value->constant.value_word_count;
        uint32_t *params = DUSK_NEW_ARRAY(allocator, uint32_t, param_count);
        params[0] = value->type->id;
        params[1] = value->id;
        memcpy(
            &params[2],
            value->constant.value_words,
            sizeof(uint32_t) * value->constant.value_word_count);
        duskEncodeInst(module, SpvOpConstant, params, param_count);
        break;
    }
    case DUSK_IR_VALUE_CONSTANT_BOOL: {
        DUSK_ASSERT(value->type->kind == DUSK_TYPE_BOOL);

        duskEmitType(module, value->type);

        uint32_t params[2] = {value->type->id, value->id};
        duskEncodeInst(
            module,
            value->const_bool.value ? SpvOpConstantTrue : SpvOpConstantFalse,
            params,
            DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_IR_VALUE_CONSTANT_COMPOSITE: {
        duskEmitType(module, value->type);

        size_t literal_count =
            duskArrayLength(value->constant_composite.values);
        size_t param_count = 2 + literal_count;
        uint32_t *params =
            duskAllocate(allocator, sizeof(uint32_t) * param_count);
        params[0] = value->type->id;
        params[1] = value->id;
        for (size_t i = 0; i < literal_count; ++i)
        {
            params[2 + i] = value->constant_composite.values[i]->id;
            DUSK_ASSERT(params[2 + i] > 0);
        }

        duskEncodeInst(module, SpvOpConstantComposite, params, param_count);
        break;
    }
    case DUSK_IR_VALUE_FUNCTION: {
        {
            DUSK_ASSERT(value->type->function.return_type->id > 0);
            DUSK_ASSERT(value->id > 0);
            DUSK_ASSERT(value->type->id > 0);

            uint32_t params[4] = {
                value->type->function.return_type->id,
                value->id,
                SpvFunctionControlMaskNone,
                value->type->id,
            };
            duskEncodeInst(
                module, SpvOpFunction, params, DUSK_CARRAY_LENGTH(params));
        }

        for (size_t i = 0; i < duskArrayLength(value->function.params); ++i)
        {
            DuskIRValue *func_param = value->function.params[i];
            duskEmitValue(module, func_param);
        }

        for (size_t i = 0; i < duskArrayLength(value->function.blocks); ++i)
        {
            DuskIRValue *block = value->function.blocks[i];

            uint32_t params[1] = {block->id};
            duskEncodeInst(
                module, SpvOpLabel, params, DUSK_CARRAY_LENGTH(params));

            if (i == 0)
            {
                for (size_t j = 0;
                     j < duskArrayLength(value->function.variables);
                     ++j)
                {
                    DuskIRValue *variable = value->function.variables[j];
                    duskEmitValue(module, variable);
                }
            }

            duskEmitValue(module, block);
        }

        duskEncodeInst(module, SpvOpFunctionEnd, NULL, 0);
        break;
    }
    case DUSK_IR_VALUE_BLOCK: {
        for (size_t i = 0; i < duskArrayLength(value->block.insts); ++i)
        {
            DuskIRValue *inst = value->block.insts[i];
            duskEmitValue(module, inst);
        }
        break;
    }
    case DUSK_IR_VALUE_STORE: {
        DUSK_ASSERT(value->store.pointer->id > 0);
        DUSK_ASSERT(value->store.value->id > 0);

        uint32_t params[2] = {
            value->store.pointer->id,
            value->store.value->id,
        };

        duskEncodeInst(module, SpvOpStore, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_IR_VALUE_LOAD: {
        DUSK_ASSERT(value->load.pointer->id > 0);
        DUSK_ASSERT(value->id > 0);

        uint32_t params[3] = {
            value->type->id,
            value->id,
            value->load.pointer->id,
        };

        duskEncodeInst(module, SpvOpLoad, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_IR_VALUE_FUNCTION_CALL: {
        size_t func_param_count = duskArrayLength(value->function_call.params);
        size_t param_count = 3 + func_param_count;
        uint32_t *params =
            duskAllocate(allocator, sizeof(uint32_t) * param_count);
        params[0] = value->type->id;
        params[1] = value->id;
        params[2] = value->function_call.function->id;
        for (size_t i = 0; i < func_param_count; ++i)
        {
            params[3 + i] = value->function_call.params[i]->id;
        }

        duskEncodeInst(module, SpvOpFunctionCall, params, param_count);
        break;
    }
    case DUSK_IR_VALUE_ACCESS_CHAIN: {
        DUSK_ASSERT(value->type->id > 0);
        DUSK_ASSERT(value->id > 0);
        DUSK_ASSERT(value->access_chain.base->id > 0);

        size_t literal_count = duskArrayLength(value->access_chain.indices);
        size_t param_count = 3 + literal_count;
        uint32_t *params =
            duskAllocate(allocator, sizeof(uint32_t) * param_count);
        params[0] = value->type->id;
        params[1] = value->id;
        params[2] = value->access_chain.base->id;
        for (size_t i = 0; i < literal_count; ++i)
        {
            DUSK_ASSERT(value->access_chain.indices[i]->id > 0);
            params[3 + i] = value->access_chain.indices[i]->id;
        }

        duskEncodeInst(module, SpvOpAccessChain, params, param_count);
        break;
    }
    case DUSK_IR_VALUE_COMPOSITE_EXTRACT: {
        DUSK_ASSERT(value->type->id > 0);
        DUSK_ASSERT(value->id > 0);
        DUSK_ASSERT(value->composite_extract.composite->id > 0);

        size_t literal_count =
            duskArrayLength(value->composite_extract.indices);
        size_t param_count = 3 + literal_count;
        uint32_t *params =
            duskAllocate(allocator, sizeof(uint32_t) * param_count);
        params[0] = value->type->id;
        params[1] = value->id;
        params[2] = value->composite_extract.composite->id;
        for (size_t i = 0; i < literal_count; ++i)
        {
            params[3 + i] = value->composite_extract.indices[i];
        }

        duskEncodeInst(module, SpvOpCompositeExtract, params, param_count);
        break;
    }
    case DUSK_IR_VALUE_VECTOR_SHUFFLE: {
        DUSK_ASSERT(value->type->id > 0);
        DUSK_ASSERT(value->id > 0);
        DUSK_ASSERT(value->vector_shuffle.vec1->id > 0);
        DUSK_ASSERT(value->vector_shuffle.vec2->id > 0);

        size_t literal_count = duskArrayLength(value->vector_shuffle.indices);
        size_t param_count = 4 + literal_count;
        uint32_t *params =
            duskAllocate(allocator, sizeof(uint32_t) * param_count);
        params[0] = value->type->id;
        params[1] = value->id;
        params[2] = value->vector_shuffle.vec1->id;
        params[3] = value->vector_shuffle.vec2->id;
        for (size_t i = 0; i < literal_count; ++i)
        {
            params[4 + i] = value->vector_shuffle.indices[i];
        }

        duskEncodeInst(module, SpvOpVectorShuffle, params, param_count);
        break;
    }
    case DUSK_IR_VALUE_COMPOSITE_CONSTRUCT: {
        DUSK_ASSERT(value->type->id > 0);
        DUSK_ASSERT(value->id > 0);

        size_t literal_count =
            duskArrayLength(value->composite_construct.values);
        size_t param_count = 2 + literal_count;
        uint32_t *params =
            duskAllocate(allocator, sizeof(uint32_t) * param_count);
        params[0] = value->type->id;
        params[1] = value->id;
        for (size_t i = 0; i < literal_count; ++i)
        {
            params[2 + i] = value->composite_construct.values[i]->id;
            DUSK_ASSERT(params[2 + i] > 0);
        }

        duskEncodeInst(module, SpvOpCompositeConstruct, params, param_count);
        break;
    }
    case DUSK_IR_VALUE_FUNCTION_PARAMETER: {
        DUSK_ASSERT(value->id > 0);

        uint32_t params[2] = {
            value->type->id,
            value->id,
        };
        duskEncodeInst(
            module, SpvOpFunctionParameter, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    }
}

DuskArray(uint32_t)
    duskIRModuleEmit(DuskCompiler *compiler, DuskIRModule *module)
{
    DuskAllocator *allocator = module->allocator;

    for (size_t i = 0; i < duskArrayLength(compiler->types); ++i)
    {
        DuskType *type = compiler->types[i];
        switch (type->kind)
        {
        case DUSK_TYPE_ARRAY: {
            DuskType *uint_type =
                duskTypeNewScalar(compiler, DUSK_SCALAR_TYPE_UINT);
            uint_type->emit = true;
            type->array.size_ir_value =
                duskIRConstIntCreate(module, uint_type, type->array.size);
            break;
        }
        default: break;
        }
    }

    for (size_t i = 0; i < duskArrayLength(compiler->types); ++i)
    {
        DuskType *type = compiler->types[i];
        if (type->emit)
        {
            type->id = duskReserveId(module);
        }
    }

    for (size_t i = 0; i < duskArrayLength(module->consts); ++i)
    {
        DuskIRValue *value = module->consts[i];
        value->id = duskReserveId(module);
    }

    for (size_t i = 0; i < duskArrayLength(module->globals); ++i)
    {
        DuskIRValue *value = module->globals[i];
        value->id = duskReserveId(module);
    }

    for (size_t i = 0; i < duskArrayLength(module->functions); ++i)
    {
        DuskIRValue *function = module->functions[i];
        function->id = duskReserveId(module);

        for (size_t j = 0; j < duskArrayLength(function->function.params); ++j)
        {
            DuskIRValue *param = function->function.params[j];
            param->id = duskReserveId(module);
        }

        for (size_t j = 0; j < duskArrayLength(function->function.blocks); ++j)
        {
            DuskIRValue *block = function->function.blocks[j];
            block->id = duskReserveId(module);

            if (j == 0)
            {
                for (size_t k = 0;
                     k < duskArrayLength(function->function.variables);
                     ++k)
                {

                    DuskIRValue *variable = function->function.variables[k];
                    variable->id = duskReserveId(module);
                }
            }

            for (size_t k = 0; k < duskArrayLength(block->block.insts); ++k)
            {

                DuskIRValue *inst = block->block.insts[k];
                inst->id = duskReserveId(module);
            }
        }
    }

    duskArrayPush(&module->stream, SpvMagicNumber);
    duskArrayPush(&module->stream, SpvVersion);
    duskArrayPush(&module->stream, 28); // Khronos compiler ID
    duskArrayPush(&module->stream, 0);  // ID Bound (fill out later)
    duskArrayPush(&module->stream, 0);

    {
        uint32_t params[1] = {SpvCapabilityShader};
        duskEncodeInst(
            module, SpvOpCapability, params, DUSK_CARRAY_LENGTH(params));
    }

    {
        uint32_t params[5];
        memset(params, 0, sizeof(params));

        params[0] = module->glsl_ext_inst_id;

        char *str = "GLSL.std.450";
        memcpy(&params[1], str, 12);

        duskEncodeInst(
            module, SpvOpExtInstImport, params, DUSK_CARRAY_LENGTH(params));
    }

    {
        uint32_t params[2] = {SpvAddressingModelLogical, SpvMemoryModelGLSL450};
        duskEncodeInst(
            module, SpvOpMemoryModel, params, DUSK_CARRAY_LENGTH(params));
    }

    for (size_t i = 0; i < duskArrayLength(module->entry_points); ++i)
    {
        DuskIREntryPoint *entry_point = module->entry_points[i];
        size_t entry_point_name_len = strlen(entry_point->name);

        size_t name_word_count = DUSK_ROUND_UP(4, entry_point_name_len + 1) / 4;

        size_t param_count = 2 + name_word_count +
                             duskArrayLength(entry_point->referenced_globals);
        uint32_t *params = DUSK_NEW_ARRAY(allocator, uint32_t, param_count);

        switch (entry_point->stage)
        {
        case DUSK_SHADER_STAGE_FRAGMENT:
            params[0] = SpvExecutionModelFragment;
            break;
        case DUSK_SHADER_STAGE_VERTEX:
            params[0] = SpvExecutionModelVertex;
            break;
        case DUSK_SHADER_STAGE_COMPUTE:
            params[0] = SpvExecutionModelGLCompute;
            break;
        }

        params[1] = entry_point->function->id;

        memcpy(&params[2], entry_point->name, entry_point_name_len + 1);

        for (size_t i = 0; i < duskArrayLength(entry_point->referenced_globals);
             ++i)
        {
            params[2 + name_word_count + i] =
                entry_point->referenced_globals[i]->id;
        }

        duskEncodeInst(module, SpvOpEntryPoint, params, param_count);
    }

    for (size_t i = 0; i < duskArrayLength(module->entry_points); ++i)
    {
        DuskIREntryPoint *entry_point = module->entry_points[i];
        switch (entry_point->stage)
        {
        case DUSK_SHADER_STAGE_FRAGMENT: {
            uint32_t params[2] = {
                entry_point->function->id,
                SpvExecutionModeOriginUpperLeft,
            };

            duskEncodeInst(
                module, SpvOpExecutionMode, params, DUSK_CARRAY_LENGTH(params));
            break;
        }
        case DUSK_SHADER_STAGE_VERTEX: break;
        case DUSK_SHADER_STAGE_COMPUTE: break;
        }
    }

    {
        uint32_t params[2] = {SpvSourceLanguageGLSL, 450};
        duskEncodeInst(module, SpvOpSource, params, DUSK_CARRAY_LENGTH(params));
    }

    // TODO: generate names here

    for (size_t i = 0; i < duskArrayLength(compiler->types); ++i)
    {
        DuskType *type = compiler->types[i];
        if (!type->emit) continue;

        duskEmitDecorations(module, type->id, type->decorations);

        if (type->kind == DUSK_TYPE_STRUCT)
        {
            for (uint32_t j = 0;
                 j < duskArrayLength(type->struct_.field_decorations);
                 ++j)
            {
                duskEmitMemberDecorations(
                    module, type->id, j, type->struct_.field_decorations[j]);
            }
        }
    }

    for (size_t i = 0; i < duskArrayLength(module->globals); ++i)
    {
        DuskIRValue *value = module->globals[i];
        duskEmitDecorations(module, value->id, value->decorations);
    }

    for (size_t i = 0; i < duskArrayLength(compiler->types); ++i)
    {
        DuskType *type = compiler->types[i];
        duskEmitType(module, type);
    }

    for (size_t i = 0; i < duskArrayLength(module->consts); ++i)
    {
        DuskIRValue *value = module->consts[i];
        duskEmitValue(module, value);
    }

    for (size_t i = 0; i < duskArrayLength(module->globals); ++i)
    {
        DuskIRValue *value = module->globals[i];
        duskEmitValue(module, value);
    }

    for (size_t i = 0; i < duskArrayLength(module->functions); ++i)
    {
        DuskIRValue *function = module->functions[i];
        duskEmitValue(module, function);
    }

    // Fill out ID bound
    module->stream[3] = module->last_id + 1;

    return module->stream;
}
