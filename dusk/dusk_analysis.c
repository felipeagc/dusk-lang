#include "dusk_internal.h"

typedef struct DuskAnalyzerState
{
    DuskArray(DuskScope *) scope_stack;
    DuskArray(DuskStmt *) break_stack;
    DuskArray(DuskStmt *) continue_stack;
} DuskAnalyzerState;

static void
duskAnalyzeDecl(DuskCompiler *compiler, DuskAnalyzerState *state, DuskDecl *decl);
static void
duskTryRegisterDecl(DuskCompiler *compiler, DuskAnalyzerState *state, DuskDecl *decl);

DuskScope *duskScopeCreate(
    DuskAllocator *allocator, DuskScope *parent, DuskScopeOwnerType type, void *owner)
{
    DuskMap *map = duskMapCreate(allocator, 16);

    DuskScope *scope = DUSK_NEW(allocator, DuskScope);
    *scope = (DuskScope){
        .type = type,
        .map = map,
        .parent = parent,
    };
    if (type != DUSK_SCOPE_OWNER_TYPE_NONE)
    {
        DUSK_ASSERT(owner != NULL);
        memcpy(&owner, &scope->owner, sizeof(void *));
    }
    else
    {
        DUSK_ASSERT(owner == NULL);
    }
    return scope;
}

DuskDecl *duskScopeLookupLocal(DuskScope *scope, const char *name)
{
    DUSK_ASSERT(scope != NULL);

    DuskDecl *decl = NULL;
    if (duskMapGet(scope->map, name, (void **)&decl))
    {
        DUSK_ASSERT(decl != NULL);
        return decl;
    }

    return NULL;
}

DuskDecl *duskScopeLookup(DuskScope *scope, const char *name)
{
    DuskDecl *decl = duskScopeLookupLocal(scope, name);
    if (decl) return decl;

    if (scope->parent)
    {
        DuskDecl *decl = duskScopeLookup(scope->parent, name);
        if (decl) return decl;
    }

    return NULL;
}

void duskScopeSet(DuskScope *scope, const char *name, DuskDecl *decl)
{
    DUSK_ASSERT(decl != NULL);
    duskMapSet(scope->map, name, decl);
}

static DuskScope *duskCurrentScope(DuskAnalyzerState *state)
{
    DUSK_ASSERT(duskArrayLength(state->scope_stack) > 0);
    return state->scope_stack[duskArrayLength(state->scope_stack) - 1];
}

static bool
duskExprResolveInteger(DuskAnalyzerState *state, DuskExpr *expr, int64_t *out_int)
{
    switch (expr->kind)
    {
    case DUSK_EXPR_INT_LITERAL: {
        *out_int = expr->int_literal;
        return true;
    }
    case DUSK_EXPR_IDENT: {
        DuskDecl *ident_decl = duskScopeLookup(duskCurrentScope(state), expr->ident);
        if (!ident_decl)
        {
            return false;
        }

        switch (ident_decl->kind)
        {
        // TODO: add resolve integer for constants
        default: return false;
        }

        return false;
    }

    case DUSK_EXPR_FLOAT_LITERAL:
    case DUSK_EXPR_ARRAY_TYPE:
    case DUSK_EXPR_MATRIX_TYPE:
    case DUSK_EXPR_VECTOR_TYPE:
    case DUSK_EXPR_SCALAR_TYPE:
    case DUSK_EXPR_BOOL_TYPE:
    case DUSK_EXPR_VOID_TYPE:
    case DUSK_EXPR_RUNTIME_ARRAY_TYPE:
    case DUSK_EXPR_STRUCT_TYPE: {
        return false;
    }
    }

    return false;
}

static bool duskIsExprAssignable(DuskAnalyzerState *state, DuskExpr *expr)
{
    switch (expr->kind)
    {
    case DUSK_EXPR_IDENT: {
        DuskScope *scope = duskCurrentScope(state);

        DuskDecl *decl = duskScopeLookup(scope, expr->ident);
        if (!decl) return false;

        if (decl->kind == DUSK_DECL_VAR)
        {
            switch (decl->var.storage_class)
            {
            case DUSK_STORAGE_CLASS_FUNCTION:
            case DUSK_STORAGE_CLASS_INPUT:
            case DUSK_STORAGE_CLASS_OUTPUT:
            case DUSK_STORAGE_CLASS_UNIFORM: return true;
            case DUSK_STORAGE_CLASS_PARAMETER: return false;
            }
        }

        break;
    }
    default: {
        break;
    }
    }

    return false;
}

