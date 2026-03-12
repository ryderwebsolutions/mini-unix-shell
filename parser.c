#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "parser.h"
#include "utils.h"

/* =========================================================================
 * TOKENISER
 * =========================================================================
 * Splits the input line into a flat list of tokens, honouring:
 *   - single-quoted strings  ('...')  – no escape processing inside
 *   - double-quoted strings  ("...")  – backslash escapes honoured
 *   - backslash escapes outside quotes
 *   - the special operator tokens: | < > >> &
 * ========================================================================= */

typedef struct {
    char *tokens[MAX_TOKENS];
    int   count;
} TokenList;

static void free_token_list(TokenList *tl)
{
    for (int i = 0; i < tl->count; i++)
        free(tl->tokens[i]);
}

/*
 * tokenise – fill `out` from `line`.
 * Returns 0 on success, -1 if the token limit is exceeded.
 */
static int tokenise(const char *line, TokenList *out)
{
    out->count = 0;
    const char *p = line;
    char buf[4096];

    while (*p) {
        /* --- skip whitespace --- */
        while (*p && isspace((unsigned char)*p))
            p++;
        if (!*p)
            break;

        /* --- comments: '#' outside quotes starts a line comment ---
         * Everything from '#' to end-of-line is discarded.  This lets
         * shell scripts use comments and matches POSIX sh behaviour. */
        if (*p == '#')
            break;

        /* --- single-character operators: | < & --- */
        if (*p == '|' || *p == '<' || *p == '&') {
            char op[2] = { *p, '\0' };
            out->tokens[out->count++] = safe_strdup(op);
            p++;
            goto check_limit;
        }

        /* --- > or >> --- */
        if (*p == '>') {
            if (*(p + 1) == '>') {
                out->tokens[out->count++] = safe_strdup(">>");
                p += 2;
            } else {
                out->tokens[out->count++] = safe_strdup(">");
                p++;
            }
            goto check_limit;
        }

        /* --- regular token (may contain quoted spans) --- */
        {
            int  blen     = 0;
            int  in_single = 0;
            int  in_double = 0;

            while (*p && (in_single || in_double ||
                          (!isspace((unsigned char)*p) &&
                           *p != '|' && *p != '<' &&
                           *p != '>' && *p != '&')))
            {
                if (*p == '\'' && !in_double) {
                    in_single = !in_single;
                    p++;
                } else if (*p == '"' && !in_single) {
                    in_double = !in_double;
                    p++;
                } else if (*p == '\\' && !in_single && *(p + 1)) {
                    /* backslash escape – include the next character literally */
                    buf[blen++] = *(p + 1);
                    p += 2;
                } else {
                    buf[blen++] = *p++;
                }

                if (blen >= (int)sizeof(buf) - 1)
                    break;
            }

            buf[blen] = '\0';
            if (blen > 0)
                out->tokens[out->count++] = safe_strdup(buf);
        }

check_limit:
        if (out->count >= MAX_TOKENS - 1) {
            fprintf(stderr, "shell: too many tokens in command\n");
            return -1;
        }
    }

    out->tokens[out->count] = NULL;
    return 0;
}

/* =========================================================================
 * COMMAND / PIPELINE BUILDER
 * ========================================================================= */

static Command *alloc_command(void)
{
    Command *c     = safe_malloc(sizeof(Command));
    c->argv        = safe_malloc(MAX_ARGS * sizeof(char *));
    c->argc        = 0;
    c->input_file  = NULL;
    c->output_file = NULL;
    c->append      = 0;
    c->argv[0]     = NULL;
    return c;
}

static void free_command(Command *c)
{
    if (!c) return;
    for (int i = 0; i < c->argc; i++)
        free(c->argv[i]);
    free(c->argv);
    free(c->input_file);
    free(c->output_file);
    free(c);
}

void free_pipeline(Pipeline *p)
{
    if (!p) return;
    for (int i = 0; i < p->count; i++)
        free_command(p->commands[i]);
    free(p->commands);
    free(p);
}

/*
 * parse_input – public entry point.
 *
 * Grammar (simplified):
 *   pipeline   = command ( '|' command )* [ '&' ]
 *   command    = token* redirection*
 *   redirection = '<' filename
 *               | '>' filename
 *               | '>>' filename
 */
Pipeline *parse_input(const char *line)
{
    TokenList tl = { .count = 0 };
    if (tokenise(line, &tl) < 0)
        return NULL;
    if (tl.count == 0)
        return NULL;

    Pipeline *pl   = safe_malloc(sizeof(Pipeline));
    pl->commands   = safe_malloc(MAX_TOKENS * sizeof(Command *));
    pl->count      = 0;
    pl->background = 0;

    Command *cur = alloc_command();

    for (int i = 0; i < tl.count; i++) {
        const char *t = tl.tokens[i];

        if (strcmp(t, "&") == 0) {
            pl->background = 1;
            break; /* & must be last */

        } else if (strcmp(t, "|") == 0) {
            /* finish the current command and start a new one */
            cur->argv[cur->argc] = NULL;
            pl->commands[pl->count++] = cur;
            cur = alloc_command();

        } else if (strcmp(t, "<") == 0) {
            if (i + 1 < tl.count) {
                free(cur->input_file);
                cur->input_file = safe_strdup(tl.tokens[++i]);
            } else {
                fprintf(stderr, "shell: syntax error near '<'\n");
                goto parse_error;
            }

        } else if (strcmp(t, ">") == 0 || strcmp(t, ">>") == 0) {
            cur->append = (strcmp(t, ">>") == 0);
            if (i + 1 < tl.count) {
                free(cur->output_file);
                cur->output_file = safe_strdup(tl.tokens[++i]);
            } else {
                fprintf(stderr, "shell: syntax error near '%s'\n", t);
                goto parse_error;
            }

        } else {
            /* plain argument */
            if (cur->argc < MAX_ARGS - 1) {
                cur->argv[cur->argc++] = safe_strdup(t);
                cur->argv[cur->argc]   = NULL;
            }
        }
    }

    /* push the last command */
    cur->argv[cur->argc] = NULL;
    if (cur->argc > 0) {
        pl->commands[pl->count++] = cur;
    } else {
        free_command(cur);
    }

    free_token_list(&tl);

    if (pl->count == 0) {
        free(pl->commands);
        free(pl);
        return NULL;
    }
    return pl;

parse_error:
    free_command(cur);
    free_pipeline(pl);
    free_token_list(&tl);
    return NULL;
}
