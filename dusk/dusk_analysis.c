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

    case DUSK_EXPR_STRUCT_LITERAL:
    case DUSK_EXPR_ACCESS:
    case DUSK_EXPR_FUNCTION_CALL:
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
            case DUSK_STORAGE_CLASS_STORAGE: return true;
            case DUSK_STORAGE_CLASS_UNIFORM:
            case DUSK_STORAGE_CLASS_UNIFORM_CONSTANT:
            case DUSK_STORAGE_CLASS_PUSH_CONSTANT:
            case DUSK_STORAGE_CLASS_PARAMETER: return false;
            }
        }

        break;
    }
    case DUSK_EXPR_ACCESS: {
        return duskIsExprAssignable(state, expr->access.base_expr);
    }
    default: {
        break;
    }
    }

    return false;
}

static const char *duskGetAttributeName(DuskAttributeKind kind)
{
    switch (kind)
    {
    case DUSK_ATTRIBUTE_BINDING: return "binding";
    case DUSK_ATTRIBUTE_SET: return "set";
    case DUSK_ATTRIBUTE_BLOCK: return "block";
    case DUSK_ATTRIBUTE_BUILTIN: return "builtin";
    case DUSK_ATTRIBUTE_LOCATION: return "location";
    case DUSK_ATTRIBUTE_OFFSET: return "offset";
    case DUSK_ATTRIBUTE_PUSH_CONSTANT: return "push_constant";
    case DUSK_ATTRIBUTE_STAGE: return "stage";
    case DUSK_ATTRIBUTE_STORAGE: return "storage";
    case DUSK_ATTRIBUTE_UNIFORM: return "uniform";
    case DUSK_ATTRIBUTE_UNKNOWN: return "<unknown>";
    }
    return "<unknown>";
}

static void duskCheckTypeAttributes(
    DuskAnalyzerState *state,
    DuskCompiler *compiler,
    DuskExpr *type_expr,
    DuskArray(DuskAttribute) attributes)
{
    (void)state;

    DuskAttribute *block_attribute = NULL;

    for (size_t i = 0; i < duskArrayLength(attributes); ++i)
    {
        DuskAttribute *attribute = &attributes[i];
        switch (attribute->kind)
        {
        case DUSK_ATTRIBUTE_BLOCK: block_attribute = attribute; break;
        default: {
            duskAddError(
                compiler,
                type_expr->location,
                "unexpected attribute: '%s'",
                duskGetAttributeName(attribute->kind));
            break;
        }
        }
    }

    if (!type_expr->as_type) return;

    if (type_expr->as_type->kind != DUSK_TYPE_STRUCT && block_attribute)
    {
        duskAddError(
            compiler,
            type_expr->location,
            "'block' attribute is only used for struct types");
    }
}

static void duskCheckGlobalVariableAttributes(
    DuskAnalyzerState *state,
    DuskCompiler *compiler,
    DuskDecl *var_decl,
    DuskArray(DuskAttribute) attributes)
{
    if (duskArrayLength(state->function_stack)) return;

    DuskAttribute *set_attribute = NULL;
    DuskAttribute *binding_attribute = NULL;
    DuskAttribute *push_constant_attribute = NULL;
    DuskAttribute *uniform_attribute = NULL;
    DuskAttribute *storage_attribute = NULL;

    for (size_t i = 0; i < duskArrayLength(attributes); ++i)
    {
        DuskAttribute *attribute = &attributes[i];
        switch (attribute->kind)
        {
        case DUSK_ATTRIBUTE_SET: set_attribute = attribute; break;
        case DUSK_ATTRIBUTE_BINDING: binding_attribute = attribute; break;
        case DUSK_ATTRIBUTE_PUSH_CONSTANT:
            push_constant_attribute = attribute;
            break;
        case DUSK_ATTRIBUTE_UNIFORM: uniform_attribute = attribute; break;
        case DUSK_ATTRIBUTE_STORAGE: storage_attribute = attribute; break;
        default: {
            duskAddError(
                compiler,
                var_decl->location,
                "unexpected attribute: '%s'",
                duskGetAttributeName(attribute->kind));
            break;
        }
        }
    }

    if (!set_attribute && !binding_attribute && !push_constant_attribute)
    {
        duskAddError(
            compiler,
            var_decl->location,
            "global variable needs either the 'set' and 'binding' attributes "
            "or a 'push_constant' attribute");
    }

    if ((set_attribute || binding_attribute) && push_constant_attribute)
    {
        duskAddError(
            compiler,
            var_decl->location,
            "global variable cannot have binding and push constant attributes "
            "at the same time");
    }

    if (!push_constant_attribute && ((set_attribute || binding_attribute) &&
                                     !(set_attribute && binding_attribute)))
    {
        duskAddError(
            compiler,
            var_decl->location,
            "global variable needs both 'set' and 'binding' attributes");
    }

    if (uniform_attribute && storage_attribute)
    {
        duskAddError(
            compiler,
            var_decl->location,
            "global variable cannot have both 'uniform' and 'storage' "
            "attributes");
    }
}