static void duskAnalyzeExpr(
    DuskCompiler *compiler,
    DuskAnalyzerState *state,
    DuskExpr *expr,
    DuskType *expected_type,
    bool must_be_assignable)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    switch (expr->kind)
    {
    case DUSK_EXPR_VOID_TYPE: {
        expr->type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);
        expr->as_type = duskTypeNewBasic(compiler, DUSK_TYPE_VOID);
        break;
    }
    case DUSK_EXPR_BOOL_TYPE: {
        expr->type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);
        expr->as_type = duskTypeNewBasic(compiler, DUSK_TYPE_BOOL);
        break;
    }
    case DUSK_EXPR_FLOAT_LITERAL: {
        if (expected_type && expected_type->kind == DUSK_TYPE_FLOAT)
        {
            expr->type = expected_type;
        }
        else
        {
            expr->type = duskTypeNewBasic(compiler, DUSK_TYPE_UNTYPED_FLOAT);
        }
        break;
    }
    case DUSK_EXPR_INT_LITERAL: {
        if (expected_type && (expected_type->kind == DUSK_TYPE_INT ||
                              expected_type->kind == DUSK_TYPE_FLOAT))
        {
            expr->type = expected_type;
        }
        else
        {
            expr->type = duskTypeNewBasic(compiler, DUSK_TYPE_UNTYPED_INT);
        }
        break;
    }
    case DUSK_EXPR_IDENT: {
        DuskDecl *ident_decl = duskScopeLookup(duskCurrentScope(state), expr->ident);
        if (!ident_decl)
        {
            duskAddError(
                compiler, expr->location, "cannot find symbol '%s'", expr->ident);
            break;
        }

        expr->type = ident_decl->type;
        break;
    }
    case DUSK_EXPR_SCALAR_TYPE: {
        expr->type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);
        expr->as_type = duskTypeNewScalar(compiler, expr->scalar_type);
        break;
    }
    case DUSK_EXPR_VECTOR_TYPE: {
        DuskType *scalar_type =
            duskTypeNewScalar(compiler, expr->vector_type.scalar_type);

        expr->type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);
        expr->as_type =
            duskTypeNewVector(compiler, scalar_type, expr->vector_type.length);
        break;
    }
    case DUSK_EXPR_MATRIX_TYPE: {
        DuskType *scalar_type =
            duskTypeNewScalar(compiler, expr->matrix_type.scalar_type);

        expr->type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);
        expr->as_type = duskTypeNewMatrix(
            compiler, scalar_type, expr->matrix_type.cols, expr->matrix_type.rows);
        break;
    }
    case DUSK_EXPR_ARRAY_TYPE: {
        DuskType *type_type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);

        duskAnalyzeExpr(compiler, state, expr->array_type.size_expr, NULL, false);
        duskAnalyzeExpr(compiler, state, expr->array_type.sub_expr, type_type, false);

        int64_t array_size = 0;
        if (!duskExprResolveInteger(state, expr->array_type.size_expr, &array_size))
        {
            duskAddError(
                compiler,
                expr->array_type.size_expr->location,
                "failed to resolve integer for array size expression");
            break;
        }

        if (array_size <= 0)
        {
            duskAddError(
                compiler,
                expr->array_type.size_expr->location,
                "array size must be greater than 0");
            break;
        }

        DuskType *sub_type = expr->array_type.sub_expr->as_type;
        if (!sub_type)
        {
            break;
        }

        expr->type = type_type;
        expr->as_type = duskTypeNewArray(compiler, sub_type, (size_t)array_size);
        break;
    }
    case DUSK_EXPR_RUNTIME_ARRAY_TYPE: {
        DuskType *type_type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);

        duskAnalyzeExpr(compiler, state, expr->array_type.sub_expr, type_type, false);

        DuskType *sub_type = expr->array_type.sub_expr->as_type;
        if (!sub_type)
        {
            break;
        }

        expr->type = type_type;
        expr->as_type = duskTypeNewRuntimeArray(compiler, sub_type);
        break;
    }
    case DUSK_EXPR_STRUCT_TYPE: {
        duskAddError(compiler, expr->location, "struct type unimplemented");
        break;
    }
    }

    if (!expr->type)
    {
        DUSK_ASSERT(duskArrayLength(compiler->errors) > 0);
    }

    if (expected_type && expr->type)
    {
        if (expected_type != expr->type)
        {
            duskAddError(
                compiler,
                expr->location,
                "type mismatch, expected '%s', instead got '%s'",
                duskTypeToPrettyString(allocator, expected_type),
                duskTypeToPrettyString(allocator, expr->type));
        }
    }

    if (must_be_assignable && !duskIsExprAssignable(state, expr))
    {
        duskAddError(compiler, expr->location, "expected expression to be assignable");
    }
}

static void
duskAnalyzeStmt(DuskCompiler *compiler, DuskAnalyzerState *state, DuskStmt *stmt)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    switch (stmt->kind)
    {
    case DUSK_STMT_DECL: {
        duskTryRegisterDecl(compiler, state, stmt->decl);
        duskAnalyzeDecl(compiler, state, stmt->decl);
        break;
    }
    case DUSK_STMT_EXPR: {
        duskAnalyzeExpr(compiler, state, stmt->expr, NULL, false);
        break;
    }
    case DUSK_STMT_ASSIGN: {
        duskAnalyzeExpr(compiler, state, stmt->assign.assigned_expr, NULL, true);
        DuskType *type = stmt->assign.assigned_expr->type;
        if (!type)
        {
            DUSK_ASSERT(duskArrayLength(compiler->errors) > 0);
            break;
        }

        duskAnalyzeExpr(compiler, state, stmt->assign.value_expr, type, false);
        break;
    }
    case DUSK_STMT_BLOCK: {
        stmt->block.scope = duskScopeCreate(
            allocator, duskCurrentScope(state), DUSK_SCOPE_OWNER_TYPE_NONE, NULL);

        for (size_t i = 0; i < duskArrayLength(stmt->block.stmts); ++i)
        {
            DuskStmt *sub_stmt = stmt->block.stmts[i];
            duskAnalyzeStmt(compiler, state, sub_stmt);
        }
        break;
    }
    }
}

