#include "dusk_internal.h"

static size_t DUSK_BUILTIN_FUNCTION_PARAM_COUNTS[DUSK_BUILTIN_FUNCTION_MAX] = {
    [DUSK_BUILTIN_FUNCTION_SAMPLER_TYPE] = 0,
    [DUSK_BUILTIN_FUNCTION_IMAGE_1D_TYPE] = 1,
    [DUSK_BUILTIN_FUNCTION_IMAGE_2D_TYPE] = 1,
    [DUSK_BUILTIN_FUNCTION_IMAGE_2D_ARRAY_TYPE] = 1,
    [DUSK_BUILTIN_FUNCTION_IMAGE_3D_TYPE] = 1,
    [DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_TYPE] = 1,
    [DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_ARRAY_TYPE] = 1,
    [DUSK_BUILTIN_FUNCTION_IMAGE_1D_SAMPLER_TYPE] = 1,
    [DUSK_BUILTIN_FUNCTION_IMAGE_2D_SAMPLER_TYPE] = 1,
    [DUSK_BUILTIN_FUNCTION_IMAGE_2D_ARRAY_SAMPLER_TYPE] = 1,
    [DUSK_BUILTIN_FUNCTION_IMAGE_3D_SAMPLER_TYPE] = 1,
    [DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_SAMPLER_TYPE] = 1,
    [DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_ARRAY_SAMPLER_TYPE] = 1,
};

typedef struct DuskAnalyzerState
{
    DuskArray(DuskScope *) scope_stack;
    DuskArray(DuskStmt *) break_stack;
    DuskArray(DuskStmt *) continue_stack;
    DuskArray(DuskDecl *) module_stack;
    DuskArray(DuskDecl *) function_stack;
} DuskAnalyzerState;

static void duskAnalyzeDecl(
    DuskCompiler *compiler, DuskAnalyzerState *state, DuskDecl *decl);
static void duskTryRegisterDecl(
    DuskCompiler *compiler, DuskAnalyzerState *state, DuskDecl *decl);

DuskScope *duskScopeCreate(
    DuskAllocator *allocator,
    DuskScope *parent,
    DuskScopeOwnerType type,
    void *owner)
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

