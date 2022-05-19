#include "dusk_internal.h"
#include "spirv.h"
#include "GLSL.std.450.h"

static void duskEmitType(DuskIRModule *module, DuskType *type);
static void duskEmitValue(DuskIRModule *module, DuskIRValue *value);

static const char *
duskIRConstToString(DuskAllocator *allocator, DuskIRValue *value)
{
    if (value->const_string) return value->const_string;

    switch (value->kind) {
    case DUSK_IR_VALUE_CONSTANT_BOOL: {
        value->const_string =
            (value->const_bool.value ? "@bool_true" : "@bool_false");
        break;
    }
    case DUSK_IR_VALUE_CONSTANT: {
        switch (value->type->kind) {
        case DUSK_TYPE_INT: {
            if (value->type->int_.is_signed) {
                switch (value->type->int_.bits) {
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
            } else {
                switch (value->type->int_.bits) {
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
            switch (value->type->float_.bits) {
            case 32: {
                float val = 0.0f;
                memcpy(&val, value->constant.value_words, sizeof(float));
                value->const_string = duskSprintf(allocator, "@f32(%f)", val);
                break;
            }
            case 64: {
                double val = 0.0f;
                memcpy(&val, value->constant.value_words, sizeof(double));
                value->const_string = duskSprintf(allocator, "@f64(%lf)", val);
                break;
            }
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
        duskStringBuilderAppend(sb, "{");
        for (size_t i = 0;
             i < duskArrayLength(value->constant_composite.values_arr);
             ++i) {
            if (i != 0) duskStringBuilderAppend(sb, ",");
            DuskIRValue *elem_value = value->constant_composite.values_arr[i];
            const char *elem_str = duskIRConstToString(allocator, elem_value);
            duskStringBuilderAppend(sb, elem_str);
        }
        duskStringBuilderAppend(sb, "}");

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
    if (duskMapGet(module->const_cache, value_str, (void **)&existing_value)) {
        DUSK_ASSERT(existing_value != NULL);
        return existing_value;
    }

    duskMapSet(module->const_cache, value_str, value);
    duskArrayPush(&module->consts_arr, value);

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

    duskArrayPush(&m->stream_arr, opcode_word);
    for (uint32_t i = 0; i < params_count; ++i) {
        duskArrayPush(&m->stream_arr, params[i]);
    }
}

bool duskIRBlockIsTerminated(DuskIRValue *block)
{
    DUSK_ASSERT(block->kind == DUSK_IR_VALUE_BLOCK);

    size_t inst_count = duskArrayLength(block->block.insts_arr);
    if (inst_count == 0) return false;

    DuskIRValue *inst = block->block.insts_arr[inst_count - 1];
    switch (inst->kind) {
    case DUSK_IR_VALUE_RETURN:
    case DUSK_IR_VALUE_DISCARD:
    case DUSK_IR_VALUE_BRANCH:
    case DUSK_IR_VALUE_BRANCH_COND: {
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
    module->stream_arr = duskArrayCreate(allocator, uint32_t);
    module->extensions_arr = duskArrayCreate(allocator, const char *);
    module->capabilities_arr = duskArrayCreate(allocator, uint32_t);

    duskArrayPush(&module->capabilities_arr, SpvCapabilityShader);

    module->const_cache = duskMapCreate(allocator, 32);
    module->consts_arr = duskArrayCreate(allocator, DuskIRValue *);

    module->glsl_ext_inst_id = duskReserveId(module);

    module->entry_points_arr = duskArrayCreate(allocator, DuskIREntryPoint *);
    module->functions_arr = duskArrayCreate(allocator, DuskIRValue *);
    module->globals_arr = duskArrayCreate(allocator, DuskIRValue *);

    return module;
}

DuskIRValue *duskIRBlockCreate(DuskIRModule *module)
{
    DuskIRValue *value = DUSK_NEW(module->allocator, DuskIRValue);
    value->kind = DUSK_IR_VALUE_BLOCK;
    value->block.insts_arr = duskArrayCreate(module->allocator, DuskIRValue *);
    return value;
}

DuskIRValue *
duskIRFunctionCreate(DuskIRModule *module, DuskType *type, const char *name)
{
    DuskIRValue *value = DUSK_NEW(module->allocator, DuskIRValue);
    value->kind = DUSK_IR_VALUE_FUNCTION;
    value->type = type;
    value->function.name = name;
    value->function.blocks_arr =
        duskArrayCreate(module->allocator, DuskIRValue *);
    value->function.variables_arr =
        duskArrayCreate(module->allocator, DuskIRValue *);
    value->function.params_arr =
        duskArrayCreate(module->allocator, DuskIRValue *);
    for (size_t i = 0; i < type->function.param_type_count; ++i) {
        DuskIRValue *param_value = DUSK_NEW(module->allocator, DuskIRValue);
        param_value->kind = DUSK_IR_VALUE_FUNCTION_PARAMETER;
        param_value->type = type->function.param_types[i];
        duskArrayPush(&value->function.params_arr, param_value);
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
    duskArrayPush(&function->function.blocks_arr, block);
}

DuskIRValue *duskIRVariableCreate(
    DuskIRModule *module, DuskType *type, DuskStorageClass storage_class)
{
    DuskIRValue *value = DUSK_NEW(module->allocator, DuskIRValue);
    value->kind = DUSK_IR_VALUE_VARIABLE;
    value->type = duskTypeNewPointer(module->compiler, type, storage_class);
    value->var.storage_class = storage_class;

    duskTypeMarkNotDead(value->type);

    switch (storage_class) {
    case DUSK_STORAGE_CLASS_WORKGROUP:
    case DUSK_STORAGE_CLASS_STORAGE:
    case DUSK_STORAGE_CLASS_UNIFORM:
    case DUSK_STORAGE_CLASS_INPUT:
    case DUSK_STORAGE_CLASS_OUTPUT:
    case DUSK_STORAGE_CLASS_PUSH_CONSTANT:
    case DUSK_STORAGE_CLASS_UNIFORM_CONSTANT: {
        duskArrayPush(&module->globals_arr, value);
        break;
    }
    case DUSK_STORAGE_CLASS_PARAMETER:
    case DUSK_STORAGE_CLASS_FUNCTION: break;
    }

    return value;
}

DuskIREntryPoint *duskIRModuleAddEntryPoint(
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
    entry_point->referenced_globals_arr =
        duskArrayCreate(module->allocator, DuskIRValue *);
    duskArrayResize(
        &entry_point->referenced_globals_arr, referenced_global_count);
    memcpy(
        entry_point->referenced_globals_arr,
        referenced_globals,
        sizeof(DuskIRValue *) * referenced_global_count);

    duskArrayPush(&module->entry_points_arr, entry_point);

    return entry_point;
}

void duskIREntryPointReferenceGlobal(
    DuskIREntryPoint *entry_point, DuskIRValue *global)
{
    for (size_t i = 0; i < duskArrayLength(entry_point->referenced_globals_arr);
         ++i) {
        if (global == entry_point->referenced_globals_arr[i]) {
            // Already referenced
            return;
        }
    }

    duskArrayPush(&entry_point->referenced_globals_arr, global);
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

    switch (type->int_.bits) {
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

    switch (type->float_.bits) {
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

    value->constant_composite.values_arr =
        duskArrayCreate(module->allocator, DuskIRValue *);
    duskArrayResize(&value->constant_composite.values_arr, value_count);
    memcpy(
        value->constant_composite.values_arr,
        values,
        value_count * sizeof(DuskIRValue *));

    return duskIRGetCachedConst(module, value);
}

static void duskIRBlockAppendInst(DuskIRValue *block, DuskIRValue *inst)
{
    if (!duskIRBlockIsTerminated(block)) {
        duskArrayPush(&block->block.insts_arr, inst);
    }
}

DuskIRDecoration duskIRCreateDecoration(
    DuskAllocator *allocator,
    DuskIRDecorationKind kind,
    size_t literal_count,
    uint32_t *literals)
{
    DuskIRDecoration decoration = {0};
    decoration.kind = kind;
    decoration.literal_count = literal_count;
    decoration.literals = DUSK_NEW_ARRAY(allocator, uint32_t, literal_count);
    if (literal_count > 0) {
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

void duskIRCreateBranch(
    DuskIRModule *module, DuskIRValue *block, DuskIRValue *dest_block)
{
    DuskIRValue *inst = DUSK_NEW(module->allocator, DuskIRValue);
    inst->type = duskTypeNewBasic(module->compiler, DUSK_TYPE_VOID);
    inst->kind = DUSK_IR_VALUE_BRANCH;
    inst->branch.dest_block = dest_block;
    duskIRBlockAppendInst(block, inst);
}

void duskIRCreateBranchCond(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskIRValue *condition,
    DuskIRValue *true_block,
    DuskIRValue *false_block)
{
    DuskIRValue *inst = DUSK_NEW(module->allocator, DuskIRValue);
    inst->type = duskTypeNewBasic(module->compiler, DUSK_TYPE_VOID);
    inst->kind = DUSK_IR_VALUE_BRANCH_COND;
    inst->branch_cond.cond = condition;
    inst->branch_cond.true_block = true_block;
    inst->branch_cond.false_block = false_block;
    duskIRBlockAppendInst(block, inst);
}

void duskIRCreateSelectionMerge(
    DuskIRModule *module, DuskIRValue *block, DuskIRValue *merge_block)
{
    DuskIRValue *inst = DUSK_NEW(module->allocator, DuskIRValue);
    inst->type = duskTypeNewBasic(module->compiler, DUSK_TYPE_VOID);
    inst->kind = DUSK_IR_VALUE_SELECTION_MERGE;
    inst->selection_merge.merge_block = merge_block;
    duskIRBlockAppendInst(block, inst);
}

void duskIRCreateLoopMerge(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskIRValue *merge_block,
    DuskIRValue *continue_block)
{
    DuskIRValue *inst = DUSK_NEW(module->allocator, DuskIRValue);
    inst->type = duskTypeNewBasic(module->compiler, DUSK_TYPE_VOID);
    inst->kind = DUSK_IR_VALUE_LOOP_MERGE;
    inst->loop_merge.merge_block = merge_block;
    inst->loop_merge.continue_block = continue_block;
    duskIRBlockAppendInst(block, inst);
}

DuskIRValue *duskIRCreatePhi(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskType *type,
    size_t pair_count,
    const DuskIRPhiPair *pairs)
{
    DuskIRValue *inst = DUSK_NEW(module->allocator, DuskIRValue);
    inst->type = type;
    inst->kind = DUSK_IR_VALUE_PHI;

    inst->phi.pair_count = pair_count;
    inst->phi.pairs =
        DUSK_NEW_ARRAY(module->allocator, DuskIRPhiPair, pair_count);

    memcpy(inst->phi.pairs, pairs, sizeof(DuskIRPhiPair) * pair_count);

    duskIRBlockAppendInst(block, inst);
    return inst;
}

DuskIRValue *duskIRCreateArrayLength(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskIRValue *struct_ptr,
    uint32_t struct_member_index)
{
    DuskIRValue *inst = DUSK_NEW(module->allocator, DuskIRValue);
    inst->type = duskTypeNewScalar(module->compiler, DUSK_SCALAR_TYPE_UINT);
    inst->kind = DUSK_IR_VALUE_ARRAY_LENGTH;

    inst->array_length.struct_ptr = struct_ptr;
    inst->array_length.struct_member_index = struct_member_index;

    duskIRBlockAppendInst(block, inst);
    return inst;
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
    inst->function_call.params_arr =
        duskArrayCreate(module->allocator, DuskIRValue *);
    duskArrayResize(&inst->function_call.params_arr, param_count);

    for (size_t i = 0; i < param_count; ++i) {
        inst->function_call.params_arr[i] = params[i];
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
    DUSK_ASSERT(base->type->kind == DUSK_TYPE_POINTER);
    DuskStorageClass storage_class = base->type->pointer.storage_class;

    DuskIRValue *inst = DUSK_NEW(module->allocator, DuskIRValue);
    inst->kind = DUSK_IR_VALUE_ACCESS_CHAIN;
    inst->access_chain.base = base;
    inst->access_chain.indices_arr =
        duskArrayCreate(module->allocator, DuskIRValue *);
    duskArrayResize(&inst->access_chain.indices_arr, index_count);
    memcpy(
        inst->access_chain.indices_arr,
        indices,
        index_count * sizeof(DuskIRValue *));

    for (size_t i = 0; i < index_count; ++i) {
        duskTypeMarkNotDead(indices[i]->type);
    }

    inst->type =
        duskTypeNewPointer(module->compiler, accessed_type, storage_class);
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
    inst->composite_extract.indices_arr =
        duskArrayCreate(module->allocator, uint32_t);
    duskArrayResize(&inst->composite_extract.indices_arr, index_count);
    memcpy(
        inst->composite_extract.indices_arr,
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
    inst->vector_shuffle.indices_arr =
        duskArrayCreate(module->allocator, uint32_t);
    duskArrayResize(&inst->vector_shuffle.indices_arr, index_count);
    memcpy(
        inst->vector_shuffle.indices_arr,
        indices,
        index_count * sizeof(uint32_t));

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
    inst->composite_construct.values_arr =
        duskArrayCreate(module->allocator, DuskIRValue *);
    duskArrayResize(&inst->composite_construct.values_arr, value_count);
    memcpy(
        inst->composite_construct.values_arr,
        values,
        value_count * sizeof(DuskIRValue *));

    inst->type = composite_type;
    duskTypeMarkNotDead(inst->type);

    duskIRBlockAppendInst(block, inst);
    return inst;
}

DuskIRValue *duskIRCreateCast(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskType *destination_type,
    DuskIRValue *value)
{
    DuskIRValue *inst = DUSK_NEW(module->allocator, DuskIRValue);
    inst->kind = DUSK_IR_VALUE_CAST;
    inst->cast.value = value;

    inst->type = destination_type;
    duskTypeMarkNotDead(inst->type);

    duskIRBlockAppendInst(block, inst);
    return inst;
}

DuskIRValue *duskIRCreateBuiltinCall(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskBuiltinFunctionKind builtin_kind,
    DuskType *destination_type,
    size_t param_count,
    DuskIRValue **params)
{
    DuskIRValue *inst = DUSK_NEW(module->allocator, DuskIRValue);
    inst->kind = DUSK_IR_VALUE_BUILTIN_CALL;
    inst->builtin_call.builtin_kind = builtin_kind;
    inst->builtin_call.param_count = param_count;
    inst->builtin_call.params =
        DUSK_NEW_ARRAY(module->allocator, DuskIRValue *, param_count);

    memcpy(
        inst->builtin_call.params, params, sizeof(DuskIRValue *) * param_count);

    inst->type = destination_type;
    duskTypeMarkNotDead(inst->type);

    duskIRBlockAppendInst(block, inst);
    return inst;
}

DuskIRValue *duskIRCreateBinaryOperation(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskBinaryOp op,
    DuskType *destination_type,
    DuskIRValue *left,
    DuskIRValue *right)
{
    DuskIRValue *inst = DUSK_NEW(module->allocator, DuskIRValue);
    inst->kind = DUSK_IR_VALUE_BINARY_OPERATION;
    inst->binary.op = op;
    inst->binary.left = left;
    inst->binary.right = right;

    inst->type = destination_type;
    duskTypeMarkNotDead(inst->type);

    duskIRBlockAppendInst(block, inst);
    return inst;
}

DuskIRValue *duskIRCreateUnaryOperation(
    DuskIRModule *module,
    DuskIRValue *block,
    DuskUnaryOp op,
    DuskType *destination_type,
    DuskIRValue *right)
{
    DuskIRValue *inst = DUSK_NEW(module->allocator, DuskIRValue);
    inst->kind = DUSK_IR_VALUE_UNARY_OPERATION;
    inst->unary.op = op;
    inst->unary.right = right;

    inst->type = destination_type;
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
    if (duskIRIsLvalue(value)) {
        return duskIRCreateLoad(module, block, value);
    }

    return value;
}

static void duskEmitType(DuskIRModule *module, DuskType *type)
{
    if (!type->emit) return;

    type->emit = false;

    DuskAllocator *allocator = module->allocator;

    switch (type->kind) {
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

        switch (type->pointer.storage_class) {
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
        case DUSK_STORAGE_CLASS_WORKGROUP:
            storage_class = SpvStorageClassWorkgroup;
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
        size_t func_param_count = type->function.param_type_count;

        duskEmitType(module, type->function.return_type);
        for (size_t i = 0; i < func_param_count; ++i) {
            DuskType *param_type = type->function.param_types[i];
            duskEmitType(module, param_type);
        }

        uint32_t param_count = 2 + func_param_count;
        uint32_t *params = DUSK_NEW_ARRAY(allocator, uint32_t, param_count);

        params[0] = type->id;
        params[1] = type->function.return_type->id;

        for (size_t i = 0; i < func_param_count; ++i) {
            DuskType *param_type = type->function.param_types[i];
            params[2 + i] = param_type->id;
        }

        duskEncodeInst(module, SpvOpTypeFunction, params, param_count);
        break;
    }
    case DUSK_TYPE_STRUCT: {
        size_t field_count = type->struct_.field_count;

        for (size_t i = 0; i < field_count; ++i) {
            duskEmitType(module, type->struct_.field_types[i]);
        }

        uint32_t word_count = 1 + (uint32_t)field_count;
        uint32_t *params = DUSK_NEW_ARRAY(allocator, uint32_t, word_count);
        params[0] = type->id;

        for (size_t i = 0; i < field_count; ++i) {
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
        switch (type->image.dim) {
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
    DuskIRModule *module,
    uint32_t id,
    DuskArray(DuskIRDecoration) decorations_arr)
{
    if (decorations_arr == NULL) return;
    if (id == 0) return;

    for (size_t i = 0; i < duskArrayLength(decorations_arr); ++i) {
        DuskIRDecoration *decoration = &decorations_arr[i];
        DUSK_ASSERT(decoration->literals != NULL);

        size_t param_count = 2 + decoration->literal_count;
        uint32_t *params = duskAllocateZeroed(
            module->allocator, param_count * sizeof(uint32_t));
        params[0] = id;

        switch (decoration->kind) {
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
        case DUSK_IR_DECORATION_ARRAY_STRIDE:
            params[1] = SpvDecorationArrayStride;
            break;
        case DUSK_IR_DECORATION_NON_WRITABLE:
            params[1] = SpvDecorationNonWritable;
            break;

        case DUSK_IR_DECORATION_OFFSET: continue;
        }

        memcpy(
            &params[2],
            decoration->literals,
            sizeof(uint32_t) * decoration->literal_count);

        duskEncodeInst(module, SpvOpDecorate, params, param_count);
    }
}

static void duskEmitMemberDecorations(
    DuskIRModule *module,
    uint32_t id,
    uint32_t member_index,
    DuskArray(DuskIRDecoration) decorations_arr)
{
    if (decorations_arr == NULL) return;
    if (id == 0) return;

    for (size_t i = 0; i < duskArrayLength(decorations_arr); ++i) {
        DuskIRDecoration *decoration = &decorations_arr[i];
        DUSK_ASSERT(decoration->literals != NULL);

        size_t param_count = 3 + decoration->literal_count;
        uint32_t *params = duskAllocateZeroed(
            module->allocator, param_count * sizeof(uint32_t));
        params[0] = id;
        params[1] = member_index;

        switch (decoration->kind) {
        case DUSK_IR_DECORATION_OFFSET: params[2] = SpvDecorationOffset; break;
        case DUSK_IR_DECORATION_NON_WRITABLE:
        case DUSK_IR_DECORATION_ARRAY_STRIDE:
        case DUSK_IR_DECORATION_LOCATION:
        case DUSK_IR_DECORATION_BUILTIN:
        case DUSK_IR_DECORATION_SET:
        case DUSK_IR_DECORATION_BINDING:
        case DUSK_IR_DECORATION_BLOCK: continue;
        }

        memcpy(
            &params[3],
            decoration->literals,
            sizeof(uint32_t) * decoration->literal_count);

        duskEncodeInst(module, SpvOpMemberDecorate, params, param_count);
    }
}

static void duskEmitValue(DuskIRModule *module, DuskIRValue *value)
{
    if (value->emitted) return;
    value->emitted = true;

    DuskAllocator *allocator = module->allocator;

    switch (value->kind) {
    case DUSK_IR_VALUE_VARIABLE: {
        SpvStorageClass storage_class = 0;
        switch (value->var.storage_class) {
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
        case DUSK_STORAGE_CLASS_WORKGROUP:
            storage_class = SpvStorageClassWorkgroup;
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
        if (value->return_.value) {
            duskEmitValue(module, value->return_.value);
            uint32_t params[1] = {value->return_.value->id};
            duskEncodeInst(
                module, SpvOpReturnValue, params, DUSK_CARRAY_LENGTH(params));
        } else {
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
            duskArrayLength(value->constant_composite.values_arr);
        size_t param_count = 2 + literal_count;
        uint32_t *params =
            duskAllocate(allocator, sizeof(uint32_t) * param_count);
        params[0] = value->type->id;
        params[1] = value->id;
        for (size_t i = 0; i < literal_count; ++i) {
            params[2 + i] = value->constant_composite.values_arr[i]->id;
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

        for (size_t i = 0; i < duskArrayLength(value->function.params_arr);
             ++i) {
            DuskIRValue *func_param = value->function.params_arr[i];
            duskEmitValue(module, func_param);
        }

        for (size_t i = 0; i < duskArrayLength(value->function.blocks_arr);
             ++i) {
            DuskIRValue *block = value->function.blocks_arr[i];

            uint32_t params[1] = {block->id};
            duskEncodeInst(
                module, SpvOpLabel, params, DUSK_CARRAY_LENGTH(params));

            if (i == 0) {
                for (size_t j = 0;
                     j < duskArrayLength(value->function.variables_arr);
                     ++j) {
                    DuskIRValue *variable = value->function.variables_arr[j];
                    duskEmitValue(module, variable);
                }
            }

            duskEmitValue(module, block);
        }

        duskEncodeInst(module, SpvOpFunctionEnd, NULL, 0);
        break;
    }
    case DUSK_IR_VALUE_BLOCK: {
        for (size_t i = 0; i < duskArrayLength(value->block.insts_arr); ++i) {
            DuskIRValue *inst = value->block.insts_arr[i];
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
        size_t func_param_count =
            duskArrayLength(value->function_call.params_arr);
        size_t param_count = 3 + func_param_count;
        uint32_t *params =
            duskAllocate(allocator, sizeof(uint32_t) * param_count);
        params[0] = value->type->id;
        params[1] = value->id;
        params[2] = value->function_call.function->id;
        for (size_t i = 0; i < func_param_count; ++i) {
            params[3 + i] = value->function_call.params_arr[i]->id;
        }

        duskEncodeInst(module, SpvOpFunctionCall, params, param_count);
        break;
    }
    case DUSK_IR_VALUE_ACCESS_CHAIN: {
        DUSK_ASSERT(value->type->id > 0);
        DUSK_ASSERT(value->id > 0);
        DUSK_ASSERT(value->access_chain.base->id > 0);

        size_t literal_count = duskArrayLength(value->access_chain.indices_arr);
        size_t param_count = 3 + literal_count;
        uint32_t *params =
            duskAllocate(allocator, sizeof(uint32_t) * param_count);
        params[0] = value->type->id;
        params[1] = value->id;
        params[2] = value->access_chain.base->id;
        for (size_t i = 0; i < literal_count; ++i) {
            DUSK_ASSERT(value->access_chain.indices_arr[i]->id > 0);
            params[3 + i] = value->access_chain.indices_arr[i]->id;
        }

        duskEncodeInst(module, SpvOpAccessChain, params, param_count);
        break;
    }
    case DUSK_IR_VALUE_COMPOSITE_EXTRACT: {
        DUSK_ASSERT(value->type->id > 0);
        DUSK_ASSERT(value->id > 0);
        DUSK_ASSERT(value->composite_extract.composite->id > 0);

        size_t literal_count =
            duskArrayLength(value->composite_extract.indices_arr);
        size_t param_count = 3 + literal_count;
        uint32_t *params =
            duskAllocate(allocator, sizeof(uint32_t) * param_count);
        params[0] = value->type->id;
        params[1] = value->id;
        params[2] = value->composite_extract.composite->id;
        for (size_t i = 0; i < literal_count; ++i) {
            params[3 + i] = value->composite_extract.indices_arr[i];
        }

        duskEncodeInst(module, SpvOpCompositeExtract, params, param_count);
        break;
    }
    case DUSK_IR_VALUE_VECTOR_SHUFFLE: {
        DUSK_ASSERT(value->type->id > 0);
        DUSK_ASSERT(value->id > 0);
        DUSK_ASSERT(value->vector_shuffle.vec1->id > 0);
        DUSK_ASSERT(value->vector_shuffle.vec2->id > 0);

        size_t literal_count =
            duskArrayLength(value->vector_shuffle.indices_arr);
        size_t param_count = 4 + literal_count;
        uint32_t *params =
            duskAllocate(allocator, sizeof(uint32_t) * param_count);
        params[0] = value->type->id;
        params[1] = value->id;
        params[2] = value->vector_shuffle.vec1->id;
        params[3] = value->vector_shuffle.vec2->id;
        for (size_t i = 0; i < literal_count; ++i) {
            params[4 + i] = value->vector_shuffle.indices_arr[i];
        }

        duskEncodeInst(module, SpvOpVectorShuffle, params, param_count);
        break;
    }
    case DUSK_IR_VALUE_COMPOSITE_CONSTRUCT: {
        DUSK_ASSERT(value->type->id > 0);
        DUSK_ASSERT(value->id > 0);

        size_t literal_count =
            duskArrayLength(value->composite_construct.values_arr);
        size_t param_count = 2 + literal_count;
        uint32_t *params =
            duskAllocate(allocator, sizeof(uint32_t) * param_count);
        params[0] = value->type->id;
        params[1] = value->id;
        for (size_t i = 0; i < literal_count; ++i) {
            params[2 + i] = value->composite_construct.values_arr[i]->id;
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
    case DUSK_IR_VALUE_CAST: {
        SpvOp op = 0;

        uint32_t params[3] = {
            value->type->id,
            value->id,
            value->cast.value->id,
        };

        DuskType *source_type = value->cast.value->type;
        DuskType *dest_type = value->type;

        if (source_type->kind == DUSK_TYPE_FLOAT &&
            dest_type->kind == DUSK_TYPE_INT) {
            if (dest_type->int_.is_signed) {
                op = SpvOpConvertFToS;
            } else {
                op = SpvOpConvertFToU;
            }
        } else if (
            source_type->kind == DUSK_TYPE_INT &&
            dest_type->kind == DUSK_TYPE_FLOAT) {
            if (source_type->int_.is_signed) {
                op = SpvOpConvertSToF;
            } else {
                op = SpvOpConvertUToF;
            }
        } else if (
            source_type->kind == DUSK_TYPE_INT &&
            dest_type->kind == DUSK_TYPE_INT) {
            if (source_type->int_.is_signed) {
                if (dest_type->int_.is_signed) {
                    op = SpvOpSConvert;
                } else {
                    op = SpvOpBitcast;
                }
            } else {
                if (dest_type->int_.is_signed) {
                    op = SpvOpBitcast;
                } else {
                    op = SpvOpUConvert;
                }
            }
        } else if (
            source_type->kind == DUSK_TYPE_FLOAT &&
            dest_type->kind == DUSK_TYPE_FLOAT) {
            op = SpvOpFConvert;
        } else {
            DUSK_ASSERT(0);
        }

        duskEncodeInst(module, op, params, DUSK_CARRAY_LENGTH(params));

        break;
    }
    case DUSK_IR_VALUE_BUILTIN_CALL: {
        uint32_t glsl_inst = 0;
        SpvOp op = 0;

        switch (value->builtin_call.builtin_kind) {
        case DUSK_BUILTIN_FUNCTION_RADIANS:
            glsl_inst = GLSLstd450Radians;
            break;
        case DUSK_BUILTIN_FUNCTION_DEGREES:
            glsl_inst = GLSLstd450Degrees;
            break;
        case DUSK_BUILTIN_FUNCTION_ROUND: glsl_inst = GLSLstd450Round; break;
        case DUSK_BUILTIN_FUNCTION_TRUNC: glsl_inst = GLSLstd450Trunc; break;
        case DUSK_BUILTIN_FUNCTION_FLOOR: glsl_inst = GLSLstd450Floor; break;
        case DUSK_BUILTIN_FUNCTION_CEIL: glsl_inst = GLSLstd450Ceil; break;
        case DUSK_BUILTIN_FUNCTION_FRACT: glsl_inst = GLSLstd450Fract; break;
        case DUSK_BUILTIN_FUNCTION_SQRT: glsl_inst = GLSLstd450Sqrt; break;
        case DUSK_BUILTIN_FUNCTION_INVERSE_SQRT:
            glsl_inst = GLSLstd450InverseSqrt;
            break;
        case DUSK_BUILTIN_FUNCTION_LOG: glsl_inst = GLSLstd450Log; break;
        case DUSK_BUILTIN_FUNCTION_LOG2: glsl_inst = GLSLstd450Log2; break;
        case DUSK_BUILTIN_FUNCTION_EXP: glsl_inst = GLSLstd450Exp; break;
        case DUSK_BUILTIN_FUNCTION_EXP2: glsl_inst = GLSLstd450Exp2; break;

        case DUSK_BUILTIN_FUNCTION_SIN: glsl_inst = GLSLstd450Sin; break;
        case DUSK_BUILTIN_FUNCTION_COS: glsl_inst = GLSLstd450Cos; break;
        case DUSK_BUILTIN_FUNCTION_TAN: glsl_inst = GLSLstd450Tan; break;
        case DUSK_BUILTIN_FUNCTION_ASIN: glsl_inst = GLSLstd450Asin; break;
        case DUSK_BUILTIN_FUNCTION_ACOS: glsl_inst = GLSLstd450Acos; break;
        case DUSK_BUILTIN_FUNCTION_ATAN: glsl_inst = GLSLstd450Atan; break;
        case DUSK_BUILTIN_FUNCTION_SINH: glsl_inst = GLSLstd450Sinh; break;
        case DUSK_BUILTIN_FUNCTION_COSH: glsl_inst = GLSLstd450Cosh; break;
        case DUSK_BUILTIN_FUNCTION_TANH: glsl_inst = GLSLstd450Tanh; break;
        case DUSK_BUILTIN_FUNCTION_ASINH: glsl_inst = GLSLstd450Asinh; break;
        case DUSK_BUILTIN_FUNCTION_ACOSH: glsl_inst = GLSLstd450Acosh; break;
        case DUSK_BUILTIN_FUNCTION_ATANH: glsl_inst = GLSLstd450Atanh; break;

        case DUSK_BUILTIN_FUNCTION_ABS: {
            if (value->type->kind == DUSK_TYPE_INT) {
                glsl_inst = GLSLstd450SAbs;
            } else {
                glsl_inst = GLSLstd450FAbs;
            }
            break;
        }

        case DUSK_BUILTIN_FUNCTION_DISTANCE:
            glsl_inst = GLSLstd450Distance;
            break;

        case DUSK_BUILTIN_FUNCTION_NORMALIZE:
            glsl_inst = GLSLstd450Normalize;
            break;

        case DUSK_BUILTIN_FUNCTION_DOT: op = SpvOpDot; break;

        case DUSK_BUILTIN_FUNCTION_LENGTH: glsl_inst = GLSLstd450Length; break;

        case DUSK_BUILTIN_FUNCTION_CROSS: glsl_inst = GLSLstd450Cross; break;

        case DUSK_BUILTIN_FUNCTION_REFLECT:
            glsl_inst = GLSLstd450Reflect;
            break;

        case DUSK_BUILTIN_FUNCTION_REFRACT:
            glsl_inst = GLSLstd450Refract;
            break;

        case DUSK_BUILTIN_FUNCTION_MIN: {
            if (value->type->kind == DUSK_TYPE_INT) {
                if (value->type->int_.is_signed) {
                    glsl_inst = GLSLstd450SMin;
                } else {
                    glsl_inst = GLSLstd450UMin;
                }
            } else {
                glsl_inst = GLSLstd450FMin;
            }
            break;
        }

        case DUSK_BUILTIN_FUNCTION_MAX: {
            if (value->type->kind == DUSK_TYPE_INT) {
                if (value->type->int_.is_signed) {
                    glsl_inst = GLSLstd450SMax;
                } else {
                    glsl_inst = GLSLstd450UMax;
                }
            } else {
                glsl_inst = GLSLstd450FMax;
            }
            break;
        }

        case DUSK_BUILTIN_FUNCTION_MIX: glsl_inst = GLSLstd450FMix; break;

        case DUSK_BUILTIN_FUNCTION_CLAMP: {
            if (value->type->kind == DUSK_TYPE_INT) {
                if (value->type->int_.is_signed) {
                    glsl_inst = GLSLstd450SClamp;
                } else {
                    glsl_inst = GLSLstd450UClamp;
                }
            } else {
                glsl_inst = GLSLstd450FClamp;
            }
            break;
        }

        case DUSK_BUILTIN_FUNCTION_DETERMINANT:
            glsl_inst = GLSLstd450Determinant;
            break;

        case DUSK_BUILTIN_FUNCTION_INVERSE:
            glsl_inst = GLSLstd450MatrixInverse;
            break;

        case DUSK_BUILTIN_FUNCTION_IMAGE_SAMPLE:
            op = SpvOpImageSampleImplicitLod;
            break;

        case DUSK_BUILTIN_FUNCTION_IMAGE_SAMPLE_LOD:
            op = SpvOpImageSampleExplicitLod;
            break;

        case DUSK_BUILTIN_FUNCTION_IMAGE_LOAD: DUSK_ASSERT(!"TODO"); break;

        case DUSK_BUILTIN_FUNCTION_IMAGE_STORE: DUSK_ASSERT(!"TODO"); break;

        case DUSK_BUILTIN_FUNCTION_IMAGE_QUERY_LEVELS:
            DUSK_ASSERT(!"TODO");
            break;

        case DUSK_BUILTIN_FUNCTION_IMAGE_QUERY_LOD: DUSK_ASSERT(!"TODO"); break;

        case DUSK_BUILTIN_FUNCTION_IMAGE_QUERY_SIZE:
            DUSK_ASSERT(!"TODO");
            break;

        case DUSK_BUILTIN_FUNCTION_IMAGE_QUERY_SIZE_LOD:
            DUSK_ASSERT(!"TODO");
            break;

        case DUSK_BUILTIN_FUNCTION_SAMPLER_TYPE:
        case DUSK_BUILTIN_FUNCTION_IMAGE_1D_TYPE:
        case DUSK_BUILTIN_FUNCTION_IMAGE_2D_TYPE:
        case DUSK_BUILTIN_FUNCTION_IMAGE_2D_ARRAY_TYPE:
        case DUSK_BUILTIN_FUNCTION_IMAGE_3D_TYPE:
        case DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_TYPE:
        case DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_ARRAY_TYPE:
        case DUSK_BUILTIN_FUNCTION_IMAGE_1D_SAMPLER_TYPE:
        case DUSK_BUILTIN_FUNCTION_IMAGE_2D_SAMPLER_TYPE:
        case DUSK_BUILTIN_FUNCTION_IMAGE_2D_ARRAY_SAMPLER_TYPE:
        case DUSK_BUILTIN_FUNCTION_IMAGE_3D_SAMPLER_TYPE:
        case DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_SAMPLER_TYPE:
        case DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_ARRAY_SAMPLER_TYPE:
        case DUSK_BUILTIN_FUNCTION_COUNT: {
            DUSK_ASSERT(0);
            break;
        }
        }

        if (glsl_inst != 0) {
            size_t param_count = 4 + value->builtin_call.param_count;
            uint32_t *params = DUSK_NEW_ARRAY(allocator, uint32_t, param_count);
            params[0] = value->type->id;
            params[1] = value->id;
            params[2] = module->glsl_ext_inst_id;
            params[3] = glsl_inst;

            for (size_t i = 0; i < value->builtin_call.param_count; ++i) {
                params[4 + i] = value->builtin_call.params[i]->id;
                DUSK_ASSERT(params[4 + i] > 0);
            }

            duskEncodeInst(module, SpvOpExtInst, params, param_count);
        } else {
            DUSK_ASSERT(op != 0);

            switch (op) {
            case SpvOpImageSampleExplicitLod: {
                size_t param_count = 6;
                uint32_t *params =
                    DUSK_NEW_ARRAY(allocator, uint32_t, param_count);
                params[0] = value->type->id;
                params[1] = value->id;

                params[2] = value->builtin_call.params[0]->id;
                params[3] = value->builtin_call.params[1]->id;
                params[4] = SpvImageOperandsLodMask;
                params[5] = value->builtin_call.params[2]->id;

                duskEncodeInst(module, op, params, param_count);
                break;
            }
            default: {
                size_t param_count = 2 + value->builtin_call.param_count;
                uint32_t *params =
                    DUSK_NEW_ARRAY(allocator, uint32_t, param_count);
                params[0] = value->type->id;
                params[1] = value->id;

                for (size_t i = 0; i < value->builtin_call.param_count; ++i) {
                    params[2 + i] = value->builtin_call.params[i]->id;
                    DUSK_ASSERT(params[2 + i] > 0);
                }

                duskEncodeInst(module, op, params, param_count);
                break;
            }
            }
        }
        break;
    }
    case DUSK_IR_VALUE_BINARY_OPERATION: {
        SpvOp op = 0;

        DuskType *left_type = value->binary.left->type;
        DuskType *right_type = value->binary.right->type;

        DuskType *left_scalar_type = duskGetScalarType(left_type);
        DuskType *right_scalar_type = duskGetScalarType(right_type);

        DUSK_ASSERT(left_scalar_type == right_scalar_type);

        uint32_t left_val_id = value->binary.left->id;
        uint32_t right_val_id = value->binary.right->id;

        if (left_type != right_type) {
            DUSK_ASSERT(
                left_type->kind == DUSK_TYPE_VECTOR ||
                right_type->kind == DUSK_TYPE_VECTOR);
            DUSK_ASSERT(value->binary.op == DUSK_BINARY_OP_MUL);
        }

        DuskType *scalar_type = left_scalar_type;

        switch (value->binary.op) {
        case DUSK_BINARY_OP_ADD: {
            if (scalar_type->kind == DUSK_TYPE_FLOAT) {
                op = SpvOpFAdd;
            } else if (scalar_type->kind == DUSK_TYPE_INT) {
                op = SpvOpIAdd;
            } else {
                DUSK_ASSERT(0);
            }
            break;
        }
        case DUSK_BINARY_OP_SUB: {
            if (scalar_type->kind == DUSK_TYPE_FLOAT) {
                op = SpvOpFSub;
            } else if (scalar_type->kind == DUSK_TYPE_INT) {
                op = SpvOpISub;
            } else {
                DUSK_ASSERT(0);
            }
            break;
        }
        case DUSK_BINARY_OP_MUL: {
            if (left_type->kind == DUSK_TYPE_VECTOR &&
                left_type->vector.sub == right_type) {
                op = SpvOpVectorTimesScalar;
            } else if (
                right_type->kind == DUSK_TYPE_VECTOR &&
                right_type->vector.sub == left_type) {
                op = SpvOpVectorTimesScalar;
                uint32_t temp_val_id = left_val_id;
                left_val_id = right_val_id;
                right_val_id = temp_val_id;
            } else if (
                left_type->kind == DUSK_TYPE_MATRIX &&
                left_type->matrix.col_type->vector.sub == right_type) {
                op = SpvOpMatrixTimesScalar;
            } else if (
                right_type->kind == DUSK_TYPE_MATRIX &&
                right_type->matrix.col_type->vector.sub == left_type) {
                uint32_t temp_val_id = left_val_id;
                left_val_id = right_val_id;
                right_val_id = temp_val_id;
                op = SpvOpMatrixTimesScalar;
            } else if (
                left_type->kind == DUSK_TYPE_VECTOR &&
                right_type->kind == DUSK_TYPE_MATRIX &&
                left_type == right_type->matrix.col_type) {
                op = SpvOpVectorTimesMatrix;
            } else if (
                left_type->kind == DUSK_TYPE_MATRIX &&
                right_type->kind == DUSK_TYPE_VECTOR &&
                right_type == left_type->matrix.col_type) {
                op = SpvOpMatrixTimesVector;
            } else if (
                left_type->kind == DUSK_TYPE_MATRIX &&
                right_type->kind == DUSK_TYPE_MATRIX &&
                left_type == right_type) {
                op = SpvOpMatrixTimesMatrix;
            } else if (scalar_type->kind == DUSK_TYPE_FLOAT) {
                op = SpvOpFMul;
            } else if (scalar_type->kind == DUSK_TYPE_INT) {
                op = SpvOpIMul;
            } else {
                DUSK_ASSERT(0);
            }
            break;
        }
        case DUSK_BINARY_OP_DIV: {
            if (scalar_type->kind == DUSK_TYPE_FLOAT) {
                op = SpvOpFDiv;
            } else if (scalar_type->kind == DUSK_TYPE_INT) {
                if (scalar_type->int_.is_signed) {
                    op = SpvOpSDiv;
                } else {
                    op = SpvOpUDiv;
                }
            } else {
                DUSK_ASSERT(0);
            }
            break;
        }
        case DUSK_BINARY_OP_MOD: {
            if (scalar_type->kind == DUSK_TYPE_INT) {
                if (scalar_type->int_.is_signed) {
                    op = SpvOpSMod;
                } else {
                    op = SpvOpUMod;
                }
            } else {
                DUSK_ASSERT(0);
            }
            break;
        }
        case DUSK_BINARY_OP_BITAND: {
            if (scalar_type->kind == DUSK_TYPE_INT) {
                op = SpvOpBitwiseAnd;
            } else {
                DUSK_ASSERT(0);
            }
            break;
        }
        case DUSK_BINARY_OP_BITOR: {
            if (scalar_type->kind == DUSK_TYPE_INT) {
                op = SpvOpBitwiseOr;
            } else {
                DUSK_ASSERT(0);
            }
            break;
        }
        case DUSK_BINARY_OP_BITXOR: {
            if (scalar_type->kind == DUSK_TYPE_INT) {
                op = SpvOpBitwiseXor;
            } else {
                DUSK_ASSERT(0);
            }
            break;
        }
        case DUSK_BINARY_OP_LSHIFT: {
            if (scalar_type->kind == DUSK_TYPE_INT) {
                op = SpvOpShiftLeftLogical;
            } else {
                DUSK_ASSERT(0);
            }
            break;
        }
        case DUSK_BINARY_OP_RSHIFT: {
            if (scalar_type->kind == DUSK_TYPE_INT) {
                if (scalar_type->int_.is_signed) {
                    op = SpvOpShiftRightArithmetic;
                } else {
                    op = SpvOpShiftRightLogical;
                }
            } else {
                DUSK_ASSERT(0);
            }
            break;
        }
        case DUSK_BINARY_OP_EQ: {
            if (scalar_type->kind == DUSK_TYPE_FLOAT) {
                op = SpvOpFOrdEqual;
            } else if (scalar_type->kind == DUSK_TYPE_INT) {
                op = SpvOpIEqual;
            } else {
                DUSK_ASSERT(0);
            }
            break;
        }
        case DUSK_BINARY_OP_NOTEQ: {
            if (scalar_type->kind == DUSK_TYPE_FLOAT) {
                op = SpvOpFOrdNotEqual;
            } else if (scalar_type->kind == DUSK_TYPE_INT) {
                op = SpvOpINotEqual;
            } else {
                DUSK_ASSERT(0);
            }
            break;
        }
        case DUSK_BINARY_OP_LESS: {
            if (scalar_type->kind == DUSK_TYPE_FLOAT) {
                op = SpvOpFOrdLessThan;
            } else if (scalar_type->kind == DUSK_TYPE_INT) {
                if (scalar_type->int_.is_signed) {
                    op = SpvOpSLessThan;
                } else {
                    op = SpvOpULessThan;
                }
            } else {
                DUSK_ASSERT(0);
            }
            break;
        }
        case DUSK_BINARY_OP_LESSEQ: {
            if (scalar_type->kind == DUSK_TYPE_FLOAT) {
                op = SpvOpFOrdLessThanEqual;
            } else if (scalar_type->kind == DUSK_TYPE_INT) {
                if (scalar_type->int_.is_signed) {
                    op = SpvOpSLessThanEqual;
                } else {
                    op = SpvOpULessThanEqual;
                }
            } else {
                DUSK_ASSERT(0);
            }
            break;
        }
        case DUSK_BINARY_OP_GREATER: {
            if (scalar_type->kind == DUSK_TYPE_FLOAT) {
                op = SpvOpFOrdGreaterThan;
            } else if (scalar_type->kind == DUSK_TYPE_INT) {
                if (scalar_type->int_.is_signed) {
                    op = SpvOpSGreaterThan;
                } else {
                    op = SpvOpUGreaterThan;
                }
            } else {
                DUSK_ASSERT(0);
            }
            break;
        }
        case DUSK_BINARY_OP_GREATEREQ: {
            if (scalar_type->kind == DUSK_TYPE_FLOAT) {
                op = SpvOpFOrdGreaterThanEqual;
            } else if (scalar_type->kind == DUSK_TYPE_INT) {
                if (scalar_type->int_.is_signed) {
                    op = SpvOpSGreaterThanEqual;
                } else {
                    op = SpvOpUGreaterThanEqual;
                }
            } else {
                DUSK_ASSERT(0);
            }
            break;
        }

        case DUSK_BINARY_OP_AND:
        case DUSK_BINARY_OP_OR: DUSK_ASSERT(0); break;

        case DUSK_BINARY_OP_MAX: DUSK_ASSERT(0); break;
        }

        DUSK_ASSERT(value->type->id);
        DUSK_ASSERT(value->id);
        DUSK_ASSERT(value->binary.left->id);
        DUSK_ASSERT(value->binary.right->id);

        uint32_t params[4] = {
            value->type->id,
            value->id,
            left_val_id,
            right_val_id,
        };
        duskEncodeInst(module, op, params, DUSK_CARRAY_LENGTH(params));

        break;
    }
    case DUSK_IR_VALUE_UNARY_OPERATION: {
        SpvOp op = 0;

        DuskType *scalar_type = duskGetScalarType(value->type);

        switch (value->unary.op) {
        case DUSK_UNARY_OP_NEGATE: {
            if (scalar_type->kind == DUSK_TYPE_FLOAT) {
                op = SpvOpFNegate;
            } else if (scalar_type->kind == DUSK_TYPE_INT) {
                op = SpvOpSNegate;
            } else {
                DUSK_ASSERT(0);
            }
            break;
        }
        case DUSK_UNARY_OP_NOT: op = SpvOpNot; break;
        case DUSK_UNARY_OP_BITNOT: op = SpvOpBitReverse; break;
        }

        uint32_t params[3] = {
            value->type->id,
            value->id,
            value->unary.right->id,
        };
        duskEncodeInst(module, op, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_IR_VALUE_BRANCH: {
        uint32_t params[1] = {value->branch.dest_block->id};
        duskEncodeInst(module, SpvOpBranch, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_IR_VALUE_BRANCH_COND: {
        uint32_t params[3] = {
            value->branch_cond.cond->id,
            value->branch_cond.true_block->id,
            value->branch_cond.false_block->id,
        };
        duskEncodeInst(
            module, SpvOpBranchConditional, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_IR_VALUE_SELECTION_MERGE: {
        uint32_t params[2] = {
            value->selection_merge.merge_block->id,
            0x0, // None
        };
        duskEncodeInst(
            module, SpvOpSelectionMerge, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_IR_VALUE_LOOP_MERGE: {
        uint32_t params[3] = {
            value->loop_merge.merge_block->id,
            value->loop_merge.continue_block->id,
            0x0, // None
        };
        duskEncodeInst(
            module, SpvOpLoopMerge, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    case DUSK_IR_VALUE_PHI: {
        size_t param_count = 2 + value->phi.pair_count * 2;
        uint32_t *params = DUSK_NEW_ARRAY(allocator, uint32_t, param_count);
        params[0] = value->type->id;
        params[1] = value->id;
        for (size_t i = 0; i < value->phi.pair_count; ++i) {
            params[2 + i * 2] = value->phi.pairs[i].value->id;
            params[2 + i * 2 + 1] = value->phi.pairs[i].block->id;
        }
        duskEncodeInst(module, SpvOpPhi, params, param_count);
        break;
    }
    case DUSK_IR_VALUE_ARRAY_LENGTH: {
        uint32_t params[4] = {
            value->type->id,
            value->id,
            value->array_length.struct_ptr->id,
            value->array_length.struct_member_index,
        };
        duskEncodeInst(
            module, SpvOpArrayLength, params, DUSK_CARRAY_LENGTH(params));
        break;
    }
    }
}

DuskArray(uint32_t)
    duskIRModuleEmit(DuskCompiler *compiler, DuskIRModule *module)
{
    DuskAllocator *allocator = module->allocator;

    for (size_t i = 0; i < duskArrayLength(compiler->types_arr); ++i) {
        DuskType *type = compiler->types_arr[i];
        switch (type->kind) {
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

    bool got_byte_type = false;
    bool got_short_type = false;
    bool got_long_type = false;
    bool got_half_type = false;
    bool got_double_type = false;

    for (size_t i = 0; i < duskArrayLength(compiler->types_arr); ++i) {
        DuskType *type = compiler->types_arr[i];
        if (type->emit) {
            type->id = duskReserveId(module);

            if (!got_byte_type && type->kind == DUSK_TYPE_INT &&
                type->int_.bits == 8) {
                got_byte_type = true;
                duskArrayPush(&module->capabilities_arr, SpvCapabilityInt8);
            }

            if (!got_short_type && type->kind == DUSK_TYPE_INT &&
                type->int_.bits == 16) {
                got_short_type = true;
                duskArrayPush(&module->capabilities_arr, SpvCapabilityInt16);
            }

            if (!got_long_type && type->kind == DUSK_TYPE_INT &&
                type->int_.bits == 64) {
                got_long_type = true;
                duskArrayPush(&module->capabilities_arr, SpvCapabilityInt64);
            }

            if (!got_half_type && type->kind == DUSK_TYPE_FLOAT &&
                type->float_.bits == 16) {
                got_half_type = true;
                duskArrayPush(&module->capabilities_arr, SpvCapabilityFloat16);
            }

            if (!got_double_type && type->kind == DUSK_TYPE_FLOAT &&
                type->float_.bits == 64) {
                got_double_type = true;
                duskArrayPush(&module->capabilities_arr, SpvCapabilityFloat64);
            }
        }
    }

    for (size_t i = 0; i < duskArrayLength(module->consts_arr); ++i) {
        DuskIRValue *value = module->consts_arr[i];
        value->id = duskReserveId(module);
    }

    for (size_t i = 0; i < duskArrayLength(module->globals_arr); ++i) {
        DuskIRValue *value = module->globals_arr[i];
        value->id = duskReserveId(module);
    }

    for (size_t i = 0; i < duskArrayLength(module->functions_arr); ++i) {
        DuskIRValue *function = module->functions_arr[i];
        function->id = duskReserveId(module);

        for (size_t j = 0; j < duskArrayLength(function->function.params_arr);
             ++j) {
            DuskIRValue *param = function->function.params_arr[j];
            param->id = duskReserveId(module);
        }

        for (size_t j = 0; j < duskArrayLength(function->function.blocks_arr);
             ++j) {
            DuskIRValue *block = function->function.blocks_arr[j];
            block->id = duskReserveId(module);

            if (j == 0) {
                for (size_t k = 0;
                     k < duskArrayLength(function->function.variables_arr);
                     ++k) {

                    DuskIRValue *variable = function->function.variables_arr[k];
                    variable->id = duskReserveId(module);
                }
            }

            for (size_t k = 0; k < duskArrayLength(block->block.insts_arr);
                 ++k) {

                DuskIRValue *inst = block->block.insts_arr[k];
                inst->id = duskReserveId(module);
            }
        }
    }

    duskArrayPush(&module->stream_arr, SpvMagicNumber);
    duskArrayPush(&module->stream_arr, SpvVersion);
    duskArrayPush(&module->stream_arr, 28); // Khronos compiler ID
    duskArrayPush(&module->stream_arr, 0);  // ID Bound (fill out later)
    duskArrayPush(&module->stream_arr, 0);

    for (size_t i = 0; i < duskArrayLength(module->capabilities_arr); ++i) {
        uint32_t capability = module->capabilities_arr[i];
        duskEncodeInst(module, SpvOpCapability, &capability, 1);
    }

    for (size_t i = 0; i < duskArrayLength(module->extensions_arr); ++i) {
        const char *ext = module->extensions_arr[i];
        size_t ext_strlen = strlen(ext);

        size_t param_word_count = DUSK_ROUND_UP(4, ext_strlen + 1) / 4;
        uint32_t *param_words =
            DUSK_NEW_ARRAY(allocator, uint32_t, param_word_count);
        memcpy(param_words, ext, ext_strlen);

        duskEncodeInst(module, SpvOpExtension, param_words, param_word_count);
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

    for (size_t i = 0; i < duskArrayLength(module->entry_points_arr); ++i) {
        DuskIREntryPoint *entry_point = module->entry_points_arr[i];
        size_t entry_point_name_len = strlen(entry_point->name);

        size_t name_word_count = DUSK_ROUND_UP(4, entry_point_name_len + 1) / 4;

        size_t param_count =
            2 + name_word_count +
            duskArrayLength(entry_point->referenced_globals_arr);
        uint32_t *params = DUSK_NEW_ARRAY(allocator, uint32_t, param_count);

        switch (entry_point->stage) {
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

        for (size_t i = 0;
             i < duskArrayLength(entry_point->referenced_globals_arr);
             ++i) {
            params[2 + name_word_count + i] =
                entry_point->referenced_globals_arr[i]->id;
        }

        duskEncodeInst(module, SpvOpEntryPoint, params, param_count);
    }

    for (size_t i = 0; i < duskArrayLength(module->entry_points_arr); ++i) {
        DuskIREntryPoint *entry_point = module->entry_points_arr[i];
        switch (entry_point->stage) {
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

    for (size_t i = 0; i < duskArrayLength(compiler->types_arr); ++i) {
        DuskType *type = compiler->types_arr[i];
        if (!type->emit) continue;

        if (type->kind == DUSK_TYPE_STRUCT && type->struct_.is_block) {
            DuskIRDecoration decoration = duskIRCreateDecoration(
                allocator, DUSK_IR_DECORATION_BLOCK, 0, NULL);
            duskArrayPush(&type->decorations_arr, decoration);
        }

        if ((type->kind == DUSK_TYPE_ARRAY ||
             type->kind == DUSK_TYPE_RUNTIME_ARRAY) &&
            type->array.layout != DUSK_STRUCT_LAYOUT_UNKNOWN) {
            uint32_t stride =
                duskTypeSizeOf(allocator, type->array.sub, type->array.layout);
            if (type->array.layout == DUSK_STRUCT_LAYOUT_STD140) {
                stride = DUSK_ROUND_UP(16, stride);
            }

            DuskIRDecoration decoration = duskIRCreateDecoration(
                allocator, DUSK_IR_DECORATION_ARRAY_STRIDE, 1, &stride);
            duskArrayPush(&type->decorations_arr, decoration);
        }

        duskEmitDecorations(module, type->id, type->decorations_arr);

        if (type->kind == DUSK_TYPE_STRUCT &&
            type->struct_.layout != DUSK_STRUCT_LAYOUT_UNKNOWN) {
            for (uint32_t j = 0; j < type->struct_.field_count; ++j) {
                duskEmitMemberDecorations(
                    module,
                    type->id,
                    j,
                    type->struct_.field_decoration_arrays[j]);
            }
        }
    }

    for (size_t i = 0; i < duskArrayLength(module->globals_arr); ++i) {
        DuskIRValue *value = module->globals_arr[i];
        duskEmitDecorations(module, value->id, value->decorations_arr);
    }

    for (size_t i = 0; i < duskArrayLength(compiler->types_arr); ++i) {
        DuskType *type = compiler->types_arr[i];
        duskEmitType(module, type);
    }

    for (size_t i = 0; i < duskArrayLength(module->consts_arr); ++i) {
        DuskIRValue *value = module->consts_arr[i];
        duskEmitValue(module, value);
    }

    for (size_t i = 0; i < duskArrayLength(module->globals_arr); ++i) {
        DuskIRValue *value = module->globals_arr[i];
        duskEmitValue(module, value);
    }

    for (size_t i = 0; i < duskArrayLength(module->functions_arr); ++i) {
        DuskIRValue *function = module->functions_arr[i];
        duskEmitValue(module, function);
    }

    // Fill out ID bound
    module->stream_arr[3] = module->last_id + 1;

    return module->stream_arr;
}
