#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "executor.h"
#include "builtins.h"
#include "utils.h"

/* =========================================================================
 * POSIX-standard exit codes for exec failures
 *
 *   127  command not found  (ENOENT from execvp)
 *   126  command found but not executable  (EACCES / ENOEXEC)
 *   1    any other exec error
 * ========================================================================= */
#define EXIT_CMD_NOT_FOUND   127
#define EXIT_CMD_NOT_EXEC    126

/* =========================================================================
 * I/O Redirection helper
 * =========================================================================
 *
 * apply_redirections – wire the file descriptors requested by `cmd`.
 *
 * Must be called in the child process (after fork) before execvp().
 * Also called in-process for built-ins, wrapped in an fd save/restore so
 * the shell's own stdin/stdout are not permanently altered.
 *
 * Returns 0 on success, -1 on error (message already printed to stderr).
 * ========================================================================= */
static int apply_redirections(Command *cmd)
{
    /* Input redirection: open the source file and point stdin at it. */
    if (cmd->input_file) {
        int fd = open(cmd->input_file, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "shell: %s: %s\n",
                    cmd->input_file, strerror(errno));
            return -1;
        }
        /*
         * dup2(oldfd, newfd) makes newfd refer to the same open-file
         * description as oldfd, then closes oldfd.  After this call,
         * STDIN_FILENO reads from the file we just opened.
         */
        dup2(fd, STDIN_FILENO);
        close(fd);   /* fd is no longer needed; STDIN_FILENO holds the reference */
    }

    /* Output redirection: open/create the target file and point stdout at it. */
    if (cmd->output_file) {
        int flags = O_WRONLY | O_CREAT |
                    (cmd->append ? O_APPEND : O_TRUNC);
        int fd = open(cmd->output_file, flags, 0644);
        if (fd < 0) {
            fprintf(stderr, "shell: %s: %s\n",
                    cmd->output_file, strerror(errno));
            return -1;
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }

    return 0;
}

/* =========================================================================
 * Built-in in-process execution (with fd save/restore)
 * =========================================================================
 *
 * Built-ins must run in the shell process so they can mutate its state
 * (cd changes cwd, export changes environ, exit terminates the process).
 * But they still need to respect I/O redirections.
 *
 * Strategy: save the real stdin/stdout fds with dup(), apply redirections,
 * run the built-in, then restore with dup2() so subsequent commands are
 * unaffected.
 * ========================================================================= */
static int run_builtin_inprocess(Command *cmd)
{
    int saved_in  = dup(STDIN_FILENO);
    int saved_out = dup(STDOUT_FILENO);
    if (saved_in < 0 || saved_out < 0) {
        perror("dup");
        if (saved_in  >= 0) close(saved_in);
        if (saved_out >= 0) close(saved_out);
        return 1;
    }

    int status = 0;
    if (apply_redirections(cmd) < 0) {
        status = 1;
        goto restore;
    }

    status = execute_builtin(cmd);  /* 'exit' built-in will call exit() here */

restore:
    /* Restore the shell's original stdin/stdout regardless of errors above */
    dup2(saved_in,  STDIN_FILENO);
    dup2(saved_out, STDOUT_FILENO);
    close(saved_in);
    close(saved_out);
    return status;
}

/* =========================================================================
 * Core: fork + execvp + optional wait
 * =========================================================================
 *
 * fork_and_exec – the fundamental unit of external command execution.
 *
 * Steps:
 *   1. fork()    — duplicate the current process.
 *   2. child:    reset signals, wire fds, call execvp().
 *   3. parent:   either wait (foreground) or record pid (background).
 *
 * Why execvp() over execve()?
 *   execvp() searches PATH automatically, matching what users expect when
 *   they type "ls" instead of "/bin/ls".  execve() requires an absolute
 *   path and a manually constructed envp[].
 *
 * Exit code conventions after exec failure:
 *   ENOENT  → 127  (command not found — mirrors bash/sh behaviour)
 *   EACCES  → 126  (found but not executable)
 *   other   → 1    (unexpected error)
 *
 * Returns the child's exit status for foreground commands, 0 for background,
 * or -1 if fork() itself failed.
 * ========================================================================= */
