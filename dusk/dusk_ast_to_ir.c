#include "dusk_internal.h"
#include "spirv.h"

static void duskGenerateLocalDecl(
    DuskIRModule *module, DuskDecl *func_decl, DuskDecl *decl);

static DuskIRValue *duskGetLastBlock(DuskIRValue *function)
{
    return function->function
        .blocks_arr[duskArrayLength(function->function.blocks_arr) - 1];
}

uint32_t duskTypeAlignOf(
    DuskAllocator *allocator, DuskType *type, DuskStructLayout layout)
{
    uint32_t alignment = 0;
    switch (type->kind) {
    case DUSK_TYPE_BOOL: alignment = 1; break;
    case DUSK_TYPE_INT: alignment = type->int_.bits / 8; break;
    case DUSK_TYPE_FLOAT: alignment = type->float_.bits / 8; break;

    case DUSK_TYPE_VECTOR: {
        switch (type->vector.size) {
        case 1:
            alignment = duskTypeSizeOf(allocator, type->vector.sub, layout) * 1;
            break;
        case 2:
            alignment = duskTypeSizeOf(allocator, type->vector.sub, layout) * 2;
            break;
        case 3:
        case 4:
            alignment = duskTypeSizeOf(allocator, type->vector.sub, layout) * 4;
            break;
        default: DUSK_ASSERT(0); break;
        }
        break;
    }

    case DUSK_TYPE_MATRIX: {
        alignment = duskTypeAlignOf(allocator, type->matrix.col_type, layout);
        break;
    }

    case DUSK_TYPE_RUNTIME_ARRAY:
    case DUSK_TYPE_ARRAY: {
        switch (layout) {
        case DUSK_STRUCT_LAYOUT_STD140: {
            uint32_t elem_alignment =
                duskTypeAlignOf(allocator, type->array.sub, layout);
            alignment = DUSK_ROUND_UP(16, elem_alignment);
            break;
        }

        case DUSK_STRUCT_LAYOUT_UNKNOWN:
        case DUSK_STRUCT_LAYOUT_STD430: {
            alignment = duskTypeAlignOf(allocator, type->array.sub, layout);
            break;
        }
        }
        break;
    }

    case DUSK_TYPE_STRUCT: {
        for (size_t i = 0; i < type->struct_.field_count; ++i) {
            DuskType *field_type = type->struct_.field_types[i];
            uint32_t field_align =
                duskTypeAlignOf(allocator, field_type, type->struct_.layout);
            if (alignment < field_align) {
                alignment = field_align;
            }
        }

        if (layout == DUSK_STRUCT_LAYOUT_STD140) {
            alignment = DUSK_ROUND_UP(16, alignment);
        }

        break;
    }

    case DUSK_TYPE_TYPE:
    case DUSK_TYPE_VOID:
    case DUSK_TYPE_FUNCTION:
    case DUSK_TYPE_STRING:
    case DUSK_TYPE_UNTYPED_FLOAT:
    case DUSK_TYPE_UNTYPED_INT:
    case DUSK_TYPE_SAMPLER:
    case DUSK_TYPE_SAMPLED_IMAGE:
    case DUSK_TYPE_IMAGE:
    case DUSK_TYPE_POINTER: break;
    }

    return alignment;
}

uint32_t duskTypeSizeOf(
    DuskAllocator *allocator, DuskType *type, DuskStructLayout layout)
{
    uint32_t size = 0;
    switch (type->kind) {
    case DUSK_TYPE_BOOL: size = 1; break;
    case DUSK_TYPE_INT: size = type->int_.bits / 8; break;
    case DUSK_TYPE_FLOAT: size = type->float_.bits / 8; break;

    case DUSK_TYPE_VECTOR: {
        size = duskTypeSizeOf(allocator, type->vector.sub, layout) *
               type->vector.size;
        break;
    }

    case DUSK_TYPE_MATRIX: {
        size = duskTypeSizeOf(allocator, type->matrix.col_type, layout) *
               type->matrix.cols;
        break;
    }

    case DUSK_TYPE_ARRAY: {
        switch (layout) {
        case DUSK_STRUCT_LAYOUT_STD140: {
            uint32_t elem_size =
                duskTypeSizeOf(allocator, type->array.sub, layout);
            elem_size = DUSK_ROUND_UP(16, elem_size);
            uint32_t elem_alignment =
                duskTypeAlignOf(allocator, type->array.sub, layout);
            size = DUSK_ROUND_UP(elem_alignment, elem_size) * type->array.size;
            break;
        }

        case DUSK_STRUCT_LAYOUT_UNKNOWN:
        case DUSK_STRUCT_LAYOUT_STD430: {
            uint32_t elem_size =
                duskTypeSizeOf(allocator, type->array.sub, layout);
            uint32_t elem_alignment =
                duskTypeAlignOf(allocator, type->array.sub, layout);
            size = DUSK_ROUND_UP(elem_alignment, elem_size) * type->array.size;
            break;
        }
        }
        break;
    }

    case DUSK_TYPE_STRUCT: {
        uint32_t struct_alignment = duskTypeAlignOf(allocator, type, layout);

        if (!type->struct_.field_decoration_arrays) {
            type->struct_.field_decoration_arrays = DUSK_NEW_ARRAY(
                allocator,
                DuskArray(DuskIRDecoration),
                type->struct_.field_count);
        }

        for (size_t i = 0; i < type->struct_.field_count; ++i) {
            DuskType *field_type = type->struct_.field_types[i];
            uint32_t field_align =
                duskTypeAlignOf(allocator, field_type, type->struct_.layout);
            size = DUSK_ROUND_UP(field_align, size);

            if (!type->struct_.field_decoration_arrays[i]) {
                type->struct_.field_decoration_arrays[i] =
                    duskArrayCreate(allocator, DuskIRDecoration);
            }

            // Store the field offset
            DuskIRDecoration decoration = duskIRCreateDecoration(
                allocator, DUSK_IR_DECORATION_OFFSET, 1, &size);
            duskArrayPush(
                &type->struct_.field_decoration_arrays[i], decoration);

            uint32_t field_size =
                duskTypeSizeOf(allocator, field_type, type->struct_.layout);
            size += field_size;
        }

        size = DUSK_ROUND_UP(struct_alignment, size);
        break;
    }

    case DUSK_TYPE_RUNTIME_ARRAY: {
        size = 0;
        break;
    }

    case DUSK_TYPE_TYPE:
    case DUSK_TYPE_VOID:
    case DUSK_TYPE_FUNCTION:
    case DUSK_TYPE_STRING:
    case DUSK_TYPE_UNTYPED_FLOAT:
    case DUSK_TYPE_UNTYPED_INT:
    case DUSK_TYPE_SAMPLER:
    case DUSK_TYPE_SAMPLED_IMAGE:
    case DUSK_TYPE_IMAGE:
    case DUSK_TYPE_POINTER: break;
    }

    return size;
}

static void duskReferenceGlobalOperands(void *user_data, DuskIRValue *operand)
{
    DuskIREntryPoint *entry_point = (DuskIREntryPoint *)user_data;
    if (operand->kind == DUSK_IR_VALUE_VARIABLE) {
        switch (operand->var.storage_class) {
        case DUSK_STORAGE_CLASS_PUSH_CONSTANT:
        case DUSK_STORAGE_CLASS_UNIFORM:
        case DUSK_STORAGE_CLASS_UNIFORM_CONSTANT:
        case DUSK_STORAGE_CLASS_STORAGE:
        case DUSK_STORAGE_CLASS_WORKGROUP: {
            duskIREntryPointReferenceGlobal(entry_point, operand);
            break;
        }

        case DUSK_STORAGE_CLASS_PARAMETER:
        case DUSK_STORAGE_CLASS_FUNCTION:
        case DUSK_STORAGE_CLASS_INPUT:
        case DUSK_STORAGE_CLASS_OUTPUT: break;
        }
    }
}

