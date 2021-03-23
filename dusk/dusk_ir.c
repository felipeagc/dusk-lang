#include "dusk_internal.h"
#include "spirv.h"

static void duskEmitType(DuskIRModule *module, DuskType *type);
static void duskEmitValue(DuskIRModule *module, DuskIRValue *value);

static const char *duskIRConstToString(DuskAllocator *allocator, DuskIRValue *value)
{
    if (value->const_string) return value->const_string;

    switch (value->kind)
    {
    case DUSK_IR_VALUE_CONSTANT_BOOL: {
        value->const_string = (value->const_bool.value ? "@bool_true" : "@bool_false");
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
                        allocator, "@i8(%c)", *(int8_t *)value->constant.value_words);
                    break;
                case 16:
                    value->const_string = duskSprintf(
                        allocator, "@i16(%hd)", *(int16_t *)value->constant.value_words);
                    break;
                case 32:
                    value->const_string = duskSprintf(
                        allocator, "@i32(%d)", *(int32_t *)value->constant.value_words);
                    break;
                case 64:
                    value->const_string = duskSprintf(
                        allocator, "@i64(%ld)", *(int64_t *)value->constant.value_words);
                    break;
                }
            }
            else
            {
                switch (value->type->int_.bits)
                {
                case 8:
                    value->const_string = duskSprintf(
                        allocator, "@u8(%uc)", *(uint8_t *)value->constant.value_words);
                    break;
                case 16:
                    value->const_string = duskSprintf(
                        allocator, "@u16(%hu)", *(uint16_t *)value->constant.value_words);
                    break;
                case 32:
                    value->const_string = duskSprintf(
                        allocator, "@u32(%u)", *(uint32_t *)value->constant.value_words);
                    break;
                case 64:
                    value->const_string = duskSprintf(
                        allocator, "@u64(%lu)", *(uint64_t *)value->constant.value_words);
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
                    allocator, "@f32(%f)", *(float *)value->constant.value_words);
                break;
            case 64:
                value->const_string = duskSprintf(
                    allocator, "@f64(%lf)", *(double *)value->constant.value_words);
                break;
            }
            break;
        }
        default: DUSK_ASSERT(0); break;
        }
        break;
    }
    default: DUSK_ASSERT(0); break;
    }

    return value->const_string;
}

static DuskIRValue *duskIRGetCachedConst(DuskIRModule *module, DuskIRValue *value)
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

static void
duskEncodeInst(DuskIRModule *m, SpvOp opcode, uint32_t *params, size_t params_count)
{
    uint32_t opcode_word = opcode;
    opcode_word |= ((uint16_t)(params_count + 1)) << 16;

    duskArrayPush(&m->stream, opcode_word);
    for (uint32_t i = 0; i < params_count; ++i)
    {
        duskArrayPush(&m->stream, params[i]);
    }
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

    module->entry_points = duskArrayCreate(allocator, DuskIREntryPoint);
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

DuskIRValue *duskIRFunctionCreate(DuskIRModule *module, DuskType *type, const char *name)
{
    DuskIRValue *value = DUSK_NEW(module->allocator, DuskIRValue);
    value->kind = DUSK_IR_VALUE_FUNCTION;
    value->type = type;
    value->function.name = name;
    value->function.blocks = duskArrayCreate(module->allocator, DuskIRValue *);

    DuskIRValue *first_block = duskIRBlockCreate(module);
    duskIRFunctionAddBlock(value, first_block);

    return value;
}

void duskIRFunctionAddBlock(DuskIRValue *function, DuskIRValue *block)
{
    DUSK_ASSERT(function->kind == DUSK_IR_VALUE_FUNCTION);
    DUSK_ASSERT(block->kind == DUSK_IR_VALUE_BLOCK);
    duskArrayPush(&function->function.blocks, block);
}

DuskIRValue *
duskIRVariableCreate(DuskIRModule *module, DuskType *type, DuskStorageClass storage_class)
{
    DuskIRValue *value = DUSK_NEW(module->allocator, DuskIRValue);
    value->kind = DUSK_IR_VALUE_VARIABLE;
    value->type = duskTypeNewPointer(module->compiler, type, storage_class);
    value->var.storage_class = storage_class;

    return value;
}

void duskIRModuleAddEntryPoint(
    DuskIRModule *module, DuskIRValue *function, const char *name, DuskShaderStage stage)
{
    DuskIREntryPoint entry_point = {
        .function = function,
        .name = name,
        .stage = stage,
    };
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
        value->constant.value_words = DUSK_NEW_ARRAY(module->allocator, uint32_t, 1);
        uint8_t val = (uint8_t)int_value;
        memcpy(value->constant.value_words, &val, sizeof(val));
        break;
    }
    case 16: {
        value->constant.value_word_count = 1;
        value->constant.value_words = DUSK_NEW_ARRAY(module->allocator, uint32_t, 1);
        uint16_t val = (uint16_t)int_value;
        memcpy(value->constant.value_words, &val, sizeof(val));
        break;
    }
    case 32: {
        value->constant.value_word_count = 1;
        value->constant.value_words = DUSK_NEW_ARRAY(module->allocator, uint32_t, 1);
        uint32_t val = (uint32_t)int_value;
        memcpy(value->constant.value_words, &val, sizeof(val));
        break;
    }
    case 64: {
        value->constant.value_word_count = 2;
        value->constant.value_words = DUSK_NEW_ARRAY(module->allocator, uint32_t, 2);
        memcpy(value->constant.value_words, &int_value, sizeof(uint64_t));
        break;
    }
    default: DUSK_ASSERT(0); break;
    }

    return duskIRGetCachedConst(module, value);
}

