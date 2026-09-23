#ifndef SCALAR_OUTPUT_H
#define SCALAR_OUTPUT_H

#include <stdio.h>

/* Compare existing file identities and paths with an existing parent directory. */
int output_paths_equal(const char *a, const char *b);

/* Keep the historical stdout stream and optionally mirror it to a data file.
 * A NULL file means stdout only, including when stdout already names the file. */
int scalar_output_open(FILE **file, const char *path);
int scalar_output_printf(FILE *file, const char *format, ...);
int scalar_output_flush(FILE *file);

#endif
