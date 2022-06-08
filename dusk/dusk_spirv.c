#include "dusk_internal.h"

static void
duskSpvValueEmit(DuskArray(uint32_t) * stream_arr, DuskSpvValue *value)
{
    if (value->emitted) return;

    DUSK_ASSERT(value->op != SpvOpMax);

    bool has_result, has_result_type;
    SpvHasResultAndType(value->op, &has_result, &has_result_type);

    uint32_t actual_param_count = value->param_count;
    if (has_result) actual_param_count++;
    if (has_result_type) {
        actual_param_count++;
        DUSK_ASSERT(value->type);
        DUSK_ASSERT(value->type->spv_value);
        DUSK_ASSERT(value->type->spv_value->id != 0);
        duskSpvValueEmit(stream_arr, value->type->spv_value);
    }

    uint32_t opcode_word = value->op;
    opcode_word |= ((uint16_t)(actual_param_count + 1)) << 16;
    duskArrayPush(stream_arr, opcode_word);

    if (has_result_type) {
        duskArrayPush(stream_arr, value->type->spv_value->id);
    }

    if (has_result) {
        DUSK_ASSERT(value->id != 0);
        duskArrayPush(stream_arr, value->id);
    }

    for (uint32_t i = 0; i < value->param_count; ++i) {
        if (value->params[i]->op != SpvOpMax) {
            DUSK_ASSERT(value->params[i]->id != 0);
        }
        duskArrayPush(stream_arr, value->params[i]->id);
    }

    value->emitted = true;
}

SpvStorageClass duskStorageClassToSpv(DuskStorageClass storage_class)
{
    switch (storage_class) {
    case DUSK_STORAGE_CLASS_FUNCTION: return SpvStorageClassFunction;
    case DUSK_STORAGE_CLASS_INPUT: return SpvStorageClassInput;
    case DUSK_STORAGE_CLASS_OUTPUT: return SpvStorageClassOutput;
    case DUSK_STORAGE_CLASS_UNIFORM: return SpvStorageClassUniform;
    case DUSK_STORAGE_CLASS_STORAGE: return SpvStorageClassStorageBuffer; break;
    case DUSK_STORAGE_CLASS_UNIFORM_CONSTANT:
        return SpvStorageClassUniformConstant;
    case DUSK_STORAGE_CLASS_PUSH_CONSTANT: return SpvStorageClassPushConstant;
    case DUSK_STORAGE_CLASS_WORKGROUP: return SpvStorageClassWorkgroup;
    case DUSK_STORAGE_CLASS_PARAMETER: DUSK_ASSERT(0); break;
    }

    DUSK_ASSERT(0);
    return SpvStorageClassMax;
}

DuskSpvValue *duskSpvCreateLiteralValue(DuskSpvModule *module, uint32_t literal)
{
    DuskSpvValue *value = DUSK_NEW(module->allocator, DuskSpvValue);
    value->id = literal;
    value->op = SpvOpMax;
    return value;
}

DuskSpvValue *duskSpvCreateValue(
    DuskSpvModule *module,
    SpvOp op,
    DuskType *type,
    uint32_t param_count,
    DuskSpvValue **params)
{
    DuskSpvValue *value = DUSK_NEW(module->allocator, DuskSpvValue);
    value->id = 0;
    value->op = op;
    value->type = type;
    value->param_count = param_count;
    value->params =
        DUSK_NEW_ARRAY(module->allocator, DuskSpvValue *, param_count);
    if (param_count > 0) {
        memcpy(value->params, params, sizeof(DuskSpvValue *) * param_count);
    }

    for (size_t i = 0; i < param_count; ++i) {
        DUSK_ASSERT(value->params[i]);
    }

    return value;
}

