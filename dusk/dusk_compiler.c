#include "dusk_internal.h"

#include <stdio.h>
#include <stdlib.h>

DuskCompiler *duskCompilerCreate(void)
{
    DuskCompiler *compiler = malloc(sizeof(*compiler));

    DuskArena *arena = duskArenaCreate(NULL, 1 << 13);
    DuskAllocator *allocator = duskArenaGetAllocator(arena);

    *compiler = (DuskCompiler){
        .main_arena = arena,
        .errors = duskArrayCreate(allocator, DuskError),
    };
    return compiler;
}

void duskCompilerDestroy(DuskCompiler *compiler)
{
    duskArenaDestroy(compiler->main_arena);
    free(compiler);
}

void duskThrow(DuskCompiler *compiler)
{
    longjmp(compiler->jump_buffer, 1);
}

void duskAddError(DuskCompiler *compiler, DuskLocation loc, const char *fmt, ...)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);
    va_list vl;
    va_start(vl, fmt);
    const char *message = duskVsprintf(allocator, fmt, vl);

    DuskError error = {
        .message = message,
        .location = loc,
    };

    duskArrayPush(&compiler->errors, error);
}

void duskCompile(
    DuskCompiler *compiler, const char *path, const char *text, size_t text_length)
{
    DuskAllocator *allocator = duskArenaGetAllocator(compiler->main_arena);

    if (setjmp(compiler->jump_buffer) != 0)
    {
        for (size_t i = 0; i < duskArrayLength(compiler->errors); ++i)
        {
            DuskError err = compiler->errors[i];
            fprintf(
                stderr,
                "%s:%zu:%zu: %s\n",
                err.location.file->path,
                err.location.line,
                err.location.col,
                err.message);
        }
        return;
    }

    DuskFile *file = DUSK_NEW(allocator, DuskFile);
    *file = (DuskFile){
        .path = path,
        .text = text,
        .text_length = text_length,
        .decls = duskArrayCreate(allocator, DuskDecl *),
    };
    duskParse(compiler, file);
}