static bool duskExprResolveInteger(
    DuskAnalyzerState *state, DuskExpr *expr, int64_t *out_int)
{
    switch (expr->kind)
    {
    case DUSK_EXPR_INT_LITERAL: {
        *out_int = expr->int_literal;
        return true;
    }
    case DUSK_EXPR_IDENT: {
        DuskDecl *ident_decl =
            duskScopeLookup(duskCurrentScope(state), expr->identifier.str);
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

    case DUSK_EXPR_BUILTIN_FUNCTION_CALL:
    case DUSK_EXPR_BOOL_LITERAL:
    case DUSK_EXPR_STRING_LITERAL:
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

        DuskDecl *decl = duskScopeLookup(scope, expr->identifier.str);
        if (!decl) return false;

        if (decl->kind == DUSK_DECL_VAR)
        {
            switch (decl->var.storage_class)
            {
            case DUSK_STORAGE_CLASS_FUNCTION:
            case DUSK_STORAGE_CLASS_INPUT:
            case DUSK_STORAGE_CLASS_OUTPUT:
            case DUSK_STORAGE_CLASS_UNIFORM: return true;
            case DUSK_STORAGE_CLASS_UNIFORM_CONSTANT:
            case DUSK_STORAGE_CLASS_PUSH_CONSTANT:
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
    case DUSK_EXPR_STRING_LITERAL: {
        expr->type = duskTypeNewBasic(compiler, DUSK_TYPE_STRING);
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
    case DUSK_EXPR_BOOL_LITERAL: {
        expr->type = duskTypeNewBasic(compiler, DUSK_TYPE_BOOL);
        break;
    }
    case DUSK_EXPR_IDENT: {
        DuskDecl *ident_decl =
            duskScopeLookup(duskCurrentScope(state), expr->identifier.str);
        if (!ident_decl)
        {
            duskAddError(
                compiler,
                expr->location,
                "cannot find symbol '%s'",
                expr->identifier.str);
            break;
        }

        expr->identifier.decl = ident_decl;

        expr->type = ident_decl->type;
        if (ident_decl->kind == DUSK_DECL_TYPE)
        {
            expr->as_type = ident_decl->typedef_.type_expr->as_type;
        }
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
        DuskType *col_type =
            duskTypeNewVector(compiler, scalar_type, expr->matrix_type.rows);

        expr->type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);
        expr->as_type =
            duskTypeNewMatrix(compiler, col_type, expr->matrix_type.cols);
        break;
    }
    case DUSK_EXPR_ARRAY_TYPE: {
        DuskType *type_type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);

        duskAnalyzeExpr(
            compiler, state, expr->array_type.size_expr, NULL, false);
        duskAnalyzeExpr(
            compiler, state, expr->array_type.sub_expr, type_type, false);

        int64_t array_size = 0;
        if (!duskExprResolveInteger(
                state, expr->array_type.size_expr, &array_size))
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
        expr->as_type =
            duskTypeNewArray(compiler, sub_type, (size_t)array_size);
        break;
    }
    case DUSK_EXPR_RUNTIME_ARRAY_TYPE: {
        DuskType *type_type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);

        duskAnalyzeExpr(
            compiler, state, expr->array_type.sub_expr, type_type, false);

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
        size_t field_count =
            duskArrayLength(expr->struct_type.field_type_exprs);

        bool got_duplicate_field_names = false;

        DuskMap *field_map = duskMapCreate(NULL, field_count);

        for (size_t i = 0; i < field_count; ++i)
        {
            if (duskMapGet(field_map, expr->struct_type.field_names[i], NULL))
            {
                duskAddError(
                    compiler,
                    expr->location,
                    "duplicate struct member name: '%s'",
                    expr->struct_type.field_names[i]);
                got_duplicate_field_names = true;
                continue;
            }
            duskMapSet(field_map, expr->struct_type.field_names[i], NULL);
        }

        duskMapDestroy(field_map);

        if (got_duplicate_field_names)
        {
            DUSK_ASSERT(duskArrayLength(compiler->errors) > 0);
            break;
        }

        DuskType *type_type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);

        bool got_all_field_types = true;

        DuskArray(DuskType *) field_types =
            duskArrayCreate(allocator, DuskType *);
        duskArrayResize(&field_types, field_count);

        for (size_t i = 0; i < field_count; ++i)
        {
            DuskExpr *field_type_expr = expr->struct_type.field_type_exprs[i];
            duskAnalyzeExpr(compiler, state, field_type_expr, type_type, false);
            if (!field_type_expr->as_type)
            {
                got_all_field_types = false;
                break;
            }
            field_types[i] = field_type_expr->as_type;
        }

        if (!got_all_field_types)
        {
            DUSK_ASSERT(duskArrayLength(compiler->errors) > 0);
            break;
        }

        expr->type = type_type;
        expr->as_type = duskTypeNewStruct(
            compiler,
            expr->struct_type.name,
            expr->struct_type.field_names,
            field_types);

        break;
    }
    case DUSK_EXPR_BUILTIN_FUNCTION_CALL: {
        size_t required_param_count =
            DUSK_BUILTIN_FUNCTION_PARAM_COUNTS[expr->builtin_call.kind];
        if (required_param_count != duskArrayLength(expr->builtin_call.params))
        {
            duskAddError(
                compiler,
                expr->location,
                "invalid parameter count for builtin call: expected %zu, "
                "instead got %zu",
                required_param_count,
                duskArrayLength(expr->builtin_call.params));
            break;
        }

        switch (expr->builtin_call.kind)
        {
        case DUSK_BUILTIN_FUNCTION_SAMPLER_TYPE: {
            expr->type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);
            expr->as_type = duskTypeNewBasic(compiler, DUSK_TYPE_SAMPLER);
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
        case DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_ARRAY_SAMPLER_TYPE: {
            DuskType *type_type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);
            duskAnalyzeExpr(
                compiler,
                state,
                expr->builtin_call.params[0],
                type_type,
                false);
            DuskType *sampled_type = expr->builtin_call.params[0]->as_type;

            if (!sampled_type)
            {
                DUSK_ASSERT(duskArrayLength(compiler->errors) > 0);
            }

            if (sampled_type->kind != DUSK_TYPE_VOID &&
                sampled_type->kind != DUSK_TYPE_FLOAT &&
                sampled_type->kind != DUSK_TYPE_INT)
            {
                duskAddError(
                    compiler,
                    expr->builtin_call.params[0]->location,
                    "expected a scalar type or void, instead got '%s'",
                    duskTypeToPrettyString(allocator, sampled_type));
                break;
            }

            DuskImageDimension dim = DUSK_IMAGE_DIMENSION_2D;
            bool depth = false;
            bool arrayed = false;
            bool multisampled = false;
            bool sampled = false;

            switch (expr->builtin_call.kind)
            {
            case DUSK_BUILTIN_FUNCTION_IMAGE_1D_TYPE:
            case DUSK_BUILTIN_FUNCTION_IMAGE_1D_SAMPLER_TYPE: {
                dim = DUSK_IMAGE_DIMENSION_1D;
                depth = false;
                arrayed = false;
                multisampled = false;
                sampled = true;
                break;
            }
            case DUSK_BUILTIN_FUNCTION_IMAGE_2D_TYPE:
            case DUSK_BUILTIN_FUNCTION_IMAGE_2D_SAMPLER_TYPE: {
                dim = DUSK_IMAGE_DIMENSION_2D;
                depth = false;
                arrayed = false;
                multisampled = false;
                sampled = true;
                break;
            }
            case DUSK_BUILTIN_FUNCTION_IMAGE_2D_ARRAY_TYPE:
            case DUSK_BUILTIN_FUNCTION_IMAGE_2D_ARRAY_SAMPLER_TYPE: {
                dim = DUSK_IMAGE_DIMENSION_2D;
                depth = false;
                arrayed = true;
                multisampled = false;
                sampled = true;
                break;
            }
            case DUSK_BUILTIN_FUNCTION_IMAGE_3D_TYPE:
            case DUSK_BUILTIN_FUNCTION_IMAGE_3D_SAMPLER_TYPE: {
                dim = DUSK_IMAGE_DIMENSION_3D;
                depth = false;
                arrayed = false;
                multisampled = false;
                sampled = true;
                break;
            }
            case DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_TYPE:
            case DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_SAMPLER_TYPE: {
                dim = DUSK_IMAGE_DIMENSION_CUBE;
                depth = false;
                arrayed = false;
                multisampled = false;
                sampled = true;
                break;
            }
            case DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_ARRAY_TYPE:
            case DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_ARRAY_SAMPLER_TYPE: {
                dim = DUSK_IMAGE_DIMENSION_CUBE;
                depth = false;
                arrayed = true;
                multisampled = false;
                sampled = true;
                break;
            }
            default: DUSK_ASSERT(0); break;
            }

            expr->type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);
            expr->as_type = duskTypeNewImage(
                compiler,
                sampled_type,
                dim,
                depth,
                arrayed,
                multisampled,
                sampled);

            switch (expr->builtin_call.kind)
            {
            case DUSK_BUILTIN_FUNCTION_IMAGE_1D_SAMPLER_TYPE:
            case DUSK_BUILTIN_FUNCTION_IMAGE_2D_SAMPLER_TYPE:
            case DUSK_BUILTIN_FUNCTION_IMAGE_2D_ARRAY_SAMPLER_TYPE:
            case DUSK_BUILTIN_FUNCTION_IMAGE_3D_SAMPLER_TYPE:
            case DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_SAMPLER_TYPE:
            case DUSK_BUILTIN_FUNCTION_IMAGE_CUBE_ARRAY_SAMPLER_TYPE: {
                expr->as_type =
                    duskTypeNewSampledImage(compiler, expr->as_type);
                break;
            }
            default: break;
            }
            break;
        }
        case DUSK_BUILTIN_FUNCTION_MAX: DUSK_ASSERT(0); break;
        }
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
        duskAddError(
            compiler, expr->location, "expected expression to be assignable");
    }
}