void duskSpvModuleAddExtension(DuskSpvModule *module, const char *ext_name)
{
    size_t ext_strlen = strlen(ext_name);

    size_t param_word_count = DUSK_ROUND_UP(4, ext_strlen + 1) / 4;
    uint32_t *param_words =
        DUSK_NEW_ARRAY(module->allocator, uint32_t, param_word_count);
    memcpy(param_words, ext_name, ext_strlen);

    DuskSpvValue **param_values =
        DUSK_NEW_ARRAY(module->allocator, DuskSpvValue *, param_word_count);
    for (size_t i = 0; i < param_word_count; ++i) {
        param_values[i] = duskSpvCreateLiteralValue(module, param_words[i]);
    }

    DuskSpvValue *value = duskSpvCreateValue(
        module, SpvOpExtension, NULL, param_word_count, param_values);
    duskArrayPush(&module->extensions_arr, value);
}

void duskSpvModuleAddCapability(DuskSpvModule *module, SpvCapability capability)
{
    DuskSpvValue *param_values[1] = {
        duskSpvCreateLiteralValue(module, capability),
    };
    DuskSpvValue *value =
        duskSpvCreateValue(module, SpvOpCapability, NULL, 1, param_values);
    duskArrayPush(&module->capabilities_arr, value);
}

DuskSpvValue *duskSpvModuleAddEntryPoint(
    DuskSpvModule *module,
    SpvExecutionModel execution_model,
    const char *name,
    DuskSpvValue *function)
{
    size_t name_strlen = strlen(name);

    size_t name_word_count = DUSK_ROUND_UP(4, name_strlen + 1) / 4;
    uint32_t *name_words =
        DUSK_NEW_ARRAY(module->allocator, uint32_t, name_word_count);
    memcpy(name_words, name, name_strlen);

    uint32_t param_count = 2 + name_word_count;
    DuskSpvValue **param_values =
        DUSK_NEW_ARRAY(module->allocator, DuskSpvValue *, param_count);
    param_values[0] = duskSpvCreateLiteralValue(module, execution_model);
    param_values[1] = function;
    for (size_t i = 0; i < name_word_count; ++i) {
        param_values[i + 2] = duskSpvCreateLiteralValue(module, name_words[i]);
    }

    DuskSpvValue *value = duskSpvCreateValue(
        module, SpvOpEntryPoint, NULL, param_count, param_values);
    duskArrayPush(&module->entry_points_arr, value);
    return value;
}

void duskSpvModuleAddToMemoryModelSection(
    DuskSpvModule *module, DuskSpvValue *value)
{
    DUSK_ASSERT(value);
    duskArrayPush(&module->memory_model_arr, value);
}

void duskSpvModuleAddToExecutionModesSection(
    DuskSpvModule *module, DuskSpvValue *value)
{
    DUSK_ASSERT(value);
    duskArrayPush(&module->execution_modes_arr, value);
}

void duskSpvDecorate(
    DuskSpvModule *module,
    DuskSpvValue *value,
    SpvDecoration decoration,
    size_t literal_count,
    uint32_t *literals)
{
    size_t param_count = literal_count + 2;
    DuskSpvValue **param_values =
        DUSK_NEW_ARRAY(module->allocator, DuskSpvValue *, param_count);
    param_values[0] = value;
    param_values[1] = duskSpvCreateLiteralValue(module, (uint32_t)decoration);
    for (size_t i = 0; i < literal_count; ++i) {
        param_values[2 + i] = duskSpvCreateLiteralValue(module, literals[i]);
    }

    DuskSpvValue *decoration_value = duskSpvCreateValue(
        module, SpvOpDecorate, NULL, param_count, param_values);
    duskArrayPush(&module->decorations_arr, decoration_value);
}

void duskSpvDecorateMember(
    DuskSpvModule *module,
    DuskSpvValue *struct_type,
    uint32_t member_index,
    SpvDecoration decoration,
    size_t literal_count,
    uint32_t *literals)
{
    size_t param_count = literal_count + 3;
    DuskSpvValue **param_values =
        DUSK_NEW_ARRAY(module->allocator, DuskSpvValue *, param_count);
    param_values[0] = struct_type;
    param_values[1] = duskSpvCreateLiteralValue(module, (uint32_t)member_index);
    param_values[2] = duskSpvCreateLiteralValue(module, (uint32_t)decoration);
    for (size_t i = 0; i < literal_count; ++i) {
        param_values[3 + i] = duskSpvCreateLiteralValue(module, literals[i]);
    }

    DuskSpvValue *decoration_value = duskSpvCreateValue(
        module, SpvOpMemberDecorate, NULL, param_count, param_values);
    duskArrayPush(&module->decorations_arr, decoration_value);
}

