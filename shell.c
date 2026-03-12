/*
 * shell.c — entry point and REPL (Read-Eval-Print Loop).
 *
 * Module responsibilities (each split into its own static function):
 *
 *   setup_signals()  — install SIGINT + SIGCHLD handlers once at startup
 *   print_prompt()   — render "user@host:~/cwd$ " to stdout (TTY only)
 *   read_line()      — read one line safely via getline(); caller must free()
 *   run_repl()       — the main loop: prompt → read → trim → parse → execute
 *   main()           — call setup_signals() then run_repl()
 *
 * Design notes:
 *   • getline() is used instead of fgets() so there is no hard line-length
 *     cap: the buffer grows automatically via realloc inside libc.
 *   • The prompt is suppressed when stdin is not a TTY so the shell can be
 *     used in scripts:  echo "ls" | ./mysh
 *   • last_exit_status tracks the exit code of the most recent command so
 *     that future expansion of $? will have something to read.
 *   • SIGINT is ignored in the shell process; the child inherits the default
 *     disposition after fork() so Ctrl-C kills the child, not the shell.
 *   • SIGCHLD with WNOHANG reaps background children without blocking.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <limits.h>
#include <errno.h>

#include "parser.h"
#include "executor.h"
#include "builtins.h"
#include "utils.h"

/* =========================================================================
 * Global state
 * ========================================================================= */

/*
 * Exit status of the last foreground command.
 * Exposed here so builtins or future $? expansion can read it.
 */
static int last_exit_status = 0;

/* =========================================================================
 * Signal handlers
 * ========================================================================= */

/*
 * handle_sigint – called when the user presses Ctrl-C.
 *
 * The shell itself does NOT exit.  Writing "\n" ensures the terminal cursor
 * moves to a fresh line so print_prompt() renders correctly on the next
 * iteration.  The foreground child receives SIGINT from the kernel
 * automatically because children inherit the default (terminate) action
 * after fork() unless they explicitly reset it.
 */
static void handle_sigint(int sig)
{
    (void)sig;
    /* async-signal-safe write */
    write(STDOUT_FILENO, "\n", 1);
}

/*
 * handle_sigchld – reap any background child that has exited.
 *
 * WNOHANG makes waitpid non-blocking: it returns immediately if no child
 * has changed state.  The loop handles the case where multiple children
 * finish between two deliveries of SIGCHLD (signals are not queued).
 * SA_NOCLDSTOP (set in setup_signals) prevents delivery for stopped children.
 */
static void handle_sigchld(int sig)
{
    (void)sig;
    int   wstatus;
    pid_t pid;
    while ((pid = waitpid(-1, &wstatus, WNOHANG)) > 0) {
        if (WIFEXITED(wstatus))
            dprintf(STDOUT_FILENO,
                    "\n[done] pid %d exited with status %d\n",
                    pid, WEXITSTATUS(wstatus));
        else if (WIFSIGNALED(wstatus))
            dprintf(STDOUT_FILENO,
                    "\n[done] pid %d killed by signal %d\n",
                    pid, WTERMSIG(wstatus));
    }
}

static void setup_signals(void)
{
    struct sigaction sa_int = {
        .sa_handler = handle_sigint,
        .sa_flags   = SA_RESTART,
    };
    struct sigaction sa_chld = {
        .sa_handler = handle_sigchld,
        /* SA_RESTART: auto-restart interrupted syscalls (e.g. fgets/getline).
         * SA_NOCLDSTOP: skip SIGCHLD for stopped (Ctrl-Z) children. */
        .sa_flags   = SA_RESTART | SA_NOCLDSTOP,
    };
    sigemptyset(&sa_int.sa_mask);
    sigemptyset(&sa_chld.sa_mask);

    sigaction(SIGINT,  &sa_int,  NULL);
    sigaction(SIGCHLD, &sa_chld, NULL);
}

/* =========================================================================
 * Prompt rendering
 * ========================================================================= */

/*
 * print_prompt – write "user@host:~/cwd$ " to stdout.
 *
 * Only emits output when stdin is a TTY; when the shell is driven by a
 * script or pipeline the prompt would clutter the output.
 *
 * Colour codes used:
 *   \033[1;32m  bold green  (user@host)
 *   \033[1;34m  bold blue   (path)
 *   \033[0m     reset
 */
