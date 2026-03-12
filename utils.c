#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "utils.h"

/* -------------------------------------------------------------------------- */
/* Memory helpers                                                              */
/* -------------------------------------------------------------------------- */

void *safe_malloc(size_t size)
{
    void *p = malloc(size);
    if (!p) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    return p;
}

char *safe_strdup(const char *s)
{
    char *p = strdup(s);
    if (!p) {
        perror("strdup");
        exit(EXIT_FAILURE);
    }
    return p;
}

/* -------------------------------------------------------------------------- */
/* String helpers                                                              */
/* -------------------------------------------------------------------------- */

/*
 * Trim leading and trailing ASCII whitespace in-place.
 * Returns a pointer into `s` (may not equal `s` if leading spaces existed).
 */
char *trim_whitespace(char *s)
{
    while (isspace((unsigned char)*s))
        s++;

    if (*s == '\0')
        return s;

    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end))
        *end-- = '\0';

    return s;
}

/* -------------------------------------------------------------------------- */
/* Error reporting                                                             */
/* -------------------------------------------------------------------------- */

void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}
