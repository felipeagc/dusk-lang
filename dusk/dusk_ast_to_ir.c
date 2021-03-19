#include "dusk_internal.h"

static void duskGenerateDecl(DuskCompiler *compiler, DuskIRModule *module, DuskDecl *decl)
{
    (void)compiler;

    if (decl->type)
    {
        duskTypeEmit(decl->type);
    }

    switch (decl->kind)
    {
    case DUSK_DECL_FUNCTION: {
        DUSK_ASSERT(decl->type);
        decl->ir_value = duskIRFunctionCreate(module, decl->type, decl->name);
        duskArrayPush(&module->functions, decl->ir_value);

        break;
    }
    case DUSK_DECL_VAR: {
        break;
    }
    case DUSK_DECL_MODULE: {
        if (strcmp(decl->name, compiler->selected_module) != 0)
        {
            break;
        }

        for (size_t i = 0; i < duskArrayLength(decl->module.decls); ++i)
        {
            DuskDecl *sub_decl = decl->module.decls[i];
            duskGenerateDecl(compiler, module, sub_decl);
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

    for (size_t i = 0; i < duskArrayLength(file->decls); ++i)
    {
        DuskDecl *decl = file->decls[i];

        duskGenerateDecl(compiler, module, decl);
    }

    return module;
}