void duskSpvModuleAddToTypesAndConstsSection(
    DuskSpvModule *module, DuskSpvValue *value)
{
    DUSK_ASSERT(value);
    duskArrayPush(&module->types_consts_arr, value);
}

void duskSpvModuleAddToFunctionsSection(
    DuskSpvModule *module, DuskSpvValue *value)
{
    DUSK_ASSERT(value);
    duskArrayPush(&module->functions_arr, value);
}

void duskSpvModuleAddToGlobalsSection(
    DuskSpvModule *module, DuskSpvValue *value)
{
    DUSK_ASSERT(value);
    duskArrayPush(&module->globals_arr, value);
}

static void duskSpvModuleAddEntryPointUsedGlobals(
    DuskSpvModule *module, DuskSpvValue *entry_point)
{
    DuskArray(DuskSpvValue *) func_starts =
        duskArrayCreate(module->allocator, DuskSpvValue *);
    duskArrayPush(&func_starts, entry_point->params[1]);

    DuskArray(DuskSpvValue *) referenced_globals =
        duskArrayCreate(module->allocator, DuskSpvValue *);

    while (duskArrayLength(func_starts) > 0) {
        DuskSpvValue *func_start =
            func_starts[duskArrayLength(func_starts) - 1];
        duskArrayPop(&func_starts);

        size_t i = 0;
        while (module->functions_arr[i] != func_start) {
            i++;
        }

        while (module->functions_arr[i]->op != SpvOpFunctionEnd) {
            DuskSpvValue *value = module->functions_arr[i];

            if (value->op == SpvOpFunctionCall) {
                DuskSpvValue *func = value->params[0];
                duskArrayPush(&func_starts, func);
            }

            for (size_t j = 0; j < value->param_count; ++j) {
                DuskSpvValue *param = value->params[j];
                DUSK_ASSERT(param);
                if (param->op == SpvOpVariable) {
                    DUSK_ASSERT(param->params[0]->op == SpvOpMax);
                    switch (param->params[0]->id) {
                    case SpvStorageClassInput:
                    case SpvStorageClassOutput:
                    case SpvStorageClassUniform:
                    case SpvStorageClassUniformConstant:
                    case SpvStorageClassStorageBuffer:
                    case SpvStorageClassPushConstant:
                    case SpvStorageClassWorkgroup: {
                        bool found = false;
                        for (size_t k = 0; k < duskArrayLength(referenced_globals); ++k) {
                            if (referenced_globals[k] == param) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            duskArrayPush(&referenced_globals, param);
                        }
                        break;
                    }
                    default: break;
                    }
                }
            }

            ++i;
        }
    }

    size_t new_entry_point_params_len =
        entry_point->param_count + duskArrayLength(referenced_globals);
    DuskSpvValue **new_entry_point_params = DUSK_NEW_ARRAY(
        module->allocator, DuskSpvValue *, new_entry_point_params_len);
    memcpy(
        new_entry_point_params,
        entry_point->params,
        sizeof(DuskSpvValue *) * entry_point->param_count);
    memcpy(
        new_entry_point_params + entry_point->param_count,
        referenced_globals,
        sizeof(DuskSpvValue *) * duskArrayLength(referenced_globals));
    entry_point->param_count = new_entry_point_params_len;
    entry_point->params = new_entry_point_params;
}

