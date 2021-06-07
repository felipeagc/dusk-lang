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

    [DUSK_BUILTIN_FUNCTION_SIN] = 1,
    [DUSK_BUILTIN_FUNCTION_COS] = 1,
    [DUSK_BUILTIN_FUNCTION_TAN] = 1,
    [DUSK_BUILTIN_FUNCTION_ASIN] = 1,
    [DUSK_BUILTIN_FUNCTION_ACOS] = 1,
    [DUSK_BUILTIN_FUNCTION_ATAN] = 1,
    [DUSK_BUILTIN_FUNCTION_SINH] = 1,
    [DUSK_BUILTIN_FUNCTION_COSH] = 1,
    [DUSK_BUILTIN_FUNCTION_TANH] = 1,
    [DUSK_BUILTIN_FUNCTION_ASINH] = 1,
    [DUSK_BUILTIN_FUNCTION_ACOSH] = 1,
    [DUSK_BUILTIN_FUNCTION_ATANH] = 1,

    [DUSK_BUILTIN_FUNCTION_RADIANS] = 1,
    [DUSK_BUILTIN_FUNCTION_DEGREES] = 1,
    [DUSK_BUILTIN_FUNCTION_ROUND] = 1,
    [DUSK_BUILTIN_FUNCTION_TRUNC] = 1,
    [DUSK_BUILTIN_FUNCTION_FLOOR] = 1,
    [DUSK_BUILTIN_FUNCTION_CEIL] = 1,
    [DUSK_BUILTIN_FUNCTION_FRACT] = 1,
    [DUSK_BUILTIN_FUNCTION_SQRT] = 1,
    [DUSK_BUILTIN_FUNCTION_INVERSE_SQRT] = 1,
    [DUSK_BUILTIN_FUNCTION_LOG] = 1,
    [DUSK_BUILTIN_FUNCTION_LOG2] = 1,
    [DUSK_BUILTIN_FUNCTION_EXP] = 1,
    [DUSK_BUILTIN_FUNCTION_EXP2] = 1,
};

typedef struct DuskAnalyzerState {
    DuskArray(DuskScope *) scope_stack_arr;
    DuskArray(DuskStmt *) break_stack_arr;
    DuskArray(DuskStmt *) continue_stack_arr;
    DuskArray(DuskDecl *) function_stack_arr;
    DuskArray(DuskStructLayout) struct_layout_stack_arr;
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
    if (type != DUSK_SCOPE_OWNER_TYPE_NONE) {
        DUSK_ASSERT(owner != NULL);
        memcpy(&owner, &scope->owner, sizeof(void *));
    } else {
        DUSK_ASSERT(owner == NULL);
    }
    return scope;
}

DuskDecl *duskScopeLookupLocal(DuskScope *scope, const char *name)
{
    DUSK_ASSERT(scope != NULL);

    DuskDecl *decl = NULL;
    if (duskMapGet(scope->map, name, (void **)&decl)) {
        DUSK_ASSERT(decl != NULL);
        return decl;
    }

    return NULL;
}