static void print_prompt(void)
{
    if (!isatty(STDIN_FILENO))
        return;

    /* --- current working directory --- */
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd)))
        strncpy(cwd, "?", sizeof(cwd) - 1);

    /* Replace leading $HOME with ~ */
    char *display_cwd = cwd;
    const char *home = getenv("HOME");
    if (home) {
        size_t hlen = strlen(home);
        if (strncmp(cwd, home, hlen) == 0 &&
            (cwd[hlen] == '/' || cwd[hlen] == '\0')) {
            display_cwd      = cwd + hlen - 1;
            *display_cwd     = '~';   /* overwrite the last char of home */
        }
    }

    /* --- user name --- */
    const char *user = getenv("USER");
    if (!user) user  = getenv("LOGNAME");
    if (!user) user  = "user";

    /* --- short hostname (strip domain) --- */
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) < 0)
        strncpy(hostname, "localhost", sizeof(hostname) - 1);
    char *dot = strchr(hostname, '.');
    if (dot) *dot = '\0';

    printf("\033[1;32m%s@%s\033[0m:\033[1;34m%s\033[0m$ ",
           user, hostname, display_cwd);
    fflush(stdout);
}

/* =========================================================================
 * Safe line reader
 * ========================================================================= */

/*
 * read_line – read one line from stdin using POSIX getline().
 *
 * Returns a heap-allocated, newline-stripped string on success.
 * Returns NULL on EOF (Ctrl-D) or unrecoverable read error.
 * The caller is responsible for calling free() on the returned pointer.
 *
 * Why getline() instead of fgets():
 *   fgets() requires a pre-allocated fixed-size buffer, so very long lines
 *   are silently truncated.  getline() starts with a small buffer and
 *   calls realloc() as needed, so any line length is handled correctly.
 */
static char *read_line(void)
{
    char   *buf  = NULL;
    size_t  cap  = 0;
    ssize_t nread;

    errno = 0;
    nread = getline(&buf, &cap, stdin);

    if (nread < 0) {
        free(buf);
        /* Distinguish real errors from clean EOF */
        if (errno != 0)
            perror("getline");
        return NULL;   /* signals EOF / error to the caller */
    }

    /* Strip the trailing newline that getline() includes */
    if (nread > 0 && buf[nread - 1] == '\n')
        buf[nread - 1] = '\0';

    return buf;   /* caller must free() */
}

/* =========================================================================
 * Main REPL loop
 * ========================================================================= */

/*
 * run_repl – Read-Eval-Print Loop.
 *
 * Each iteration:
 *   1. Print prompt (no-op if stdin is not a TTY)
 *   2. Read one line with read_line()
 *   3. Exit gracefully on EOF
 *   4. Trim leading/trailing whitespace; skip empty lines
 *   5. Record the raw trimmed line in the history buffer
 *   6. Parse the line into a Pipeline structure
 *   7. Execute the Pipeline; save the exit status
 *   8. Release all parser-allocated memory
 */
static void run_repl(void)
{
    char *raw = NULL;

    while (1) {
        /* Step 1: prompt */
        print_prompt();

        /* Step 2: read */
        raw = read_line();

        /* Step 3: EOF / error → clean exit */
        if (!raw) {
            if (isatty(STDIN_FILENO))
                printf("\nexit\n"); /* friendly message in interactive mode */
            break;
        }

        /* Step 4: trim; skip blank lines */
        char *line = trim_whitespace(raw);
        if (*line == '\0') {
            free(raw);
            continue;
        }

        /* Step 5: history */
        history_add(line);

        /* Step 6: parse */
        Pipeline *pipeline = parse_input(line);
        free(raw);   /* raw is no longer needed after parsing */
        raw = NULL;

        if (!pipeline)
            continue;  /* parse error already reported inside parse_input() */

        /* Step 7: execute */
        last_exit_status = execute_pipeline(pipeline);

        /* Step 8: free parser memory */
        free_pipeline(pipeline);
    }

    free(raw); /* in case we broke out of the loop with raw != NULL */
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

int main(void)
{
    /*
     * Register history_free() so heap-allocated history strings are released
     * on exit().  This keeps valgrind --leak-check=full clean, which matters
     * when grading or demonstrating the project.
     */
    atexit(history_free);

    setup_signals();
    run_repl();
    return last_exit_status;
}