DuskSpvModule *duskSpvModuleCreate(DuskCompiler *compiler)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);
    DuskSpvModule *module = DUSK_NEW(allocator, DuskSpvModule);

    module->compiler = compiler;
    module->allocator = allocator;

    module->capabilities_arr = duskArrayCreate(NULL, DuskSpvValue *);
    module->extensions_arr = duskArrayCreate(NULL, DuskSpvValue *);
    module->memory_model_arr = duskArrayCreate(NULL, DuskSpvValue *);
    module->entry_points_arr = duskArrayCreate(NULL, DuskSpvValue *);
    module->execution_modes_arr = duskArrayCreate(NULL, DuskSpvValue *);
    module->decorations_arr = duskArrayCreate(NULL, DuskSpvValue *);
    module->types_consts_arr = duskArrayCreate(NULL, DuskSpvValue *);
    module->globals_arr = duskArrayCreate(NULL, DuskSpvValue *);
    module->functions_arr = duskArrayCreate(NULL, DuskSpvValue *);

    return module;
}

void duskSpvModuleDestroy(DuskSpvModule *module)
{
    duskArrayFree(&module->capabilities_arr);
    duskArrayFree(&module->extensions_arr);
    duskArrayFree(&module->memory_model_arr);
    duskArrayFree(&module->entry_points_arr);
    duskArrayFree(&module->execution_modes_arr);
    duskArrayFree(&module->decorations_arr);
    duskArrayFree(&module->types_consts_arr);
    duskArrayFree(&module->globals_arr);
    duskArrayFree(&module->functions_arr);
}

uint32_t *duskSpvModuleEmit(
    DuskAllocator *allocator, DuskSpvModule *module, size_t *out_length)
{
    DuskArray(uint32_t) stream_arr = duskArrayCreate(NULL, uint32_t);

    duskArrayPush(&stream_arr, SpvMagicNumber);
    duskArrayPush(&stream_arr, SpvVersion);
    duskArrayPush(&stream_arr, 28); // Khronos compiler ID
    duskArrayPush(&stream_arr, 0);  // ID Bound (fill out later)
    duskArrayPush(&stream_arr, 0);

    DuskArray(DuskSpvValue *) sections[] = {
        module->capabilities_arr,
        module->extensions_arr,
        module->memory_model_arr,
        module->entry_points_arr,
        module->execution_modes_arr,
        module->decorations_arr,
        module->types_consts_arr,
        module->globals_arr,
        module->functions_arr,
    };
    const uint32_t section_count = DUSK_CARRAY_LENGTH(sections);

    for (size_t i = 0; i < duskArrayLength(module->entry_points_arr); ++i) {
        DuskSpvValue *entry_point = module->entry_points_arr[i];
        duskSpvModuleAddEntryPointUsedGlobals(module, entry_point);
    }

    uint32_t last_id = 0;

    // Reserve IDs
    for (size_t i = 0; i < section_count; ++i) {
        DuskArray(DuskSpvValue *) section = sections[i];
        for (size_t j = 0; j < duskArrayLength(section); ++j) {
            DuskSpvValue *value = section[j];
            DUSK_ASSERT(value);

            if (value->op == SpvOpMax) continue;
            if (value->id > 0) continue;

            bool has_result, has_result_type;
            SpvHasResultAndType(value->op, &has_result, &has_result_type);

            if (has_result_type) {
                DuskSpvValue *type_value = value->type->spv_value;
                if (type_value->id == 0) {
                    type_value->id = ++last_id;
                }
            }

            if (has_result) {
                value->id = ++last_id;
            }
        }
    }

    stream_arr[3] = last_id + 1;

    // Emit instructions
    for (size_t i = 0; i < section_count; ++i) {
        DuskArray(DuskSpvValue *) section = sections[i];
        for (size_t j = 0; j < duskArrayLength(section); ++j) {
            DuskSpvValue *value = section[j];
            duskSpvValueEmit(&stream_arr, value);
        }
    }

    uint32_t *result =
        DUSK_NEW_ARRAY(allocator, uint32_t, duskArrayLength(stream_arr));
    memcpy(result, stream_arr, duskArrayLength(stream_arr) * sizeof(uint32_t));
    *out_length = duskArrayLength(stream_arr);
    duskArrayFree(&stream_arr);
    return result;
}