static void duskWithOperands(
    DuskIRValue **values,
    size_t value_count,
    void *user_data,
    void (*callback)(void *user_data, DuskIRValue *operand))
{
    for (size_t i = 0; i < value_count; ++i) {
        DuskIRValue *value = values[i];
        switch (value->kind) {
        case DUSK_IR_VALUE_CONSTANT_BOOL:
        case DUSK_IR_VALUE_CONSTANT:
        case DUSK_IR_VALUE_CONSTANT_COMPOSITE:
        case DUSK_IR_VALUE_FUNCTION:
        case DUSK_IR_VALUE_FUNCTION_PARAMETER:
        case DUSK_IR_VALUE_BLOCK:
        case DUSK_IR_VALUE_VARIABLE:
        case DUSK_IR_VALUE_DISCARD:
        case DUSK_IR_VALUE_BRANCH:
        case DUSK_IR_VALUE_SELECTION_MERGE:
        case DUSK_IR_VALUE_LOOP_MERGE: break;

        case DUSK_IR_VALUE_RETURN: {
            if (value->return_.value) callback(user_data, value->return_.value);
            break;
        }
        case DUSK_IR_VALUE_STORE: {
            callback(user_data, value->store.value);
            callback(user_data, value->store.pointer);
            break;
        }
        case DUSK_IR_VALUE_LOAD: {
            callback(user_data, value->load.pointer);
            break;
        }
        case DUSK_IR_VALUE_FUNCTION_CALL: {
            for (size_t j = 0;
                 j < duskArrayLength(value->function_call.params_arr);
                 j++) {
                DuskIRValue *param = value->function_call.params_arr[j];
                callback(user_data, param);
            }
            break;
        }
        case DUSK_IR_VALUE_ACCESS_CHAIN: {
            callback(user_data, value->access_chain.base);
            break;
        }
        case DUSK_IR_VALUE_COMPOSITE_EXTRACT: {
            callback(user_data, value->composite_extract.composite);
            break;
        }
        case DUSK_IR_VALUE_VECTOR_SHUFFLE: {
            callback(user_data, value->vector_shuffle.vec1);
            callback(user_data, value->vector_shuffle.vec2);
            break;
        }
        case DUSK_IR_VALUE_COMPOSITE_CONSTRUCT: {
            for (size_t j = 0;
                 j < duskArrayLength(value->composite_construct.values_arr);
                 j++) {
                DuskIRValue *component =
                    value->composite_construct.values_arr[j];
                callback(user_data, component);
            }
            break;
        }
        case DUSK_IR_VALUE_CAST: {
            callback(user_data, value->cast.value);
            break;
        }
        case DUSK_IR_VALUE_BUILTIN_CALL: {
            for (size_t j = 0; j < value->builtin_call.param_count; j++) {
                DuskIRValue *param = value->builtin_call.params[j];
                callback(user_data, param);
            }
            break;
        }
        case DUSK_IR_VALUE_BINARY_OPERATION: {
            callback(user_data, value->binary.left);
            callback(user_data, value->binary.right);
            break;
        }
        case DUSK_IR_VALUE_UNARY_OPERATION: {
            callback(user_data, value->unary.right);
            break;
        }
        case DUSK_IR_VALUE_BRANCH_COND: {
            callback(user_data, value->branch_cond.cond);
            break;
        }
        case DUSK_IR_VALUE_PHI: {
            for (size_t j = 0; j < value->phi.pair_count; j++) {
                DuskIRPhiPair pair = value->phi.pairs[j];
                callback(user_data, pair.value);
            }
            break;
        }
        case DUSK_IR_VALUE_ARRAY_LENGTH: {
            callback(user_data, value->array_length.struct_ptr);
            break;
        }
        }
    }
}

static void duskDecorateFromAttributes(
    DuskIRModule *module,
    DuskArray(DuskIRDecoration) * decorations_arr,
    size_t attrib_count,
    DuskAttribute *attributes)
{
    if (*decorations_arr == NULL) {
        *decorations_arr = duskArrayCreate(module->allocator, DuskIRDecoration);
    }

    for (size_t i = 0; i < attrib_count; ++i) {
        DuskAttribute *attribute = &attributes[i];
        switch (attribute->kind) {
        case DUSK_ATTRIBUTE_LOCATION: {
            DUSK_ASSERT(attribute->value_expr_count == 1);
            DUSK_ASSERT(attribute->value_exprs[0]->resolved_int);
            uint32_t location =
                (uint32_t)*attribute->value_exprs[0]->resolved_int;

            DuskIRDecoration decoration = duskIRCreateDecoration(
                module->allocator, DUSK_IR_DECORATION_LOCATION, 1, &location);
            duskArrayPush(decorations_arr, decoration);
            break;
        }
        case DUSK_ATTRIBUTE_SET: {
            DUSK_ASSERT(attribute->value_expr_count == 1);
            DUSK_ASSERT(attribute->value_exprs[0]->resolved_int);
            uint32_t descriptor_set =
                (uint32_t)*attribute->value_exprs[0]->resolved_int;

            DuskIRDecoration decoration = duskIRCreateDecoration(
                module->allocator, DUSK_IR_DECORATION_SET, 1, &descriptor_set);
            duskArrayPush(decorations_arr, decoration);
            break;
        }
        case DUSK_ATTRIBUTE_BINDING: {
            DUSK_ASSERT(attribute->value_expr_count == 1);
            DUSK_ASSERT(attribute->value_exprs[0]->resolved_int);
            uint32_t binding =
                (uint32_t)*attribute->value_exprs[0]->resolved_int;

            DuskIRDecoration decoration = duskIRCreateDecoration(
                module->allocator, DUSK_IR_DECORATION_BINDING, 1, &binding);
            duskArrayPush(decorations_arr, decoration);
            break;
        }
        case DUSK_ATTRIBUTE_OFFSET: {
            DUSK_ASSERT(attribute->value_expr_count == 1);
            DUSK_ASSERT(attribute->value_exprs[0]->resolved_int);
            uint32_t offset =
                (uint32_t)*attribute->value_exprs[0]->resolved_int;

            DuskIRDecoration decoration = duskIRCreateDecoration(
                module->allocator, DUSK_IR_DECORATION_OFFSET, 1, &offset);
            duskArrayPush(decorations_arr, decoration);
            break;
        }
        case DUSK_ATTRIBUTE_READ_ONLY: {
            DUSK_ASSERT(attribute->value_expr_count == 0);

            DuskIRDecoration decoration = duskIRCreateDecoration(
                module->allocator, DUSK_IR_DECORATION_NON_WRITABLE, 0, NULL);
            duskArrayPush(decorations_arr, decoration);
            break;
        }
        case DUSK_ATTRIBUTE_BUILTIN: {
            DUSK_ASSERT(attribute->value_expr_count == 1);
            DUSK_ASSERT(attribute->value_exprs[0]->kind == DUSK_EXPR_IDENT);
            const char *builtin_name = attribute->value_exprs[0]->string.str;

            uint32_t builtin = 0;
            if (strcmp(builtin_name, "position") == 0) {
                builtin = SpvBuiltInPosition;
            } else if (strcmp(builtin_name, "frag_coord") == 0) {
                builtin = SpvBuiltInFragCoord;
            } else if (strcmp(builtin_name, "vertex_id") == 0) {
                builtin = SpvBuiltInVertexId;
            } else if (strcmp(builtin_name, "vertex_index") == 0) {
                builtin = SpvBuiltInVertexIndex;
            } else if (strcmp(builtin_name, "instance_id") == 0) {
                builtin = SpvBuiltInInstanceId;
            } else if (strcmp(builtin_name, "instance_index") == 0) {
                builtin = SpvBuiltInInstanceIndex;
            } else if (strcmp(builtin_name, "frag_depth") == 0) {
                builtin = SpvBuiltInFragDepth;
            } else if (strcmp(builtin_name, "num_workgroups") == 0) {
                builtin = SpvBuiltInNumWorkgroups;
            } else if (strcmp(builtin_name, "workgroup_size") == 0) {
                builtin = SpvBuiltInWorkgroupSize;
            } else if (strcmp(builtin_name, "workgroup_id") == 0) {
                builtin = SpvBuiltInWorkgroupId;
            } else if (strcmp(builtin_name, "local_invocation_id") == 0) {
                builtin = SpvBuiltInLocalInvocationId;
            } else if (strcmp(builtin_name, "local_invocation_index") == 0) {
                builtin = SpvBuiltInLocalInvocationIndex;
            } else if (strcmp(builtin_name, "global_invocation_id") == 0) {
                builtin = SpvBuiltInGlobalInvocationId;
            } else {
                DUSK_ASSERT(0);
            }

            DuskIRDecoration decoration = duskIRCreateDecoration(
                module->allocator, DUSK_IR_DECORATION_BUILTIN, 1, &builtin);
            duskArrayPush(decorations_arr, decoration);
            break;
        }
        default: break;
        }
    }
}

