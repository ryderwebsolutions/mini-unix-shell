#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>

/*
 * utils.h — small helpers used across every translation unit.
 *
 *  safe_malloc  – malloc that aborts on OOM instead of returning NULL
 *  safe_strdup  – strdup that aborts on OOM
 *  trim_whitespace – strips leading/trailing whitespace in-place
 *  die          – print errno message and exit(1)
 */

void  *safe_malloc(size_t size);
char  *safe_strdup(const char *s);
char  *trim_whitespace(char *s);
void   die(const char *msg);

#endif /* UTILS_H */
