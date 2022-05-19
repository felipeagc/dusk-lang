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

    struct optparse_long longopts[] = {{"output", 'o', OPTPARSE_REQUIRED}, {0}};

    char *out_path = NULL;
    char *in_path = NULL;

    int option;
    struct optparse options;

    optparse_init(&options, argv);
    while ((option = optparse_long(&options, longopts, NULL)) != -1) {
        switch (option) {
        case 'o': {
            size_t out_path_len = strlen(options.optarg);
            out_path = malloc(out_path_len + 1);
            memcpy(out_path, options.optarg, out_path_len + 1);
            break;
        }
        case '?':
            fprintf(stderr, "%s: %s\n", argv[0], options.errmsg);
            exit(EXIT_FAILURE);
            break;
        }
    }

    char *arg;
    while ((arg = optparse_arg(&options))) {
        in_path = arg;
    }

    if (!in_path) {
        fprintf(stderr, "Usage: %s [-o <output path>] <filename>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    DuskCompiler *compiler = duskCompilerCreate();

    size_t text_size = 0;
    const char *text = loadFile(in_path, &text_size);

    size_t spirv_size = 0;
    uint8_t *spirv =
        duskCompile(compiler, in_path, text, text_size, &spirv_size);

    if (!spirv) {
        fprintf(stderr, "Compilation failed!\n");
        exit(1);
    }

    FILE *f = fopen(out_path ? out_path : "a.spv", "wb");
    if (!f) {
        fprintf(stderr, "Failed to open output file\n");
        exit(1);
    }

    fwrite(spirv, 1, spirv_size, f);

    fclose(f);

    duskCompilerDestroy(compiler);
    if (out_path) {
        free(out_path);
    }

    return 0;
}