static void duskAnalyzeStmt(
    DuskCompiler *compiler, DuskAnalyzerState *state, DuskStmt *stmt)
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
    case DUSK_STMT_RETURN: {
        DUSK_ASSERT(duskArrayLength(state->function_stack) > 0);
        DuskDecl *func =
            state->function_stack[duskArrayLength(state->function_stack) - 1];

        DUSK_ASSERT(func->type);

        DuskType *return_type = func->type->function.return_type;
        if (stmt->return_.expr)
        {
            duskAnalyzeExpr(
                compiler, state, stmt->return_.expr, return_type, false);
        }
        else
        {
            if (return_type->kind != DUSK_TYPE_VOID)
            {
                duskAddError(
                    compiler,
                    stmt->location,
                    "function return type is not void, expected expression for "
                    "return "
                    "statement");
            }
        }
        break;
    }
    case DUSK_STMT_ASSIGN: {
        duskAnalyzeExpr(
            compiler, state, stmt->assign.assigned_expr, NULL, true);
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
            allocator,
            duskCurrentScope(state),
            DUSK_SCOPE_OWNER_TYPE_NONE,
            NULL);

        for (size_t i = 0; i < duskArrayLength(stmt->block.stmts); ++i)
        {
            DuskStmt *sub_stmt = stmt->block.stmts[i];
            duskAnalyzeStmt(compiler, state, sub_stmt);
        }
        break;
    }
    }
}

