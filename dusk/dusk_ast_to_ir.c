#include "dusk_internal.h"

static void duskGenerateLocalDecl(
    DuskIRModule *module, DuskIRValue *function, DuskDecl *decl);

static void
duskGenerateExpr(DuskIRModule *module, DuskIRValue *function, DuskExpr *expr)
{
    switch (expr->kind)
    {
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
            duskArrayLength(expr->struct_literal.field_values);
        DuskIRValue **field_values = duskAllocateZeroed(
            module->allocator, sizeof(DuskIRValue *) * field_value_count);

        for (size_t i = 0; i < field_value_count; ++i)
        {
            const char *field_name = expr->struct_literal.field_names[i];
            uintptr_t index;
            if (duskMapGet(
                    struct_type->struct_.index_map, field_name, (void *)&index))
            {
                duskGenerateExpr(
                    module, function, expr->struct_literal.field_values[i]);
                field_values[index] =
                    expr->struct_literal.field_values[i]->ir_value;
                DUSK_ASSERT(field_values[index]);
            }
            else
            {
                DUSK_ASSERT(0);
            }
        }

        bool all_fields_constant = true;
        for (size_t i = 0; i < field_value_count; ++i)
        {
            DUSK_ASSERT(field_values[i]);
            if (!duskIRValueIsConstant(field_values[i]))
            {
                all_fields_constant = false;
            }
        }

        if (all_fields_constant)
        {
            expr->ir_value = duskIRConstCompositeCreate(
                module, struct_type, field_value_count, field_values);
        }
        else
        {
            DUSK_ASSERT(duskArrayLength(function->function.blocks) > 0);
            DuskIRValue *block =
                function->function
                    .blocks[duskArrayLength(function->function.blocks) - 1];

            for (size_t i = 0; i < field_value_count; ++i)
            {
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
        switch (func_type->kind)
        {
        case DUSK_TYPE_FUNCTION: {
            duskGenerateExpr(module, function, expr->function_call.func_expr);

            DuskArray(DuskIRValue *) param_values =
                duskArrayCreate(module->allocator, DuskIRValue *);
            duskArrayResize(
                &param_values, duskArrayLength(expr->function_call.params));

            for (size_t i = 0; i < duskArrayLength(expr->function_call.params);
                 ++i)
            {
                DuskExpr *param_expr = expr->function_call.params[i];
                duskGenerateExpr(module, function, param_expr);
                DUSK_ASSERT(param_expr->ir_value);
                param_values[i] = param_expr->ir_value;
            }

            DUSK_ASSERT(duskArrayLength(function->function.blocks) > 0);
            DuskIRValue *block =
                function->function
                    .blocks[duskArrayLength(function->function.blocks) - 1];

            expr->ir_value = duskIRCreateFunctionCall(
                module,
                block,
                expr->function_call.func_expr->ir_value,
                duskArrayLength(param_values),
                param_values);
            break;
        }

        case DUSK_TYPE_TYPE: {
            DuskType *constructed_type = expr->function_call.func_expr->as_type;
            size_t param_count = duskArrayLength(expr->function_call.params);

            size_t value_count = 0;

            switch (constructed_type->kind)
            {
            case DUSK_TYPE_VECTOR: {
                value_count = constructed_type->vector.size;
                break;
            }
            case DUSK_TYPE_MATRIX: {
                value_count = constructed_type->matrix.cols;
                break;
            }
            default: DUSK_ASSERT(0); break;
            }

            DUSK_ASSERT(value_count > 0);
            DuskIRValue **values = duskAllocateZeroed(
                module->allocator, sizeof(DuskIRValue *) * value_count);

            if (function)
            {
                DUSK_ASSERT(duskArrayLength(function->function.blocks) > 0);
                DuskIRValue *block =
                    function->function
                        .blocks[duskArrayLength(function->function.blocks) - 1];

                bool all_constants = true;
                if (param_count == value_count)
                {
                    for (size_t i = 0; i < value_count; ++i)
                    {
                        DuskExpr *param = expr->function_call.params[i];
                        duskGenerateExpr(module, function, param);
                        values[i] = param->ir_value;
                        if (!duskIRValueIsConstant(values[i]))
                        {
                            values[i] =
                                duskIRLoadLvalue(module, block, values[i]);
                            all_constants = false;
                        }
                    }
                }
                else if (param_count == 1)
                {
                    DuskExpr *param = expr->function_call.params[0];
                    duskGenerateExpr(module, function, param);
                    DuskIRValue *param_value = param->ir_value;
                    if (!duskIRValueIsConstant(param_value))
                    {
                        param_value =
                            duskIRLoadLvalue(module, block, param->ir_value);
                        all_constants = false;
                    }

                    for (size_t i = 0; i < value_count; ++i)
                    {
                        values[i] = param_value;
                    }
                }
                else
                {
                    DUSK_ASSERT(0);
                }

                if (all_constants)
                {
                    expr->ir_value = duskIRConstCompositeCreate(
                        module, constructed_type, value_count, values);
                }
                else
                {
                    expr->ir_value = duskIRCreateCompositeConstruct(
                        module, block, constructed_type, value_count, values);
                }
            }
            else
            {
                // Not inside a function, must be a constant

                if (param_count == value_count)
                {
                    for (size_t i = 0; i < value_count; ++i)
                    {
                        DuskExpr *param = expr->function_call.params[i];
                        duskGenerateExpr(module, function, param);
                        values[i] = param->ir_value;
                        DUSK_ASSERT(duskIRValueIsConstant(values[i]));
                    }
                }
                else if (param_count == 1)
                {
                    DuskExpr *param = expr->function_call.params[0];
                    duskGenerateExpr(module, function, param);
                    DuskIRValue *param_value = param->ir_value;
                    DUSK_ASSERT(duskIRValueIsConstant(param_value));

                    for (size_t i = 0; i < value_count; ++i)
                    {
                        values[i] = param_value;
                    }
                }
                else
                {
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

    case DUSK_EXPR_BUILTIN_FUNCTION_CALL: {
        switch (expr->builtin_call.kind)
        {
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
        case DUSK_BUILTIN_FUNCTION_MAX: DUSK_ASSERT(0); break;
        }
        break;
    }

    case DUSK_EXPR_ACCESS: {
        DUSK_ASSERT(duskArrayLength(function->function.blocks) > 0);
        DuskIRValue *block =
            function->function
                .blocks[duskArrayLength(function->function.blocks) - 1];

        DuskExpr *left_expr = expr->access.base_expr;
        duskGenerateExpr(module, function, left_expr);

        for (size_t i = 0; i < duskArrayLength(expr->access.chain); ++i)
        {
            DUSK_ASSERT(left_expr->ir_value);

            DuskExpr *right_expr = expr->access.chain[i];

            switch (left_expr->type->kind)
            {
            case DUSK_TYPE_VECTOR: {
                DUSK_ASSERT(right_expr->kind == DUSK_EXPR_IDENT);
                DUSK_ASSERT(right_expr->identifier.shuffle_indices);

                DuskArray(uint32_t) indices =
                    right_expr->identifier.shuffle_indices;
                size_t index_count = duskArrayLength(indices);

                if (index_count > 1)
                {
                    DuskIRValue *vec_value =
                        duskIRLoadLvalue(module, block, left_expr->ir_value);
                    right_expr->ir_value = duskIRCreateVectorShuffle(
                        module,
                        block,
                        vec_value,
                        vec_value,
                        index_count,
                        indices);
                }
                else
                {
                    DUSK_ASSERT(index_count == 1);

                    if (duskIRIsLvalue(left_expr->ir_value))
                    {
                        DuskIRValue *index_value = duskIRConstIntCreate(
                            module,
                            duskTypeNewScalar(
                                module->compiler, DUSK_SCALAR_TYPE_UINT),
                            indices[0]);

                        right_expr->ir_value = duskIRCreateAccessChain(
                            module,
                            block,
                            right_expr->type,
                            left_expr->ir_value,
                            1,
                            &index_value);
                    }
                    else
                    {
                        DuskIRValue *vec_value = duskIRLoadLvalue(
                            module, block, left_expr->ir_value);
                        right_expr->ir_value = duskIRCreateCompositeExtract(
                            module, block, vec_value, index_count, indices);
                    }
                }

                break;
            }
            case DUSK_TYPE_STRUCT: {
                uintptr_t field_index = 0;
                if (!duskMapGet(
                        left_expr->type->struct_.index_map,
                        right_expr->identifier.str,
                        (void *)&field_index))
                {
                    DUSK_ASSERT(0);
                }

                if (duskIRIsLvalue(left_expr->ir_value))
                {
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
                }
                else
                {
                    uint32_t index = (uint32_t)field_index;
                    DuskIRValue *struct_value =
                        duskIRLoadLvalue(module, block, left_expr->ir_value);
                    right_expr->ir_value = duskIRCreateCompositeExtract(
                        module, block, struct_value, 1, &index);
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

    case DUSK_EXPR_STRING_LITERAL:
    case DUSK_EXPR_BOOL_TYPE:
    case DUSK_EXPR_ARRAY_TYPE:
    case DUSK_EXPR_MATRIX_TYPE:
    case DUSK_EXPR_RUNTIME_ARRAY_TYPE:
    case DUSK_EXPR_SCALAR_TYPE:
    case DUSK_EXPR_STRUCT_TYPE:
    case DUSK_EXPR_VECTOR_TYPE:
    case DUSK_EXPR_VOID_TYPE: break;
    }
}

static void
duskGenerateStmt(DuskIRModule *module, DuskIRValue *function, DuskStmt *stmt)
{
    DUSK_ASSERT(duskArrayLength(function->function.blocks) > 0);
    DuskIRValue *block =
        function->function
            .blocks[duskArrayLength(function->function.blocks) - 1];

    switch (stmt->kind)
    {
    case DUSK_STMT_RETURN: {
        DuskIRValue *returned_value = NULL;
        if (stmt->return_.expr)
        {
            duskGenerateExpr(module, function, stmt->return_.expr);
            returned_value = stmt->return_.expr->ir_value;
            returned_value =
                duskIRLoadLvalue(module, block, stmt->return_.expr->ir_value);
            DUSK_ASSERT(returned_value);
        }

        duskIRCreateReturn(module, block, returned_value);
        break;
    }
    case DUSK_STMT_DISCARD: {
        duskIRCreateDiscard(module, block);
        break;
    }
    case DUSK_STMT_DECL: {
        duskGenerateLocalDecl(module, function, stmt->decl);
        break;
    }
    case DUSK_STMT_ASSIGN: {
        duskGenerateExpr(module, function, stmt->assign.assigned_expr);
        duskGenerateExpr(module, function, stmt->assign.value_expr);

        DuskIRValue *pointer = stmt->assign.assigned_expr->ir_value;
        DuskIRValue *value = stmt->assign.value_expr->ir_value;
        value = duskIRLoadLvalue(module, block, value);

        duskIRCreateStore(module, block, pointer, value);
        break;
    }
    case DUSK_STMT_EXPR: {
        duskGenerateExpr(module, function, stmt->expr);
        break;
    }
    case DUSK_STMT_BLOCK: {
        for (size_t i = 0; i < duskArrayLength(stmt->block.stmts); ++i)
        {
            duskGenerateStmt(module, function, stmt->block.stmts[i]);
        }
        break;
    }
    }
}

static void duskGenerateLocalDecl(
    DuskIRModule *module, DuskIRValue *function, DuskDecl *decl)
{
    DuskIRValue *block =
        function->function
            .blocks[duskArrayLength(function->function.blocks) - 1];

    DUSK_ASSERT(decl->type);
    if (decl->type)
    {
        duskTypeMarkNotDead(decl->type);
    }
    DUSK_ASSERT(decl->type->emit);

    switch (decl->kind)
    {
    case DUSK_DECL_VAR: {
        DUSK_ASSERT(decl->type);

        bool should_create_var = true;
        switch (decl->type->kind)
        {
        case DUSK_TYPE_IMAGE:
        case DUSK_TYPE_SAMPLED_IMAGE:
        case DUSK_TYPE_SAMPLER: should_create_var = false; break;
        default: break;
        }

        if (should_create_var)
        {
            decl->ir_value = duskIRVariableCreate(
                module, decl->type, DUSK_STORAGE_CLASS_FUNCTION);
            duskArrayPush(&function->function.variables, decl->ir_value);
        }

        if (decl->var.value_expr)
        {
            duskGenerateExpr(module, function, decl->var.value_expr);
            DuskIRValue *assigned_value = decl->var.value_expr->ir_value;

            if (should_create_var)
            {
                assigned_value =
                    duskIRLoadLvalue(module, block, assigned_value);
                duskIRCreateStore(
                    module, block, decl->ir_value, assigned_value);
            }
            else
            {
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
    if (decl->type)
    {
        duskTypeMarkNotDead(decl->type);
    }

    switch (decl->kind)
    {
    case DUSK_DECL_FUNCTION: {
        DUSK_ASSERT(decl->type);

        DuskType *function_type = decl->type;
        if (decl->function.is_entry_point)
        {
            // Entry point is a function with no parameters and no return type
            function_type = duskTypeNewFunction(
                module->compiler,
                duskTypeNewBasic(module->compiler, DUSK_TYPE_VOID),
                duskArrayCreate(module->allocator, DuskType *));

            decl->function.entry_point_referenced_globals =
                duskArrayCreate(module->allocator, DuskIRValue *);
        }
        decl->ir_value =
            duskIRFunctionCreate(module, function_type, decl->name);
        duskArrayPush(&module->functions, decl->ir_value);

        size_t param_count = duskArrayLength(decl->function.parameter_decls);
        if (decl->function.is_entry_point)
        {
            for (size_t i = 0; i < param_count; ++i)
            {
                DuskDecl *param_decl = decl->function.parameter_decls[i];

                param_decl->ir_value = duskIRVariableCreate(
                    module, param_decl->type, DUSK_STORAGE_CLASS_INPUT);
                duskArrayPush(&module->globals, param_decl->ir_value);
                duskArrayPush(
                    &decl->function.entry_point_referenced_globals,
                    param_decl->ir_value);

                DUSK_ASSERT(param_decl->ir_value);
            }
        }
        else
        {
            for (size_t i = 0; i < param_count; ++i)
            {
                DuskDecl *param_decl = decl->function.parameter_decls[i];
                param_decl->ir_value = decl->ir_value->function.params[i];
                DUSK_ASSERT(param_decl->ir_value);
            }
        }

        size_t stmt_count = duskArrayLength(decl->function.stmts);
        for (size_t i = 0; i < stmt_count; ++i)
        {
            DuskStmt *stmt = decl->function.stmts[i];
            duskGenerateStmt(module, decl->ir_value, stmt);
        }

        for (size_t i = 0; i < duskArrayLength(decl->ir_value->function.blocks);
             ++i)
        {
            DuskIRValue *block = decl->ir_value->function.blocks[i];
            if (!duskIRBlockIsTerminated(block))
            {
                if (decl->type->function.return_type->kind == DUSK_TYPE_VOID)
                {
                    duskIRCreateReturn(module, block, NULL);
                }
                else
                {
                    DUSK_ASSERT(0); // Missing terminator instruction
                }
            }
        }

        if (decl->function.is_entry_point)
        {
            duskIRModuleAddEntryPoint(
                module,
                decl->ir_value,
                decl->function.link_name,
                decl->function.entry_point_stage,
                duskArrayLength(decl->function.entry_point_referenced_globals),
                decl->function.entry_point_referenced_globals);
        }

        break;
    }
    case DUSK_DECL_VAR: {
        DUSK_ASSERT(decl->type);

        DuskStorageClass storage_class = DUSK_STORAGE_CLASS_UNIFORM;
        switch (decl->type->kind)
        {
        case DUSK_TYPE_SAMPLER:
        case DUSK_TYPE_IMAGE:
        case DUSK_TYPE_SAMPLED_IMAGE: {
            storage_class = DUSK_STORAGE_CLASS_UNIFORM_CONSTANT;
            break;
        }
        default: storage_class = DUSK_STORAGE_CLASS_UNIFORM; break;
        }

        decl->ir_value =
            duskIRVariableCreate(module, decl->type, storage_class);
        duskArrayPush(&module->globals, decl->ir_value);
        break;
    }
    case DUSK_DECL_TYPE: break;
    }
}

DuskIRModule *duskGenerateIRModule(DuskCompiler *compiler, DuskFile *file)
{
    (void)file;

    DuskIRModule *module = duskIRModuleCreate(compiler);

    for (size_t i = 0; i < duskArrayLength(file->decls); ++i)
    {
        DuskDecl *decl = file->decls[i];
        duskGenerateGlobalDecl(module, decl);
    }

    return module;
}
