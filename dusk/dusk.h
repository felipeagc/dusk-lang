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

uint8_t *duskCompile(
    DuskCompiler *compiler,
    const char *path,
    const char *text,
    size_t text_length,
    const char *selected_module,
    size_t *spirv_byte_size);

#ifdef __cplusplus
}
#endif

#endif