static void duskCheckEntryPointInterfaceAttributes(
    DuskAnalyzerState *state,
    DuskCompiler *compiler,
    DuskLocation location,
    DuskArray(DuskAttribute) attributes)
{
    DuskAttribute *location_attribute = NULL;
    DuskAttribute *builtin_attribute = NULL;

    for (size_t i = 0; i < duskArrayLength(attributes); ++i)
    {
        DuskAttribute *attribute = &attributes[i];
        switch (attribute->kind)
        {
        case DUSK_ATTRIBUTE_LOCATION: location_attribute = attribute; break;
        case DUSK_ATTRIBUTE_BUILTIN: builtin_attribute = attribute; break;
        default: {
            duskAddError(
                compiler,
                location,
                "unexpected attribute: '%s'",
                duskGetAttributeName(attribute->kind));
            break;
        }
        }
    }

    if (location_attribute && builtin_attribute)
    {
        duskAddError(
            compiler,
            location,
            "entry point interface needs either a location or builtin "
            "attribute, not both");
    }
    else if (location_attribute)
    {
        if (duskArrayLength(location_attribute->value_exprs) != 1)
        {
            duskAddError(
                compiler,
                location,
                "location attribute needs exactly 1 parameter");
        }
        else
        {
            int64_t resolved_int;
            if (!duskExprResolveInteger(
                    state, location_attribute->value_exprs[0], &resolved_int))
            {
                duskAddError(
                    compiler,
                    location,
                    "location attribute needs an integer parameter");
            }
        }
    }
    else if (builtin_attribute)
    {
        if (duskArrayLength(builtin_attribute->value_exprs) != 1)
        {
            duskAddError(
                compiler,
                location,
                "builtin attribute needs exactly 1 parameter");
        }
        else if (builtin_attribute->value_exprs[0]->kind != DUSK_EXPR_IDENT)
        {
            duskAddError(
                compiler,
                location,
                "builtin attribute needs a builtin name identifier as a "
                "parameter");
        }
        else
        {
            // TODO: check if provided builtin identifier is valid
        }
    }
    else
    {
        duskAddError(
            compiler,
            location,
            "entry point interface needs either a location or builtin "
            "attribute");
    }
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
    case DUSK_EXPR_STRUCT_LITERAL: {
        DuskType *type_type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);

        duskAnalyzeExpr(
            compiler, state, expr->struct_literal.type_expr, type_type, false);

        DuskType *struct_type = expr->struct_literal.type_expr->as_type;
        if (!struct_type)
        {
            DUSK_ASSERT(duskArrayLength(compiler->errors) > 0);
            break;
        }

        if (struct_type->kind != DUSK_TYPE_STRUCT)
        {
            duskAddError(
                compiler,
                expr->location,
                "expected a struct type for struct literal, instead got: '%s'",
                duskTypeToPrettyString(allocator, struct_type));
            break;
        }

        bool *got_fields = duskAllocateZeroed(
            allocator,
            sizeof(bool) * duskArrayLength(struct_type->struct_.field_types));

        for (size_t i = 0;
             i < duskArrayLength(expr->struct_literal.field_names);
             ++i)
        {
            const char *field_name = expr->struct_literal.field_names[i];
            uintptr_t index;
            if (duskMapGet(
                    struct_type->struct_.index_map, field_name, (void *)&index))
            {
                got_fields[index] = true;
            }
            else
            {
                duskAddError(
                    compiler,
                    expr->location,
                    "no field called '%s' in type '%s'",
                    field_name,
                    duskTypeToPrettyString(allocator, struct_type));
            }
        }

        bool got_all_fields = true;
        for (size_t i = 0;
             i < duskArrayLength(struct_type->struct_.field_types);
             ++i)
        {
            const char *field_name = struct_type->struct_.field_names[i];
            if (!got_fields[i])
            {
                duskAddError(
                    compiler,
                    expr->location,
                    "missing field '%s' in struct literal for type '%s'",
                    field_name,
                    duskTypeToPrettyString(allocator, struct_type));
                got_all_fields = false;
            }
        }

        if (!got_all_fields)
        {
            DUSK_ASSERT(duskArrayLength(compiler->errors) > 0);
            break;
        }

        if (duskArrayLength(struct_type->struct_.field_types) !=
            duskArrayLength(expr->struct_literal.field_values))
        {
            DUSK_ASSERT(duskArrayLength(compiler->errors) > 0);
            break;
        }

        for (size_t i = 0;
             i < duskArrayLength(expr->struct_literal.field_values);
             ++i)
        {
            duskAnalyzeExpr(
                compiler,
                state,
                expr->struct_literal.field_values[i],
                struct_type->struct_.field_types[i],
                false);
        }

        expr->type = struct_type;

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
        for (size_t i = 0;
             i < duskArrayLength(expr->struct_type.field_attributes);
             ++i)
        {
            DuskArray(DuskAttribute) field_attributes =
                expr->struct_type.field_attributes[i];

            for (size_t j = 0; j < duskArrayLength(field_attributes); ++j)
            {
                DuskAttribute *attribute = &field_attributes[j];

                for (size_t k = 0; k < duskArrayLength(attribute->value_exprs);
                     ++k)
                {
                    DuskExpr *value_expr = attribute->value_exprs[k];
                    int64_t resolved_int;
                    if (duskExprResolveInteger(
                            state, value_expr, &resolved_int))
                    {
                        value_expr->resolved_int =
                            duskAllocateZeroed(allocator, sizeof(int64_t));
                        *value_expr->resolved_int = resolved_int;
                    }
                }
            }
        }

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
            field_types,
            expr->struct_type.field_attributes);

        break;
    }
    case DUSK_EXPR_FUNCTION_CALL: {
        duskAnalyzeExpr(
            compiler, state, expr->function_call.func_expr, NULL, false);
        if (!expr->function_call.func_expr->type)
        {
            DUSK_ASSERT(duskArrayLength(compiler->errors) > 0);
            break;
        }

        DuskType *func_type = expr->function_call.func_expr->type;
        switch (func_type->kind)
        {
        case DUSK_TYPE_FUNCTION: {
            expr->type = func_type->function.return_type;
            DUSK_ASSERT(expr->type);

            if (duskArrayLength(expr->function_call.params) !=
                duskArrayLength(func_type->function.param_types))
            {
                duskAddError(
                    compiler,
                    expr->location,
                    "wrong number of function parameters, exptected %zu, "
                    "instead got %zu",
                    duskArrayLength(func_type->function.param_types),
                    duskArrayLength(expr->function_call.params));
                break;
            }

            for (size_t i = 0; i < duskArrayLength(expr->function_call.params);
                 ++i)
            {
                DuskExpr *param = expr->function_call.params[i];
                DuskType *expected_param_type =
                    func_type->function.param_types[i];
                duskAnalyzeExpr(
                    compiler, state, param, expected_param_type, false);
            }
            break;
        }
        case DUSK_TYPE_TYPE: {
            DuskType *constructed_type = expr->function_call.func_expr->as_type;
            DUSK_ASSERT(constructed_type);
            expr->type = constructed_type;

            size_t param_count = duskArrayLength(expr->function_call.params);

            switch (constructed_type->kind)
            {
            case DUSK_TYPE_VECTOR: {
                if (!(param_count == 1 ||
                      param_count == constructed_type->vector.size))
                {
                    duskAddError(
                        compiler,
                        expr->location,
                        "wrong element count for '%s' constructor, expected "
                        "%u, instead got %zu",
                        duskTypeToPrettyString(allocator, constructed_type),
                        constructed_type->vector.size,
                        param_count);
                    break;
                }

                for (size_t i = 0; i < param_count; ++i)
                {
                    DuskExpr *param = expr->function_call.params[i];
                    DuskType *expected_param_type =
                        constructed_type->vector.sub;
                    duskAnalyzeExpr(
                        compiler, state, param, expected_param_type, false);
                }

                break;
            }
            case DUSK_TYPE_MATRIX: {
                if (!(param_count == 1 ||
                      param_count == constructed_type->matrix.cols))
                {
                    duskAddError(
                        compiler,
                        expr->location,
                        "wrong element count for '%s' constructor, expected "
                        "%u, instead got %zu",
                        duskTypeToPrettyString(allocator, constructed_type),
                        constructed_type->matrix.cols,
                        param_count);
                    break;
                }

                for (size_t i = 0; i < param_count; ++i)
                {
                    DuskExpr *param = expr->function_call.params[i];
                    DuskType *expected_param_type =
                        constructed_type->matrix.col_type;
                    duskAnalyzeExpr(
                        compiler, state, param, expected_param_type, false);
                }

                break;
            }
            default: {
                expr->type = NULL;
                duskAddError(
                    compiler,
                    expr->location,
                    "cannot construct type: '%s'",
                    duskTypeToPrettyString(allocator, constructed_type));
                break;
            }
            }

            break;
        }
        default: {
            duskAddError(
                compiler,
                expr->location,
                "called expression is not of type function, instead got type: "
                "'%s'",
                duskTypeToPrettyString(allocator, func_type));
            break;
        }
        }

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

    case DUSK_EXPR_ACCESS: {
        duskAnalyzeExpr(compiler, state, expr->access.base_expr, NULL, false);
        DuskExpr *left_expr = expr->access.base_expr;
        if (!left_expr)
        {
            DUSK_ASSERT(duskArrayLength(compiler->errors) > 0);
            break;
        }

        for (size_t i = 0; i < duskArrayLength(expr->access.chain); ++i)
        {
            DuskExpr *right_expr = expr->access.chain[i];
            DUSK_ASSERT(right_expr->kind == DUSK_EXPR_IDENT);
            const char *accessed_field_name = right_expr->identifier.str;

            if (!left_expr->type)
            {
                DUSK_ASSERT(duskArrayLength(compiler->errors) > 0);
                break;
            }

            switch (left_expr->type->kind)
            {
            case DUSK_TYPE_VECTOR: {
                size_t new_vec_dim = strlen(accessed_field_name);
                if (new_vec_dim > 4)
                {
                    duskAddError(
                        compiler,
                        right_expr->location,
                        "invalid vector shuffle: '%s'",
                        accessed_field_name);
                    break;
                }

                DuskArray(uint32_t) shuffle_indices =
                    duskArrayCreate(allocator, uint32_t);
                duskArrayResize(&shuffle_indices, new_vec_dim);

                bool valid = true;
                for (size_t j = 0; j < new_vec_dim; j++)
                {
                    char c = accessed_field_name[j];
                    switch (c)
                    {
                    case 'x':
                    case 'r': shuffle_indices[j] = 0; break;
                    case 'y':
                    case 'g': shuffle_indices[j] = 1; break;
                    case 'z':
                    case 'b': shuffle_indices[j] = 2; break;
                    case 'w':
                    case 'a': shuffle_indices[j] = 3; break;

                    default: {
                        duskAddError(
                            compiler,
                            right_expr->location,
                            "invalid vector element in shuffle: '%c'",
                            c);
                        valid = false;
                        break;
                    }
                    }

                    if (shuffle_indices[j] >= left_expr->type->vector.size)
                    {
                        duskAddError(
                            compiler,
                            right_expr->location,
                            "invalid vector shuffle: '%s'",
                            accessed_field_name);
                        break;
                    }

                    if (!valid)
                    {
                        DUSK_ASSERT(duskArrayLength(compiler->errors) > 0);
                        break;
                    }
                }

                if (!valid)
                {
                    DUSK_ASSERT(duskArrayLength(compiler->errors) > 0);
                    break;
                }

                right_expr->identifier.shuffle_indices = shuffle_indices;

                if (new_vec_dim == 1)
                {
                    right_expr->type = left_expr->type->vector.sub;
                }
                else
                {
                    right_expr->type = duskTypeNewVector(
                        compiler, left_expr->type->vector.sub, new_vec_dim);
                }

                break;
            }
            case DUSK_TYPE_STRUCT: {
                uintptr_t field_index = 0;
                if (!duskMapGet(
                        left_expr->type->struct_.index_map,
                        accessed_field_name,
                        (void *)&field_index))
                {
                    duskAddError(
                        compiler,
                        right_expr->location,
                        "no struct field named '%s' in type '%s'",
                        accessed_field_name,
                        duskTypeToPrettyString(allocator, left_expr->type));
                    break;
                }

                right_expr->type =
                    left_expr->type->struct_.field_types[field_index];

                break;
            }
            default: {
                duskAddError(
                    compiler,
                    left_expr->location,
                    "expression of type '%s' cannot be accessed",
                    duskTypeToPrettyString(allocator, left_expr->type));
                break;
            }
            }

            left_expr = right_expr;
        }

        expr->type = left_expr->type;

        break;
    }
    }

    if (!expr->type)
    {
        DUSK_ASSERT(duskArrayLength(compiler->errors) > 0);
    }

    {
        int64_t resolved_int;
        if (duskExprResolveInteger(state, expr, &resolved_int))
        {
            expr->resolved_int =
                duskAllocateZeroed(allocator, sizeof(*expr->resolved_int));
            *expr->resolved_int = resolved_int;
        }
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
    case DUSK_STMT_DISCARD: {
        DUSK_ASSERT(duskArrayLength(state->function_stack) > 0);
        DuskDecl *func =
            state->function_stack[duskArrayLength(state->function_stack) - 1];

        DUSK_ASSERT(func->type);
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

    for (size_t i = 0; i < duskArrayLength(decl->attributes); ++i)
    {
        DuskAttribute *attribute = &decl->attributes[i];

        for (size_t j = 0; j < duskArrayLength(attribute->value_exprs); ++j)
        {
            DuskExpr *value_expr = attribute->value_exprs[j];
            int64_t resolved_int;
            if (duskExprResolveInteger(state, value_expr, &resolved_int))
            {
                value_expr->resolved_int =
                    duskAllocateZeroed(allocator, sizeof(int64_t));
                *value_expr->resolved_int = resolved_int;
            }
        }
    }

    switch (decl->kind)
    {
    case DUSK_DECL_FUNCTION: {
        DUSK_ASSERT(decl->function.scope == NULL);

        decl->function.link_name = decl->name;

        for (size_t i = 0; i < duskArrayLength(decl->attributes); ++i)
        {
            DuskAttribute *attrib = &decl->attributes[i];
            switch (attrib->kind)
            {
            case DUSK_ATTRIBUTE_STAGE: {
                if (duskArrayLength(attrib->value_exprs) != 1)
                {
                    duskAddError(
                        compiler,
                        decl->location,
                        "invalid value count for attribute \"%s\"",
                        attrib->name);
                    continue;
                }

                if (attrib->value_exprs[0]->kind != DUSK_EXPR_IDENT)
                {
                    duskAddError(
                        compiler,
                        decl->location,
                        "invalid value for attribute \"%s\"",
                        attrib->name);
                    continue;
                }

                decl->function.is_entry_point = true;

                const char *stage_str = attrib->value_exprs[0]->identifier.str;
                if (strcmp(stage_str, "fragment") == 0)
                {
                    decl->function.entry_point_stage =
                        DUSK_SHADER_STAGE_FRAGMENT;
                }
                else if (strcmp(stage_str, "vertex") == 0)
                {
                    decl->function.entry_point_stage = DUSK_SHADER_STAGE_VERTEX;
                }
                else if (strcmp(stage_str, "compute") == 0)
                {
                    decl->function.entry_point_stage =
                        DUSK_SHADER_STAGE_COMPUTE;
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
                break;
            }

            default: break;
            }
        }

        for (size_t i = 0;
             i < duskArrayLength(decl->function.return_type_attributes);
             ++i)
        {
            DuskAttribute *attribute =
                &decl->function.return_type_attributes[i];

            for (size_t j = 0; j < duskArrayLength(attribute->value_exprs); ++j)
            {
                DuskExpr *value_expr = attribute->value_exprs[j];
                int64_t resolved_int;
                if (duskExprResolveInteger(state, value_expr, &resolved_int))
                {
                    value_expr->resolved_int =
                        duskAllocateZeroed(allocator, sizeof(int64_t));
                    *value_expr->resolved_int = resolved_int;
                }
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
            duskArrayPop(&state->function_stack);
            duskArrayPop(&state->scope_stack);
            DUSK_ASSERT(duskArrayLength(compiler->errors) > 0);
            break;
        }

        decl->type = duskTypeNewFunction(compiler, return_type, param_types);

        if (decl->function.is_entry_point)
        {
            for (size_t i = 0; i < param_count; ++i)
            {
                DuskDecl *param_decl = decl->function.parameter_decls[i];
                if (param_decl->type->kind != DUSK_TYPE_STRUCT)
                {
                    duskCheckEntryPointInterfaceAttributes(
                        state,
                        compiler,
                        param_decl->location,
                        param_decl->attributes);
                }
                else
                {
                    DuskType *struct_type = param_decl->type;
                    for (size_t j = 0;
                         j < duskArrayLength(struct_type->struct_.field_types);
                         ++j)
                    {
                        DuskArray(DuskAttribute) field_attributes =
                            struct_type->struct_.field_attributes[j];

                        duskCheckEntryPointInterfaceAttributes(
                            state,
                            compiler,
                            param_decl->location,
                            field_attributes);
                    }
                }
            }

            switch (decl->type->function.return_type->kind)
            {
            case DUSK_TYPE_VOID: break;
            case DUSK_TYPE_STRUCT: {
                DuskType *struct_type = decl->type->function.return_type;
                for (size_t j = 0;
                     j < duskArrayLength(struct_type->struct_.field_types);
                     ++j)
                {
                    DuskArray(DuskAttribute) field_attributes =
                        struct_type->struct_.field_attributes[j];

                    duskCheckEntryPointInterfaceAttributes(
                        state, compiler, decl->location, field_attributes);
                }
                break;
            }
            default: {
                duskCheckEntryPointInterfaceAttributes(
                    state,
                    compiler,
                    decl->location,
                    decl->function.return_type_attributes);
                break;
            }
            }
        }

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

        if (decl->typedef_.type_expr->kind == DUSK_EXPR_STRUCT_TYPE)
        {
            decl->typedef_.type_expr->struct_type.name = decl->name;
        }

        duskAnalyzeExpr(
            compiler, state, decl->typedef_.type_expr, type_type, false);
        decl->type = type_type;

        duskCheckTypeAttributes(
            state, compiler, decl->typedef_.type_expr, decl->attributes);
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

        if (duskArrayLength(state->function_stack) == 0)
        {
            duskCheckGlobalVariableAttributes(
                state, compiler, decl, decl->attributes);
        }

        // TODO: check if variable has struct type. If yes,
        // check if it has the proper layout for the given storage class

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
}
