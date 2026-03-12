# mini-unix-shell

A minimal Unix shell written in C (`-std=c11`), built as a learning project to understand process management, pipes, and signal handling at the syscall level.

---

## Features

| Feature | Detail |
|---|---|
| **Interactive prompt** | `user@host:~/cwd$` with `~` home-dir shortening |
| **Pipelines** | Arbitrary-length: `cmd1 \| cmd2 \| cmd3` |
| **I/O redirection** | `<`, `>`, `>>` per command in a pipeline |
| **Background jobs** | Trailing `&`; shell doesn't wait; SIGCHLD handles reaping |
| **Quoting** | Single-quoted (`'…'`) and double-quoted (`"…"`) strings; backslash escape |
| **Built-ins** | `cd`, `pwd`, `echo`, `export`, `unset`, `history`, `exit` |
| **SIGINT (Ctrl-C)** | Kills the foreground child; shell stays alive |

---

## Architecture

```
shell.c          — REPL loop, signal setup, prompt rendering
  │
  ├─ parser.c/h  — tokeniser + grammar → Pipeline / Command structs
  │
  ├─ executor.c/h— fork/exec, pipe wiring, fd redirections, background jobs
  │
  ├─ builtins.c/h— in-process commands: cd pwd echo export unset history exit
  │
  └─ utils.c/h   — safe_malloc, safe_strdup, trim_whitespace, die
```

Data flow for a command line:

```
fgets() ──► parse_input() ──► Pipeline* ──► execute_pipeline()
                                               ├─ is_builtin? → run in-process
                                               └─ otherwise  → fork + execvp
```

---

## Build

Requires **GCC** (or any C11-compliant compiler) and **GNU Make** on a POSIX system (Linux, macOS, WSL).

```bash
make          # builds ./mysh
make run      # builds and launches the shell
make clean    # removes object files and the binary
make rebuild  # clean + all
```

---

## Usage

```
$ make run
user@host:~$ echo hello world
hello world
user@host:~$ ls -la | grep shell | wc -l
3
user@host:~$ cat /etc/passwd > /tmp/out.txt
user@host:~$ sort < /tmp/out.txt | head -5
user@host:~$ sleep 5 &
[bg] pid 12345
user@host:~$ history
     1  echo hello world
     2  ls -la | grep shell | wc -l
     ...
user@host:~$ exit
```

---

## Built-in reference

| Command | Syntax | Description |
|---|---|---|
| `cd` | `cd [dir]` | Change directory; no arg → `$HOME` |
| `pwd` | `pwd` | Print current directory |
| `echo` | `echo [args...]` | Print args separated by spaces |
| `export` | `export [NAME=VAL…]` | Set env vars; no args → print all |
| `unset` | `unset NAME…` | Remove env vars |
| `history` | `history` | Print command history (last 512) |
| `exit` | `exit [code]` | Exit shell with optional status code |

---

## File structure

```
mini-unix-shell/
├── shell.c        Entry point and REPL
├── parser.c       Tokeniser and pipeline builder
├── parser.h
├── executor.c     Fork/exec, pipe management, redirections
├── executor.h
├── builtins.c     Built-in command implementations
├── builtins.h
├── utils.c        Shared helpers (memory, strings, error)
├── utils.h
├── Makefile
└── README.md
```

---

## Limitations & known gaps

- No tab-completion or readline integration (use `rl` wrapper or `rlwrap`)
- No job control (`fg`, `bg`, `jobs`) — background processes run silently
- No here-documents (`<<`)
- No variable expansion (`$VAR` in words) — use `export` to set, not expand
- No subshell (`$(...)` or backtick) support