static void
duskGenerateExpr(DuskIRModule *module, DuskDecl *func_decl, DuskExpr *expr)
{
    switch (expr->kind) {
    case DUSK_EXPR_IDENT: {
        DUSK_ASSERT(expr->identifier.decl);
        DUSK_ASSERT(expr->type);
        expr->ir_value = expr->identifier.decl->ir_value;
        break;
    }

    case DUSK_EXPR_INT_LITERAL: {
        DUSK_ASSERT(
            expr->type->kind != DUSK_TYPE_UNTYPED_INT &&
            expr->type->kind != DUSK_TYPE_UNTYPED_FLOAT);
        expr->ir_value = duskIRConstIntCreate(
            module, expr->type, (uint64_t)expr->int_literal);
        break;
    }

    case DUSK_EXPR_FLOAT_LITERAL: {
        DUSK_ASSERT(
            expr->type->kind != DUSK_TYPE_UNTYPED_INT &&
            expr->type->kind != DUSK_TYPE_UNTYPED_FLOAT);
        expr->ir_value = duskIRConstFloatCreate(
            module, expr->type, (double)expr->float_literal);
        break;
    }

    case DUSK_EXPR_BOOL_LITERAL: {
        expr->ir_value = duskIRConstBoolCreate(module, expr->bool_literal);
        break;
    }

    case DUSK_EXPR_STRUCT_LITERAL: {
        DuskType *struct_type = expr->type;
        size_t field_value_count =
            duskArrayLength(expr->struct_literal.field_values_arr);
        DuskIRValue **field_values = duskAllocateZeroed(
            module->allocator, sizeof(DuskIRValue *) * field_value_count);

        for (size_t i = 0; i < field_value_count; ++i) {
            const char *field_name = expr->struct_literal.field_names_arr[i];
            uintptr_t index;
            if (duskMapGet(
                    struct_type->struct_.index_map,
                    field_name,
                    (void *)&index)) {
                duskGenerateExpr(
                    module,
                    func_decl,
                    expr->struct_literal.field_values_arr[i]);
                field_values[index] =
                    expr->struct_literal.field_values_arr[i]->ir_value;
                DUSK_ASSERT(field_values[index]);
            } else {
                DUSK_ASSERT(0);
            }
        }

        bool all_fields_constant = true;
        for (size_t i = 0; i < field_value_count; ++i) {
            DUSK_ASSERT(field_values[i]);
            if (!duskIRValueIsConstant(field_values[i])) {
                all_fields_constant = false;
            }
        }

        if (all_fields_constant) {
            expr->ir_value = duskIRConstCompositeCreate(
                module, struct_type, field_value_count, field_values);
        } else {
            DuskIRValue *function = func_decl->ir_value;
            DUSK_ASSERT(duskArrayLength(function->function.blocks_arr) > 0);

            DuskIRValue *block = duskGetLastBlock(function);

            for (size_t i = 0; i < field_value_count; ++i) {
                field_values[i] =
                    duskIRLoadLvalue(module, block, field_values[i]);
            }

            expr->ir_value = duskIRCreateCompositeConstruct(
                module, block, struct_type, field_value_count, field_values);
        }

        break;
    }

    case DUSK_EXPR_FUNCTION_CALL: {
        DuskType *func_type = expr->function_call.func_expr->type;
        switch (func_type->kind) {
        case DUSK_TYPE_FUNCTION: {
            duskGenerateExpr(module, func_decl, expr->function_call.func_expr);

            size_t param_count =
                duskArrayLength(expr->function_call.params_arr);
            DuskIRValue **param_values =
                DUSK_NEW_ARRAY(module->allocator, DuskIRValue *, param_count);

            for (size_t i = 0; i < param_count; ++i) {
                DuskExpr *param_expr = expr->function_call.params_arr[i];
                duskGenerateExpr(module, func_decl, param_expr);
                DUSK_ASSERT(param_expr->ir_value);
                param_values[i] = param_expr->ir_value;
            }

            DuskIRValue *function = func_decl->ir_value;
            DUSK_ASSERT(duskArrayLength(function->function.blocks_arr) > 0);
            DuskIRValue *block = duskGetLastBlock(function);

            expr->ir_value = duskIRCreateFunctionCall(
                module,
                block,
                expr->function_call.func_expr->ir_value,
                param_count,
                param_values);
            break;
        }

        case DUSK_TYPE_TYPE: {
            DuskType *constructed_type = expr->function_call.func_expr->as_type;
            size_t param_count =
                duskArrayLength(expr->function_call.params_arr);

            switch (constructed_type->kind) {
            case DUSK_TYPE_INT:
            case DUSK_TYPE_FLOAT: {
                DUSK_ASSERT(param_count == 1);
                DuskExpr *param = expr->function_call.params_arr[0];

                duskGenerateExpr(module, func_decl, param);
                DuskIRValue *value = param->ir_value;
                if (duskIRIsLvalue(value)) {
                    DUSK_ASSERT(func_decl);
                    DuskIRValue *function = func_decl->ir_value;
                    DUSK_ASSERT(
                        duskArrayLength(function->function.blocks_arr) > 0);
                    DuskIRValue *block = duskGetLastBlock(function);

                    value = duskIRLoadLvalue(module, block, value);
                }

                if (constructed_type != param->type) {
                    DUSK_ASSERT(func_decl);
                    DuskIRValue *function = func_decl->ir_value;
                    DUSK_ASSERT(
                        duskArrayLength(function->function.blocks_arr) > 0);
                    DuskIRValue *block = duskGetLastBlock(function);

                    expr->ir_value = duskIRCreateCast(
                        module, block, constructed_type, value);
                } else {
                    expr->ir_value = value;
                }

                break;
            }
            case DUSK_TYPE_VECTOR: {
                size_t value_count = constructed_type->vector.size;
                DuskIRValue **values = duskAllocateZeroed(
                    module->allocator, sizeof(DuskIRValue *) * value_count);

                if (func_decl) {
                    DuskIRValue *function = func_decl->ir_value;
                    DUSK_ASSERT(
                        duskArrayLength(function->function.blocks_arr) > 0);
                    DuskIRValue *block = duskGetLastBlock(function);

                    bool all_constants = true;
                    if (param_count == 1 && value_count != param_count) {
                        DuskExpr *param = expr->function_call.params_arr[0];
                        duskGenerateExpr(module, func_decl, param);
                        DuskIRValue *param_value = param->ir_value;
                        if (!duskIRValueIsConstant(param_value)) {
                            param_value = duskIRLoadLvalue(
                                module, block, param->ir_value);
                            all_constants = false;
                        }

                        for (size_t i = 0; i < value_count; ++i) {
                            values[i] = param_value;
                        }
                    } else {
                        // Mixed vector constructor
                        DUSK_ASSERT(constructed_type->kind == DUSK_TYPE_VECTOR);

                        size_t elem_index = 0;
                        for (size_t i = 0; i < param_count; ++i) {
                            DuskExpr *param = expr->function_call.params_arr[i];
                            duskGenerateExpr(module, func_decl, param);

                            if (param->type->kind == DUSK_TYPE_VECTOR) {
                                all_constants = false;
                                DuskIRValue *loaded_composite =
                                    duskIRLoadLvalue(
                                        module, block, param->ir_value);
                                for (uint32_t j = 0;
                                     j < param->type->vector.size;
                                     ++j) {
                                    values[elem_index] =
                                        duskIRCreateCompositeExtract(
                                            module,
                                            block,
                                            param->type->vector.sub,
                                            loaded_composite,
                                            1,
                                            &j);

                                    elem_index++;
                                }
                            } else {
                                values[elem_index] = param->ir_value;
                                if (!duskIRValueIsConstant(
                                        values[elem_index])) {
                                    values[elem_index] = duskIRLoadLvalue(
                                        module, block, values[elem_index]);
                                    all_constants = false;
                                }

                                elem_index++;
                            }
                        }
                    }

                    if (all_constants) {
                        expr->ir_value = duskIRConstCompositeCreate(
                            module, constructed_type, value_count, values);
                    } else {
                        expr->ir_value = duskIRCreateCompositeConstruct(
                            module,
                            block,
                            constructed_type,
                            value_count,
                            values);
                    }
                } else {
                    // Not inside a function, must be a constant

                    if (param_count == value_count) {
                        for (size_t i = 0; i < value_count; ++i) {
                            DuskExpr *param = expr->function_call.params_arr[i];
                            duskGenerateExpr(module, func_decl, param);
                            values[i] = param->ir_value;
                            DUSK_ASSERT(duskIRValueIsConstant(values[i]));
                        }
                    } else if (param_count == 1) {
                        DuskExpr *param = expr->function_call.params_arr[0];
                        duskGenerateExpr(module, func_decl, param);
                        DuskIRValue *param_value = param->ir_value;
                        DUSK_ASSERT(duskIRValueIsConstant(param_value));

                        for (size_t i = 0; i < value_count; ++i) {
                            values[i] = param_value;
                        }
                    } else {
                        DUSK_ASSERT(0);
                    }

                    expr->ir_value = duskIRConstCompositeCreate(
                        module, constructed_type, value_count, values);
                }
                break;
            }
            case DUSK_TYPE_MATRIX: {
                size_t value_count = constructed_type->matrix.cols;
                DuskIRValue **values = duskAllocateZeroed(
                    module->allocator, sizeof(DuskIRValue *) * value_count);

                if (func_decl) {
                    DuskIRValue *function = func_decl->ir_value;
                    DUSK_ASSERT(
                        duskArrayLength(function->function.blocks_arr) > 0);
                    DuskIRValue *block = duskGetLastBlock(function);

                    bool all_constants = true;
                    if (param_count == 1 && value_count != param_count) {
                        DuskExpr *param = expr->function_call.params_arr[0];
                        duskGenerateExpr(module, func_decl, param);
                        DuskIRValue *param_value = param->ir_value;
                        if (!duskIRValueIsConstant(param_value)) {
                            param_value = duskIRLoadLvalue(
                                module, block, param->ir_value);
                            all_constants = false;
                        }

                        for (size_t i = 0; i < value_count; ++i) {
                            values[i] = param_value;
                        }
                    } else if (param_count == value_count) {
                        for (size_t i = 0; i < param_count; ++i) {
                            DuskExpr *param = expr->function_call.params_arr[i];
                            duskGenerateExpr(module, func_decl, param);
                            values[i] = param->ir_value;
                            if (!duskIRValueIsConstant(values[i])) {
                                values[i] =
                                    duskIRLoadLvalue(module, block, values[i]);
                                all_constants = false;
                            }
                        }
                    } else {
                        DUSK_ASSERT(0);
                    }

                    if (all_constants) {
                        expr->ir_value = duskIRConstCompositeCreate(
                            module, constructed_type, value_count, values);
                    } else {
                        expr->ir_value = duskIRCreateCompositeConstruct(
                            module,
                            block,
                            constructed_type,
                            value_count,
                            values);
                    }
                } else {
                    // Not inside a function, must be a constant

                    if (param_count == value_count) {
                        for (size_t i = 0; i < value_count; ++i) {
                            DuskExpr *param = expr->function_call.params_arr[i];
                            duskGenerateExpr(module, func_decl, param);
                            values[i] = param->ir_value;
                            DUSK_ASSERT(duskIRValueIsConstant(values[i]));
                        }
                    } else if (param_count == 1) {
                        DuskExpr *param = expr->function_call.params_arr[0];
                        duskGenerateExpr(module, func_decl, param);
                        DuskIRValue *param_value = param->ir_value;
                        DUSK_ASSERT(duskIRValueIsConstant(param_value));

                        for (size_t i = 0; i < value_count; ++i) {
                            values[i] = param_value;
                        }
                    } else {
                        DUSK_ASSERT(0);
                    }

                    expr->ir_value = duskIRConstCompositeCreate(
                        module, constructed_type, value_count, values);
                }
                break;
            }
            default: DUSK_ASSERT(0); break;
            }

            break;
        }

        default: DUSK_ASSERT(0); break;
        }

        break;
    }

    case DUSK_EXPR_BUILTIN_FUNCTION_CALL: {
        switch (expr->builtin_call.kind) {
        case DUSK_BUILTIN_FUNCTION_RADIANS:
        case DUSK_BUILTIN_FUNCTION_DEGREES:
        case DUSK_BUILTIN_FUNCTION_ROUND:
        case DUSK_BUILTIN_FUNCTION_TRUNC:
        case DUSK_BUILTIN_FUNCTION_FLOOR:
        case DUSK_BUILTIN_FUNCTION_CEIL:
        case DUSK_BUILTIN_FUNCTION_FRACT:
        case DUSK_BUILTIN_FUNCTION_SQRT:
        case DUSK_BUILTIN_FUNCTION_INVERSE_SQRT:
        case DUSK_BUILTIN_FUNCTION_LOG:
        case DUSK_BUILTIN_FUNCTION_LOG2:
        case DUSK_BUILTIN_FUNCTION_EXP:
        case DUSK_BUILTIN_FUNCTION_EXP2:

        case DUSK_BUILTIN_FUNCTION_SIN:
        case DUSK_BUILTIN_FUNCTION_COS:
        case DUSK_BUILTIN_FUNCTION_TAN:
        case DUSK_BUILTIN_FUNCTION_ASIN:
        case DUSK_BUILTIN_FUNCTION_ACOS:
        case DUSK_BUILTIN_FUNCTION_ATAN:
        case DUSK_BUILTIN_FUNCTION_SINH:
        case DUSK_BUILTIN_FUNCTION_COSH:
        case DUSK_BUILTIN_FUNCTION_TANH:
        case DUSK_BUILTIN_FUNCTION_ASINH:
        case DUSK_BUILTIN_FUNCTION_ACOSH:
        case DUSK_BUILTIN_FUNCTION_ATANH:

        case DUSK_BUILTIN_FUNCTION_ABS:
        case DUSK_BUILTIN_FUNCTION_DISTANCE:
        case DUSK_BUILTIN_FUNCTION_NORMALIZE:
        case DUSK_BUILTIN_FUNCTION_DOT:
        case DUSK_BUILTIN_FUNCTION_LENGTH:
        case DUSK_BUILTIN_FUNCTION_CROSS:
        case DUSK_BUILTIN_FUNCTION_REFLECT:
        case DUSK_BUILTIN_FUNCTION_REFRACT:
        case DUSK_BUILTIN_FUNCTION_MIN:
        case DUSK_BUILTIN_FUNCTION_MAX:
        case DUSK_BUILTIN_FUNCTION_MIX:
        case DUSK_BUILTIN_FUNCTION_CLAMP:
        case DUSK_BUILTIN_FUNCTION_DETERMINANT:
        case DUSK_BUILTIN_FUNCTION_INVERSE: {
            DUSK_ASSERT(func_decl);
            DuskIRValue *function = func_decl->ir_value;
            DUSK_ASSERT(duskArrayLength(function->function.blocks_arr) > 0);
            DuskIRValue *block = duskGetLastBlock(function);

            size_t param_count = duskArrayLength(expr->builtin_call.params_arr);
            DuskIRValue **params =
                DUSK_NEW_ARRAY(module->allocator, DuskIRValue *, param_count);

            for (size_t i = 0; i < param_count; ++i) {
                duskGenerateExpr(
                    module, func_decl, expr->builtin_call.params_arr[i]);
                params[i] = expr->builtin_call.params_arr[i]->ir_value;
                params[i] = duskIRLoadLvalue(module, block, params[i]);
            }

            expr->ir_value = duskIRCreateBuiltinCall(
                module,
                block,
                expr->builtin_call.kind,
                expr->type,
                param_count,
                params);
            break;
        }
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
        case DUSK_BUILTIN_FUNCTION_SAMPLER_TYPE: {
            DUSK_ASSERT(!"not a runtime value");
            break;
        }
        case DUSK_BUILTIN_FUNCTION_COUNT: DUSK_ASSERT(0); break;
        }
        break;
    }

    case DUSK_EXPR_ACCESS: {
        DuskIRValue *function = func_decl->ir_value;
        DUSK_ASSERT(duskArrayLength(function->function.blocks_arr) > 0);
        DuskIRValue *block = duskGetLastBlock(function);

        DuskExpr *left_expr = expr->access.base_expr;
        duskGenerateExpr(module, func_decl, left_expr);

        for (size_t i = 0; i < duskArrayLength(expr->access.chain_arr); ++i) {
            DUSK_ASSERT(left_expr->ir_value);

            DuskExpr *right_expr = expr->access.chain_arr[i];
            const char *accessed_field_name = right_expr->identifier.str;

            switch (left_expr->type->kind) {
            case DUSK_TYPE_VECTOR: {
                DUSK_ASSERT(right_expr->kind == DUSK_EXPR_IDENT);
                DUSK_ASSERT(right_expr->identifier.shuffle_indices_arr);

                DuskArray(uint32_t) indices_arr =
                    right_expr->identifier.shuffle_indices_arr;
                size_t index_count = duskArrayLength(indices_arr);

                if (index_count > 1) {
                    DuskIRValue *vec_value =
                        duskIRLoadLvalue(module, block, left_expr->ir_value);
                    right_expr->ir_value = duskIRCreateVectorShuffle(
                        module,
                        block,
                        vec_value,
                        vec_value,
                        index_count,
                        indices_arr);
                } else {
                    DUSK_ASSERT(index_count == 1);

                    if (duskIRIsLvalue(left_expr->ir_value)) {
                        DuskIRValue *index_value = duskIRConstIntCreate(
                            module,
                            duskTypeNewScalar(
                                module->compiler, DUSK_SCALAR_TYPE_UINT),
                            indices_arr[0]);

                        right_expr->ir_value = duskIRCreateAccessChain(
                            module,
                            block,
                            right_expr->type,
                            left_expr->ir_value,
                            1,
                            &index_value);
                    } else {
                        DuskIRValue *vec_value = duskIRLoadLvalue(
                            module, block, left_expr->ir_value);
                        right_expr->ir_value = duskIRCreateCompositeExtract(
                            module,
                            block,
                            left_expr->type->vector.sub,
                            vec_value,
                            index_count,
                            indices_arr);
                    }
                }

                break;
            }
            case DUSK_TYPE_STRUCT: {
                uintptr_t field_index = 0;
                if (!duskMapGet(
                        left_expr->type->struct_.index_map,
                        right_expr->identifier.str,
                        (void *)&field_index)) {
                    DUSK_ASSERT(0);
                }

                if (duskIRIsLvalue(left_expr->ir_value)) {
                    DuskIRValue *index_value = duskIRConstIntCreate(
                        module,
                        duskTypeNewScalar(
                            module->compiler, DUSK_SCALAR_TYPE_UINT),
                        field_index);

                    right_expr->ir_value = duskIRCreateAccessChain(
                        module,
                        block,
                        right_expr->type,
                        left_expr->ir_value,
                        1,
                        &index_value);
                } else {
                    uint32_t index = (uint32_t)field_index;
                    DuskIRValue *struct_value =
                        duskIRLoadLvalue(module, block, left_expr->ir_value);
                    right_expr->ir_value = duskIRCreateCompositeExtract(
                        module,
                        block,
                        left_expr->type->struct_.field_types[field_index],
                        struct_value,
                        1,
                        &index);
                }

                break;
            }
            case DUSK_TYPE_RUNTIME_ARRAY: {
                if (strcmp(accessed_field_name, "len") == 0) {
                    DuskExpr *struct_expr = NULL;
                    if (i >= 2) {
                        struct_expr = expr->access.chain_arr[i - 2];
                    } else if (i >= 1) {
                        struct_expr = expr->access.base_expr;
                    } else {
                        DUSK_ASSERT(0);
                    }

                    DuskIRValue *struct_ptr = struct_expr->ir_value;
                    DUSK_ASSERT(struct_ptr->type->kind == DUSK_TYPE_POINTER);
                    DUSK_ASSERT(
                        struct_ptr->type->pointer.sub->kind ==
                        DUSK_TYPE_STRUCT);

                    DuskType *struct_ty = struct_ptr->type->pointer.sub;

                    uintptr_t struct_member_index = 0;
                    if (!duskMapGet(
                            struct_ty->struct_.index_map,
                            left_expr->identifier.str,
                            (void *)&struct_member_index)) {
                        DUSK_ASSERT(0);
                    }

                    right_expr->ir_value = duskIRCreateArrayLength(
                        module, block, struct_ptr, struct_member_index);
                } else {
                    DUSK_ASSERT(0);
                }
                break;
            }
            default: {
                DUSK_ASSERT(0);
                break;
            }
            }

            DUSK_ASSERT(right_expr->ir_value);

            left_expr = right_expr;
        }

        expr->ir_value = left_expr->ir_value;

        break;
    }

    case DUSK_EXPR_STRUCT_TYPE: {
        DuskType *type = expr->as_type;

        DUSK_ASSERT(type);
        DUSK_ASSERT(type->kind == DUSK_TYPE_STRUCT);

        if (!type->struct_.field_decoration_arrays) {
            type->struct_.field_decoration_arrays = DUSK_NEW_ARRAY(
                module->allocator,
                DuskArray(DuskIRDecoration),
                type->struct_.field_count);
        }

        for (size_t i = 0; i < type->struct_.field_count; ++i) {
            if (!type->struct_.field_decoration_arrays[i]) {
                type->struct_.field_decoration_arrays[i] =
                    duskArrayCreate(module->allocator, DuskIRDecoration);
            }
            duskDecorateFromAttributes(
                module,
                &type->struct_.field_decoration_arrays[i],
                duskArrayLength(type->struct_.field_attribute_arrays[i]),
                type->struct_.field_attribute_arrays[i]);
        }

        break;
    }

    case DUSK_EXPR_ARRAY_ACCESS: {
        DuskIRValue *function = func_decl->ir_value;
        DUSK_ASSERT(duskArrayLength(function->function.blocks_arr) > 0);
        DuskIRValue *block = duskGetLastBlock(function);

        duskGenerateExpr(module, func_decl, expr->access.base_expr);
        DuskIRValue *base_value = expr->access.base_expr->ir_value;
        DUSK_ASSERT(base_value);

        DuskArray(DuskIRValue *) index_values_arr =
            duskArrayCreate(module->allocator, DuskIRValue *);

        for (size_t i = 0; i < duskArrayLength(expr->access.chain_arr); ++i) {

            DuskExpr *index_expr = expr->access.chain_arr[i];
            duskGenerateExpr(module, func_decl, index_expr);
            DUSK_ASSERT(index_expr->ir_value);

            duskArrayPush(&index_values_arr, index_expr->ir_value);
        }

        DUSK_ASSERT(duskIRIsLvalue(base_value));

        expr->ir_value = duskIRCreateAccessChain(
            module,
            block,
            expr->type,
            base_value,
            duskArrayLength(index_values_arr),
            index_values_arr);

        break;
    }

    case DUSK_EXPR_BINARY: {
        DUSK_ASSERT(func_decl);
        DuskIRValue *function = func_decl->ir_value;
        DUSK_ASSERT(duskArrayLength(function->function.blocks_arr) > 0);
        DuskIRValue *block = duskGetLastBlock(function);

        switch (expr->binary.op) {
        case DUSK_BINARY_OP_MAX:
        case DUSK_BINARY_OP_EQ:
        case DUSK_BINARY_OP_NOTEQ:
        case DUSK_BINARY_OP_GREATER:
        case DUSK_BINARY_OP_GREATEREQ:
        case DUSK_BINARY_OP_LESS:
        case DUSK_BINARY_OP_LESSEQ: {
            duskGenerateExpr(module, func_decl, expr->binary.left);
            duskGenerateExpr(module, func_decl, expr->binary.right);

            DuskIRValue *left_val =
                duskIRLoadLvalue(module, block, expr->binary.left->ir_value);
            DuskIRValue *right_val =
                duskIRLoadLvalue(module, block, expr->binary.right->ir_value);

            expr->ir_value = duskIRCreateBinaryOperation(
                module,
                block,
                expr->binary.op,
                expr->type,
                left_val,
                right_val);
            break;
        }

        case DUSK_BINARY_OP_ADD:
        case DUSK_BINARY_OP_SUB:
        case DUSK_BINARY_OP_MUL:
        case DUSK_BINARY_OP_DIV:
        case DUSK_BINARY_OP_MOD:
        case DUSK_BINARY_OP_BITAND:
        case DUSK_BINARY_OP_BITOR:
        case DUSK_BINARY_OP_BITXOR:
        case DUSK_BINARY_OP_LSHIFT:
        case DUSK_BINARY_OP_RSHIFT: {
            duskGenerateExpr(module, func_decl, expr->binary.left);
            duskGenerateExpr(module, func_decl, expr->binary.right);

            DuskIRValue *left_val =
                duskIRLoadLvalue(module, block, expr->binary.left->ir_value);
            DuskIRValue *right_val =
                duskIRLoadLvalue(module, block, expr->binary.right->ir_value);

            DuskType *left_scalar_type =
                duskGetScalarType(expr->binary.left->type);
            DuskType *right_scalar_type =
                duskGetScalarType(expr->binary.right->type);

            DUSK_ASSERT(left_scalar_type);
            DUSK_ASSERT(right_scalar_type);
            DUSK_ASSERT(left_scalar_type == right_scalar_type);
            DUSK_ASSERT(duskTypeIsRuntime(left_scalar_type));
            DUSK_ASSERT(duskTypeIsRuntime(right_scalar_type));

            expr->ir_value = duskIRCreateBinaryOperation(
                module,
                block,
                expr->binary.op,
                expr->type,
                left_val,
                right_val);
            break;
        }

        case DUSK_BINARY_OP_AND: {
            DuskIRValue *first_cond_true_block = duskIRBlockCreate(module);
            DuskIRValue *merge_block = duskIRBlockCreate(module);

            // First condition
            duskGenerateExpr(module, func_decl, expr->binary.left);
            DuskIRValue *first_cond = duskIRLoadLvalue(
                module,
                duskGetLastBlock(function),
                expr->binary.left->ir_value);
            duskIRCreateSelectionMerge(
                module, duskGetLastBlock(function), merge_block);
            duskIRCreateBranchCond(
                module,
                duskGetLastBlock(function),
                first_cond,
                first_cond_true_block,
                merge_block);

            // First condition is true
            duskIRFunctionAddBlock(function, first_cond_true_block);
            duskGenerateExpr(module, func_decl, expr->binary.right);
            DuskIRValue *second_cond = duskIRLoadLvalue(
                module,
                duskGetLastBlock(function),
                expr->binary.right->ir_value);
            duskIRCreateBranch(module, duskGetLastBlock(function), merge_block);

            // Merge block
            duskIRFunctionAddBlock(function, merge_block);
            DuskIRPhiPair pairs[2] = {
                {block, first_cond},
                {first_cond_true_block, second_cond},
            };
            expr->ir_value =
                duskIRCreatePhi(module, merge_block, expr->type, 2, pairs);
            break;
        }

        case DUSK_BINARY_OP_OR: {
            DuskIRValue *first_cond_false_block = duskIRBlockCreate(module);
            DuskIRValue *merge_block = duskIRBlockCreate(module);

            // First condition
            duskGenerateExpr(module, func_decl, expr->binary.left);
            DuskIRValue *first_cond = duskIRLoadLvalue(
                module,
                duskGetLastBlock(function),
                expr->binary.left->ir_value);
            duskIRCreateSelectionMerge(
                module, duskGetLastBlock(function), merge_block);
            duskIRCreateBranchCond(
                module,
                duskGetLastBlock(function),
                first_cond,
                merge_block,
                first_cond_false_block);

            // First condition false
            duskIRFunctionAddBlock(function, first_cond_false_block);
            duskGenerateExpr(module, func_decl, expr->binary.right);
            DuskIRValue *second_cond = duskIRLoadLvalue(
                module,
                duskGetLastBlock(function),
                expr->binary.right->ir_value);
            duskIRCreateBranch(module, duskGetLastBlock(function), merge_block);

            // Merge block
            duskIRFunctionAddBlock(function, merge_block);
            DuskIRPhiPair pairs[2] = {
                {block, first_cond},
                {first_cond_false_block, second_cond},
            };
            expr->ir_value =
                duskIRCreatePhi(module, merge_block, expr->type, 2, pairs);
            break;
        }
        }

        break;
    }

    case DUSK_EXPR_UNARY: {
        DUSK_ASSERT(func_decl);
        DuskIRValue *function = func_decl->ir_value;
        DUSK_ASSERT(duskArrayLength(function->function.blocks_arr) > 0);
        DuskIRValue *block = duskGetLastBlock(function);

        duskGenerateExpr(module, func_decl, expr->unary.right);

        DuskIRValue *right_val =
            duskIRLoadLvalue(module, block, expr->unary.right->ir_value);

        expr->ir_value = duskIRCreateUnaryOperation(
            module, block, expr->unary.op, expr->type, right_val);
    }

    case DUSK_EXPR_STRING_LITERAL:
    case DUSK_EXPR_BOOL_TYPE:
    case DUSK_EXPR_ARRAY_TYPE:
    case DUSK_EXPR_MATRIX_TYPE:
    case DUSK_EXPR_RUNTIME_ARRAY_TYPE:
    case DUSK_EXPR_SCALAR_TYPE:
    case DUSK_EXPR_VECTOR_TYPE:
    case DUSK_EXPR_VOID_TYPE: break;
    }
}

