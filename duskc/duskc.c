#include <dusk.h>
#include <dusk_internal.h>
#include <stdio.h>
#include <stdlib.h>

#define OPTPARSE_IMPLEMENTATION
#include "optparse.h"

static char *loadFile(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    *out_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *data = malloc(*out_size);
    size_t read_size = fread(data, 1, *out_size, f);
    DUSK_ASSERT(read_size == *out_size);

    fclose(f);

    return data;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    struct optparse_long longopts[] = {
        {"shader-stage", 'T', OPTPARSE_REQUIRED},
        {"entry-point", 'E', OPTPARSE_REQUIRED},
        {"output", 'o', OPTPARSE_REQUIRED},
        {0}};

    char *path = NULL;

    char *arg;
    int option;
    struct optparse options;

    optparse_init(&options, argv);
    while ((option = optparse_long(&options, longopts, NULL)) != -1)
    {
        switch (option)
        {
        case 'T': break;
        case 'E': break;
        case 'o': break;
        case '?':
            fprintf(stderr, "%s: %s\n", argv[0], options.errmsg);
            exit(EXIT_FAILURE);
        }
    }

    while ((arg = optparse_arg(&options)))
    {
        path = arg;
    }

    if (!path)
    {
        fprintf(
            stderr,
            "Usage: %s [--shader-stage <stage>] [--entry-point <entry point>] [-o "
            "<output path>] <filename>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    DuskCompiler *compiler = duskCompilerCreate();

    size_t text_size = 0;
    const char *text = loadFile(path, &text_size);

    size_t spirv_size = 0;
    uint8_t *spirv = duskCompile(compiler, path, text, text_size, &spirv_size);

    if (!spirv)
    {
        fprintf(stderr, "Compilation failed!\n");
        exit(1);
    }

    FILE *f = fopen("a.spv", "wb");
    if (!f)
    {
        fprintf(stderr, "Failed to open output file\n");
        exit(1);
    }

    fwrite(spirv, 1, spirv_size, f);

    fclose(f);

    duskCompilerDestroy(compiler);

    return 0;
}
