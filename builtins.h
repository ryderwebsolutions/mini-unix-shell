#ifndef BUILTINS_H
#define BUILTINS_H

/*
 * builtins.h — built-in commands that run inside the shell process.
 *
 * Built-ins must run in-process (not forked) because they need to mutate
 * the shell's own state: cd changes the working directory, export/unset
 * change environment variables, exit terminates the shell, etc.
 *
 * Provided built-ins:
 *   cd       [dir]          change working directory (defaults to $HOME)
 *   pwd                     print working directory
 *   echo     [args...]      print arguments separated by spaces
 *   export   [NAME=VALUE…]  set environment variables (no args → print all)
 *   unset    NAME…          remove environment variables
 *   history                 print command history
 *   exit     [code]         exit the shell with optional exit code
 */

#include "parser.h"

/* Returns 1 if `name` is the name of a built-in command, 0 otherwise. */
int is_builtin(const char *name);

/*
 * execute_builtin – run the built-in named by cmd->argv[0].
 * Returns the exit status (0 = success).
 * Undefined behaviour if is_builtin(cmd->argv[0]) == 0.
 */
int execute_builtin(Command *cmd);

/*
 * history_add – record a line in the in-memory history ring buffer.
 * Called by shell.c after every non-empty input line.
 */
void history_add(const char *line);

#endif /* BUILTINS_H */