static void
duskGenerateStmt(DuskIRModule *module, DuskDecl *func_decl, DuskStmt *stmt)
{
    DuskIRValue *function = func_decl->ir_value;

    DUSK_ASSERT(duskArrayLength(function->function.blocks_arr) > 0);
    DuskIRValue *block = duskGetLastBlock(function);

    switch (stmt->kind) {
    case DUSK_STMT_RETURN: {
        if (func_decl->function.is_entry_point) {
            DuskType *return_type = func_decl->type->function.return_type;
            size_t output_count =
                duskArrayLength(func_decl->function.entry_point_outputs_arr);

            switch (return_type->kind) {
            case DUSK_TYPE_VOID: {
                DUSK_ASSERT(output_count == 0);
                break;
            }
            case DUSK_TYPE_STRUCT: {
                DUSK_ASSERT(output_count == return_type->struct_.field_count);

                duskGenerateExpr(module, func_decl, stmt->return_.expr);
                DuskIRValue *struct_value = stmt->return_.expr->ir_value;
                struct_value = duskIRLoadLvalue(
                    module, block, stmt->return_.expr->ir_value);

                for (uint32_t i = 0; i < output_count; ++i) {
                    DuskIRValue *field_value = duskIRCreateCompositeExtract(
                        module,
                        block,
                        return_type->struct_.field_types[i],
                        struct_value,
                        1,
                        &i);
                    DUSK_ASSERT(!duskIRIsLvalue(field_value));

                    duskIRCreateStore(
                        module,
                        block,
                        func_decl->function.entry_point_outputs_arr[i],
                        field_value);
                }
                break;
            }
            default: {
                DUSK_ASSERT(output_count == 1);
                DUSK_ASSERT(stmt->return_.expr);
                DuskIRValue *output_value =
                    func_decl->function.entry_point_outputs_arr[0];

                duskGenerateExpr(module, func_decl, stmt->return_.expr);
                DuskIRValue *returned_value = stmt->return_.expr->ir_value;
                returned_value = duskIRLoadLvalue(
                    module, block, stmt->return_.expr->ir_value);

                duskIRCreateStore(module, block, output_value, returned_value);
                break;
            }
            }

            duskIRCreateReturn(module, block, NULL);
        } else {
            DuskIRValue *returned_value = NULL;
            if (stmt->return_.expr) {
                duskGenerateExpr(module, func_decl, stmt->return_.expr);
                returned_value = stmt->return_.expr->ir_value;
                returned_value = duskIRLoadLvalue(
                    module, block, stmt->return_.expr->ir_value);
                DUSK_ASSERT(returned_value);
            }

            duskIRCreateReturn(module, block, returned_value);
        }
        break;
    }
    case DUSK_STMT_DISCARD: {
        duskIRCreateDiscard(module, block);
        break;
    }
    case DUSK_STMT_DECL: {
        duskGenerateLocalDecl(module, func_decl, stmt->decl);
        break;
    }
    case DUSK_STMT_ASSIGN: {
        duskGenerateExpr(module, func_decl, stmt->assign.assigned_expr);
        duskGenerateExpr(module, func_decl, stmt->assign.value_expr);

        DuskIRValue *pointer = stmt->assign.assigned_expr->ir_value;
        DuskIRValue *value = stmt->assign.value_expr->ir_value;
        value = duskIRLoadLvalue(module, block, value);

        duskIRCreateStore(module, block, pointer, value);
        break;
    }
    case DUSK_STMT_EXPR: {
        duskGenerateExpr(module, func_decl, stmt->expr);
        break;
    }
    case DUSK_STMT_BLOCK: {
        for (size_t i = 0; i < duskArrayLength(stmt->block.stmts_arr); ++i) {
            duskGenerateStmt(module, func_decl, stmt->block.stmts_arr[i]);
        }
        break;
    }
    case DUSK_STMT_IF: {
        // Create blocks
        DuskIRValue *true_block = duskIRBlockCreate(module);
        DuskIRValue *false_block = NULL;
        if (stmt->if_.false_stmt) {
            false_block = duskIRBlockCreate(module);
        }
        DuskIRValue *merge_block = duskIRBlockCreate(module);

        // Generate conditional branch
        duskGenerateExpr(module, func_decl, stmt->if_.cond_expr);
        DuskIRValue *cond = duskIRLoadLvalue(
            module, duskGetLastBlock(function), stmt->if_.cond_expr->ir_value);
        duskIRCreateSelectionMerge(
            module, duskGetLastBlock(function), merge_block);
        duskIRCreateBranchCond(
            module,
            duskGetLastBlock(function),
            cond,
            true_block,
            false_block ? false_block : merge_block);

        // Generate code and add blocks

        duskIRFunctionAddBlock(function, true_block);
        duskGenerateStmt(module, func_decl, stmt->if_.true_stmt);
        duskIRCreateBranch(module, duskGetLastBlock(function), merge_block);

        if (false_block) {
            duskIRFunctionAddBlock(function, false_block);
            duskGenerateStmt(module, func_decl, stmt->if_.false_stmt);
            duskIRCreateBranch(module, duskGetLastBlock(function), merge_block);
        }

        duskIRFunctionAddBlock(function, merge_block);
        break;
    }
    case DUSK_STMT_WHILE: {
        // Create blocks
        DuskIRValue *header_block = duskIRBlockCreate(module);
        DuskIRValue *cond_block = duskIRBlockCreate(module);
        DuskIRValue *body_block = duskIRBlockCreate(module);
        DuskIRValue *continue_block = duskIRBlockCreate(module);
        DuskIRValue *merge_block = duskIRBlockCreate(module);

        duskIRCreateBranch(module, block, header_block);

        // Header block
        duskIRFunctionAddBlock(function, header_block);
        duskIRCreateLoopMerge(
            module, header_block, merge_block, continue_block);
        duskIRCreateBranch(module, header_block, cond_block);

        // Cond block
        duskIRFunctionAddBlock(function, cond_block);
        duskGenerateExpr(module, func_decl, stmt->while_.cond_expr);
        DuskIRValue *cond = duskIRLoadLvalue(
            module,
            duskGetLastBlock(function),
            stmt->while_.cond_expr->ir_value);
        duskIRCreateBranchCond(
            module, duskGetLastBlock(function), cond, body_block, merge_block);

        // Body block
        duskIRFunctionAddBlock(function, body_block);
        duskGenerateStmt(module, func_decl, stmt->while_.stmt);
        duskIRCreateBranch(module, duskGetLastBlock(function), continue_block);

        // Continue block
        duskIRFunctionAddBlock(function, continue_block);
        duskIRCreateBranch(module, continue_block, header_block);

        duskIRFunctionAddBlock(function, merge_block);
        break;
    }
    }
}