static void
duskTryRegisterDecl(DuskCompiler *compiler, DuskAnalyzerState *state, DuskDecl *decl)
{
    DuskScope *scope = duskCurrentScope(state);

    DUSK_ASSERT(decl->name);
    if (duskScopeLookup(scope, decl->name) != NULL)
    {
        duskAddError(compiler, decl->location, "duplicate declaration: '%s'", decl->name);
        return;
    }

    duskScopeSet(scope, decl->name, decl);
}

static void
duskAnalyzeDecl(DuskCompiler *compiler, DuskAnalyzerState *state, DuskDecl *decl)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    switch (decl->kind)
    {
    case DUSK_DECL_MODULE: {
        DUSK_ASSERT(decl->module.scope == NULL);

        decl->module.scope = duskScopeCreate(
            allocator, duskCurrentScope(state), DUSK_SCOPE_OWNER_TYPE_MODULE, decl);

        duskArrayPush(&state->scope_stack, decl->module.scope);

        for (size_t i = 0; i < duskArrayLength(decl->module.decls); ++i)
        {
            DuskDecl *sub_decl = decl->module.decls[i];
            duskTryRegisterDecl(compiler, state, sub_decl);
        }

        for (size_t i = 0; i < duskArrayLength(decl->module.decls); ++i)
        {
            DuskDecl *sub_decl = decl->module.decls[i];
            duskAnalyzeDecl(compiler, state, sub_decl);
        }

        duskArrayPop(&state->scope_stack);

        break;
    }
    case DUSK_DECL_FUNCTION: {
        DUSK_ASSERT(decl->function.scope == NULL);

        decl->function.scope = duskScopeCreate(
            allocator, duskCurrentScope(state), DUSK_SCOPE_OWNER_TYPE_FUNCTION, decl);

        duskArrayPush(&state->scope_stack, decl->function.scope);

        for (size_t i = 0; i < duskArrayLength(decl->function.parameter_decls); ++i)
        {
            DuskDecl *param_decl = decl->function.parameter_decls[i];
            duskTryRegisterDecl(compiler, state, param_decl);
            duskAnalyzeDecl(compiler, state, param_decl);
        }

        for (size_t i = 0; i < duskArrayLength(decl->function.stmts); ++i)
        {
            DuskStmt *stmt = decl->function.stmts[i];
            duskAnalyzeStmt(compiler, state, stmt);
        }

        duskArrayPop(&state->scope_stack);
        break;
    }
    case DUSK_DECL_TYPE: {
        DuskType *type_type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);
        duskAnalyzeExpr(compiler, state, decl->typedef_.type_expr, type_type, false);
        decl->type = type_type;
        break;
    }
    case DUSK_DECL_VAR: {
        DuskType *type_type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);

        DuskType *var_type = NULL;
        if (decl->var.type_expr)
        {
            duskAnalyzeExpr(compiler, state, decl->var.type_expr, type_type, false);
            var_type = decl->var.type_expr->as_type;
            if (!var_type) break;
        }

        if (decl->var.value_expr)
        {
            duskAnalyzeExpr(compiler, state, decl->var.value_expr, var_type, false);
            if (var_type == NULL)
            {
                var_type = decl->var.value_expr->type;
            }
        }

        if (!var_type)
        {
            DUSK_ASSERT(duskArrayLength(compiler->errors) > 0);
        }

        if (!duskTypeIsRuntime(var_type))
        {
            duskAddError(
                compiler,
                decl->location,
                "variable type is not representable at runtime: '%s'",
                duskTypeToPrettyString(allocator, var_type));
            break;
        }

        decl->type = var_type;
        break;
    }
    }
}

void duskAnalyzeFile(DuskCompiler *compiler, DuskFile *file)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    DuskAnalyzerState *state = DUSK_NEW(allocator, DuskAnalyzerState);
    *state = (DuskAnalyzerState){
        .scope_stack = duskArrayCreate(allocator, DuskScope *),
        .break_stack = duskArrayCreate(allocator, DuskStmt *),
        .continue_stack = duskArrayCreate(allocator, DuskStmt *),
    };

    duskArrayPush(&state->scope_stack, file->scope);

    for (size_t i = 0; i < duskArrayLength(file->decls); ++i)
    {
        DuskDecl *decl = file->decls[i];
        duskTryRegisterDecl(compiler, state, decl);
    }

    for (size_t i = 0; i < duskArrayLength(file->decls); ++i)
    {
        DuskDecl *decl = file->decls[i];
        duskAnalyzeDecl(compiler, state, decl);
    }

    duskArrayPop(&state->scope_stack);
}
