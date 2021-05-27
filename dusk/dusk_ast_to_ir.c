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

    case DUSK_EXPR_FUNCTION_CALL: {
        DUSK_ASSERT(duskArrayLength(function->function.blocks) > 0);
        DuskIRValue *block =
            function->function
                .blocks[duskArrayLength(function->function.blocks) - 1];

        duskGenerateExpr(module, function, expr->function_call.func_expr);

        DuskArray(DuskIRValue *) param_values =
            duskArrayCreate(module->allocator, DuskIRValue *);
        duskArrayResize(
            &param_values, duskArrayLength(expr->function_call.params));

        for (size_t i = 0; i < duskArrayLength(expr->function_call.params); ++i)
        {
            DuskExpr *param_expr = expr->function_call.params[i];
            duskGenerateExpr(module, function, param_expr);
            DUSK_ASSERT(param_expr->ir_value);
            param_values[i] = param_expr->ir_value;
        }

        expr->ir_value = duskIRCreateFunctionCall(
            module,
            block,
            expr->function_call.func_expr->ir_value,
            duskArrayLength(param_values),
            param_values);

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
        DUSK_ASSERT(!"unimplemented");
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
    case DUSK_STMT_BLOCK: DUSK_ASSERT(0 && "unimplemented"); break;
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
        decl->ir_value = duskIRFunctionCreate(module, decl->type, decl->name);
        duskArrayPush(&module->functions, decl->ir_value);

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

    for (size_t i = 0; i < duskArrayLength(file->entry_points); ++i)
    {
        DuskEntryPoint *entry_point = &file->entry_points[i];
        duskIRModuleAddEntryPoint(
            module,
            entry_point->function_decl->ir_value,
            entry_point->name,
            entry_point->stage);
    }

    return module;
}
