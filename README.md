# mini-unix-shell

A minimal Unix shell written in C (`-std=c11`), built from scratch to understand process management, pipelines, I/O redirection, and signal handling at the syscall level.

> **Portfolio note:** every syscall (`fork`, `execvp`, `waitpid`, `dup2`, `pipe`, `sigaction`) is explained in-line in the source so the code reads as a learning resource, not just an implementation.

---

## Features

| Feature | Detail |
|---|---|
| **Interactive prompt** | `user@host:~/cwd$` with `~` home-dir shortening, suppressed in script mode |
| **Pipelines** | Arbitrary-length: `cmd1 \| cmd2 \| cmd3` |
| **Input redirection** | `< file` — replaces stdin with a file |
| **Output redirection** | `> file` truncate, `>> file` append |
| **Background jobs** | Trailing `&`; shell does not wait; `SIGCHLD` reaps children automatically |
| **Quoting** | Single-quoted (`'…'`) and double-quoted (`"…"`) strings; backslash escape |
| **Comments** | `#` outside quotes discards the rest of the line (script-friendly) |
| **Built-ins** | `cd`, `pwd`, `echo`, `export`, `unset`, `history`, `help`, `exit` |
| **SIGINT (Ctrl-C)** | Kills the foreground child; shell stays alive |
| **Clean shutdown** | History memory freed on exit; `valgrind --leak-check=full` reports zero leaks |

---

## Architecture

```
shell.c          — REPL loop, signal setup, prompt rendering
  │
  ├─ parser.c/h  — tokeniser + grammar → Pipeline / Command structs
  │
  ├─ executor.c/h— fork/exec, pipe wiring, fd redirections, background jobs
  │
  ├─ builtins.c/h— in-process commands: cd pwd echo export unset history help exit
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

## Data structures

### `Command` (`parser.h`)
Represents one stage in a pipeline (e.g. `grep foo < bar.txt`).

```c
typedef struct {
    char **argv;       // NULL-terminated argument vector — passed to execvp()
    int    argc;       // number of arguments
    char  *input_file; // filename for < redirection, or NULL
    char  *output_file;// filename for > / >> redirection, or NULL
    int    append;     // 1 → >> (append), 0 → > (truncate)
} Command;
```

### `Pipeline` (`parser.h`)
An ordered sequence of `Command`s joined by `|`, plus a background flag.

```c
typedef struct {
    Command **commands; // array of Command pointers
    int       count;    // number of stages
    int       background; // 1 → trailing &
} Pipeline;
```

Pipe wiring: for `N` commands, `N-1` `pipe()` pairs are created.  
`commands[i]` reads from `pipes[i-1][0]` and writes to `pipes[i][1]`.  
The first stage inherits shell stdin; the last inherits shell stdout (unless redirected).

---

## Build

Requires **GCC** and **GNU Make** on a POSIX system (Linux, macOS, WSL).

```bash
make              # build ./mysh  (release)
make debug        # build with AddressSanitizer + UBSan
make run          # build and launch interactive session
make valgrind     # run under valgrind --leak-check=full
make clean        # remove object files and binary
make install      # copy to /usr/local/bin/mysh
make uninstall    # remove from /usr/local/bin
```

---

## Usage

```
$ make run
user@host:~$ help

Built-in commands:

  cd [dir]                 Change working directory (default: $HOME)
  pwd                      Print current working directory
  echo [args...]           Print arguments to stdout
  export [NAME=VAL]        Set env variable (no args: list all)
  unset NAME...            Remove environment variable(s)
  history                  Show command history (last 512 entries)
  help                     Show this help message
  exit [code]              Exit the shell with an optional exit code

user@host:~$ ls -la | grep shell | wc -l
3
user@host:~$ cat /etc/passwd > /tmp/out.txt
user@host:~$ sort < /tmp/out.txt | head -5
user@host:~$ sleep 5 &
[bg] pid 12345
user@host:~$ cd /tmp ; pwd
/tmp
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
| `help` | `help` | List all built-in commands |
| `exit` | `exit [code]` | Exit shell with optional status code |

---

## File structure

```
mini-unix-shell/
├── shell.c        Entry point: REPL, prompt, signal setup
├── parser.c       Tokeniser and Pipeline builder
├── parser.h       Pipeline / Command struct definitions
├── executor.c     fork/execvp/waitpid, pipe wiring, redirections
├── executor.h
├── builtins.c     cd, pwd, echo, export, unset, history, help, exit
├── builtins.h
├── utils.c        safe_malloc, safe_strdup, trim_whitespace, die
├── utils.h
├── Makefile       Build, debug, valgrind, install targets
└── README.md
```

---

## POSIX exit codes

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | General error |
| 126 | Command found but not executable |
| 127 | Command not found |
| 128+N | Killed by signal N |

---

## Limitations & potential extensions

- No tab-completion (use `rlwrap ./mysh` for readline support)
- No job control (`fg`, `bg`, `jobs`)
- No here-documents (`<<`)
- No `$VAR` word expansion
- No subshell (`$(...)`)
- No `&&` / `||` conditional operators

---

## Contributing

1. Fork the repo and create a feature branch
2. Follow the existing style: `c11`, 4-space indent, `static` for file-private functions
3. Run `make debug` and `make valgrind` before submitting a PR — both must pass cleanly


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
