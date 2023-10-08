#ifndef DUSK_H
#define DUSK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

typedef struct DuskCompiler DuskCompiler;

DuskCompiler *duskCompilerCreate(void);
void duskCompilerDestroy(DuskCompiler *compiler);

// Returns NULL if there was an error.
uint8_t *duskCompile(
    DuskCompiler *compiler,
    const char *path,
    const char *text,
    size_t text_length,
    size_t *spirv_byte_size);

// Builds a null-terminated string containing the error messages from the last
// compilation.
char *duskCompilerGetErrorsStringMalloc(DuskCompiler *compiler);

#ifdef __cplusplus
}
#endif

#endif
