#include "dusk_internal.h"

static void duskGenerateLocalDecl(
    DuskIRModule *module, DuskIRValue *function, DuskDecl *decl);

static void duskGenerateExpr(DuskIRModule *module, DuskExpr *expr)
{
    switch (expr->kind)
    {
    case DUSK_EXPR_IDENT: {
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
            duskGenerateExpr(module, stmt->return_.expr);
            returned_value = stmt->return_.expr->ir_value;
            DUSK_ASSERT(returned_value);
        }

        DuskIRValue *return_ = duskIRCreateReturn(module, returned_value);
        duskArrayPush(&block->block.insts, return_);
        break;
    }
    case DUSK_STMT_DECL: {
        duskGenerateLocalDecl(module, function, stmt->decl);
        break;
    }
    case DUSK_STMT_ASSIGN:
    case DUSK_STMT_EXPR:
    case DUSK_STMT_BLOCK: DUSK_ASSERT(0 && "unimplemented"); break;
    }
}

static void duskGenerateLocalDecl(
    DuskIRModule *module, DuskIRValue *function, DuskDecl *decl)
{
    /* DuskIRValue *block = */
    /*     function->function */
    /*         .blocks[duskArrayLength(function->function.blocks) - 1]; */

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

        decl->ir_value = duskIRVariableCreate(
            module, decl->type, DUSK_STORAGE_CLASS_FUNCTION);
        duskArrayPush(&function->function.variables, decl->ir_value);

        // TODO: assign
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

        if (decl->type->function.return_type->kind == DUSK_TYPE_VOID)
        {
            if (stmt_count == 0 ||
                decl->function.stmts[stmt_count - 1]->kind != DUSK_STMT_RETURN)
            {
                DuskIRValue *return_ = duskIRCreateReturn(module, NULL);
                DuskIRValue *last_block =
                    decl->ir_value->function.blocks
                        [duskArrayLength(decl->ir_value->function.blocks) - 1];
                duskArrayPush(&last_block->block.insts, return_);
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
