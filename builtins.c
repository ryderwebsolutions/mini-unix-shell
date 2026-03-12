#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "builtins.h"
#include "utils.h"

/* =========================================================================
 * History ring buffer
 * ========================================================================= */

#define HISTORY_SIZE 512

static char *history_buf[HISTORY_SIZE];
static int   history_count = 0;

void history_add(const char *line)
{
    if (history_count < HISTORY_SIZE) {
        history_buf[history_count++] = safe_strdup(line);
    } else {
        /* Ring: discard oldest entry and shift everything left by one. */
        free(history_buf[0]);
        memmove(history_buf, history_buf + 1,
                (HISTORY_SIZE - 1) * sizeof(char *));
        history_buf[HISTORY_SIZE - 1] = safe_strdup(line);
    }
}

/* =========================================================================
 * Individual built-in implementations
 * ========================================================================= */

/* cd [dir]
 * Changes the shell's current working directory.
 * With no argument, navigates to $HOME. */
static int builtin_cd(Command *cmd)
{
    const char *path = (cmd->argc > 1) ? cmd->argv[1] : getenv("HOME");
    if (!path) {
        fprintf(stderr, "cd: HOME not set\n");
        return 1;
    }
    if (chdir(path) < 0) {
        fprintf(stderr, "cd: %s: %s\n", path, strerror(errno));
        return 1;
    }
    return 0;
}

/* pwd
 * Prints the absolute path of the current working directory. */
static int builtin_pwd(Command *cmd)
{
    (void)cmd;
    char buf[4096];
    if (!getcwd(buf, sizeof(buf))) {
        perror("pwd");
        return 1;
    }
    puts(buf);
    return 0;
}

/* echo [args...]
 * Prints arguments separated by single spaces, followed by a newline. */
static int builtin_echo(Command *cmd)
{
    for (int i = 1; i < cmd->argc; i++) {
        if (i > 1) putchar(' ');
        fputs(cmd->argv[i], stdout);
    }
    putchar('\n');
    return 0;
}

/* export [NAME=VALUE...]
 * With arguments: set one or more environment variables.
 * With no arguments: print all currently exported variables. */
static int builtin_export(Command *cmd)
{
    if (cmd->argc < 2) {
        extern char **environ;
        for (char **ep = environ; *ep; ep++)
            printf("export %s\n", *ep);
        return 0;
    }

    int status = 0;
    for (int i = 1; i < cmd->argc; i++) {
        char *eq = strchr(cmd->argv[i], '=');
        if (!eq) {
            /* No '=' — treat the whole argument as a variable name.
             * Export it if it already exists in the environment. */
            continue;
        }
        /* Duplicate so we can NUL-terminate the name part. */
        char *pair = safe_strdup(cmd->argv[i]);
        pair[eq - cmd->argv[i]] = '\0';   /* NUL-terminate name */
        if (setenv(pair, eq + 1, /*overwrite=*/1) < 0) {
            perror("export");
            status = 1;
        }
        free(pair);
    }
    return status;
}

/* unset NAME...
 * Removes one or more environment variables. */
static int builtin_unset(Command *cmd)
{
    for (int i = 1; i < cmd->argc; i++)
        unsetenv(cmd->argv[i]);
    return 0;
}

/* history
 * Prints the in-memory command history, numbered from 1. */
static int builtin_history(Command *cmd)
{
    (void)cmd;
    for (int i = 0; i < history_count; i++)
        printf("  %4d  %s\n", i + 1, history_buf[i]);
    return 0;
}

/* exit [code]
 * Exits the shell with the given status code (default 0). */
static int builtin_exit(Command *cmd)
{
    int code = (cmd->argc > 1) ? atoi(cmd->argv[1]) : 0;
    exit(code);
}

/* =========================================================================
 * Dispatch table
 * ========================================================================= */

typedef struct {
    const char *name;
    int (*fn)(Command *);
} Builtin;

static const Builtin dispatch[] = {
    { "cd",      builtin_cd      },
    { "pwd",     builtin_pwd     },
    { "echo",    builtin_echo    },
    { "export",  builtin_export  },
    { "unset",   builtin_unset   },
    { "history", builtin_history },
    { "exit",    builtin_exit    },
    { NULL,      NULL            },  /* sentinel */
};

int is_builtin(const char *name)
{
    for (int i = 0; dispatch[i].name; i++)
        if (strcmp(name, dispatch[i].name) == 0)
            return 1;
    return 0;
}

int execute_builtin(Command *cmd)
{
    for (int i = 0; dispatch[i].name; i++)
        if (strcmp(cmd->argv[0], dispatch[i].name) == 0)
            return dispatch[i].fn(cmd);
    return 127; /* unreachable if preceded by is_builtin() check */
}
