#ifndef DUSK_H
#define DUSK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef struct DuskCompiler DuskCompiler;

DuskCompiler *duskCompilerCreate(void);
void duskCompilerDestroy(DuskCompiler *compiler);

void duskCompile(
    DuskCompiler *compiler, const char *path, const char *text, size_t text_length);

#ifdef __cplusplus
}
#endif

#endif
