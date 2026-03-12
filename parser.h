#ifndef PARSER_H
#define PARSER_H

/*
 * parser.h — public interface for input tokenisation and parsing.
 *
 * The parser converts a raw input line into a Pipeline, which is an ordered
 * sequence of Commands joined by '|'.  Each Command holds:
 *   - an argv[] ready to pass to execvp()
 *   - optional input/output redirection filenames
 *   - an append flag (for >>)
 *
 * The Pipeline itself carries a background flag (trailing &).
 *
 * Callers must free the returned Pipeline with free_pipeline() when done.
 */

#define MAX_TOKENS 256   /* max tokens in one input line   */
#define MAX_ARGS   128   /* max arguments per single command */

/* A single command in a pipeline, e.g. "grep foo < bar.txt" */
typedef struct {
    char **argv;          /* NULL-terminated argument vector              */
    int    argc;          /* number of arguments (argv[argc] == NULL)     */
    char  *input_file;    /* filename for < redirection, or NULL          */
    char  *output_file;   /* filename for > / >> redirection, or NULL     */
    int    append;        /* 1 → append (>>), 0 → truncate (>)           */
} Command;

/* An entire pipeline, e.g. "cat f | sort | uniq -c &" */
typedef struct {
    Command **commands;   /* array of Command pointers                    */
    int       count;      /* number of commands                           */
    int       background; /* 1 → trailing &, run without waiting         */
} Pipeline;

/*
 * parse_input – tokenise and parse a (trimmed, non-empty) input line.
 * Returns a heap-allocated Pipeline on success, NULL on empty/parse error.
 */
Pipeline *parse_input(const char *line);

/*
 * free_pipeline – release all memory owned by a Pipeline (and its Commands).
 */
void free_pipeline(Pipeline *p);

#endif /* PARSER_H */