DuskIRValue *
duskIRConstFloatCreate(DuskIRModule *module, DuskType *type, double double_value)
{
    DuskIRValue *value = DUSK_NEW(module->allocator, DuskIRValue);
    value->type = type;
    value->kind = DUSK_IR_VALUE_CONSTANT;

    switch (type->float_.bits)
    {
    case 32: {
        value->constant.value_word_count = 1;
        value->constant.value_words = DUSK_NEW_ARRAY(module->allocator, uint32_t, 1);
        float val = (float)double_value;
        memcpy(value->constant.value_words, &val, sizeof(val));
        break;
    }
    case 64: {
        value->constant.value_word_count = 2;
        value->constant.value_words = DUSK_NEW_ARRAY(module->allocator, uint32_t, 2);
        memcpy(value->constant.value_words, &double_value, sizeof(uint64_t));
        break;
    }
    default: DUSK_ASSERT(0); break;
    }

    return duskIRGetCachedConst(module, value);
}

DuskIRValue *duskIRCreateReturn(DuskIRModule *module, DuskIRValue *value)
{
    DuskIRValue *inst = DUSK_NEW(module->allocator, DuskIRValue);
    inst->type = duskTypeNewBasic(module->compiler, DUSK_TYPE_VOID);
    inst->kind = DUSK_IR_VALUE_RETURN;
    inst->return_.value = value;
    return inst;
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
    case DUSK_TYPE_INT: {
        uint32_t params[3] = {type->id, type->int_.bits, (uint32_t)type->int_.is_signed};
        duskEncodeInst(module, SpvOpTypeInt, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_TYPE_FLOAT: {
        uint32_t params[2] = {type->id, type->float_.bits};
        duskEncodeInst(module, SpvOpTypeFloat, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_TYPE_POINTER: {
        duskEmitType(module, type->pointer.sub);

        SpvStorageClass storage_class = 0;

        switch (type->pointer.storage_class)
        {
        case DUSK_STORAGE_CLASS_FUNCTION: storage_class = SpvStorageClassFunction; break;
        case DUSK_STORAGE_CLASS_INPUT: storage_class = SpvStorageClassInput; break;
        case DUSK_STORAGE_CLASS_OUTPUT: storage_class = SpvStorageClassOutput; break;
        case DUSK_STORAGE_CLASS_UNIFORM: storage_class = SpvStorageClassUniform; break;
        case DUSK_STORAGE_CLASS_UNIFORM_CONSTANT:
            storage_class = SpvStorageClassUniformConstant;
            break;
        case DUSK_STORAGE_CLASS_PUSH_CONSTANT:
            storage_class = SpvStorageClassPushConstant;
            break;
        case DUSK_STORAGE_CLASS_PARAMETER: DUSK_ASSERT(0); break;
        }

        uint32_t params[3] = {type->id, storage_class, type->pointer.sub->id};
        duskEncodeInst(module, SpvOpTypePointer, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_TYPE_RUNTIME_ARRAY: {
        duskEmitType(module, type->array.sub);

        uint32_t params[2] = {type->id, type->array.sub->id};
        duskEncodeInst(module, SpvOpTypeRuntimeArray, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_TYPE_ARRAY: {
        DUSK_ASSERT(type->array.size_ir_value);

        duskEmitType(module, type->array.sub);
        duskEmitValue(module, type->array.size_ir_value);

        uint32_t params[3] = {
            type->id, type->array.sub->id, type->array.size_ir_value->id};
        duskEncodeInst(module, SpvOpTypeArray, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_TYPE_VECTOR: {
        duskEmitType(module, type->vector.sub);

        uint32_t params[3] = {type->id, type->vector.sub->id, type->vector.size};
        duskEncodeInst(module, SpvOpTypeVector, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_TYPE_MATRIX: {
        duskEmitType(module, type->matrix.col_type);

        uint32_t params[3] = {type->id, type->matrix.col_type->id, type->matrix.cols};
        duskEncodeInst(module, SpvOpTypeMatrix, params, DUSK_CARRAY_LENGTH(params));
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
        duskEncodeInst(module, SpvOpTypeImage, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_TYPE_SAMPLED_IMAGE: {
        duskEmitType(module, type->sampled_image.image_type);

        uint32_t params[] = {
            type->id,
            type->sampled_image.image_type->id,
        };
        duskEncodeInst(module, SpvOpTypeSampledImage, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_TYPE_UNTYPED_INT:
    case DUSK_TYPE_UNTYPED_FLOAT:
    case DUSK_TYPE_TYPE: break;
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
        break;
    }
    case DUSK_IR_VALUE_RETURN: {
        if (value->return_.value)
        {
            duskEmitValue(module, value->return_.value);
            uint32_t params[1] = {value->return_.value->id};
            duskEncodeInst(module, SpvOpReturnValue, params, DUSK_CARRAY_LENGTH(params));
        }
        else
        {
            duskEncodeInst(module, SpvOpReturn, NULL, 0);
        }
        break;
    }
    case DUSK_IR_VALUE_CONSTANT: {
        DUSK_ASSERT(
            value->type->kind == DUSK_TYPE_INT || value->type->kind == DUSK_TYPE_FLOAT);

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
    case DUSK_IR_VALUE_FUNCTION: {
        {
            uint32_t params[4] = {
                value->type->function.return_type->id,
                value->id,
                SpvFunctionControlMaskNone,
                value->type->id,
            };
            duskEncodeInst(module, SpvOpFunction, params, DUSK_CARRAY_LENGTH(params));
        }

        for (size_t i = 0; i < duskArrayLength(value->function.params); ++i)
        {
            DuskIRValue *func_param = value->function.params[i];
            uint32_t params[2] = {
                func_param->type->id,
                func_param->id,
            };
            duskEncodeInst(
                module, SpvOpFunctionParameter, params, DUSK_CARRAY_LENGTH(params));
        }

        for (size_t i = 0; i < duskArrayLength(value->function.blocks); ++i)
        {
            DuskIRValue *block = value->function.blocks[i];
            duskEmitValue(module, block);
        }

        duskEncodeInst(module, SpvOpFunctionEnd, NULL, 0);
        break;
    }
    case DUSK_IR_VALUE_BLOCK: {
        uint32_t params[1] = {value->id};
        duskEncodeInst(module, SpvOpLabel, params, DUSK_CARRAY_LENGTH(params));

        for (size_t i = 0; i < duskArrayLength(value->block.insts); ++i)
        {
            DuskIRValue *inst = value->block.insts[i];
            duskEmitValue(module, inst);
        }
        break;
    }
    }
}

DuskArray(uint32_t) duskIRModuleEmit(DuskCompiler *compiler, DuskIRModule *module)
{
    DuskAllocator *allocator = module->allocator;

    for (size_t i = 0; i < duskArrayLength(compiler->types); ++i)
    {
        DuskType *type = compiler->types[i];
        switch (type->kind)
        {
        case DUSK_TYPE_ARRAY: {
            DuskType *uint_type = duskTypeNewScalar(compiler, DUSK_SCALAR_TYPE_UINT);
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

    for (size_t i = 0; i < duskArrayLength(module->functions); ++i)
    {
        DuskIRValue *function = module->functions[i];
        function->id = duskReserveId(module);

        for (size_t j = 0; j < duskArrayLength(function->function.blocks); ++j)
        {
            DuskIRValue *block = function->function.blocks[j];
            block->id = duskReserveId(module);

            for (size_t k = 0; k < duskArrayLength(block->block.insts); ++k)
            {
                DuskIRValue *inst = block->block.insts[k];
                inst->id = duskReserveId(module);
            }
        }
    }

    static const uint8_t MAGIC_NUMBER[4] = {'D', 'U', 'S', 'K'};
    uint32_t uint_magic_number;
    memcpy(&uint_magic_number, MAGIC_NUMBER, sizeof(uint32_t));

    duskArrayPush(&module->stream, SpvMagicNumber);
    duskArrayPush(&module->stream, SpvVersion);
    duskArrayPush(&module->stream, uint_magic_number);
    duskArrayPush(&module->stream, 0); // ID Bound (fill out later)
    duskArrayPush(&module->stream, 0);

    {
        uint32_t params[1] = {SpvCapabilityShader};
        duskEncodeInst(module, SpvOpCapability, params, DUSK_CARRAY_LENGTH(params));
    }

    {
        uint32_t params[5];
        memset(params, 0, sizeof(params));

        params[0] = module->glsl_ext_inst_id;

        char *str = "GLSL.std.450";
        memcpy(&params[1], str, 12);

        duskEncodeInst(module, SpvOpExtInstImport, params, DUSK_CARRAY_LENGTH(params));
    }

    {
        uint32_t params[2] = {SpvAddressingModelLogical, SpvMemoryModelGLSL450};
        duskEncodeInst(module, SpvOpMemoryModel, params, DUSK_CARRAY_LENGTH(params));
    }

    for (size_t i = 0; i < duskArrayLength(module->entry_points); ++i)
    {
        DuskIREntryPoint *entry_point = &module->entry_points[i];
        size_t entry_point_name_len = strlen(entry_point->name);

        size_t name_word_count = DUSK_ROUND_TO_4(entry_point_name_len + 1) / 4;

        size_t param_count = 2 + name_word_count;
        uint32_t *params = DUSK_NEW_ARRAY(allocator, uint32_t, param_count);

        switch (entry_point->stage)
        {
        case DUSK_SHADER_STAGE_FRAGMENT: params[0] = SpvExecutionModelFragment; break;
        case DUSK_SHADER_STAGE_VERTEX: params[0] = SpvExecutionModelVertex; break;
        case DUSK_SHADER_STAGE_COMPUTE: params[0] = SpvExecutionModelGLCompute; break;
        }

        params[1] = entry_point->function->id;

        memcpy(&params[2], entry_point->name, entry_point_name_len + 1);

        duskEncodeInst(module, SpvOpEntryPoint, params, param_count);
    }

    for (size_t i = 0; i < duskArrayLength(module->entry_points); ++i)
    {
        DuskIREntryPoint *entry_point = &module->entry_points[i];
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

    for (size_t i = 0; i < duskArrayLength(module->functions); ++i)
    {
        DuskIRValue *function = module->functions[i];
        duskEmitValue(module, function);
    }

    // Fill out ID bound
    module->stream[3] = module->last_id + 1;

    return module->stream;
}
