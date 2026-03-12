/*
 * shell.c — entry point and REPL (Read-Eval-Print Loop).
 *
 * Responsibilities:
 *   - Print prompt (user@host:cwd$)
 *   - Read a line from stdin (handles EOF / Ctrl-D gracefully)
 *   - Pass raw line to parser; pass resulting Pipeline to executor
 *   - Record every non-empty line in the history ring buffer
 *   - Install SIGINT / SIGCHLD signal handlers so:
 *       • Ctrl-C interrupts the current foreground command, not the shell
 *       • Background children are reaped without zombies
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <limits.h>

#include "parser.h"
#include "executor.h"
#include "builtins.h"
#include "utils.h"

/* =========================================================================
 * Configuration
 * ========================================================================= */

#define INPUT_LIMIT 4096        /* maximum characters per input line */

/* =========================================================================
 * Signal handlers
 * ========================================================================= */

/*
 * SIGINT (Ctrl-C): the shell ignores it; only the foreground child receives
 * it (because the child is in a different process group after fork by
 * default on most systems, but we don't do setpgrp here for simplicity —
 * the shell's SIG_IGN disposition means it alone survives).
 */
static void handle_sigint(int sig)
{
    (void)sig;
    /* Print a fresh prompt on the next iteration of the REPL. */
    write(STDOUT_FILENO, "\n", 1);
}

/*
 * SIGCHLD: reap any background child that has finished so it doesn't
 * become a zombie.  WNOHANG prevents blocking if no child is ready.
 */
static void handle_sigchld(int sig)
{
    (void)sig;
    int wstatus;
    pid_t pid;
    while ((pid = waitpid(-1, &wstatus, WNOHANG)) > 0) {
        /* Optionally notify the user that a background job finished. */
        if (WIFEXITED(wstatus))
            dprintf(STDOUT_FILENO,
                    "\n[done] pid %d exited with status %d\n",
                    pid, WEXITSTATUS(wstatus));
    }
}

/* =========================================================================
 * Prompt
 * ========================================================================= */

static void print_prompt(void)
{
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd)))
        strncpy(cwd, "?", sizeof(cwd));

    char *home = getenv("HOME");
    char *display_cwd = cwd;

    /* Replace leading $HOME with ~ for a cleaner display. */
    if (home) {
        size_t hlen = strlen(home);
        if (strncmp(cwd, home, hlen) == 0 &&
            (cwd[hlen] == '/' || cwd[hlen] == '\0')) {
            display_cwd = cwd + hlen - 1;  /* points to the slash after $HOME */
            *display_cwd = '~';
        }
    }

    const char *user = getenv("USER");
    if (!user) user = getenv("LOGNAME");
    if (!user) user = "user";

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) < 0)
        strncpy(hostname, "localhost", sizeof(hostname));
    /* Truncate to the short hostname */
    char *dot = strchr(hostname, '.');
    if (dot) *dot = '\0';

    printf("\033[1;32m%s@%s\033[0m:\033[1;34m%s\033[0m$ ",
           user, hostname, display_cwd);
    fflush(stdout);
}

/* =========================================================================
 * REPL
 * ========================================================================= */

static void run_repl(void)
{
    char line[INPUT_LIMIT];

    while (1) {
        print_prompt();

        /* Read one line; handle EOF (Ctrl-D) */
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\nexit\n");
            break;
        }

        /* Strip the trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[--len] = '\0';

        /* Skip blank lines */
        char *trimmed = trim_whitespace(line);
        if (*trimmed == '\0')
            continue;

        /* Record in history before parsing so even bad commands are saved */
        history_add(trimmed);

        /* Parse */
        Pipeline *pipeline = parse_input(trimmed);
        if (!pipeline)
            continue;   /* parse error message already printed by parser */

        /* Execute */
        execute_pipeline(pipeline);

        /* Release parser memory */
        free_pipeline(pipeline);
    }
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void)
{
    /* Set up signal handling */
    struct sigaction sa_int  = { .sa_handler = handle_sigint,  .sa_flags = SA_RESTART };
    struct sigaction sa_chld = { .sa_handler = handle_sigchld, .sa_flags = SA_RESTART | SA_NOCLDSTOP };
    sigemptyset(&sa_int.sa_mask);
    sigemptyset(&sa_chld.sa_mask);
    sigaction(SIGINT,  &sa_int,  NULL);
    sigaction(SIGCHLD, &sa_chld, NULL);

    run_repl();
    return 0;
}