static void duskTryRegisterDecl(
    DuskCompiler *compiler, DuskAnalyzerState *state, DuskDecl *decl)
{
    DuskScope *scope = duskCurrentScope(state);

    DUSK_ASSERT(decl->name);
    if (duskScopeLookup(scope, decl->name) != NULL)
    {
        duskAddError(
            compiler,
            decl->location,
            "duplicate declaration: '%s'",
            decl->name);
        return;
    }

    duskScopeSet(scope, decl->name, decl);
}

static void duskAnalyzeDecl(
    DuskCompiler *compiler, DuskAnalyzerState *state, DuskDecl *decl)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    switch (decl->kind)
    {
    case DUSK_DECL_FUNCTION: {
        DUSK_ASSERT(decl->function.scope == NULL);

        {
            DuskStringBuilder *sb = duskStringBuilderCreate(NULL, 1024);
            for (size_t i = 0; i < duskArrayLength(state->module_stack); ++i)
            {
                DuskDecl *module_decl = state->module_stack[i];
                duskStringBuilderAppendFormat(sb, "%s.", module_decl->name);
            }
            duskStringBuilderAppend(sb, decl->name);

            decl->function.link_name = duskStringBuilderBuild(sb, allocator);

            duskStringBuilderDestroy(sb);
        }

        for (size_t i = 0; i < duskArrayLength(decl->attributes); ++i)
        {
            DuskAttribute *attrib = &decl->attributes[i];
            if (strcmp(attrib->name, "entry_point") == 0)
            {
                if (duskArrayLength(attrib->value_exprs) != 1)
                {
                    duskAddError(
                        compiler,
                        decl->location,
                        "invalid value count for attribute \"%s\"",
                        attrib->name);
                    continue;
                }

                if (attrib->value_exprs[0]->kind != DUSK_EXPR_STRING_LITERAL)
                {
                    duskAddError(
                        compiler,
                        decl->location,
                        "invalid value for attribute \"%s\"",
                        attrib->name);
                    continue;
                }

                DuskEntryPoint entry_point = {
                    .function_decl = decl,
                    .name = decl->function.link_name,
                };

                const char *stage_str = attrib->value_exprs[0]->identifier.str;
                if (strcmp(stage_str, "fragment") == 0)
                {
                    entry_point.stage = DUSK_SHADER_STAGE_FRAGMENT;
                }
                else if (strcmp(stage_str, "vertex") == 0)
                {
                    entry_point.stage = DUSK_SHADER_STAGE_VERTEX;
                }
                else if (strcmp(stage_str, "compute") == 0)
                {
                    entry_point.stage = DUSK_SHADER_STAGE_COMPUTE;
                }
                else
                {
                    duskAddError(
                        compiler,
                        decl->location,
                        "invalid shader stage for entry point: \"%s\"",
                        stage_str);
                    continue;
                }

                duskArrayPush(&decl->location.file->entry_points, entry_point);
            }
        }

        decl->function.scope = duskScopeCreate(
            allocator,
            duskCurrentScope(state),
            DUSK_SCOPE_OWNER_TYPE_FUNCTION,
            decl);

        DuskType *type_type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);

        size_t param_count = duskArrayLength(decl->function.parameter_decls);

        bool got_all_param_types = true;
        DuskType *return_type = NULL;
        DuskArray(DuskType *) param_types =
            duskArrayCreate(allocator, DuskType *);
        duskArrayResize(&param_types, param_count);

        duskAnalyzeExpr(
            compiler, state, decl->function.return_type_expr, type_type, false);
        return_type = decl->function.return_type_expr->as_type;

        duskArrayPush(&state->function_stack, decl);
        duskArrayPush(&state->scope_stack, decl->function.scope);

        for (size_t i = 0; i < param_count; ++i)
        {
            DuskDecl *param_decl = decl->function.parameter_decls[i];
            duskTryRegisterDecl(compiler, state, param_decl);
            duskAnalyzeDecl(compiler, state, param_decl);
            param_types[i] = param_decl->type;
            if (param_types[i] == NULL)
            {
                got_all_param_types = false;
            }
        }

        if (!got_all_param_types || return_type == NULL)
        {
            DUSK_ASSERT(duskArrayLength(compiler->errors) > 0);
            break;
        }

        decl->type = duskTypeNewFunction(compiler, return_type, param_types);

        bool got_return_stmt = false;

        for (size_t i = 0; i < duskArrayLength(decl->function.stmts); ++i)
        {
            DuskStmt *stmt = decl->function.stmts[i];
            duskAnalyzeStmt(compiler, state, stmt);

            if (stmt->kind == DUSK_STMT_RETURN)
            {
                got_return_stmt = true;
            }
        }

        if ((decl->type->function.return_type->kind != DUSK_TYPE_VOID) &&
            (!got_return_stmt))
        {
            duskAddError(
                compiler,
                decl->location,
                "no return statement found for function '%s'",
                decl->function.link_name);
        }

        duskArrayPop(&state->function_stack);
        duskArrayPop(&state->scope_stack);

        break;
    }
    case DUSK_DECL_TYPE: {
        DuskType *type_type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);
        duskAnalyzeExpr(
            compiler, state, decl->typedef_.type_expr, type_type, false);
        decl->type = type_type;
        break;
    }
    case DUSK_DECL_VAR: {
        DuskType *type_type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);

        DUSK_ASSERT(decl->var.type_expr);
        duskAnalyzeExpr(compiler, state, decl->var.type_expr, type_type, false);
        DuskType *var_type = decl->var.type_expr->as_type;

        if (!var_type)
        {
            DUSK_ASSERT(duskArrayLength(compiler->errors) > 0);
            break;
        }

        if (decl->var.value_expr)
        {
            duskAnalyzeExpr(
                compiler, state, decl->var.value_expr, var_type, false);
            if (var_type == NULL)
            {
                var_type = decl->var.value_expr->type;
            }
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
        .module_stack = duskArrayCreate(allocator, DuskDecl *),
        .function_stack = duskArrayCreate(allocator, DuskDecl *),
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

    if (duskArrayLength(file->entry_points) == 0)
    {
        duskAddError(
            compiler,
            (DuskLocation){.file = file},
            "no entrypoints found in file");
    }
}