static int fork_and_exec(Command *cmd, int background)
{
    /*
     * Step 1: fork()
     *
     * fork() creates an exact copy of the calling process.
     * After the call:
     *   parent receives: child's pid (> 0)
     *   child  receives: 0
     *   on failure:     -1 (no child is created)
     */
    pid_t pid = fork();

    if (pid < 0) {
        /* fork failed — OS may be out of process slots or memory */
        perror("shell: fork");
        return -1;
    }

    /* ------------------------------------------------------------------ */
    /* CHILD PROCESS                                                        */
    /* ------------------------------------------------------------------ */
    if (pid == 0) {

        /*
         * Step 2a: reset signal dispositions inherited from the shell.
         *
         * The shell set SIGINT to SIG_IGN so Ctrl-C doesn't kill it.
         * Children inherit that disposition across fork(), which means
         * without this reset, Ctrl-C would silently do nothing in the
         * child too.  We restore the kernel default (terminate) so the
         * child behaves like any normal process.
         */
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);

        /*
         * Step 2b: background processes detach from stdin.
         *
         * If a background process tries to read from the terminal it would
         * block (or get SIGTTIN).  Redirect its stdin to /dev/null so it
         * fails immediately with EOF instead of stalling indefinitely.
         */
        if (background) {
            int devnull = open("/dev/null", O_RDONLY);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                close(devnull);
            }
        }

        /*
         * Step 2c: apply any < > >> redirections from the command.
         * On failure the error is already printed; exit with code 1.
         */
        if (apply_redirections(cmd) < 0)
            exit(1);

        /*
         * Step 2d: execvp() — replace this process image with the program.
         *
         * execvp() searches each directory in PATH for cmd->argv[0].
         * On success it never returns — the new program takes over entirely.
         * On failure it returns -1 and sets errno.
         */
        execvp(cmd->argv[0], cmd->argv);

        /*
         * execvp() returned — the command could not be launched.
         * Translate errno to a POSIX-standard exit code and print a
         * diagnostic before exiting the child.
         */
        switch (errno) {
            case ENOENT:
                fprintf(stderr, "shell: %s: command not found\n",
                        cmd->argv[0]);
                exit(EXIT_CMD_NOT_FOUND);  /* 127 */

            case EACCES:
            /* ENOEXEC covers "file exists but is not a valid executable" */
            case ENOEXEC:
                fprintf(stderr, "shell: %s: permission denied\n",
                        cmd->argv[0]);
                exit(EXIT_CMD_NOT_EXEC);   /* 126 */

            default:
                fprintf(stderr, "shell: %s: %s\n",
                        cmd->argv[0], strerror(errno));
                exit(1);
        }
    }

    /* ------------------------------------------------------------------ */
    /* PARENT PROCESS                                                       */
    /* ------------------------------------------------------------------ */

    /*
     * Background: record the child's pid for the user and return immediately.
     * The child will be reaped later by the SIGCHLD handler in shell.c.
     */
    if (background) {
        printf("[bg] pid %d\n", pid);
        return 0;
    }

    /*
     * Step 3: waitpid() — suspend the parent until the child finishes.
     *
     * waitpid(pid, &wstatus, 0):
     *   pid     = exactly this child (not any child)
     *   wstatus = output: encodes how the child terminated
     *   flags   = 0 means block until the child changes state
     *
     * We loop on EINTR to transparently restart if waitpid is interrupted
     * by a signal (e.g. SIGCHLD from an unrelated background child).
     *
     * Decode wstatus with the W* macros:
     *   WIFEXITED(ws)   true if child called exit() / returned from main()
     *   WEXITSTATUS(ws) the value passed to exit()  (only valid if above)
     *   WIFSIGNALED(ws) true if child was killed by a signal
     *   WTERMSIG(ws)    the signal number             (only valid if above)
     *
     * Convention: signal-killed exit status = 128 + signal_number
     * (mirrors bash; allows the caller to distinguish normal vs signal exit)
     */
    int wstatus;
    while (waitpid(pid, &wstatus, 0) < 0) {
        if (errno != EINTR) {
            perror("shell: waitpid");
            return -1;
        }
    }

    if (WIFEXITED(wstatus))
        return WEXITSTATUS(wstatus);

    if (WIFSIGNALED(wstatus)) {
        /* Mimic bash: print "Terminated" for SIGTERM, etc. */
        fprintf(stderr, "shell: %s: %s\n",
                cmd->argv[0], strsignal(WTERMSIG(wstatus)));
        return 128 + WTERMSIG(wstatus);
    }

    return -1;   /* stopped or other uncommon state */
}