DuskDecl *duskScopeLookup(DuskScope *scope, const char *name)
{
    DuskDecl *decl = duskScopeLookupLocal(scope, name);
    if (decl) return decl;

    if (scope->parent) {
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
    DUSK_ASSERT(duskArrayLength(state->scope_stack_arr) > 0);
    return state->scope_stack_arr[duskArrayLength(state->scope_stack_arr) - 1];
}

static void duskConcretizeExprType(DuskExpr *expr, DuskType *expected_type)
{
    if (!expr->type) return;

    switch (expr->kind) {
    case DUSK_EXPR_INT_LITERAL: {
        if (expr->type->kind == DUSK_TYPE_UNTYPED_INT &&
            (expected_type->kind == DUSK_TYPE_INT ||
             expected_type->kind == DUSK_TYPE_FLOAT)) {

            expr->type = expected_type;
        }
        break;
    }
    case DUSK_EXPR_FLOAT_LITERAL: {
        if (expr->type->kind == DUSK_TYPE_UNTYPED_FLOAT &&
            expected_type->kind == DUSK_TYPE_FLOAT) {
            expr->type = expected_type;
        }
        break;
    }

    default: break;
    }
}

static bool duskExprResolveInteger(
    DuskAnalyzerState *state, DuskExpr *expr, int64_t *out_int)
{
    switch (expr->kind) {
    case DUSK_EXPR_INT_LITERAL: {
        *out_int = expr->int_literal;
        return true;
    }
    case DUSK_EXPR_IDENT: {
        DuskDecl *ident_decl =
            duskScopeLookup(duskCurrentScope(state), expr->identifier.str);
        if (!ident_decl) {
            return false;
        }

        switch (ident_decl->kind) {
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
    switch (expr->kind) {
    case DUSK_EXPR_IDENT: {
        DuskScope *scope = duskCurrentScope(state);

        DuskDecl *decl = duskScopeLookup(scope, expr->identifier.str);
        if (!decl) return false;

        if (decl->kind == DUSK_DECL_VAR) {
            switch (decl->var.storage_class) {
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
    switch (kind) {
    case DUSK_ATTRIBUTE_BINDING: return "binding";
    case DUSK_ATTRIBUTE_SET: return "set";
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

static void duskCheckGlobalVariableAttributes(
    DuskAnalyzerState *state,
    DuskCompiler *compiler,
    DuskDecl *var_decl,
    DuskArray(DuskAttribute) attributes_arr)
{
    if (duskArrayLength(state->function_stack_arr)) return;

    DuskAttribute *set_attribute = NULL;
    DuskAttribute *binding_attribute = NULL;
    DuskAttribute *push_constant_attribute = NULL;
    DuskAttribute *uniform_attribute = NULL;
    DuskAttribute *storage_attribute = NULL;

    for (size_t i = 0; i < duskArrayLength(attributes_arr); ++i) {
        DuskAttribute *attribute = &attributes_arr[i];
        switch (attribute->kind) {
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

    if (set_attribute) {
        if (set_attribute->value_expr_count != 1) {
            duskAddError(
                compiler,
                var_decl->location,
                "'set' attribute requires 1 parameter");
        } else {
            int64_t resolved_int;
            if (!duskExprResolveInteger(
                    state, set_attribute->value_exprs[0], &resolved_int)) {
                duskAddError(
                    compiler,
                    var_decl->location,
                    "'set' attribute requires an integer parameter");
            }
        }
    }

    if (binding_attribute) {
        if (binding_attribute->value_expr_count != 1) {
            duskAddError(
                compiler,
                var_decl->location,
                "'binding' attribute requires exactly 1 parameter");
        } else {
            int64_t resolved_int;
            if (!duskExprResolveInteger(
                    state, binding_attribute->value_exprs[0], &resolved_int)) {
                duskAddError(
                    compiler,
                    var_decl->location,
                    "'binding' attribute requires an integer parameter");
            }
        }
    }

    if (push_constant_attribute) {
        if (push_constant_attribute->value_expr_count != 0) {
            duskAddError(
                compiler,
                var_decl->location,
                "'push_constant' attribute requires exactly 0 parameters");
        }
    }

    if (!set_attribute && !binding_attribute && !push_constant_attribute) {
        duskAddError(
            compiler,
            var_decl->location,
            "global variable needs either the 'set' and 'binding' attributes "
            "or a 'push_constant' attribute");
    }

    if ((set_attribute || binding_attribute) && push_constant_attribute) {
        duskAddError(
            compiler,
            var_decl->location,
            "global variable cannot have binding and push constant attributes "
            "at the same time");
    }

    if (!push_constant_attribute && ((set_attribute || binding_attribute) &&
                                     !(set_attribute && binding_attribute))) {
        duskAddError(
            compiler,
            var_decl->location,
            "global variable needs both 'set' and 'binding' attributes");
    }

    if (uniform_attribute && storage_attribute) {
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
    DuskArray(DuskAttribute) attributes_arr)
{
    DuskAttribute *location_attribute = NULL;
    DuskAttribute *builtin_attribute = NULL;

    for (size_t i = 0; i < duskArrayLength(attributes_arr); ++i) {
        DuskAttribute *attribute = &attributes_arr[i];
        switch (attribute->kind) {
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

    if (location_attribute && builtin_attribute) {
        duskAddError(
            compiler,
            location,
            "entry point interface needs either a location or builtin "
            "attribute, not both");
    } else if (location_attribute) {
        if (location_attribute->value_expr_count != 1) {
            duskAddError(
                compiler,
                location,
                "'location' attribute requires exactly 1 parameter");
        } else {
            int64_t resolved_int;
            if (!duskExprResolveInteger(
                    state, location_attribute->value_exprs[0], &resolved_int)) {
                duskAddError(
                    compiler,
                    location,
                    "'location' attribute requires an integer parameter");
            }
        }
    } else if (builtin_attribute) {
        if (builtin_attribute->value_expr_count != 1) {
            duskAddError(
                compiler,
                location,
                "'builtin' attribute requires exactly 1 parameter");
        } else if (builtin_attribute->value_exprs[0]->kind != DUSK_EXPR_IDENT) {
            duskAddError(
                compiler,
                location,
                "builtin attribute needs a builtin name identifier as a "
                "parameter");
        } else {
            // TODO: check if provided builtin identifier is valid
        }
    } else {
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

    switch (expr->kind) {
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
        if (expected_type && expected_type->kind == DUSK_TYPE_FLOAT) {
            expr->type = expected_type;
        } else {
            expr->type = duskTypeNewBasic(compiler, DUSK_TYPE_UNTYPED_FLOAT);
        }
        break;
    }
    case DUSK_EXPR_INT_LITERAL: {
        if (expected_type && (expected_type->kind == DUSK_TYPE_INT ||
                              expected_type->kind == DUSK_TYPE_FLOAT)) {
            expr->type = expected_type;
        } else {
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
        if (!struct_type) {
            DUSK_ASSERT(duskArrayLength(compiler->errors_arr) > 0);
            break;
        }

        if (struct_type->kind != DUSK_TYPE_STRUCT) {
            duskAddError(
                compiler,
                expr->location,
                "expected a struct type for struct literal, instead got: '%s'",
                duskTypeToPrettyString(allocator, struct_type));
            break;
        }

        bool *got_fields = duskAllocateZeroed(
            allocator, sizeof(bool) * struct_type->struct_.field_count);

        for (size_t i = 0;
             i < duskArrayLength(expr->struct_literal.field_names_arr);
             ++i) {
            const char *field_name = expr->struct_literal.field_names_arr[i];
            uintptr_t index;
            if (duskMapGet(
                    struct_type->struct_.index_map,
                    field_name,
                    (void *)&index)) {
                got_fields[index] = true;
            } else {
                duskAddError(
                    compiler,
                    expr->location,
                    "no field called '%s' in type '%s'",
                    field_name,
                    duskTypeToPrettyString(allocator, struct_type));
            }
        }

        bool got_all_fields = true;
        for (size_t i = 0; i < struct_type->struct_.field_count; ++i) {
            const char *field_name = struct_type->struct_.field_names[i];
            if (!got_fields[i]) {
                duskAddError(
                    compiler,
                    expr->location,
                    "missing field '%s' in struct literal for type '%s'",
                    field_name,
                    duskTypeToPrettyString(allocator, struct_type));
                got_all_fields = false;
            }
        }

        if (!got_all_fields) {
            DUSK_ASSERT(duskArrayLength(compiler->errors_arr) > 0);
            break;
        }

        if (struct_type->struct_.field_count !=
            duskArrayLength(expr->struct_literal.field_values_arr)) {
            DUSK_ASSERT(duskArrayLength(compiler->errors_arr) > 0);
            break;
        }

        for (size_t i = 0;
             i < duskArrayLength(expr->struct_literal.field_values_arr);
             ++i) {
            duskAnalyzeExpr(
                compiler,
                state,
                expr->struct_literal.field_values_arr[i],
                struct_type->struct_.field_types[i],
                false);
        }

        expr->type = struct_type;

        break;
    }
    case DUSK_EXPR_IDENT: {
        DuskDecl *ident_decl =
            duskScopeLookup(duskCurrentScope(state), expr->identifier.str);
        if (!ident_decl) {
            duskAddError(
                compiler,
                expr->location,
                "cannot find symbol '%s'",
                expr->identifier.str);
            break;
        }

        expr->identifier.decl = ident_decl;

        expr->type = ident_decl->type;
        if (ident_decl->kind == DUSK_DECL_TYPE) {
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
                state, expr->array_type.size_expr, &array_size)) {
            duskAddError(
                compiler,
                expr->array_type.size_expr->location,
                "failed to resolve integer for array size expression");
            break;
        }

        if (array_size <= 0) {
            duskAddError(
                compiler,
                expr->array_type.size_expr->location,
                "array size must be greater than 0");
            break;
        }

        DuskType *sub_type = expr->array_type.sub_expr->as_type;
        if (!sub_type) {
            break;
        }

        DuskStructLayout layout = DUSK_STRUCT_LAYOUT_UNKNOWN;
        if (duskArrayLength(state->struct_layout_stack_arr) > 0) {
            layout = state->struct_layout_stack_arr
                         [duskArrayLength(state->struct_layout_stack_arr) - 1];
        }

        expr->type = type_type;
        expr->as_type =
            duskTypeNewArray(compiler, layout, sub_type, (size_t)array_size);
        break;
    }
    case DUSK_EXPR_RUNTIME_ARRAY_TYPE: {
        DuskType *type_type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);

        duskAnalyzeExpr(
            compiler, state, expr->array_type.sub_expr, type_type, false);

        DuskType *sub_type = expr->array_type.sub_expr->as_type;
        if (!sub_type) {
            break;
        }

        DuskStructLayout layout = DUSK_STRUCT_LAYOUT_UNKNOWN;
        if (duskArrayLength(state->struct_layout_stack_arr) > 0) {
            layout = state->struct_layout_stack_arr
                         [duskArrayLength(state->struct_layout_stack_arr) - 1];
        }

        expr->type = type_type;
        expr->as_type = duskTypeNewRuntimeArray(compiler, layout, sub_type);
        break;
    }
    case DUSK_EXPR_STRUCT_TYPE: {
        for (size_t i = 0; i < expr->struct_type.field_count; ++i) {
            DuskArray(DuskAttribute) field_attributes_arr =
                expr->struct_type.field_attribute_arrays[i];

            for (size_t j = 0; j < duskArrayLength(field_attributes_arr); ++j) {
                DuskAttribute *attribute = &field_attributes_arr[j];

                for (size_t k = 0; k < attribute->value_expr_count; ++k) {
                    DuskExpr *value_expr = attribute->value_exprs[k];
                    int64_t resolved_int;
                    if (duskExprResolveInteger(
                            state, value_expr, &resolved_int)) {
                        value_expr->resolved_int =
                            duskAllocateZeroed(allocator, sizeof(int64_t));
                        *value_expr->resolved_int = resolved_int;
                    }
                }
            }
        }

        size_t field_count = expr->struct_type.field_count;

        bool got_duplicate_field_names = false;

        DuskMap *field_map = duskMapCreate(NULL, field_count);

        for (size_t i = 0; i < field_count; ++i) {
            if (duskMapGet(field_map, expr->struct_type.field_names[i], NULL)) {
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

        if (got_duplicate_field_names) {
            DUSK_ASSERT(duskArrayLength(compiler->errors_arr) > 0);
            break;
        }

        DuskType *type_type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);

        bool got_all_field_types = true;

        DuskType **field_types =
            DUSK_NEW_ARRAY(allocator, DuskType *, field_count);

        duskArrayPush(
            &state->struct_layout_stack_arr, expr->struct_type.layout);

        for (size_t i = 0; i < field_count; ++i) {
            DuskExpr *field_type_expr = expr->struct_type.field_type_exprs[i];
            duskAnalyzeExpr(compiler, state, field_type_expr, type_type, false);
            if (!field_type_expr->as_type) {
                got_all_field_types = false;
                break;
            }
            field_types[i] = field_type_expr->as_type;
        }

        duskArrayPop(&state->struct_layout_stack_arr);

        if (!got_all_field_types) {
            DUSK_ASSERT(duskArrayLength(compiler->errors_arr) > 0);
            break;
        }

        for (size_t i = 0; i < field_count; ++i) {
            DuskType *field_type = field_types[i];
            if (field_type->kind == DUSK_TYPE_RUNTIME_ARRAY &&
                (i != (field_count - 1))) {
                // Runtime array cannot be in the middle of a struct, only at
                // the end
                duskAddError(
                    compiler,
                    expr->struct_type.field_type_exprs[i]->location,
                    "runtime-sized arrays can only be at the end of a struct");
            }
        }

        expr->type = type_type;
        expr->as_type = duskTypeNewStruct(
            compiler,
            expr->struct_type.name,
            expr->struct_type.layout,
            expr->struct_type.field_count,
            expr->struct_type.field_names,
            field_types,
            expr->struct_type.field_attribute_arrays);

        break;
    }
    case DUSK_EXPR_FUNCTION_CALL: {
        duskAnalyzeExpr(
            compiler, state, expr->function_call.func_expr, NULL, false);
        if (!expr->function_call.func_expr->type) {
            DUSK_ASSERT(duskArrayLength(compiler->errors_arr) > 0);
            break;
        }

        DuskType *func_type = expr->function_call.func_expr->type;
        switch (func_type->kind) {
        case DUSK_TYPE_FUNCTION: {
            expr->type = func_type->function.return_type;
            DUSK_ASSERT(expr->type);

            if (duskArrayLength(expr->function_call.params_arr) !=
                func_type->function.param_type_count) {
                duskAddError(
                    compiler,
                    expr->location,
                    "wrong number of function parameters, exptected %zu, "
                    "instead got %zu",
                    func_type->function.param_type_count,
                    duskArrayLength(expr->function_call.params_arr));
                break;
            }

            for (size_t i = 0;
                 i < duskArrayLength(expr->function_call.params_arr);
                 ++i) {
                DuskExpr *param = expr->function_call.params_arr[i];
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

            size_t param_count =
                duskArrayLength(expr->function_call.params_arr);

            switch (constructed_type->kind) {
            case DUSK_TYPE_INT:
            case DUSK_TYPE_FLOAT: {
                if (param_count != 1) {
                    duskAddError(
                        compiler,
                        expr->location,
                        "wrong parameter count for '%s' constructor, expected "
                        "1, instead got %zu",
                        duskTypeToPrettyString(allocator, constructed_type),
                        param_count);
                    break;
                }

                DuskExpr *param = expr->function_call.params_arr[0];
                duskAnalyzeExpr(compiler, state, param, NULL, false);
                duskConcretizeExprType(param, constructed_type);

                if (!param->type) {
                    DUSK_ASSERT(duskArrayLength(compiler->errors_arr) > 0);
                    break;
                }

                if (param->type->kind != DUSK_TYPE_FLOAT &&
                    param->type->kind != DUSK_TYPE_INT) {
                    duskAddError(
                        compiler,
                        param->location,
                        "expected a scalar parameter for '%s' constructor",
                        duskTypeToPrettyString(allocator, constructed_type));
                    break;
                }

                break;
            }
            case DUSK_TYPE_VECTOR: {
                DuskType *elem_type = constructed_type->vector.sub;

                size_t elem_count = 0;
                bool analyzed_all_params = true;

                for (size_t i = 0; i < param_count; ++i) {
                    DuskExpr *param = expr->function_call.params_arr[i];
                    duskAnalyzeExpr(compiler, state, param, NULL, false);

                    if (!param->type) {
                        analyzed_all_params = false;
                        continue;
                    }

                    duskConcretizeExprType(param, elem_type);

                    if (param->type == elem_type) {
                        elem_count += 1;
                    } else if (
                        param->type->kind == DUSK_TYPE_VECTOR &&
                        param->type->vector.sub == elem_type) {
                        elem_count += param->type->vector.size;
                    } else {
                        duskAddError(
                            compiler,
                            param->location,
                            "unexpected type for vector constructor: '%s'",
                            duskTypeToPrettyString(allocator, param->type));
                    }
                }

                if (!analyzed_all_params) {
                    break;
                }

                if (!(elem_count == 1 ||
                      elem_count == constructed_type->vector.size)) {
                    duskAddError(
                        compiler,
                        expr->location,
                        "wrong element count for '%s' constructor, expected "
                        "%u, instead got %zu",
                        duskTypeToPrettyString(allocator, constructed_type),
                        constructed_type->vector.size,
                        elem_count);
                    break;
                }

                break;
            }
            case DUSK_TYPE_MATRIX: {
                if (!(param_count == 1 ||
                      param_count == constructed_type->matrix.cols)) {
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

                for (size_t i = 0; i < param_count; ++i) {
                    DuskExpr *param = expr->function_call.params_arr[i];
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
        if (required_param_count !=
            duskArrayLength(expr->builtin_call.params_arr)) {
            duskAddError(
                compiler,
                expr->location,
                "invalid parameter count for '@%s' call: expected %zu, "
                "instead got %zu",
                duskGetBuiltinFunctionName(expr->builtin_call.kind),
                required_param_count,
                duskArrayLength(expr->builtin_call.params_arr));
            break;
        }

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
        case DUSK_BUILTIN_FUNCTION_ATANH: {
            DUSK_ASSERT(duskArrayLength(expr->builtin_call.params_arr) == 1);
            DuskExpr *param = expr->builtin_call.params_arr[0];

            DuskType *float_type =
                duskTypeNewScalar(compiler, DUSK_SCALAR_TYPE_FLOAT);

            duskAnalyzeExpr(compiler, state, param, NULL, false);
            duskConcretizeExprType(param, float_type);

            if (param->type->kind != DUSK_TYPE_FLOAT &&
                !(param->type->kind == DUSK_TYPE_VECTOR &&
                  param->type->vector.sub->kind == DUSK_TYPE_FLOAT)) {
                duskAddError(
                    compiler,
                    expr->location,
                    "invalid parameter type for '@%s' call: expected floating "
                    "point scalar or vector, instead got '%s'",
                    duskGetBuiltinFunctionName(expr->builtin_call.kind),
                    duskTypeToPrettyString(allocator, param->type));
                break;
            }

            expr->type = param->type;

            break;
        }

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
                expr->builtin_call.params_arr[0],
                type_type,
                false);
            DuskType *sampled_type = expr->builtin_call.params_arr[0]->as_type;

            if (!sampled_type) {
                DUSK_ASSERT(duskArrayLength(compiler->errors_arr) > 0);
            }

            if (sampled_type->kind != DUSK_TYPE_VOID &&
                sampled_type->kind != DUSK_TYPE_FLOAT &&
                sampled_type->kind != DUSK_TYPE_INT) {
                duskAddError(
                    compiler,
                    expr->builtin_call.params_arr[0]->location,
                    "expected a scalar type or void, instead got '%s'",
                    duskTypeToPrettyString(allocator, sampled_type));
                break;
            }

            DuskImageDimension dim = DUSK_IMAGE_DIMENSION_2D;
            bool depth = false;
            bool arrayed = false;
            bool multisampled = false;
            bool sampled = false;

            switch (expr->builtin_call.kind) {
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

            switch (expr->builtin_call.kind) {
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
        if (!left_expr) {
            DUSK_ASSERT(duskArrayLength(compiler->errors_arr) > 0);
            break;
        }

        for (size_t i = 0; i < duskArrayLength(expr->access.chain_arr); ++i) {
            DuskExpr *right_expr = expr->access.chain_arr[i];
            DUSK_ASSERT(right_expr->kind == DUSK_EXPR_IDENT);
            const char *accessed_field_name = right_expr->identifier.str;

            if (!left_expr->type) {
                DUSK_ASSERT(duskArrayLength(compiler->errors_arr) > 0);
                break;
            }

            switch (left_expr->type->kind) {
            case DUSK_TYPE_VECTOR: {
                size_t new_vec_dim = strlen(accessed_field_name);
                if (new_vec_dim > 4) {
                    duskAddError(
                        compiler,
                        right_expr->location,
                        "invalid vector shuffle: '%s'",
                        accessed_field_name);
                    break;
                }

                DuskArray(uint32_t) shuffle_indices_arr =
                    duskArrayCreate(allocator, uint32_t);
                duskArrayResize(&shuffle_indices_arr, new_vec_dim);

                bool valid = true;
                for (size_t j = 0; j < new_vec_dim; j++) {
                    char c = accessed_field_name[j];
                    switch (c) {
                    case 'x':
                    case 'r': shuffle_indices_arr[j] = 0; break;
                    case 'y':
                    case 'g': shuffle_indices_arr[j] = 1; break;
                    case 'z':
                    case 'b': shuffle_indices_arr[j] = 2; break;
                    case 'w':
                    case 'a': shuffle_indices_arr[j] = 3; break;

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

                    if (shuffle_indices_arr[j] >=
                        left_expr->type->vector.size) {
                        duskAddError(
                            compiler,
                            right_expr->location,
                            "invalid vector shuffle: '%s'",
                            accessed_field_name);
                        break;
                    }

                    if (!valid) {
                        DUSK_ASSERT(duskArrayLength(compiler->errors_arr) > 0);
                        break;
                    }
                }

                if (!valid) {
                    DUSK_ASSERT(duskArrayLength(compiler->errors_arr) > 0);
                    break;
                }

                right_expr->identifier.shuffle_indices_arr =
                    shuffle_indices_arr;

                if (new_vec_dim == 1) {
                    right_expr->type = left_expr->type->vector.sub;
                } else {
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
                        (void *)&field_index)) {
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

    if (!expr->type) {
        DUSK_ASSERT(duskArrayLength(compiler->errors_arr) > 0);
    }

    {
        int64_t resolved_int;
        if (duskExprResolveInteger(state, expr, &resolved_int)) {
            expr->resolved_int =
                duskAllocateZeroed(allocator, sizeof(*expr->resolved_int));
            *expr->resolved_int = resolved_int;
        }
    }

    if (expected_type && expr->type) {
        if (expected_type != expr->type) {
            duskAddError(
                compiler,
                expr->location,
                "type mismatch, expected '%s', instead got '%s'",
                duskTypeToPrettyString(allocator, expected_type),
                duskTypeToPrettyString(allocator, expr->type));
        }
    }

    if (must_be_assignable && !duskIsExprAssignable(state, expr)) {
        duskAddError(
            compiler, expr->location, "expected expression to be assignable");
    }
}

static void duskAnalyzeStmt(
    DuskCompiler *compiler, DuskAnalyzerState *state, DuskStmt *stmt)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    switch (stmt->kind) {
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
        DUSK_ASSERT(duskArrayLength(state->function_stack_arr) > 0);
        DuskDecl *func = state->function_stack_arr
                             [duskArrayLength(state->function_stack_arr) - 1];

        DUSK_ASSERT(func->type);

        DuskType *return_type = func->type->function.return_type;
        if (stmt->return_.expr) {
            duskAnalyzeExpr(
                compiler, state, stmt->return_.expr, return_type, false);
        } else {
            if (return_type->kind != DUSK_TYPE_VOID) {
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
        DUSK_ASSERT(duskArrayLength(state->function_stack_arr) > 0);
        DuskDecl *func = state->function_stack_arr
                             [duskArrayLength(state->function_stack_arr) - 1];

        DUSK_ASSERT(func->type);
        break;
    }
    case DUSK_STMT_ASSIGN: {
        duskAnalyzeExpr(
            compiler, state, stmt->assign.assigned_expr, NULL, true);
        DuskType *type = stmt->assign.assigned_expr->type;
        if (!type) {
            DUSK_ASSERT(duskArrayLength(compiler->errors_arr) > 0);
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

        for (size_t i = 0; i < duskArrayLength(stmt->block.stmts_arr); ++i) {
            DuskStmt *sub_stmt = stmt->block.stmts_arr[i];
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
    if (duskScopeLookup(scope, decl->name) != NULL) {
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

    for (size_t i = 0; i < duskArrayLength(decl->attributes_arr); ++i) {
        DuskAttribute *attribute = &decl->attributes_arr[i];

        for (size_t j = 0; j < attribute->value_expr_count; ++j) {
            DuskExpr *value_expr = attribute->value_exprs[j];
            int64_t resolved_int;
            if (duskExprResolveInteger(state, value_expr, &resolved_int)) {
                value_expr->resolved_int =
                    duskAllocateZeroed(allocator, sizeof(int64_t));
                *value_expr->resolved_int = resolved_int;
            }
        }
    }

    switch (decl->kind) {
    case DUSK_DECL_FUNCTION: {
        DUSK_ASSERT(decl->function.scope == NULL);

        decl->function.link_name = decl->name;

        for (size_t i = 0; i < duskArrayLength(decl->attributes_arr); ++i) {
            DuskAttribute *attrib = &decl->attributes_arr[i];
            switch (attrib->kind) {
            case DUSK_ATTRIBUTE_STAGE: {
                if (attrib->value_expr_count != 1) {
                    duskAddError(
                        compiler,
                        decl->location,
                        "invalid value count for attribute \"%s\"",
                        attrib->name);
                    continue;
                }

                if (attrib->value_exprs[0]->kind != DUSK_EXPR_IDENT) {
                    duskAddError(
                        compiler,
                        decl->location,
                        "invalid value for attribute \"%s\"",
                        attrib->name);
                    continue;
                }

                decl->function.is_entry_point = true;

                const char *stage_str = attrib->value_exprs[0]->identifier.str;
                if (strcmp(stage_str, "fragment") == 0) {
                    decl->function.entry_point_stage =
                        DUSK_SHADER_STAGE_FRAGMENT;
                } else if (strcmp(stage_str, "vertex") == 0) {
                    decl->function.entry_point_stage = DUSK_SHADER_STAGE_VERTEX;
                } else if (strcmp(stage_str, "compute") == 0) {
                    decl->function.entry_point_stage =
                        DUSK_SHADER_STAGE_COMPUTE;
                } else {
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
             i < duskArrayLength(decl->function.return_type_attributes_arr);
             ++i) {
            DuskAttribute *attribute =
                &decl->function.return_type_attributes_arr[i];

            for (size_t j = 0; j < attribute->value_expr_count; ++j) {
                DuskExpr *value_expr = attribute->value_exprs[j];
                int64_t resolved_int;
                if (duskExprResolveInteger(state, value_expr, &resolved_int)) {
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

        size_t param_count =
            duskArrayLength(decl->function.parameter_decls_arr);

        bool got_all_param_types = true;
        DuskType *return_type = NULL;
        DuskType **param_types =
            DUSK_NEW_ARRAY(allocator, DuskType *, param_count);

        duskAnalyzeExpr(
            compiler, state, decl->function.return_type_expr, type_type, false);
        return_type = decl->function.return_type_expr->as_type;

        duskArrayPush(&state->function_stack_arr, decl);
        duskArrayPush(&state->scope_stack_arr, decl->function.scope);

        for (size_t i = 0; i < param_count; ++i) {
            DuskDecl *param_decl = decl->function.parameter_decls_arr[i];
            duskTryRegisterDecl(compiler, state, param_decl);
            duskAnalyzeDecl(compiler, state, param_decl);
            param_types[i] = param_decl->type;
            if (param_types[i] == NULL) {
                got_all_param_types = false;
            }
        }

        if (!got_all_param_types || return_type == NULL) {
            duskArrayPop(&state->function_stack_arr);
            duskArrayPop(&state->scope_stack_arr);
            DUSK_ASSERT(duskArrayLength(compiler->errors_arr) > 0);
            break;
        }

        decl->type = duskTypeNewFunction(
            compiler, return_type, param_count, param_types);

        if (decl->function.is_entry_point) {
            for (size_t i = 0; i < param_count; ++i) {
                DuskDecl *param_decl = decl->function.parameter_decls_arr[i];
                if (param_decl->type->kind != DUSK_TYPE_STRUCT) {
                    duskCheckEntryPointInterfaceAttributes(
                        state,
                        compiler,
                        param_decl->location,
                        param_decl->attributes_arr);
                } else {
                    DuskType *struct_type = param_decl->type;
                    for (size_t j = 0; j < struct_type->struct_.field_count;
                         ++j) {
                        DuskArray(DuskAttribute) field_attributes_arr =
                            struct_type->struct_.field_attribute_arrays[j];

                        duskCheckEntryPointInterfaceAttributes(
                            state,
                            compiler,
                            param_decl->location,
                            field_attributes_arr);
                    }
                }
            }

            switch (decl->type->function.return_type->kind) {
            case DUSK_TYPE_VOID: break;
            case DUSK_TYPE_STRUCT: {
                DuskType *struct_type = decl->type->function.return_type;
                for (size_t j = 0; j < struct_type->struct_.field_count; ++j) {
                    DuskArray(DuskAttribute) field_attributes_arr =
                        struct_type->struct_.field_attribute_arrays[j];

                    duskCheckEntryPointInterfaceAttributes(
                        state, compiler, decl->location, field_attributes_arr);
                }
                break;
            }
            default: {
                duskCheckEntryPointInterfaceAttributes(
                    state,
                    compiler,
                    decl->location,
                    decl->function.return_type_attributes_arr);
                break;
            }
            }
        }

        bool got_return_stmt = false;

        for (size_t i = 0; i < duskArrayLength(decl->function.stmts_arr); ++i) {
            DuskStmt *stmt = decl->function.stmts_arr[i];
            duskAnalyzeStmt(compiler, state, stmt);

            if (stmt->kind == DUSK_STMT_RETURN) {
                got_return_stmt = true;
            }
        }

        if ((decl->type->function.return_type->kind != DUSK_TYPE_VOID) &&
            (!got_return_stmt)) {
            duskAddError(
                compiler,
                decl->location,
                "no return statement found for function '%s'",
                decl->function.link_name);
        }

        duskArrayPop(&state->function_stack_arr);
        duskArrayPop(&state->scope_stack_arr);

        break;
    }
    case DUSK_DECL_TYPE: {
        DuskType *type_type = duskTypeNewBasic(compiler, DUSK_TYPE_TYPE);

        if (decl->typedef_.type_expr->kind == DUSK_EXPR_STRUCT_TYPE) {
            decl->typedef_.type_expr->struct_type.name = decl->name;
        }

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

        if (!var_type) {
            DUSK_ASSERT(duskArrayLength(compiler->errors_arr) > 0);
            break;
        }

        if (decl->var.value_expr) {
            duskAnalyzeExpr(
                compiler, state, decl->var.value_expr, var_type, false);
            if (var_type == NULL) {
                var_type = decl->var.value_expr->type;
            }
        }

        if (!duskTypeIsRuntime(var_type)) {
            duskAddError(
                compiler,
                decl->location,
                "variable type is not representable at runtime: '%s'",
                duskTypeToPrettyString(allocator, var_type));
            break;
        }

        decl->type = var_type;

        if (duskArrayLength(state->function_stack_arr) == 0) {
            duskCheckGlobalVariableAttributes(
                state, compiler, decl, decl->attributes_arr);
        }

        // Set the storage class

        if (duskArrayLength(state->function_stack_arr) == 0) {
            decl->var.storage_class = DUSK_STORAGE_CLASS_UNIFORM;

            switch (decl->type->kind) {
            case DUSK_TYPE_SAMPLER:
            case DUSK_TYPE_IMAGE:
            case DUSK_TYPE_SAMPLED_IMAGE: {
                decl->var.storage_class = DUSK_STORAGE_CLASS_UNIFORM_CONSTANT;
                break;
            }
            default:
                decl->var.storage_class = DUSK_STORAGE_CLASS_UNIFORM;
                break;
            }

            for (size_t i = 0; i < duskArrayLength(decl->attributes_arr); ++i) {
                DuskAttribute *attribute = &decl->attributes_arr[i];
                switch (attribute->kind) {
                case DUSK_ATTRIBUTE_UNIFORM: {
                    decl->var.storage_class = DUSK_STORAGE_CLASS_UNIFORM;
                    break;
                }
                case DUSK_ATTRIBUTE_STORAGE: {
                    decl->var.storage_class = DUSK_STORAGE_CLASS_STORAGE;
                    break;
                }
                case DUSK_ATTRIBUTE_PUSH_CONSTANT: {
                    decl->var.storage_class = DUSK_STORAGE_CLASS_PUSH_CONSTANT;
                    break;
                }
                default: break;
                }
            }
        }

        // Check if struct type has proper layout

        if (decl->type->kind == DUSK_TYPE_STRUCT) {
            switch (decl->var.storage_class) {
            case DUSK_STORAGE_CLASS_PUSH_CONSTANT:
            case DUSK_STORAGE_CLASS_UNIFORM: {
                if (decl->type->struct_.layout != DUSK_STRUCT_LAYOUT_STD140) {
                    duskAddError(
                        compiler,
                        decl->location,
                        "uniform buffer requires structure to have the "
                        "'std140' layout");
                }
                break;
            }
            case DUSK_STORAGE_CLASS_STORAGE: {
                if (decl->type->struct_.layout != DUSK_STRUCT_LAYOUT_STD430) {
                    duskAddError(
                        compiler,
                        decl->location,
                        "storage buffer requires structure to have the "
                        "'std430' layout");
                }
                break;
            }

            default: break;
            }
        }

        break;
    }
    }
}

void duskAnalyzeFile(DuskCompiler *compiler, DuskFile *file)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    DuskAnalyzerState *state = DUSK_NEW(allocator, DuskAnalyzerState);
    *state = (DuskAnalyzerState){
        .scope_stack_arr = duskArrayCreate(allocator, DuskScope *),
        .break_stack_arr = duskArrayCreate(allocator, DuskStmt *),
        .continue_stack_arr = duskArrayCreate(allocator, DuskStmt *),
        .function_stack_arr = duskArrayCreate(allocator, DuskDecl *),
        .struct_layout_stack_arr = duskArrayCreate(allocator, DuskStructLayout),
    };

    duskArrayPush(&state->scope_stack_arr, file->scope);

    for (size_t i = 0; i < duskArrayLength(file->decls_arr); ++i) {
        DuskDecl *decl = file->decls_arr[i];
        duskTryRegisterDecl(compiler, state, decl);
    }

    for (size_t i = 0; i < duskArrayLength(file->decls_arr); ++i) {
        DuskDecl *decl = file->decls_arr[i];
        duskAnalyzeDecl(compiler, state, decl);
    }

    duskArrayPop(&state->scope_stack_arr);
}