static void
duskGenerateLocalDecl(DuskIRModule *module, DuskDecl *func_decl, DuskDecl *decl)
{
    DuskIRValue *function = func_decl->ir_value;
    DuskIRValue *block =
        function->function
            .blocks_arr[duskArrayLength(function->function.blocks_arr) - 1];

    DUSK_ASSERT(decl->type);
    if (decl->type) {
        duskTypeMarkNotDead(decl->type);
    }
    DUSK_ASSERT(decl->type->emit);

    switch (decl->kind) {
    case DUSK_DECL_VAR: {
        DUSK_ASSERT(decl->type);

        bool should_create_var = true;
        switch (decl->type->kind) {
        case DUSK_TYPE_IMAGE:
        case DUSK_TYPE_SAMPLED_IMAGE:
        case DUSK_TYPE_SAMPLER: should_create_var = false; break;
        default: break;
        }

        if (should_create_var) {
            decl->ir_value = duskIRVariableCreate(
                module, decl->type, DUSK_STORAGE_CLASS_FUNCTION);
            duskArrayPush(&function->function.variables_arr, decl->ir_value);
        }

        if (decl->var.value_expr) {
            duskGenerateExpr(module, func_decl, decl->var.value_expr);
            DuskIRValue *assigned_value = decl->var.value_expr->ir_value;

            if (should_create_var) {
                assigned_value =
                    duskIRLoadLvalue(module, block, assigned_value);
                duskIRCreateStore(
                    module, block, decl->ir_value, assigned_value);
            } else {
                decl->ir_value = assigned_value;
            }
        }

        DUSK_ASSERT(decl->ir_value);

        break;
    }
    case DUSK_DECL_FUNCTION:
    case DUSK_DECL_TYPE: DUSK_ASSERT(0); break;
    }
}

