#include "utils/utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *file_read_all(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    uint8_t *data = NULL;
    long length = -1;
    if (fseek(f, 0, SEEK_END) == 0) length = ftell(f);
    if (length >= 0 && fseek(f, 0, SEEK_SET) == 0) data = malloc((size_t)length + 1);

    if (data) {
        size_t read = fread(data, 1, (size_t)length, f);
        if (read != (size_t)length) {
            free(data);
            data = NULL;
        } else {
            data[read] = '\0';
            if (size) *size = read;
        }
    }
    fclose(f);
    return data;
}

char *path_join(char *out, size_t out_size, const char *a, const char *b, const char *c)
{
    if (c) snprintf(out, out_size, "%s/%s/%s", a, b, c);
    else snprintf(out, out_size, "%s/%s", a, b);
    return out;
}

char *text_next_line(char **cursor)
{
    char *line = *cursor;
    if (!line || !*line) return NULL;

    char *end = strchr(line, '\n');
    if (end) {
        *end = '\0';
        *cursor = end + 1;
    } else {
        *cursor = line + strlen(line);
    }

    while (isspace((unsigned char)*line)) line++;
    size_t len = strlen(line);
    while (len > 0 && isspace((unsigned char)line[len - 1])) line[--len] = '\0';
    return line;
}