/* Thin public wrappers so callers don't pass a bare int flag */
static int run_foreground(Command *cmd) { return fork_and_exec(cmd, 0); }
static int run_background(Command *cmd) { return fork_and_exec(cmd, 1); }

/* =========================================================================
 * Pipeline execution
 * =========================================================================
 *
 * For N commands we create N-1 pipes.  Each command's stdin is wired to the
 * read end of pipe[i-1] and its stdout to the write end of pipe[i].
 * The first command inherits shell stdin; the last inherits shell stdout
 * (unless redirected by the command itself).
 *
 * All children are forked before any waiting, so they can run concurrently.
 * ========================================================================= */

static int run_pipeline(Pipeline *p)
{
    int   n    = p->count;

    /* pipes[i] connects command i → command i+1 */
    int (*pipes)[2] = safe_malloc((n - 1) * sizeof(*pipes));
    for (int i = 0; i < n - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            free(pipes);
            return -1;
        }
    }

    pid_t *pids = safe_malloc(n * sizeof(pid_t));

    for (int i = 0; i < n; i++) {
        Command *cmd = p->commands[i];

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            /* best-effort: close remaining pipe fds and wait for launched pids */
            for (int j = 0; j < n - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            for (int j = 0; j < i; j++)
                waitpid(pids[j], NULL, 0);
            free(pipes);
            free(pids);
            return -1;
        }

        if (pid == 0) {
            /* === child === */

            /* Wire stdin from previous pipe's read end */
            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }

            /* Wire stdout to next pipe's write end */
            if (i < n - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            /* Close all pipe fds — child inherits copies from the loop */
            for (int j = 0; j < n - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            /* Per-command redirections override the pipe wiring above */
            if (apply_redirections(cmd) < 0)
                exit(1);

            /* Run as built-in or external */
            if (is_builtin(cmd->argv[0])) {
                exit(execute_builtin(cmd));
            } else {
                /* Reset signals in this pipeline child too */
                signal(SIGINT,  SIG_DFL);
                signal(SIGQUIT, SIG_DFL);
                execvp(cmd->argv[0], cmd->argv);
                switch (errno) {
                    case ENOENT:
                        fprintf(stderr, "shell: %s: command not found\n", cmd->argv[0]);
                        exit(EXIT_CMD_NOT_FOUND);
                    case EACCES: case ENOEXEC:
                        fprintf(stderr, "shell: %s: permission denied\n", cmd->argv[0]);
                        exit(EXIT_CMD_NOT_EXEC);
                    default:
                        fprintf(stderr, "shell: %s: %s\n", cmd->argv[0], strerror(errno));
                        exit(1);
                }
            }
        }

        /* === parent === */
        pids[i] = pid;
    }

    /* Parent closes all pipe fds so write-ends don't block children */
    for (int i = 0; i < n - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    free(pipes);

    /* Collect all children; keep the exit status of the last stage */
    int last_status = 0;
    for (int i = 0; i < n; i++) {
        int wstatus;
        waitpid(pids[i], &wstatus, 0);
        if (i == n - 1) {
            if (WIFEXITED(wstatus))
                last_status = WEXITSTATUS(wstatus);
            else if (WIFSIGNALED(wstatus))
                last_status = 128 + WTERMSIG(wstatus);
        }
    }

    free(pids);
    return last_status;
}

/* =========================================================================
 * Public entry point
 * ========================================================================= */

int execute_pipeline(Pipeline *p)
{
    if (p->count == 0)
        return 0;

    /* ------------------------------------------------------------------ */
    /* Fast path: single command (no pipe involved)                        */
    /* ------------------------------------------------------------------ */
    if (p->count == 1) {
        Command *cmd = p->commands[0];

        if (cmd->argc == 0)
            return 0;

        /*
         * Built-ins run in-process (they need to mutate shell state).
         * All other commands go through fork + execvp.
         */
        if (is_builtin(cmd->argv[0]))
            return run_builtin_inprocess(cmd);

        return p->background ? run_background(cmd) : run_foreground(cmd);
    }

    /* ------------------------------------------------------------------ */
    /* Pipeline: two or more commands joined by '|'                        */
    /* ------------------------------------------------------------------ */
    return run_pipeline(p);
}
