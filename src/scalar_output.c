#define _XOPEN_SOURCE 700
#include "scalar_output.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *resolved_output_path(const char *path)
{
    char *resolved = realpath(path, NULL);
    if (resolved != NULL) {
        return resolved;
    }
    /* The output may not exist yet; resolve its parent to handle ./ and symlinks. */
    char *copy = strdup(path);
    if (copy == NULL) {
        return NULL;
    }
    char *slash = strrchr(copy, '/');
    const char *leaf = path;
    const char *parent = ".";
    if (slash != NULL) {
        *slash = '\0';
        leaf = slash + 1;
        parent = slash == copy ? "/" : copy;
    }
    char *directory = realpath(parent, NULL);
    if (directory != NULL) {
        size_t size = strlen(directory) + strlen(leaf) + 2;
        resolved = malloc(size);
        if (resolved != NULL) {
            snprintf(resolved, size, "%s/%s", directory, leaf);
        }
        free(directory);
    }
    free(copy);
    return resolved;
}

int output_paths_equal(const char *a, const char *b)
{
    if (strcmp(a, b) == 0) {
        return 1;
    }
    struct stat sa, sb;
    if (stat(a, &sa) == 0 && stat(b, &sb) == 0 &&
        sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino) {
        return 1;
    }
    char *ra = resolved_output_path(a);
    char *rb = resolved_output_path(b);
    int same = ra != NULL && rb != NULL && strcmp(ra, rb) == 0;
    free(ra);
    free(rb);
    return same;
}

int scalar_output_open(FILE **file, const char *path)
{
    *file = NULL;
    if (strcmp(path, "none") == 0) {
        return 0;
    }
    struct stat output_stat, stdout_stat;
    if (stat(path, &output_stat) == 0 &&
        fstat(STDOUT_FILENO, &stdout_stat) == 0 &&
        output_stat.st_dev == stdout_stat.st_dev &&
        output_stat.st_ino == stdout_stat.st_ino) {
        /* A shell redirection already writes this file. Do not truncate it
         * again or write each row twice through two independent streams. */
        return 0;
    }
    *file = fopen(path, "w");
    if (*file == NULL) {
        fprintf(stderr, "ERROR: failed to open output_file %s\n", path);
        return 1;
    }
    return 0;
}

int scalar_output_printf(FILE *file, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int failed = vfprintf(stdout, format, args) < 0;
    va_end(args);
    if (file != NULL) {
        va_start(args, format);
        if (vfprintf(file, format, args) < 0) {
            failed = 1;
        }
        va_end(args);
    }
    return failed;
}

int scalar_output_flush(FILE *file)
{
    int failed = fflush(stdout) != 0 || ferror(stdout);
    if (file != NULL && (fflush(file) != 0 || ferror(file))) {
        failed = 1;
    }
    if (failed) {
        fprintf(stderr, "ERROR: failed to write scalar output\n");
    }
    return failed;
}
