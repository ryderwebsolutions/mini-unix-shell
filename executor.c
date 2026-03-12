#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "executor.h"
#include "builtins.h"
#include "utils.h"

/* =========================================================================
 * Helpers
 * ========================================================================= */

/* Apply the I/O redirections specified in `cmd` to the current process.
 * Called from the child after fork(), and also (with dup2 save/restore)
 * when running a built-in in-process so that the shell's own fds survive. */
static int apply_redirections(Command *cmd)
{
    if (cmd->input_file) {
        int fd = open(cmd->input_file, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "shell: %s: %s\n",
                    cmd->input_file, strerror(errno));
            return -1;
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

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
 * Single-command fast path (no pipe)
 * ========================================================================= */

/*
 * Run a built-in in-process, but honour redirections by temporarily
 * replacing stdin/stdout and restoring them afterwards.
 */
static int run_builtin_inprocess(Command *cmd)
{
    /* Save originals */
    int saved_in  = dup(STDIN_FILENO);
    int saved_out = dup(STDOUT_FILENO);

    int status = 0;
    if (apply_redirections(cmd) < 0) {
        status = 1;
        goto restore;
    }

    status = execute_builtin(cmd);   /* may call exit() for 'exit' built-in */

restore:
    dup2(saved_in,  STDIN_FILENO);
    dup2(saved_out, STDOUT_FILENO);
    close(saved_in);
    close(saved_out);
    return status;
}

static int run_external(Command *cmd, int background)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        /* --- child --- */
        /* Background processes detach stdin so they don't race for input. */
        if (background) {
            int devnull = open("/dev/null", O_RDONLY);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                close(devnull);
            }
        }

        if (apply_redirections(cmd) < 0)
            exit(1);

        execvp(cmd->argv[0], cmd->argv);
        /* execvp only returns on error */
        fprintf(stderr, "shell: %s: %s\n", cmd->argv[0], strerror(errno));
        exit(127);
    }

    /* --- parent --- */
    if (background) {
        printf("[bg] pid %d\n", pid);
        return 0;
    }

    int wstatus;
    waitpid(pid, &wstatus, 0);
    if (WIFEXITED(wstatus))
        return WEXITSTATUS(wstatus);
    if (WIFSIGNALED(wstatus))
        return 128 + WTERMSIG(wstatus);
    return -1;
}

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
                execvp(cmd->argv[0], cmd->argv);
                fprintf(stderr, "shell: %s: %s\n",
                        cmd->argv[0], strerror(errno));
                exit(127);
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

    /* Fast path: single command */
    if (p->count == 1) {
        Command *cmd = p->commands[0];

        if (cmd->argc == 0)
            return 0;

        if (is_builtin(cmd->argv[0]))
            return run_builtin_inprocess(cmd);

        return run_external(cmd, p->background);
    }

    /* Multi-command pipeline */
    return run_pipeline(p);
}
