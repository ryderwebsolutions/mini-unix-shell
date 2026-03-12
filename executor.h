#ifndef EXECUTOR_H
#define EXECUTOR_H

/*
 * executor.h — public interface for command execution.
 *
 * execute_pipeline is the sole entry point.  It inspects the Pipeline
 * produced by the parser and dispatches to the appropriate strategy:
 *
 *   • Single command, no pipe
 *       – Built-in → run in-process (with optional fd save/restore for
 *                    redirections so stdin/stdout are preserved afterwards).
 *       – External  → fork + execvp; parent waits unless background.
 *
 *   • Pipeline (two or more commands)
 *       – One pipe() pair is created for every adjacent pair of commands.
 *       – Each command runs in its own fork; pipe ends are wired with dup2().
 *       – Built-ins inside a pipeline also run in a fork (unavoidable).
 *       – Parent collects all children; returns exit status of last stage.
 *
 * Returns the exit status of the last (or only) command, or -1 on error.
 */

#include "parser.h"

int execute_pipeline(Pipeline *p);

#endif /* EXECUTOR_H */