static void duskGenerateGlobalDecl(DuskIRModule *module, DuskDecl *decl)
{
    if (decl->type) {
        duskTypeMarkNotDead(decl->type);
    }

    switch (decl->kind) {
    case DUSK_DECL_FUNCTION: {
        DUSK_ASSERT(decl->type);

        DuskType *function_type = decl->type;
        if (decl->function.is_entry_point) {
            // Entry point is a function with no parameters and no return type
            function_type = duskTypeNewFunction(
                module->compiler,
                duskTypeNewBasic(module->compiler, DUSK_TYPE_VOID),
                0,
                NULL);

            decl->function.entry_point_inputs_arr =
                duskArrayCreate(module->allocator, DuskIRValue *);
            decl->function.entry_point_outputs_arr =
                duskArrayCreate(module->allocator, DuskIRValue *);
        }
        decl->ir_value =
            duskIRFunctionCreate(module, function_type, decl->name);
        duskArrayPush(&module->functions_arr, decl->ir_value);

        size_t param_count =
            duskArrayLength(decl->function.parameter_decls_arr);
        if (decl->function.is_entry_point) {
            for (size_t i = 0; i < param_count; ++i) {
                DuskDecl *param_decl = decl->function.parameter_decls_arr[i];

                switch (param_decl->type->kind) {
                case DUSK_TYPE_STRUCT: {
                    size_t field_count = param_decl->type->struct_.field_count;
                    DuskIRValue **field_values = DUSK_NEW_ARRAY(
                        module->allocator, DuskIRValue *, field_count);

                    for (size_t j = 0; j < field_count; ++j) {
                        DuskType *field_type =
                            param_decl->type->struct_.field_types[j];
                        DuskIRValue *input_value = duskIRVariableCreate(
                            module, field_type, DUSK_STORAGE_CLASS_INPUT);
                        duskArrayPush(
                            &decl->function.entry_point_inputs_arr,
                            input_value);

                        DuskArray(DuskAttribute) field_attributes_arr =
                            param_decl->type->struct_.field_attribute_arrays[j];

                        duskDecorateFromAttributes(
                            module,
                            &input_value->decorations_arr,
                            duskArrayLength(field_attributes_arr),
                            field_attributes_arr);

                        field_values[j] = input_value;
                    }

                    DUSK_ASSERT(
                        duskArrayLength(decl->ir_value->function.blocks_arr) >
                        0);

                    DuskIRValue *block = duskGetLastBlock(decl->ir_value);

                    for (size_t j = 0; j < field_count; ++j) {
                        field_values[j] =
                            duskIRLoadLvalue(module, block, field_values[j]);
                    }

                    param_decl->ir_value = duskIRCreateCompositeConstruct(
                        module,
                        block,
                        param_decl->type,
                        field_count,
                        field_values);
                    break;
                }
                default: {
                    param_decl->ir_value = duskIRVariableCreate(
                        module, param_decl->type, DUSK_STORAGE_CLASS_INPUT);
                    duskArrayPush(
                        &decl->function.entry_point_inputs_arr,
                        param_decl->ir_value);

                    duskDecorateFromAttributes(
                        module,
                        &param_decl->ir_value->decorations_arr,
                        duskArrayLength(
                            decl->function.return_type_attributes_arr),
                        decl->function.return_type_attributes_arr);

                    DUSK_ASSERT(param_decl->ir_value);
                    break;
                }
                }
            }

            DuskType *return_type = decl->type->function.return_type;
            switch (return_type->kind) {
            case DUSK_TYPE_VOID: break;
            case DUSK_TYPE_STRUCT: {
                for (size_t i = 0; i < return_type->struct_.field_count; ++i) {
                    DuskType *field_type = return_type->struct_.field_types[i];
                    DuskIRValue *output_value = duskIRVariableCreate(
                        module, field_type, DUSK_STORAGE_CLASS_OUTPUT);
                    duskArrayPush(
                        &decl->function.entry_point_outputs_arr, output_value);

                    DuskArray(DuskAttribute) field_attributes_arr =
                        return_type->struct_.field_attribute_arrays[i];

                    duskDecorateFromAttributes(
                        module,
                        &output_value->decorations_arr,
                        duskArrayLength(field_attributes_arr),
                        field_attributes_arr);
                }
                break;
            }
            default: {
                DuskIRValue *output_value = duskIRVariableCreate(
                    module, return_type, DUSK_STORAGE_CLASS_OUTPUT);
                duskArrayPush(
                    &decl->function.entry_point_outputs_arr, output_value);

                duskDecorateFromAttributes(
                    module,
                    &output_value->decorations_arr,
                    duskArrayLength(decl->function.return_type_attributes_arr),
                    decl->function.return_type_attributes_arr);
                break;
            }
            }
        } else {
            for (size_t i = 0; i < param_count; ++i) {
                DuskDecl *param_decl = decl->function.parameter_decls_arr[i];
                param_decl->ir_value = decl->ir_value->function.params_arr[i];
                DUSK_ASSERT(param_decl->ir_value);
            }
        }

        // Create entry point
        if (decl->function.is_entry_point) {
            DuskArray(DuskIRValue *) referenced_globals_arr =
                duskArrayCreate(module->allocator, DuskIRValue *);

            for (size_t i = 0;
                 i < duskArrayLength(decl->function.entry_point_inputs_arr);
                 ++i) {
                duskArrayPush(
                    &referenced_globals_arr,
                    decl->function.entry_point_inputs_arr[i]);
            }

            for (size_t i = 0;
                 i < duskArrayLength(decl->function.entry_point_outputs_arr);
                 ++i) {
                duskArrayPush(
                    &referenced_globals_arr,
                    decl->function.entry_point_outputs_arr[i]);
            }

            decl->function.entry_point = duskIRModuleAddEntryPoint(
                module,
                decl->ir_value,
                decl->function.link_name,
                decl->function.entry_point_stage,
                duskArrayLength(referenced_globals_arr),
                referenced_globals_arr);
        }

        // Generate function body
        size_t stmt_count = duskArrayLength(decl->function.stmts_arr);
        for (size_t i = 0; i < stmt_count; ++i) {
            DuskStmt *stmt = decl->function.stmts_arr[i];
            duskGenerateStmt(module, decl, stmt);
        }

        // Reference the globals in the function
        if (decl->function.is_entry_point) {
            for (size_t i = 0;
                 i < duskArrayLength(decl->ir_value->function.blocks_arr);
                 ++i) {
                DuskIRValue *block = decl->ir_value->function.blocks_arr[i];

                duskWithOperands(
                    block->block.insts_arr,
                    duskArrayLength(block->block.insts_arr),
                    (void *)decl->function.entry_point,
                    duskReferenceGlobalOperands);
            }
        }

        // Insert void returns where needed
        for (size_t i = 0;
             i < duskArrayLength(decl->ir_value->function.blocks_arr);
             ++i) {
            DuskIRValue *block = decl->ir_value->function.blocks_arr[i];

            if (!duskIRBlockIsTerminated(block)) {
                if (decl->type->function.return_type->kind == DUSK_TYPE_VOID) {
                    duskIRCreateReturn(module, block, NULL);
                } else {
                    DUSK_ASSERT(0); // Missing terminator instruction
                }
            }
        }

        break;
    }
    case DUSK_DECL_VAR: {
        DUSK_ASSERT(decl->type);

        decl->ir_value =
            duskIRVariableCreate(module, decl->type, decl->var.storage_class);

        duskDecorateFromAttributes(
            module,
            &decl->ir_value->decorations_arr,
            duskArrayLength(decl->attributes_arr),
            decl->attributes_arr);

        if (decl->type->kind == DUSK_TYPE_RUNTIME_ARRAY) {
            duskArrayPush(
                &module->extensions_arr, "SPV_EXT_descriptor_indexing");
            duskArrayPush(
                &module->capabilities_arr, SpvCapabilityRuntimeDescriptorArray);
        }

        break;
    }
    case DUSK_DECL_TYPE: break;
    }
}

DuskIRModule *duskGenerateIRModule(DuskCompiler *compiler, DuskFile *file)
{
    (void)file;

    DuskIRModule *module = duskIRModuleCreate(compiler);

    for (size_t i = 0; i < duskArrayLength(file->decls_arr); ++i) {
        DuskDecl *decl = file->decls_arr[i];
        duskGenerateGlobalDecl(module, decl);
    }

    return module;
}
