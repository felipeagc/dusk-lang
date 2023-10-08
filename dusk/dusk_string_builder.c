#include "dusk_internal.h"

struct DuskStringBuilder {
    DuskAllocator *allocator;
    DuskArray(char) arr;
};

DuskStringBuilder *
duskStringBuilderCreate(DuskAllocator *allocator, size_t initial_length)
{
    DuskStringBuilder *sb = DUSK_NEW(allocator, DuskStringBuilder);
    *sb = (DuskStringBuilder){
        .allocator = allocator,
        .arr = duskArrayCreate(allocator, char),
    };

    if (initial_length == 0) {
        initial_length = 1 << 13;
    }
    duskArrayEnsure(&sb->arr, initial_length);

    return sb;
}

void duskStringBuilderDestroy(DuskStringBuilder *sb)
{
    duskArrayFree(&sb->arr);
    duskFree(sb->allocator, sb);
}

void duskStringBuilderAppend(DuskStringBuilder *sb, const char *str)
{
    char c;
    while ((c = *str)) {
        duskArrayPush(&sb->arr, c);
        ++str;
    }
}

void duskStringBuilderAppendLen(
    DuskStringBuilder *sb, const char *str, size_t length)
{
    for (const char *s = str; s != str + length; ++s) {
        duskArrayPush(&sb->arr, *s);
    }
}

void duskStringBuilderAppendFormat(
    DuskStringBuilder *sb, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const char *string = duskVsprintf(sb->allocator, format, args);
    va_end(args);

    duskStringBuilderAppend(sb, string);

    duskFree(sb->allocator, (void *)string);
}

char *duskStringBuilderBuild(DuskStringBuilder *sb, DuskAllocator *allocator)
{
    char *new_str =
        (char *)duskAllocate(allocator, duskArrayLength(sb->arr) + 1);
    memcpy(new_str, sb->arr, duskArrayLength(sb->arr));
    new_str[duskArrayLength(sb->arr)] = '\0';
    return new_str;
}
