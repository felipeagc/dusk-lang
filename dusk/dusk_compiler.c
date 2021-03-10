#include "dusk_internal.h"

#include <stdlib.h>

DuskCompiler *duskCompilerCreate(void)
{
    DuskCompiler *compiler = malloc(sizeof(*compiler));
    *compiler = (DuskCompiler){
        .main_arena = duskArenaCreate(NULL, 1 << 13),
    };
    return compiler;
}

void duskCompilerDestroy(DuskCompiler *compiler)
{
    duskArenaDestroy(compiler->main_arena);
    free(compiler);
}

void duskCompile(DuskCompiler *compiler, const char *text, size_t text_size)
{
    duskParse(compiler, text, text_size);
}
